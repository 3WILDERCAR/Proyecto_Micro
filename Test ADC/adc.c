#include "stm32g4xx.h"
#include <stdio.h>
#include <stdint.h>
#include "adc.h"
#include "monitor.h"

void adc_config(void)
{
    volatile uint32_t i;

    /* reloj GPIOA y ADC12 */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_ADC12EN;

    /* PA0 como entrada analogica (MODER0 = 11) */
    GPIOA->MODER |= (0x3U << (0 * 2));

    /* ADC2_CFGR: OVRMOD (bit 12) */
    ADC2->CFGR |= (1U << 12);

    /* ADC2_SQR1: primera conversion = canal 1 (PA0) */
    ADC2->SQR1 &= ~(0x1FU << 6);
    ADC2->SQR1 |= (1U << 6);

    /* ADC12_CCR: CKMODE = 11 (HCLK/4) */
    ADC12_COMMON->CCR |= (3U << 16);

    /* salir de deep-power-down y activar regulador */
    ADC2->CR &= ~ADC_CR_DEEPPWD;
    ADC2->CR |= ADC_CR_ADVREGEN;

    /* esperar a que el regulador se estabilice */
    for (i = 2000; i > 0; i--)
    {
    }

    /* habilitar ADC y esperar ADRDY */
    ADC2->CR |= ADC_CR_ADEN;
    while ((ADC2->ISR & ADC_ISR_ADRDY) == 0)
    {
    }
}

uint32_t adc_read(void)
{
    ADC2->CR |= ADC_CR_ADSTART;

    /* esperar EOC */
    while ((ADC2->ISR & ADC_ISR_EOC) == 0)
    {
    }

    return ADC2->DR;   /* leer DR limpia EOC */
}

void main_adc(char *buffer, uint32_t dato, uint32_t mv, volatile uint32_t i)
{
    dato = adc_read();

    /* dato (0..4095) -> milivoltios, sin usar float */
    mv = (dato * VREF_MV) / ADC_MAX;

    sprintf(buffer, "Voltaje PA1: %lu.%03lu V\r\n", (unsigned long) (mv / 1000U), (unsigned long) (mv % 1000U));

    UART_Print(buffer);

    /* pausa para no inundar Tera Term */
    for (i = 0; i < 500000; i++)
    {
    }
}