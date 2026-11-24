#include "hw_comun.h"
#include "rgb.h"
#include "monitor.h"

/* LED RGB por PWM de TIM3: R = PB4 (CH1), G = PA7 (CH2), B = PB0 (CH3) */

#define PIN_R             4    /* PB4 */
#define PIN_G             7    /* PA7 */
#define PIN_B             0    /* PB0 */
#define AF2_TIM3          2

#define CANAL_R           1
#define CANAL_G           2
#define CANAL_B           3

/* Cálculo del PWM:
   Cuenta = 16 MHz / (PSC + 1) = 16 MHz / 63 = 253968 Hz
   Periodo = ARR + 1 = 255 cuentas -> 253968 / 255 = 996 Hz
   Ciclo de trabajo = CCRx / 255, así el byte del color (00 a FF) da 0 % a 100 % */
#define RGB_PSC           62
#define RGB_ARR           254

/* Ciclos de trabajo de los tres colores */
static void RGB_Escribir(uint8_t rojo, uint8_t verde, uint8_t azul)
{
    TIM3->CCR1 = rojo;
    TIM3->CCR2 = verde;
    TIM3->CCR3 = azul;
}

void RGB_Init(void)
{
    /* Reloj y reset de TIM3 */
    RCC_ActivarAPB1(RCC_APB1ENR1_TIM3EN, RCC_APB1RSTR1_TIM3RST);

    TIM_ConfigBase(TIM3, RGB_PSC, RGB_ARR);

    /* LED apagado */
    RGB_Escribir(0, 0, 0);

    /* Canales en PWM */
    TIM_CanalSalida(TIM3, CANAL_R, TIM_OCM_PWM1);
    TIM_CanalSalida(TIM3, CANAL_G, TIM_OCM_PWM1);
    TIM_CanalSalida(TIM3, CANAL_B, TIM_OCM_PWM1);

    TIM3->EGR = TIM_EGR_UG;

    TIM_CanalHabilitar(TIM3, CANAL_R);
    TIM_CanalHabilitar(TIM3, CANAL_G);
    TIM_CanalHabilitar(TIM3, CANAL_B);

    TIM3->CR1 |= TIM_CR1_CEN;

    /* Pines */
    GPIO_ConfigAF(GPIOB, PIN_R, AF2_TIM3, GPIO_PUSH_PULL);
    GPIO_ConfigAF(GPIOA, PIN_G, AF2_TIM3, GPIO_PUSH_PULL);
    GPIO_ConfigAF(GPIOB, PIN_B, AF2_TIM3, GPIO_PUSH_PULL);
}

/* Lee un color hex de hasta 6 dígitos; devuelve los caracteres usados o 0 */
static uint8_t RGB_LeerColor(const char *str, uint32_t *color)
{
    const char *inicio = str;
    uint32_t valor = 0;
    uint8_t digitos = 0;

    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
    {
        str += 2;
    }

    while (1)
    {
        uint32_t nibble;

        if (*str >= '0' && *str <= '9')
        {
            nibble = (uint32_t)(*str - '0');
        }
        else if (*str >= 'A' && *str <= 'F')
        {
            nibble = (uint32_t)(*str - 'A' + 10);
        }
        else if (*str >= 'a' && *str <= 'f')
        {
            nibble = (uint32_t)(*str - 'a' + 10);
        }
        else
        {
            break;
        }

        if (digitos >= 6)
        {
            return 0;
        }
        valor = (valor << 4) | nibble;
        digitos++;
        str++;
    }

    if (digitos == 0 || (*str != '\0' && *str != ' '))
    {
        return 0;
    }

    *color = valor;
    return (uint8_t)(str - inicio);
}

/* Comando: RGBOUT color */
void Cmd_RGBOUT(char *args)
{
    uint32_t color;
    uint8_t usados;
    char *ptr;

    usados = RGB_LeerColor(args, &color);
    if (usados == 0)
    {
        UART_Print("Error de sintaxis. Uso correcto: RGBOUT color (hex RRGGBB, ej. FF0000)\r\n");
        return;
    }

    ptr = args + usados;
    while (*ptr == ' ')
    {
        ptr++;
    }
    if (*ptr != '\0')
    {
        UART_Print("Error de sintaxis. Uso correcto: RGBOUT color (hex RRGGBB, ej. FF0000)\r\n");
        return;
    }

    RGB_Escribir((uint8_t)((color >> 16) & 0xFF),
                 (uint8_t)((color >> 8) & 0xFF),
                 (uint8_t)(color & 0xFF));

    UART_Print("RGB: ");
    UART_PrintHex32(color);
    UART_Print("\r\n");
}
