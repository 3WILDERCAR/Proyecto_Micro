#ifndef ADC_H
#define ADC_H

#define VREF_MV     3300U
#define ADC_MAX     4095U

void adc_config(void);
uint32_t adc_read(void);
void main_adc(char *buffer, uint32_t dato, uint32_t mv, volatile uint32_t i);

#endif /* ADC_H */