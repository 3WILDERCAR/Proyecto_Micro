#include "hw_comun.h"
#include "tick.h"
#include "display.h"

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
// Carga del tick: cada interrupción refresca un dígito con una transacción I2C de 5 bytes
// (unos 0.5 ms a 100 kHz), alrededor del 25 % del CPU con TICK_MS = 2.

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

// Rutina de interrupción del SysTick (nombre fijo del archivo de arranque)
void SysTick_Handler(void)
{
    Segment_Refresh();     // Un dígito del display de 7 segmentos por tick
}
