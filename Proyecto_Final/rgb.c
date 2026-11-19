#include "hw_comun.h"
#include "rgb.h"
#include "monitor.h"   // UART_Print, UART_PrintHex32

// LED RGB manejado con PWM de TIM3 (AyudaProgra, tabla de la NUCLEO-G431KB):
//  - RGB_R: PB4 (CN4-15, D12), AF2 = TIM3_CH1
//  - RGB_G: PA7 (CN3-6,  A6),  AF2 = TIM3_CH2
//  - RGB_B: PB0 (CN4-6,  D3),  AF2 = TIM3_CH3
//  Pines en función alterna push-pull. PB4 es el MISO del Arduino, pero aquí es RGB_R:
//  SPI1 solo transmite y PB4 nunca se pone en AF5 (AyudaProgra).
//  El pull-down "dead battery" de PB4 ya lo deshabilitó Setup_Seguridad() (UCPD1_DBDIS).
//
// Requisitos del hardware (AyudaProgra, "RGB"): salida activa en alto (pin en 1 = color
// encendido, por los NPN Q5-Q7) y ciclo de trabajo = byte del color / 255.
//
// Funcionamiento:
//  - Reloj de TIM3 = 16 MHz (HCLK_HZ, ver hw_comun.h).
//  - PWM modo 1: la salida está en 1 mientras CNT < CCRx (RM0440 30.4).
//  - ARR = 254 -> periodo de 255 cuentas, así CCRx = byte da exactamente byte / 255.
//    Con CCRx = 255 (> ARR) la salida queda en 1 fija (100 %) y con CCRx = 0 en 0 fija.
//  - PSC = 62 -> 16 MHz / 63 / 255 = 996 Hz, sin parpadeo visible.

#define PIN_R             4    // PB4
#define PIN_G             7    // PA7
#define PIN_B             0    // PB0
#define AF2_TIM3          2    // AF2 = TIM3_CH1/CH2/CH3 en PB4/PA7/PB0

#define CANAL_R           1    // TIM3_CH1
#define CANAL_G           2    // TIM3_CH2
#define CANAL_B           3    // TIM3_CH3

#define RGB_PSC           62   // 16 MHz / 63 = 253968 Hz de cuenta
#define RGB_ARR           254  // 255 cuentas por periodo -> 996 Hz

// Escribe los tres ciclos de trabajo. Con precarga (OCxPE = 1) los valores nuevos se aplican
// al final del periodo en curso, así que el cambio de color no produce pulsos cortados.
static void RGB_Escribir(uint8_t rojo, uint8_t verde, uint8_t azul)
{
    TIM3->CCR1 = rojo;
    TIM3->CCR2 = verde;
    TIM3->CCR3 = azul;
}

// Inicialización del temporizador y de los pines PWM del LED RGB.
// TIM3 arranca con los tres CCRx en 0 (LED apagado) ANTES de conectar los pines, para que
// pasen de analógico (Setup_Seguridad) directamente a 0.
void RGB_Init(void)
{
    // Reloj de TIM3 y reinicio desde el RCC
    RCC_ActivarAPB1(RCC_APB1ENR1_TIM3EN, RCC_APB1RSTR1_TIM3RST);

    // Base de tiempo de 996 Hz
    TIM_ConfigBase(TIM3, RGB_PSC, RGB_ARR);

    // LED apagado
    RGB_Escribir(0, 0, 0);

    // Tres canales en PWM modo 1 con precarga, activos en alto y habilitados
    TIM_CanalSalida(TIM3, CANAL_R, TIM_OCM_PWM1);
    TIM_CanalSalida(TIM3, CANAL_G, TIM_OCM_PWM1);
    TIM_CanalSalida(TIM3, CANAL_B, TIM_OCM_PWM1);

    // UG copia PSC, ARR y CCRx a sus registros sombra antes de arrancar (RM0440 30.4, PWM mode)
    TIM3->EGR = TIM_EGR_UG;

    TIM_CanalHabilitar(TIM3, CANAL_R);
    TIM_CanalHabilitar(TIM3, CANAL_G);
    TIM_CanalHabilitar(TIM3, CANAL_B);

    // Arrancamos el contador (con CCRx = 0 las tres salidas se quedan en 0)
    TIM3->CR1 |= TIM_CR1_CEN;

    // Al final, los pines
    GPIO_ConfigAF(GPIOB, PIN_R, AF2_TIM3, GPIO_PUSH_PULL);
    GPIO_ConfigAF(GPIOA, PIN_G, AF2_TIM3, GPIO_PUSH_PULL);
    GPIO_ConfigAF(GPIOB, PIN_B, AF2_TIM3, GPIO_PUSH_PULL);
}

// Lee un color hexadecimal de hasta 6 dígitos (con prefijo 0x opcional) terminado en espacio
// o fin de cadena. Devuelve cuántos caracteres se usaron, o 0 si el texto no es válido.
// ParseHex de monitor.c no sirve aquí porque no indica si el texto era válido.
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
            return 0;   // Más de 24 bits
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

// Comando: RGBOUT color
// Emite luz del color especificado en hexadecimal, formato RGB de 24 bits (ej. FF0000 = rojo
// al 100 %, 008000 = verde al 50 %, 000000 = apagado).
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

    // No debe haber nada más después del color
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
