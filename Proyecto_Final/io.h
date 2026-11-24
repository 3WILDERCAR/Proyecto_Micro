#ifndef IO_H
#define IO_H

#include <stdint.h>

/* Switches y LEDs en U5 */

/* Inicialización: 1 si U5 respondió */
uint8_t IO_Init(void);

/* Tarea de fondo: switches -> LEDs */
void IO_Actualizar(void);

/* Comandos */
void Cmd_IOMAP(char *args);     /* IOMAP in out [not] [all] */
void Cmd_IOUNMAP(char *args);   /* IOUNMAP */

#endif /* IO_H */
