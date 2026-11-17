#ifndef WATCH_H
#define WATCH_H

// Monitoreo de direcciones de memoria en la pantalla LCD.
// ESQUELETO: las funciones existen pero todavía no están implementadas.

// Inicialización de periféricos (separada de la ejecución de los comandos)
void Watch_Init(void);

// Comandos. args apunta al texto que sigue al nombre del comando, ya sin espacios iniciales
// (mismo convenio que Cmd_MD, Cmd_MM y Cmd_BF en memory.h)
void Cmd_LCDWATCH(char *args);   // LCDWATCH addr [{dec|hex}] [{b|hw|w}]

#endif /* WATCH_H */
