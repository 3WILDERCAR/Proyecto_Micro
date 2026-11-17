#include "hw_comun.h"
#include "sound.h"
#include "../monitor.h"   // UART_Print, UART_PrintDec, ParseDec

// Buzzer CEM-1203(42) manejado por TIM4_CH1 en PB6 (CN4-9, D6), AF2, push-pull
// (AyudaProgra, tabla de la NUCLEO-G431KB).
//
// Requisitos del hardware (AyudaProgra, "Buzzer"):
//  - Onda cuadrada al 50 % a la frecuencia pedida (el buzzer está especificado a 2048 Hz).
//  - MUTE y SOUND 0 detienen el timer y dejan PB6 en 0, NUNCA en 1 fijo: la bobina de 42 Ohm
//    quedaría con corriente continua.
//  - El pull-down "dead battery" de PB6 ya lo deshabilitó Setup_Seguridad() (UCPD1_DBDIS).
//
// Funcionamiento:
//  - Reloj de TIM4 = 16 MHz (HCLK_HZ, ver hw_comun.h).
//  - Sonido: PWM modo 1, ARR = periodo - 1 y CCR1 = (ARR + 1) / 2 -> 50 %.
//    TIM4 es de 16 bits (RM0440, Tabla 284), así que el prescaler se elige para cada
//    frecuencia de modo que ARR quepa en 16 bits.
//  - Silencio: "force inactive" (OC1M = 0100): OC1REF baja de inmediato y, con CC1P = 0
//    (activo en alto), PB6 queda en 0. Después se detiene el contador.

#define PIN_BUZZER        6     // PB6
#define AF2_TIM4          2     // AF2 = TIM4_CH1 en PB6
#define CANAL_BUZZER      1     // TIM4_CH1

#define SOUND_FREQ_MAX    20000 // Límite superior del rango audible

// Silencia el buzzer: PB6 queda en 0 y el contador detenido
static void Sound_Detener(void)
{
    TIM_CanalModo(TIM4, CANAL_BUZZER, TIM_OCM_FORZAR_BAJO);    // Primero forzamos la salida a 0
    TIM4->CR1 &= ~TIM_CR1_CEN;                                 // Luego detenemos el contador
    TIM4->CNT = 0;
}

// Genera la onda cuadrada de la frecuencia indicada (1 a SOUND_FREQ_MAX Hz)
static void Sound_Arrancar(uint32_t freq)
{
    uint32_t cuentas;
    uint32_t psc;
    uint32_t arr;

    // Cuentas de 16 MHz en un periodo, redondeado
    cuentas = (HCLK_HZ + (freq / 2)) / freq;

    // Prescaler mínimo que deja el periodo dentro de 16 bits (ARR <= 65535)
    psc = (cuentas - 1) / 65536U;
    arr = (cuentas / (psc + 1)) - 1;

    TIM4->PSC = psc;
    TIM4->ARR = arr;
    TIM4->CCR1 = (arr + 1) / 2;         // 50 % de ciclo de trabajo

    TIM_CanalModo(TIM4, CANAL_BUZZER, TIM_OCM_PWM1);

    // UG copia PSC, ARR y CCR1 a sus registros sombra y reinicia el contador, así la nueva
    // frecuencia se aplica de inmediato aunque el periodo anterior fuera largo
    TIM4->EGR = TIM_EGR_UG;

    TIM4->CR1 |= TIM_CR1_CEN;
}

// Inicialización del temporizador y del pin del buzzer.
// El canal se deja en "force inactive" ANTES de conectar el pin, para que PB6 pase de
// analógico (Setup_Seguridad) directamente a 0.
void Sound_Init(void)
{
    // Reloj de TIM4 y reinicio desde el RCC
    RCC_ActivarAPB1(RCC_APB1ENR1_TIM4EN, RCC_APB1RSTR1_TIM4RST);

    // Base de tiempo detenida; PSC y ARR se calculan en cada SOUND
    TIM_ConfigBase(TIM4, 0, 0);

    // Canal 1 como salida forzada a 0, activa en alto y habilitada
    TIM_CanalSalida(TIM4, CANAL_BUZZER, TIM_OCM_FORZAR_BAJO);
    TIM_CanalHabilitar(TIM4, CANAL_BUZZER);

    // Al final, el pin
    GPIO_ConfigAF(GPIOB, PIN_BUZZER, AF2_TIM4, GPIO_PUSH_PULL);
}

// Comando: SOUND freq
// Envía una señal cuadrada de frecuencia freq (en hertz, decimal) al buzzer. SOUND 0 lo silencia.
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

    // No debe haber nada más después de la frecuencia
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

// Comando: MUTE
// Silencia el buzzer. Lo mismo que SOUND 0.
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
