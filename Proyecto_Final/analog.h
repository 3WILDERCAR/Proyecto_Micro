#ifndef ANALOG_H
#define ANALOG_H

// Conversiones analógico-digital y digital-analógico, y monitoreo continuo del potenciómetro.
// ESQUELETO: las funciones existen pero todavía no están implementadas.

// Inicialización de periféricos (separada de la ejecución de los comandos)
void Analog_InitADC(void);
void Analog_InitDAC(void);

// Comandos. args apunta al texto que sigue al nombre del comando, ya sin espacios iniciales
// (mismo convenio que Cmd_MD, Cmd_MM y Cmd_BF en memory.h)
void Cmd_VIN(char *args);   // VIN [res]
void Cmd_VOUT(char *args);   // VOUT voltage [res]
void Cmd_POT2SEG(char *args);   // POT2SEG {on|off} [{dec|hex}]

#endif /* ANALOG_H */
