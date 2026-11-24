#include "hw_comun.h"
#include "tick.h"
#include "display.h"
#include "io.h"
#include "analog.h"
#include "watch.h"
#include "Arcade.h"

/* Tick con SysTick cada TICK_MS para las tareas de fondo */

/* Recarga del SysTick: 16 MHz / 1000 = 16000 cuentas por ms; 16000 x 2 ms = 32000 cuentas.
   Se resta 1 porque el contador va de LOAD a 0 (32000 cuentas = 31999 ... 0). */
#define TICK_RECARGA      (((HCLK_HZ / 1000U) * TICK_MS) - 1U)

void Tick_Init(void)
{
    /* Contador detenido */
    SysTick->CTRL &= ~(SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk);

    /* Recarga y contador en 0 */
    SysTick->LOAD = TICK_RECARGA & SysTick_LOAD_RELOAD_Msk;
    SysTick->VAL = 0;

    /* Prioridad más baja */
    NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);

    /* Reloj del procesador, interrupción y contador */
    SysTick->CTRL |= SysTick_CTRL_CLKSOURCE_Msk;
    SysTick->CTRL |= (SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk);
}

/* Pausa la interrupción del tick */
void Tick_Bloquear(void)
{
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    __DSB();
    __ISB();
}

/* Reanuda la interrupción del tick */
void Tick_Desbloquear(void)
{
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
}

/* Interrupción del SysTick */
void SysTick_Handler(void)
{
    static uint8_t cuenta_io = 0;
    static uint8_t cuenta_pot = 0;
    static uint8_t cuenta_watch = 2;    /* Desfasado de IOMAP */

    /* POT2SEG */
    cuenta_pot++;
    if (cuenta_pot >= POT_PERIODO_TICKS)
    {
        cuenta_pot = 0;
        Pot2Seg_Actualizar();
    }

    /* Display de 7 segmentos */
    Segment_Refresh();

    /* IOMAP */
    cuenta_io++;
    if (cuenta_io >= IO_PERIODO_TICKS)
    {
        cuenta_io = 0;
        IO_Actualizar();
    }

    /* LCDWATCH */
    cuenta_watch++;
    if (cuenta_watch >= WATCH_PERIODO_TICKS)
    {
        cuenta_watch = 0;
        Watch_Actualizar();
    }

    /* ARCADE */
    Arcade_Actualizar();
}
