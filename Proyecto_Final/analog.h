#ifndef ANALOG_H
#define ANALOG_H

#include <stdint.h>

// Conversiones analógico-digital y digital-analógico, y monitoreo continuo del potenciómetro.

// Inicialización de periféricos (separada de la ejecución de los comandos)
uint8_t Analog_InitADC(void);   // Devuelve 1 si el ADC quedó listo, 0 si no
void Analog_InitDAC(void);      // Deja el DAC configurado y el canal apagado hasta el primer VOUT

// POT2SEG: tarea de fondo (desde SysTick_Handler cada POT_PERIODO_TICKS, ver tick.h) y control
// desde otros módulos (SEGMENT lo detiene antes de mostrar su número)
void Pot2Seg_Actualizar(void);
uint8_t Pot2Seg_Activo(void);
void Pot2Seg_Detener(void);

// Comandos. args apunta al texto que sigue al nombre del comando, ya sin espacios iniciales
// (mismo convenio que Cmd_MD, Cmd_MM y Cmd_BF en memory.h)
void Cmd_VIN(char *args);       // VIN [res]
void Cmd_VOUT(char *args);      // VOUT voltage [res]
void Cmd_POT2SEG(char *args);   // POT2SEG {on|off} [{dec|hex}]

#endif /* ANALOG_H */
