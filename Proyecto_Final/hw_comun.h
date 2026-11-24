#ifndef HW_COMUN_H
#define HW_COMUN_H

#include <stdint.h>
#include "stm32g431xx.h"   /* Registros y bits de ST (CMSIS) */

/* Definiciones y rutinas compartidas */

/* Reloj del sistema: HSI16 sin divisores */
#define HCLK_HZ             16000000U

/* Modos de GPIOx_MODER */
#define GPIO_MODO_ENTRADA   0U
#define GPIO_MODO_SALIDA    1U
#define GPIO_MODO_AF        2U
#define GPIO_MODO_ANALOGICO 3U

/* Valores de GPIOx_PUPDR */
#define GPIO_SIN_PULL       0U
#define GPIO_PULL_UP        1U
#define GPIO_PULL_DOWN      2U

/* Valores de GPIOx_OTYPER */
#define GPIO_PUSH_PULL      0U
#define GPIO_OPEN_DRAIN     1U

/* Modos de salida de un canal de timer (OCxM) */
#define TIM_OCM_FORZAR_BAJO 4U
#define TIM_OCM_PWM1        6U

/* RCC */
void RCC_HSI16_Encender(void);
void RCC_ActivarAPB1(uint32_t bit_reloj, uint32_t bit_reset);
void RCC_ActivarAPB2(uint32_t bit_reloj, uint32_t bit_reset);

/* GPIO */
void GPIO_ConfigAF(GPIO_TypeDef *puerto, uint8_t pin, uint8_t af, uint32_t tipo_salida);
void GPIO_ConfigAnalogico(GPIO_TypeDef *puerto, uint8_t pin);
void GPIO_ConfigSalida(GPIO_TypeDef *puerto, uint8_t pin, uint8_t valor_inicial);
void GPIO_ConfigEntrada(GPIO_TypeDef *puerto, uint8_t pin, uint32_t pull);
void GPIO_Escribir(GPIO_TypeDef *puerto, uint8_t pin, uint8_t valor);

/* Retardo aproximado */
void Retardo_us(uint32_t us);

/* Compara una palabra del comando (sin mayúsculas); devuelve su largo o 0 */
uint8_t Texto_EsPalabra(const char *str, const char *palabra);

/* Timers TIM2-TIM4 */
void TIM_ConfigBase(TIM_TypeDef *tim, uint32_t psc, uint32_t arr);
void TIM_CanalSalida(TIM_TypeDef *tim, uint8_t canal, uint32_t modo);
void TIM_CanalModo(TIM_TypeDef *tim, uint8_t canal, uint32_t modo);
void TIM_CanalHabilitar(TIM_TypeDef *tim, uint8_t canal);

#endif /* HW_COMUN_H */
