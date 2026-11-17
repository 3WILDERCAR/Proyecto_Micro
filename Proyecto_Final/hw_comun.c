#include "hw_comun.h"

// Rutinas de hardware compartidas por los módulos de Green_Commands.
// Los registros y bits vienen de stm32g431xx.h (CMSIS de ST).

// RCC

// Enciende HSI16 y espera a que esté estable (RM0440 7.4.1).
// Solo se setea: apagarlo detendría el reloj del sistema.
void RCC_HSI16_Encender(void)
{
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY))
    {
        // Esperamos a que HSI16 esté listo
    }
}

// Habilita el reloj de un periférico de APB1 y lo reinicia desde el RCC, para dejar todos sus
// registros en su valor de reset sin importar lo que hubiera antes.
// bit_reloj: máscara de RCC_APB1ENR1 (ej. RCC_APB1ENR1_TIM3EN)
// bit_reset: máscara de RCC_APB1RSTR1 (ej. RCC_APB1RSTR1_TIM3RST)
void RCC_ActivarAPB1(uint32_t bit_reloj, uint32_t bit_reset)
{
    RCC->APB1ENR1 |= bit_reloj;

    RCC->APB1RSTR1 |= bit_reset;        // Activamos el reset
    RCC->APB1RSTR1 &= ~bit_reset;       // Lo liberamos
}

// GPIO

// Habilita el reloj del puerto. Aquí solo se setea el bit: limpiarlo primero apagaría por un
// instante el puerto que ya usan otros periféricos (ej. GPIOA con el USART1 del FTDI).
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

// Configura un pin en función alterna, en el orden que pide AyudaProgra (igual que ftdi_con.c):
// MODER = 00 primero para que el pin no maneje la línea, se limpian OTYPER, OSPEEDR, PUPDR y AFR,
// se escribe el tipo de salida y AFR, y al final MODER = 10.
// Velocidad baja y sin pull-up ni pull-down, suficiente para todos los usos de Green_Card.
// tipo_salida: GPIO_PUSH_PULL o GPIO_OPEN_DRAIN
void GPIO_ConfigAF(GPIO_TypeDef *puerto, uint8_t pin, uint8_t af, uint32_t tipo_salida)
{
    uint32_t pos2 = (uint32_t)pin * 2U;             // Campo de 2 bits en MODER, OSPEEDR, PUPDR
    uint32_t pos4 = ((uint32_t)pin & 7U) * 4U;      // Campo de 4 bits en AFR[0] (0-7) o AFR[1] (8-15)
    uint32_t afr = (uint32_t)pin >> 3;

    GPIO_HabilitarReloj(puerto);

    puerto->MODER   &= ~(3U << pos2);               // Entrada mientras se configura
    puerto->OSPEEDR &= ~(3U << pos2);               // Velocidad baja
    puerto->PUPDR   &= ~(3U << pos2);               // Sin pull internos

    puerto->OTYPER &= ~(1U << pin);
    puerto->OTYPER |= ((tipo_salida & 1U) << pin);

    puerto->AFR[afr] &= ~(0xFU << pos4);
    puerto->AFR[afr] |= (((uint32_t)af & 0xFU) << pos4);

    puerto->MODER |= (GPIO_MODO_AF << pos2);        // Al final, función alterna
}

// Timers de propósito general

// Campos de un canal en CCMR1/CCMR2 tomando el canal 1 como referencia. En TIM_CCMR1_OC1M el
// archivo de ST ya incluye OC1M[3] (bit 16), que no es contiguo con OC1M[2:0] (bits 6:4).
#define CCMR_CAMPOS_CANAL   (TIM_CCMR1_CC1S | TIM_CCMR1_OC1FE | TIM_CCMR1_OC1PE | \
                             TIM_CCMR1_OC1M | TIM_CCMR1_OC1CE)

// Campos de un canal en CCER tomando el canal 1 como referencia
#define CCER_CAMPOS_CANAL   (TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC1NP)

// CCMR1 contiene los canales 1 y 2, CCMR2 los canales 3 y 4 (RM0440 30.5)
static volatile uint32_t *TIM_RegistroCCMR(TIM_TypeDef *tim, uint8_t canal)
{
    return (canal <= 2) ? &tim->CCMR1 : &tim->CCMR2;
}

// Los canales 1 y 3 ocupan los bits 7:0 (y 16), los canales 2 y 4 los bits 15:8 (y 24)
static uint32_t TIM_DesplazamientoCCMR(uint8_t canal)
{
    return ((canal == 2) || (canal == 4)) ? 8U : 0U;
}

// Convierte un modo OCxM[3:0] a sus bits del canal 1 (OC1M[2:0] en 6:4, OC1M[3] en el bit 16)
static uint32_t TIM_BitsModo(uint32_t modo)
{
    uint32_t bits = (modo & 7U) << TIM_CCMR1_OC1M_Pos;

    if (modo & 8U)
    {
        bits |= TIM_CCMR1_OC1M_3;
    }
    return bits;
}

// Configura la base de tiempo: contador detenido, hacia arriba, alineado al borde, con precarga
// de ARR (ARPE), y los valores de PSC y ARR. No arranca el contador.
void TIM_ConfigBase(TIM_TypeDef *tim, uint32_t psc, uint32_t arr)
{
    tim->CR1 &= ~(TIM_CR1_CEN | TIM_CR1_UDIS | TIM_CR1_URS | TIM_CR1_OPM |
                  TIM_CR1_DIR | TIM_CR1_CMS | TIM_CR1_ARPE);
    tim->CR1 |= TIM_CR1_ARPE;

    tim->PSC = psc;     // Registros numéricos: se escribe el valor completo
    tim->ARR = arr;
}

// Configura un canal (1 a 4) como salida (CCxS = 00) con precarga de CCRx (OCxPE = 1) y el modo
// indicado. Los valores precargados pasan a los registros sombra con el siguiente evento de
// actualización (bit UG de EGR o desborde del contador).
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

// Cambia solo el modo OCxM de un canal ya configurado. OCxM no tiene precarga: el cambio se
// aplica de inmediato (ej. "force inactive" baja la salida al instante).
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

// Habilita la salida de un canal (CCxE = 1) con polaridad activa en alto (CCxP = 0, CCxNP = 0).
// En CCER cada canal ocupa 4 bits: el canal n empieza en el bit 4 * (n - 1) (RM0440 30.5).
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
