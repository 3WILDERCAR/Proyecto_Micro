#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

/* Display de 7 segmentos y pantalla LCD */

/* Tamaño de la LCD: 84 columnas x 6 bancos de 8 píxeles */
#define LCD_ANCHO           84
#define LCD_BANCOS          6

/* Inicialización */
uint8_t Segment_Init(void);     /* 1 si U6 respondió */
void LCD_Init(void);

/* Multiplexado, cada tick */
void Segment_Refresh(void);

/* Uso del display desde otros módulos */
uint8_t Segment_Asegurar(void);             /* Inicializa U6 si hace falta */
void Segment_MostrarDecimal(int32_t num);   /* -999 a 9999 */
void Segment_MostrarHex(uint32_t valor);    /* 0x000 a 0xFFF */
void Segment_Apagar(void);

/* Fila del teclado en U6 (1 a 4, 0 = ninguna) */
uint8_t Segment_ElegirFilaTeclado(uint8_t fila);

/* Uso de la LCD desde otros módulos (con el tick bloqueado o desde el tick) */
void LCD_Borrar(void);
void LCD_EscribirLinea(uint8_t linea, const char *texto);       /* 14 caracteres */
void LCD_EscribirLineaChica(uint8_t linea, const char *texto);  /* 21 caracteres */
void LCD_EscribirBanco(uint8_t banco, const uint8_t columnas[LCD_ANCHO]);
const uint8_t *LCD_Glifo(char c);

/* Comandos */
void Cmd_SEGMENT(char *args);   /* SEGMENT num */
void Cmd_LCD(char *args);       /* LCD string */

#endif /* DISPLAY_H */
