#include "hw_comun.h"

/* Rutinas de hardware compartidas */

/* ---------- RCC ---------- */

/* Enciende HSI16 y espera a que esté listo */
void RCC_HSI16_Encender(void)
{
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY))
    {
        /* Espera */
    }
}

/* Reloj y reset de un periférico de APB1 */
void RCC_ActivarAPB1(uint32_t bit_reloj, uint32_t bit_reset)
{
    RCC->APB1ENR1 |= bit_reloj;

    RCC->APB1RSTR1 |= bit_reset;
    RCC->APB1RSTR1 &= ~bit_reset;
}

/* Reloj y reset de un periférico de APB2 */
void RCC_ActivarAPB2(uint32_t bit_reloj, uint32_t bit_reset)
{
    RCC->APB2ENR |= bit_reloj;

    RCC->APB2RSTR |= bit_reset;
    RCC->APB2RSTR &= ~bit_reset;
}

/* ---------- GPIO ---------- */

/* Reloj del puerto */
static void GPIO_HabilitarReloj(GPIO_TypeDef *puerto)
{
    if (puerto == GPIOA)
    {
        RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    }
    else if (puerto == GPIOB)
    {
        RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    }
}

/* Pin en función alterna */
void GPIO_ConfigAF(GPIO_TypeDef *puerto, uint8_t pin, uint8_t af, uint32_t tipo_salida)
{
    uint32_t pos2 = (uint32_t)pin * 2U;
    uint32_t pos4 = ((uint32_t)pin & 7U) * 4U;
    uint32_t afr = (uint32_t)pin >> 3;

    GPIO_HabilitarReloj(puerto);

    puerto->MODER   &= ~(3U << pos2);               /* Entrada mientras se configura */
    puerto->OSPEEDR &= ~(3U << pos2);
    puerto->PUPDR   &= ~(3U << pos2);

    puerto->OTYPER &= ~(1U << pin);
    puerto->OTYPER |= ((tipo_salida & 1U) << pin);

    puerto->AFR[afr] &= ~(0xFU << pos4);
    puerto->AFR[afr] |= (((uint32_t)af & 0xFU) << pos4);

    puerto->MODER |= (GPIO_MODO_AF << pos2);
}

/* Pin analógico */
void GPIO_ConfigAnalogico(GPIO_TypeDef *puerto, uint8_t pin)
{
    uint32_t pos2 = (uint32_t)pin * 2U;
    uint32_t pos4 = ((uint32_t)pin & 7U) * 4U;
    uint32_t afr = (uint32_t)pin >> 3;

    GPIO_HabilitarReloj(puerto);

    puerto->MODER   &= ~(3U << pos2);
    puerto->OTYPER  &= ~(1U << pin);
    puerto->OSPEEDR &= ~(3U << pos2);
    puerto->PUPDR   &= ~(3U << pos2);
    puerto->AFR[afr] &= ~(0xFU << pos4);

    puerto->MODER |= (GPIO_MODO_ANALOGICO << pos2);
}

/* Escribe 1 o 0 en un pin (BSRR) */
void GPIO_Escribir(GPIO_TypeDef *puerto, uint8_t pin, uint8_t valor)
{
    if (valor)
    {
        puerto->BSRR = (1U << pin);
    }
    else
    {
        puerto->BSRR = (1U << (pin + 16U));
    }
}

/* Pin de salida push-pull con su valor inicial */
void GPIO_ConfigSalida(GPIO_TypeDef *puerto, uint8_t pin, uint8_t valor_inicial)
{
    uint32_t pos2 = (uint32_t)pin * 2U;
    uint32_t pos4 = ((uint32_t)pin & 7U) * 4U;
    uint32_t afr = (uint32_t)pin >> 3;

    GPIO_HabilitarReloj(puerto);

    puerto->MODER   &= ~(3U << pos2);
    puerto->OTYPER  &= ~(1U << pin);
    puerto->OSPEEDR &= ~(3U << pos2);
    puerto->PUPDR   &= ~(3U << pos2);
    puerto->AFR[afr] &= ~(0xFU << pos4);

    GPIO_Escribir(puerto, pin, valor_inicial);      /* Valor antes de ser salida */

    puerto->MODER |= (GPIO_MODO_SALIDA << pos2);
}

/* Pin de entrada con pull */
void GPIO_ConfigEntrada(GPIO_TypeDef *puerto, uint8_t pin, uint32_t pull)
{
    uint32_t pos2 = (uint32_t)pin * 2U;
    uint32_t pos4 = ((uint32_t)pin & 7U) * 4U;
    uint32_t afr = (uint32_t)pin >> 3;

    GPIO_HabilitarReloj(puerto);

    puerto->MODER   &= ~(3U << pos2);
    puerto->OTYPER  &= ~(1U << pin);
    puerto->OSPEEDR &= ~(3U << pos2);
    puerto->AFR[afr] &= ~(0xFU << pos4);

    puerto->PUPDR &= ~(3U << pos2);
    puerto->PUPDR |= ((pull & 3U) << pos2);
}

