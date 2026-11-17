#ifndef HW_COMUN_H
#define HW_COMUN_H

#include <stdint.h>
#include "stm32g431xx.h"   // CMSIS de ST: estructuras de los periféricos (RCC->CR, TIM3->CCR1,
                           // GPIOA->MODER...), sus direcciones y las máscaras de bits (RCC_CR_HSION,
                           // TIM_CR1_CEN, I2C_CR2_START...). También incluye core_cm4.h (SysTick, NVIC).

// Definiciones y rutinas de hardware compartidas por los módulos de Green_Commands.
// Antes cada módulo repetía sus propias direcciones, offsets y bits; ahora todo sale del
// archivo oficial de ST y aquí solo queda lo que no está en él.

// Reloj del sistema: tras el reset corre con HSI16 y los prescalers de AHB y APB no dividen
// (RM0440, RCC_CFGR con valor de reset 0x00000005). Con el prescaler de APB en 1, el reloj de los
// timers es igual al del bus (RM0440 7.2.13), así que HCLK = PCLK1 = reloj de TIM3/TIM4 = 16 MHz.
// Si en el futuro se cambia el reloj del sistema, hay que ajustar este valor.
#define HCLK_HZ             16000000U

// Valores de los campos de 2 bits de GPIOx_MODER (RM0440 9.4.1)
#define GPIO_MODO_ENTRADA   0U
#define GPIO_MODO_SALIDA    1U
#define GPIO_MODO_AF        2U
#define GPIO_MODO_ANALOGICO 3U

// Valores de GPIOx_OTYPER (RM0440 9.4.2)
#define GPIO_PUSH_PULL      0U
#define GPIO_OPEN_DRAIN     1U

// Modos de salida de un canal de timer, OCxM[3:0] (RM0440 30.4)
#define TIM_OCM_FORZAR_BAJO 4U      // 0100: force inactive level
#define TIM_OCM_PWM1        6U      // 0110: PWM modo 1 (salida en 1 mientras CNT < CCRx)

// RCC
void RCC_HSI16_Encender(void);
void RCC_ActivarAPB1(uint32_t bit_reloj, uint32_t bit_reset);

// GPIO
void GPIO_ConfigAF(GPIO_TypeDef *puerto, uint8_t pin, uint8_t af, uint32_t tipo_salida);

// Timers de propósito general (TIM2-TIM4), canales 1 a 4
void TIM_ConfigBase(TIM_TypeDef *tim, uint32_t psc, uint32_t arr);
void TIM_CanalSalida(TIM_TypeDef *tim, uint8_t canal, uint32_t modo);
void TIM_CanalModo(TIM_TypeDef *tim, uint8_t canal, uint32_t modo);
void TIM_CanalHabilitar(TIM_TypeDef *tim, uint8_t canal);

#endif /* HW_COMUN_H */
