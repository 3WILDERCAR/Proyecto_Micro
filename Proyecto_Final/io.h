#ifndef IO_H
#define IO_H

// Entradas y salidas digitales GPIO (switches y LEDs).
// ESQUELETO: las funciones existen pero todavía no están implementadas.

// Inicialización de periféricos (separada de la ejecución de los comandos)
void IO_Init(void);

// Comandos. args apunta al texto que sigue al nombre del comando, ya sin espacios iniciales
// (mismo convenio que Cmd_MD, Cmd_MM y Cmd_BF en memory.h)
void Cmd_IOMAP(char *args);   // IOMAP in out [not] [all]
void Cmd_IOUNMAP(char *args);   // IOUNMAP

#endif /* IO_H */
