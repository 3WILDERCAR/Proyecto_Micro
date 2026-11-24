#include "hw_comun.h"
#include "sound.h"
#include "monitor.h"

/* Buzzer en TIM4_CH1 (PB6, AF2). En silencio PB6 queda en 0 */

#define PIN_BUZZER        6     /* PB6 */
#define AF2_TIM4          2
#define CANAL_BUZZER      1

#define SOUND_FREQ_MAX    20000

/* Silencio: salida en 0 y contador detenido */
static void Sound_Detener(void)
{
    TIM_CanalModo(TIM4, CANAL_BUZZER, TIM_OCM_FORZAR_BAJO);
    TIM4->CR1 &= ~TIM_CR1_CEN;
    TIM4->CNT = 0;
}

/* Onda cuadrada al 50 % */
static void Sound_Arrancar(uint32_t freq)
{
    uint32_t cuentas;
    uint32_t psc;
    uint32_t arr;

    /* Cálculo del periodo:
       cuentas = 16 MHz / freq (redondeado), ej. 2048 Hz -> 7813 cuentas
       psc     = (cuentas - 1) / 65536 -> prescaler mínimo para que ARR quepa en 16 bits
       arr     = cuentas / (psc + 1) - 1
       freq real = 16 MHz / ((psc + 1) x (arr + 1))
       CCR1 = (arr + 1) / 2 -> la mitad del periodo en 1 (50 %) */
    cuentas = (HCLK_HZ + (freq / 2)) / freq;
    psc = (cuentas - 1) / 65536U;
    arr = (cuentas / (psc + 1)) - 1;

    TIM4->PSC = psc;
    TIM4->ARR = arr;
    TIM4->CCR1 = (arr + 1) / 2;

    TIM_CanalModo(TIM4, CANAL_BUZZER, TIM_OCM_PWM1);

    /* Aplica los valores nuevos */
    TIM4->EGR = TIM_EGR_UG;

    TIM4->CR1 |= TIM_CR1_CEN;
}

void Sound_Init(void)
{
    /* Reloj y reset de TIM4 */
    RCC_ActivarAPB1(RCC_APB1ENR1_TIM4EN, RCC_APB1RSTR1_TIM4RST);

    TIM_ConfigBase(TIM4, 0, 0);

    /* Canal 1 forzado a 0 */
    TIM_CanalSalida(TIM4, CANAL_BUZZER, TIM_OCM_FORZAR_BAJO);
    TIM_CanalHabilitar(TIM4, CANAL_BUZZER);

    /* Pin */
    GPIO_ConfigAF(GPIOB, PIN_BUZZER, AF2_TIM4, GPIO_PUSH_PULL);
}

/* Comando: SOUND freq */
void Cmd_SOUND(char *args)
{
    int32_t freq;
    uint8_t usados;
    char *ptr;

    usados = ParseDec(args, &freq);
    if (usados == 0)
    {
        UART_Print("Error de sintaxis. Uso correcto: SOUND freq (Hz, decimal)\r\n");
        return;
    }

    ptr = args + usados;
    while (*ptr == ' ')
    {
        ptr++;
    }
    if (*ptr != '\0')
    {
        UART_Print("Error de sintaxis. Uso correcto: SOUND freq (Hz, decimal)\r\n");
        return;
    }

    if (freq == 0)
    {
        Sound_Detener();
        UART_Print("Buzzer silenciado.\r\n");
        return;
    }

    if (freq < 0 || freq > SOUND_FREQ_MAX)
    {
        UART_Print("Error: frecuencia fuera de rango. Use 1 a 20000 Hz (0 para silenciar).\r\n");
        return;
    }

    Sound_Arrancar((uint32_t)freq);

    UART_Print("Buzzer: ");
    UART_PrintDec(freq);
    UART_Print(" Hz\r\n");
}

/* Comando: MUTE */
void Cmd_MUTE(char *args)
{
    if (*args != '\0')
    {
        UART_Print("Error de sintaxis. Uso correcto: MUTE\r\n");
        return;
    }

    Sound_Detener();
    UART_Print("Buzzer silenciado.\r\n");
}
