#ifndef ARCADE_H
#define ARCADE_H

#include <stdint.h>

// Videojuego breakout en la pantalla LCD (comando ARCADE).
// Corre en paralelo con el monitor: SysTick_Handler llama a Arcade_Actualizar() en cada tick
// (ver tick.c) y el juego avanza solo, sin bloquear el programa principal.
//
// Entradas del juego:
//  - Potenciómetro (PA0, ADC1_IN1): posición absoluta de la barra.
//  - Teclas * y # del teclado matricial: inician cada jugada (cada saque de la bola).

// Tarea de fondo. Se llama desde SysTick_Handler en cada tick (cada TICK_MS).
void Arcade_Actualizar(void);

// Uso desde otros módulos (ej. los comandos LCD y LCDWATCH, que se apropian de la pantalla).
// Arcade_Detener se llama con el tick bloqueado o desde el tick; no toca la pantalla.
uint8_t Arcade_Activo(void);    // 1 si el juego está corriendo
void Arcade_Detener(void);      // Detiene el juego y libera los pines del teclado

// Comando. args apunta al texto que sigue al nombre del comando, ya sin espacios iniciales
// (mismo convenio que Cmd_MD, Cmd_MM y Cmd_BF en memory.h)
void Cmd_ARCADE(char *args);    // ARCADE breakout   (ARCADE off para salir)

#endif /* ARCADE_H */
