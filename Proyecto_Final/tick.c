#include "hw_comun.h"
#include "tick.h"
#include "display.h"
#include "io.h"
#include "analog.h"
#include "watch.h"
#include "arcade.h"

// Tick del sistema con el SysTick del Cortex-M4 (PM0214, System timer).
// SysTick y NVIC_SetPriority vienen de core_cm4.h, que se incluye a través de stm32g431xx.h.
//
// Se usa SysTick en lugar de un TIM porque:
//  - TIM3 y TIM4 ya están asignados a RGBOUT y SOUND (AyudaProgra).
//  - El nombre de su rutina de interrupción (SysTick_Handler) es el mismo en cualquier
//    archivo de arranque y no requiere habilitar nada en el NVIC.
//
// Reloj: CLKSOURCE = 1 usa el reloj del procesador (HCLK = 16 MHz, ver hw_comun.h).
//
// Carga del tick (bus I2C a 100 kHz):
//  - Display: una transacción de 5 bytes por tick (unos 0.5 ms cada 2 ms).
//  - IOMAP: lectura de los switches (unos 0.4 ms) cada 10 ms, más la escritura de los LEDs
//    (unos 0.3 ms) solo cuando cambian.
//  - POT2SEG: una conversión del ADC (unos 41 us) cada 20 ms.
//  - LCDWATCH: una lectura de memoria cada 10 ms y, si el valor cambió, una línea de la LCD por
//    SPI1 (86 bytes a 2 MHz, unos 0.45 ms). Su contador arranca desfasado del de IOMAP para que
//    nunca les toque en el mismo tick.
//  - ARCADE: una tarea pequeña por tick en un ciclo de 10 ticks (un paso del juego, un banco de la
//    LCD por SPI1 o una lectura del teclado por I2C).
//  El peor caso de una interrupción es alrededor de 1.3 ms (display + IOMAP), menor que el
//  periodo de 2 ms; con LCDWATCH es alrededor de 1 ms (display + POT2SEG + una línea).

#define TICK_RECARGA      (((HCLK_HZ / 1000U) * TICK_MS) - 1U)   // 31999 para 2 ms

void Tick_Init(void)
{
    // Detenemos el contador mientras se configura
    SysTick->CTRL &= ~(SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk);

    // Valor de recarga (24 bits) y contador en 0 (cualquier escritura en VAL lo pone en 0)
    SysTick->LOAD = TICK_RECARGA & SysTick_LOAD_RELOAD_Msk;
    SysTick->VAL = 0;

    // Prioridad más baja (el STM32G4 implementa __NVIC_PRIO_BITS = 4), para que cualquier
    // interrupción futura pueda interrumpir al tick
    NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);

    // Reloj del procesador; al final, interrupción y contador
    SysTick->CTRL |= SysTick_CTRL_CLKSOURCE_Msk;
    SysTick->CTRL |= (SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk);
}

// Pausa la interrupción del tick (el contador sigue corriendo). Las barreras aseguran que, al
// volver de aquí, ya no puede entrar un tick nuevo; uno que ya estuviera pendiente se atiende
// antes de que el programa principal toque el bus.
void Tick_Bloquear(void)
{
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    __DSB();
    __ISB();
}

// Reanuda la interrupción del tick
void Tick_Desbloquear(void)
{
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
}

// Rutina de interrupción del SysTick (nombre fijo del archivo de arranque)
void SysTick_Handler(void)
{
    static uint8_t cuenta_io = 0;
    static uint8_t cuenta_pot = 0;
    static uint8_t cuenta_watch = 2;    // Desfase: le toca 3 ticks antes que a IOMAP

    // POT2SEG va antes del refresco para que el valor nuevo se muestre en este mismo tick
    cuenta_pot++;
    if (cuenta_pot >= POT_PERIODO_TICKS)
    {
        cuenta_pot = 0;
        Pot2Seg_Actualizar();   // Potenciómetro -> display (una conversión de unos 41 us)
    }

    Segment_Refresh();      // Un dígito del display de 7 segmentos por tick

    cuenta_io++;
    if (cuenta_io >= IO_PERIODO_TICKS)
    {
        cuenta_io = 0;
        IO_Actualizar();    // Switches -> LEDs según IOMAP
    }

    cuenta_watch++;
    if (cuenta_watch >= WATCH_PERIODO_TICKS)
    {
        cuenta_watch = 0;
        Watch_Actualizar(); // Un watch de LCDWATCH -> LCD (solo si su valor cambió)
    }

    Arcade_Actualizar();    // Breakout: una tarea pequeña por tick (no hace nada si no está activo)
}