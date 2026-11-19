#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

// Display de 7 segmentos multi-dígito y pantalla LCD.

// Inicialización de periféricos (separada de la ejecución de los comandos)
uint8_t Segment_Init(void);     // Devuelve 1 si U6 respondió, 0 si no
void LCD_Init(void);            // SPI1 + PCD8544; deja la pantalla en blanco

// Multiplexado del display: muestra un dígito por llamada. Se llama desde SysTick_Handler.
void Segment_Refresh(void);

// Uso del display desde otros módulos (ej. POT2SEG). Las funciones Mostrar y Apagar solo cambian
// lo que el tick muestra y se pueden llamar desde el tick; Segment_Asegurar usa el bus y se llama
// solo desde el programa principal.
uint8_t Segment_Asegurar(void);             // Inicializa U6 si no está en línea; 1 si quedó listo
void Segment_MostrarDecimal(int32_t num);   // -999 a 9999, alineado a la derecha
void Segment_MostrarHex(uint32_t valor);    // 0x000 a 0xFFF, como "7A3h"
void Segment_Apagar(void);                  // Apaga el display y detiene el multiplexado

// Uso de la LCD desde otros módulos (ej. LCDWATCH). Usan SPI1: el programa principal solo las
// llama con el tick bloqueado (Tick_Bloquear), o desde el tick mismo.
void LCD_Borrar(void);                                          // Pantalla completa en blanco
void LCD_EscribirLinea(uint8_t linea, const char *texto);       // Línea 0-5, fuente 6x8, 14 caracteres
void LCD_EscribirLineaChica(uint8_t linea, const char *texto);  // Línea 0-5, fuente 4x8, 21 caracteres
                                                                // (solo 0-9, A-F, h, ':', '-' y espacio)

// Comandos. args apunta al texto que sigue al nombre del comando, ya sin espacios iniciales
// (mismo convenio que Cmd_MD, Cmd_MM y Cmd_BF en memory.h)
void Cmd_SEGMENT(char *args);   // SEGMENT num
void Cmd_LCD(char *args);       // LCD string

#endif /* DISPLAY_H */