/* ---------- Retardo ---------- */

/* Espera de al menos 'us' microsegundos:
   vueltas = us x 16 (ciclos de 16 MHz en 1 us). Cada vuelta tarda varios ciclos,
   así que la espera real es mayor que la pedida, nunca menor. */
void Retardo_us(uint32_t us)
{
    volatile uint32_t vueltas = us * (HCLK_HZ / 1000000U);

    while (vueltas > 0)
    {
        vueltas--;
    }
}

/* ---------- Timers ---------- */

/* Campos de un canal en CCMR y CCER (referencia: canal 1) */
#define CCMR_CAMPOS_CANAL   (TIM_CCMR1_CC1S | TIM_CCMR1_OC1FE | TIM_CCMR1_OC1PE | \
                             TIM_CCMR1_OC1M | TIM_CCMR1_OC1CE)

#define CCER_CAMPOS_CANAL   (TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC1NP)

/* CCMR1: canales 1 y 2. CCMR2: canales 3 y 4 */
static volatile uint32_t *TIM_RegistroCCMR(TIM_TypeDef *tim, uint8_t canal)
{
    return (canal <= 2) ? &tim->CCMR1 : &tim->CCMR2;
}

/* Desplazamiento del canal dentro de CCMR */
static uint32_t TIM_DesplazamientoCCMR(uint8_t canal)
{
    return ((canal == 2) || (canal == 4)) ? 8U : 0U;
}

/* Bits de OCxM (el bit 3 va aparte) */
static uint32_t TIM_BitsModo(uint32_t modo)
{
    uint32_t bits = (modo & 7U) << TIM_CCMR1_OC1M_Pos;

    if (modo & 8U)
    {
        bits |= TIM_CCMR1_OC1M_3;
    }
    return bits;
}

/* Base de tiempo: PSC y ARR, contador detenido */
void TIM_ConfigBase(TIM_TypeDef *tim, uint32_t psc, uint32_t arr)
{
    tim->CR1 &= ~(TIM_CR1_CEN | TIM_CR1_UDIS | TIM_CR1_URS | TIM_CR1_OPM |
                  TIM_CR1_DIR | TIM_CR1_CMS | TIM_CR1_ARPE);
    tim->CR1 |= TIM_CR1_ARPE;

    tim->PSC = psc;
    tim->ARR = arr;
}

/* Canal como salida con precarga y el modo indicado */
void TIM_CanalSalida(TIM_TypeDef *tim, uint8_t canal, uint32_t modo)
{
    volatile uint32_t *ccmr;
    uint32_t desp;

    if (canal < 1 || canal > 4)
    {
        return;
    }

    ccmr = TIM_RegistroCCMR(tim, canal);
    desp = TIM_DesplazamientoCCMR(canal);

    *ccmr &= ~(CCMR_CAMPOS_CANAL << desp);
    *ccmr |= ((TIM_CCMR1_OC1PE | TIM_BitsModo(modo)) << desp);
}

/* Cambia solo el modo del canal (efecto inmediato) */
void TIM_CanalModo(TIM_TypeDef *tim, uint8_t canal, uint32_t modo)
{
    volatile uint32_t *ccmr;
    uint32_t desp;

    if (canal < 1 || canal > 4)
    {
        return;
    }

    ccmr = TIM_RegistroCCMR(tim, canal);
    desp = TIM_DesplazamientoCCMR(canal);

    *ccmr &= ~(TIM_CCMR1_OC1M << desp);
    *ccmr |= (TIM_BitsModo(modo) << desp);
}

/* Habilita la salida del canal, activa en alto */
void TIM_CanalHabilitar(TIM_TypeDef *tim, uint8_t canal)
{
    uint32_t desp;

    if (canal < 1 || canal > 4)
    {
        return;
    }

    desp = 4U * (uint32_t)(canal - 1);

    tim->CCER &= ~(CCER_CAMPOS_CANAL << desp);
    tim->CCER |= (TIM_CCER_CC1E << desp);
}

/* Compara una palabra del comando sin distinguir mayúsculas */
uint8_t Texto_EsPalabra(const char *str, const char *palabra)
{
    uint8_t i = 0;

    while (palabra[i] != '\0')
    {
        char c = str[i];

        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c - 'A' + 'a');
        }
        if (c != palabra[i])
        {
            return 0;
        }
        i++;
    }

    if (str[i] != '\0' && str[i] != ' ')
    {
        return 0;
    }
    return i;
}
