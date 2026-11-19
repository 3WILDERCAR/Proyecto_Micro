#ifndef IO_H
#define IO_H

#include <stdint.h>

// Entradas y salidas digitales: switches SW2-SW9 y LEDs D3-D10 en el expansor U5.

// Inicialización de U5 (LEDs apagados). Devuelve 1 si U5 respondió, 0 si no
uint8_t IO_Init(void);

// Tarea de fondo: lee los switches y actualiza los LEDs según los mapeos de IOMAP.
// Se llama desde SysTick_Handler cada IO_PERIODO_TICKS (ver tick.h).
void IO_Actualizar(void);

// Comandos. args apunta al texto que sigue al nombre del comando, ya sin espacios iniciales
// (mismo convenio que Cmd_MD, Cmd_MM y Cmd_BF en memory.h)
void Cmd_IOMAP(char *args);     // IOMAP in out [not] [all]
void Cmd_IOUNMAP(char *args);   // IOUNMAP

#endif /* IO_H */
