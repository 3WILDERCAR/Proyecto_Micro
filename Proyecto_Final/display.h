#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

// Display de 7 segmentos multi-dígito y pantalla LCD.

// Inicialización de periféricos (separada de la ejecución de los comandos)
uint8_t Segment_Init(void);     // Devuelve 1 si U6 respondió, 0 si no
void LCD_Init(void);            // Pendiente (comando LCD)

// Multiplexado del display: muestra un dígito por llamada. Se llama desde SysTick_Handler.
void Segment_Refresh(void);

// Comandos. args apunta al texto que sigue al nombre del comando, ya sin espacios iniciales
// (mismo convenio que Cmd_MD, Cmd_MM y Cmd_BF en memory.h)
void Cmd_SEGMENT(char *args);   // SEGMENT num
void Cmd_LCD(char *args);       // LCD string

#endif /* DISPLAY_H */
