#ifndef WATCH_H
#define WATCH_H

#include <stdint.h>

// Monitoreo de direcciones de memoria en la pantalla LCD (LCDWATCH).
// Hasta cuatro watches; el quinto sobreescribe el más viejo. El tick (SysTick_Handler) llama a
// Watch_Actualizar() y redibuja la línea de un watch cuando su valor cambia, sin bloquear al monitor.

// Inicialización de la lista de watches (separada de la ejecución de los comandos)
void Watch_Init(void);

// Tarea de fondo: revisa un watch por llamada. Se llama desde SysTick_Handler.
void Watch_Actualizar(void);

// Uso desde otros módulos (ej. el comando LCD, que se apropia de la pantalla). Watch_Detener se
// llama con el tick bloqueado o desde el tick.
uint8_t Watch_Activo(void);     // 1 si hay al menos un watch en la pantalla
void Watch_Detener(void);       // Borra la lista de watches (no toca la pantalla)

// Comandos. args apunta al texto que sigue al nombre del comando, ya sin espacios iniciales
// (mismo convenio que Cmd_MD, Cmd_MM y Cmd_BF en memory.h)
void Cmd_LCDWATCH(char *args);   // LCDWATCH addr [{dec|hex}] [{b|hw|w}]

#endif /* WATCH_H */
