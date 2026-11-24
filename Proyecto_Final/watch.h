#ifndef WATCH_H
#define WATCH_H

#include <stdint.h>

/* LCDWATCH: hasta 4 direcciones de memoria en la LCD */

void Watch_Init(void);

/* Tarea de fondo */
void Watch_Actualizar(void);

/* Control desde otros módulos */
uint8_t Watch_Activo(void);
void Watch_Detener(void);

/* Comando */
void Cmd_LCDWATCH(char *args);   /* LCDWATCH addr [{dec|hex}] [{b|hw|w}] */

#endif /* WATCH_H */
