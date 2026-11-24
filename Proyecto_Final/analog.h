#ifndef ANALOG_H
#define ANALOG_H

#include <stdint.h>

/* ADC, DAC y POT2SEG */

/* Inicialización */
uint8_t Analog_InitADC(void);   /* 1 si el ADC quedó listo */
void Analog_InitDAC(void);

/* POT2SEG: tarea de fondo y control */
void Pot2Seg_Actualizar(void);
uint8_t Pot2Seg_Activo(void);
void Pot2Seg_Detener(void);

/* Lectura del potenciómetro (desde el tick o con el tick bloqueado) */
uint8_t Analog_ADCListo(void);
uint8_t Analog_LeerPot12(uint32_t *lectura);    /* Lectura de 0 a 4095 */

/* Comandos */
void Cmd_VIN(char *args);       /* VIN [res] */
void Cmd_VOUT(char *args);      /* VOUT voltage [res] */
void Cmd_POT2SEG(char *args);   /* POT2SEG {on|off} [{dec|hex}] */

#endif /* ANALOG_H */
