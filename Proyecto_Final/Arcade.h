#ifndef ARCADE_H
#define ARCADE_H

#include <stdint.h>

/* Juego breakout en la LCD (comando ARCADE) */

/* Tarea de fondo, cada tick */
void Arcade_Actualizar(void);

/* Control desde otros módulos (Arcade_Detener con el tick bloqueado) */
uint8_t Arcade_Activo(void);
void Arcade_Detener(void);

/* Comando */
void Cmd_ARCADE(char *args);    /* ARCADE breakout | ARCADE off */

#endif /* ARCADE_H */
