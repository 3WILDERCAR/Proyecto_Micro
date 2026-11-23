#include <stdint.h>
#include <string.h>
#include "hw_comun.h"
#include "arcade.h"
#include "display.h"
#include "analog.h"
#include "watch.h"
#include "i2c1.h"
#include "tick.h"
#include "monitor.h"   // UART_Print

// Videojuego breakout en la pantalla LCD (comando ARCADE breakout).
//
// Entradas (AyudaProgra, tabla de la NUCLEO-G431KB y pines de los MCP23017):
//  - POT: PA0 (ADC1_IN1). Su valor de 12 bits es la posición ABSOLUTA de la barra: 0 = toda a la
//    izquierda, 4095 = toda a la derecha. Se promedian 4 conversiones por paso para quitar ruido.
//  - Teclado matricial de 4 filas x 3 columnas (hoja Arcade, todavía sin dibujar en KiCad). Solo se
//    leen las teclas * y #, que están en la fila 4:
//        * = fila 4, columna 1      # = fila 4, columna 3
//    Cableado supuesto, a partir de los comentarios de display.c y de la tabla de pines reservados:
//        columna 1 = PA1 (RES_ARCADE)   columna 2 = U6 GPA7 (KP_C2, no se usa)   columna 3 = PA8
//        filas 1 a 4 = U6 GPB4 a GPB7 (entradas con pull-up, activo en 0)
//    Si el esquemático final cambia el orden, solo hay que ajustar los ARC_PIN_COLx y
//    ARC_FILA4_MASCARA de más abajo.
//
// Lectura del teclado: las columnas son salidas en drenaje abierto (se bajan o se sueltan, nunca se
// manejan en alto, así dos teclas presionadas a la vez no pueden cortocircuitar dos salidas). Para
// leer una columna se baja y se lee GPIOB de U6; la fila 4 en 0 significa tecla presionada.
// La columna 2 (GPA7) no se toca: sigue como entrada con pull-up, como la deja Segment_Init.
//
// Ejecución sin bloquear: SysTick_Handler llama a Arcade_Actualizar() en cada tick (2 ms). El juego
// avanza en ciclos de ARC_PERIODO_TICKS ticks (20 ms) y reparte el trabajo para que ningún tick haga
// más de una tarea pesada (el display de 7 segmentos y IOMAP también corren en el tick):
//    fase 0     : lee el potenciómetro, mueve barra y bola, arma el cuadro (sin usar buses)
//    fases 1 a 6: manda UN banco de la LCD por SPI1 (84 bytes, unos 0.5 ms), solo los que cambiaron
//    fase 7     : lee la columna 1 del teclado (*)   (una lectura I2C, unos 0.4 ms)
//    fase 8     : lee la columna 3 del teclado (#)
//    fase 9     : libre
//
// Pantalla (84 x 48, 6 bancos de 8 píxeles; cada byte es una columna con el bit 0 arriba):
//    y  0 a  7 : marcador (PTS y vidas)
//    y 10 a 24 : 4 filas x 7 ladrillos (11 x 3 píxeles, separados 1 píxel)
//    y 45 a 46 : barra de 14 x 2 píxeles
//    La bola es de 2 x 2 píxeles y se mueve en subpíxeles de 1/16.
// El cuadro se arma en RAM (capa fija con marcador y ladrillos, más barra y bola encima) y solo se
// transmiten los bancos que cambiaron respecto de lo último enviado.
//
// Reglas: 3 vidas. Cada jugada (saque) empieza con * o #. La bola rebota en las paredes, el techo,
// la barra (el ángulo depende de dónde pega en la barra) y los ladrillos. Las filas de arriba valen
// más puntos y la bola acelera al destruir ladrillos. Si cae, pierde una vida; sin vidas sale
// GAME OVER y con todos los ladrillos destruidos sale GANASTE. En ambos casos * o # juega de nuevo.
//
// Uso compartido de SPI1 y del bus I2C1: igual que el resto de módulos, el programa principal solo
// los usa con el tick bloqueado (Tick_Bloquear). Por eso Cmd_ARCADE prepara todo con el tick pausado.

// ---------------------------------------------------------------------------------------------
// Parámetros ajustables

#define ARC_POT_INVERTIDO       0       // 1 si el potenciómetro está cableado al revés

#define ARC_PIN_COL1            1       // PA1: columna 1 del teclado (tecla *)
#define ARC_PIN_COL3            8       // PA8: columna 3 del teclado (tecla #)
#define ARC_FILA4_MASCARA       (1U << 7)   // U6 GPB7: fila 4 (en 0 = tecla presionada)
#define ARC_REBOTE_LECTURAS     2       // Lecturas seguidas (40 ms) para aceptar una tecla

#define ARC_VIDAS               3
#define ARC_RAPIDEZ_INICIAL     13      // Subpíxeles por paso en vertical (13/16 = 0.8 píxeles)
#define ARC_RAPIDEZ_MAXIMA      17


// Constantes del juego y de la pantalla

#define ARC_ANCHO               84
#define ARC_ALTO                48
#define ARC_BANCOS              6

#define ARC_PERIODO_TICKS       10      // 10 ticks = 20 ms por paso del juego
#define ARC_FASE_COL1           7
#define ARC_FASE_COL3           8

#define ARC_SUB                 4       // Bits de subpíxel: las posiciones de la bola van en 1/16

#define ARC_CAMPO_Y             8       // Primera fila de píxeles del campo de juego (bajo el marcador)

#define ARC_LAD_COLS            7
#define ARC_LAD_FILAS           4
#define ARC_LAD_TOTAL           (ARC_LAD_COLS * ARC_LAD_FILAS)
#define ARC_LAD_W               11
#define ARC_LAD_H               3
#define ARC_LAD_PASO_X          12      // 7 x 12 = 84 píxeles justos
#define ARC_LAD_PASO_Y          4
#define ARC_LAD_Y0              10

#define ARC_PALETA_W            14
#define ARC_PALETA_H            2
#define ARC_PALETA_Y            45

#define ARC_BOLA                2       // Lado de la bola, en píxeles

// Pines de la LCD (los mismos de display.c)
#define ARC_PIN_LCD_CE          11      // PA11
#define ARC_PIN_LCD_DC          12      // PA12

#define ARC_SPI_TIMEOUT         20000U
#define ARC_ADC_TIMEOUT         2000U
#define ARC_ADC_MUESTRAS        4
#define ARC_LCD_DR8             (*((volatile uint8_t *)&SPI1->DR))  // Acceso de 8 bits al DR

typedef enum
{
    ARC_ESPERANDO = 0,      // La bola está sobre la barra esperando * o #
    ARC_JUGANDO,            // La bola está en juego
    ARC_GAMEOVER,           // Sin vidas
    ARC_GANO                // Sin ladrillos
} ArcEstado_t;

// Fuente 5 x 7 (ASCII 0x20 a 0x5A: espacio, símbolos, dígitos y mayúsculas), la misma de display.c.
// Cada carácter son 5 columnas; en cada byte el bit 0 es el píxel de arriba.
static const uint8_t arc_fuente[59][5] =
{
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00},   //   ! "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},   // # $ %
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1C, 0x22, 0x41, 0x00},   // & ' (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x14, 0x08, 0x3E, 0x08, 0x14}, {0x08, 0x08, 0x3E, 0x08, 0x08},   // ) * +
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00},   // , - .
    {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},   // / 0 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10},   // 2 3 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},   // 5 6 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00},   // 8 9 :
    {0x00, 0x56, 0x36, 0x00, 0x00}, {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},   // ; < =
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x32, 0x49, 0x79, 0x41, 0x3E},   // > ? @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},   // A B C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},   // D E F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},   // G H I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},   // J K L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},   // M N O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},   // P Q R
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},   // S T U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},   // V W X
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},   // Y Z
};

// Puntos por ladrillo según su fila (la de arriba vale más)
static const uint8_t arc_puntos_fila[ARC_LAD_FILAS] = {40, 30, 20, 10};

// ---------------------------------------------------------------------------------------------
// Estado. Todo se toca desde el tick; el programa principal solo lo cambia con el tick bloqueado.

static volatile uint8_t arc_activo = 0;     // 1 si el juego está corriendo
static uint8_t arc_fase = 0;                // Fase dentro del ciclo de ARC_PERIODO_TICKS ticks
static ArcEstado_t arc_estado = ARC_ESPERANDO;

static uint8_t arc_ladrillos[ARC_LAD_FILAS];    // Bit n = ladrillo de la columna n sigue en pie
static uint32_t arc_puntos = 0;
static uint8_t arc_vidas = 0;
static uint8_t arc_destruidos = 0;

static int32_t arc_px = 0;                  // Posición X de la barra (píxel de su borde izquierdo)
static int32_t arc_bx = 0, arc_by = 0;      // Posición de la bola en 1/16 de píxel
static int32_t arc_vx = 0, arc_vy = 0;      // Velocidad de la bola en 1/16 de píxel por paso
static uint8_t arc_lado = 0;                // Alterna la dirección horizontal de cada saque

static uint8_t arc_tecla_cnt[2];            // Lecturas seguidas con la tecla presionada (0 = *, 1 = #)
static uint8_t arc_inicio = 0;              // 1 si se presionó * o # desde el último paso

static uint8_t arc_fija_sucia = 0;          // 1 si hay que rehacer la capa fija
static uint8_t arc_pendientes = 0;          // Bit n = el banco n está por enviarse a la LCD
static uint8_t arc_capa_fija[ARC_BANCOS][ARC_ANCHO];    // Marcador, ladrillos o mensaje
static uint8_t arc_cuadro[ARC_BANCOS][ARC_ANCHO];       // Capa fija + barra + bola
static uint8_t arc_enviado[ARC_BANCOS][ARC_ANCHO];      // Lo último que se mandó a la LCD


// LCD por SPI1 (misma secuencia que display.c, que ya dejó SPI1 y la pantalla configurados)

// Espera a que SPI1 termine de transmitir antes de cambiar DC o subir CE
static void Arc_LCD_EsperarFin(void)
{
    uint32_t restante = ARC_SPI_TIMEOUT;

    while ((SPI1->SR & SPI_SR_FTLVL) && (restante > 0))
    {
        restante--;
    }
    while ((SPI1->SR & SPI_SR_BSY) && (restante > 0))
    {
        restante--;
    }
}

// Envía n bytes a la LCD como comandos (dato = 0) o como datos de la pantalla (dato = 1)
static void Arc_LCD_Enviar(uint8_t dato, const uint8_t *bytes, uint16_t n)
{
    Arc_LCD_EsperarFin();                           // SPI1 libre antes de tocar DC o CE
    GPIO_Escribir(GPIOA, ARC_PIN_LCD_DC, dato);
    GPIO_Escribir(GPIOA, ARC_PIN_LCD_CE, 0);        // CE en bajo: empieza la transmisión

    for (uint16_t i = 0; i < n; i++)
    {
        uint32_t restante = ARC_SPI_TIMEOUT;

        while (!(SPI1->SR & SPI_SR_TXE) && (restante > 0))
        {
            restante--;
        }
        ARC_LCD_DR8 = bytes[i];
    }

    Arc_LCD_EsperarFin();                           // Último byte completo antes de subir CE
    GPIO_Escribir(GPIOA, ARC_PIN_LCD_CE, 1);
}

// Manda el banco 'b' del cuadro a la pantalla (columna 0, banco b, 84 columnas)
static void Arc_LCD_Banco(uint8_t b)
{
    uint8_t comandos[2];

    comandos[0] = 0x80U;                            // Set X = 0
    comandos[1] = (uint8_t)(0x40U | b);             // Set Y = banco
    Arc_LCD_Enviar(0, comandos, 2);
    Arc_LCD_Enviar(1, arc_cuadro[b], ARC_ANCHO);
}

// Fases 1 a 6: manda el siguiente banco pendiente (uno por tick)
static void Arc_EnviarBanco(void)
{
    uint8_t b = 0;

    if (arc_pendientes == 0)
    {
        return;
    }

    while (!(arc_pendientes & (1U << b)))
    {
        b++;
    }
    arc_pendientes &= (uint8_t)~(1U << b);

    memcpy(arc_enviado[b], arc_cuadro[b], ARC_ANCHO);
    Arc_LCD_Banco(b);
}


// Dibujo en las capas (cada byte es una columna de 8 píxeles con el bit 0 arriba)

// Rectángulo relleno de w x h píxeles con su esquina superior izquierda en (x, y). Recorta lo que
// se salga de la pantalla. Arma una máscara por banco y la aplica a cada columna.
static void Arc_Rect(uint8_t (*buf)[ARC_ANCHO], int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t x0 = (x < 0) ? 0 : x;
    int32_t x1 = ((x + w) > ARC_ANCHO) ? ARC_ANCHO : (x + w);
    int32_t y0 = (y < 0) ? 0 : y;
    int32_t y1 = ((y + h) > ARC_ALTO) ? ARC_ALTO : (y + h);

    if (x0 >= x1 || y0 >= y1)
    {
        return;
    }

    for (int32_t b = (y0 >> 3); b <= ((y1 - 1) >> 3); b++)
    {
        int32_t lo = (y0 > (b * 8)) ? (y0 - (b * 8)) : 0;
        int32_t hi = (y1 < ((b + 1) * 8)) ? (y1 - (b * 8)) : 8;    // Exclusivo
        uint8_t mascara = (uint8_t)(((1U << hi) - 1U) & ~((1U << lo) - 1U));

        for (int32_t cx = x0; cx < x1; cx++)
        {
            buf[b][cx] |= mascara;
        }
    }
}

// Escribe texto en un banco con la fuente de 5 x 7 (6 columnas por carácter). Las minúsculas se
// dibujan como mayúsculas y lo que no está en la tabla, como espacio.
static void Arc_Texto(uint8_t (*buf)[ARC_ANCHO], uint8_t banco, int32_t x, const char *texto)
{
    while (*texto != '\0')
    {
        char c = *texto++;
        uint8_t indice = 0;

        if (c >= 'a' && c <= 'z')
        {
            c = (char)(c - 'a' + 'A');
        }
        if (c >= 0x20 && c <= 0x5A)
        {
            indice = (uint8_t)(c - 0x20);
        }

        for (uint8_t j = 0; j < 6; j++)
        {
            int32_t col = x + j;

            if (col >= 0 && col < ARC_ANCHO)
            {
                buf[banco][col] = (j < 5) ? arc_fuente[indice][j] : 0x00;
            }
        }
        x += 6;
    }
}

static void Arc_TextoCentrado(uint8_t (*buf)[ARC_ANCHO], uint8_t banco, const char *texto)
{
    Arc_Texto(buf, banco, (ARC_ANCHO - (6 * (int32_t)strlen(texto))) / 2, texto);
}

// Escribe n en decimal en dst (mínimo 11 bytes). Devuelve cuántos dígitos tiene.
static uint8_t Arc_FormatearNumero(char *dst, uint32_t n)
{
    char tmp[11];
    uint8_t k = 0;

    do
    {
        tmp[k++] = (char)('0' + (n % 10U));
        n /= 10U;
    } while (n > 0 && k < 10);

    for (uint8_t i = 0; i < k; i++)
    {
        dst[i] = tmp[k - 1 - i];
    }
    dst[k] = '\0';
    return k;
}

// Pantalla de GAME OVER o GANASTE: título, puntaje y cómo volver a jugar
static void Arc_PantallaFinal(const char *titulo)
{
    char numero[11];
    uint8_t digitos = Arc_FormatearNumero(numero, arc_puntos);
    int32_t x0 = (ARC_ANCHO - (6 * (7 + (int32_t)digitos))) / 2;    // "PUNTOS " + número

    Arc_TextoCentrado(arc_capa_fija, 1, titulo);
    Arc_Texto(arc_capa_fija, 2, x0, "PUNTOS ");
    Arc_Texto(arc_capa_fija, 2, x0 + 42, numero);
    Arc_TextoCentrado(arc_capa_fija, 4, "* O # JUEGA");
}

// Rehace la capa fija según el estado: marcador y ladrillos (y el aviso de saque) o pantalla final
static void Arc_RedibujarFija(void)
{
    memset(arc_capa_fija, 0, sizeof(arc_capa_fija));

    if (arc_estado == ARC_GAMEOVER)
    {
        Arc_PantallaFinal("GAME OVER");
        return;
    }
    if (arc_estado == ARC_GANO)
    {
        Arc_PantallaFinal("GANASTE!");
        return;
    }

    // Marcador: "PTS 120" a la izquierda y un cuadrito por vida a la derecha
    {
        char numero[11];

        Arc_Texto(arc_capa_fija, 0, 0, "PTS");
        Arc_FormatearNumero(numero, arc_puntos);
        Arc_Texto(arc_capa_fija, 0, 24, numero);

        for (uint8_t v = 0; v < arc_vidas; v++)
        {
            Arc_Rect(arc_capa_fija, 63 + (7 * v), 2, 4, 4);
        }
    }

    // Ladrillos que siguen en pie
    for (uint8_t fila = 0; fila < ARC_LAD_FILAS; fila++)
    {
        for (uint8_t col = 0; col < ARC_LAD_COLS; col++)
        {
            if (arc_ladrillos[fila] & (1U << col))
            {
                Arc_Rect(arc_capa_fija, col * ARC_LAD_PASO_X, ARC_LAD_Y0 + (fila * ARC_LAD_PASO_Y),
                         ARC_LAD_W, ARC_LAD_H);
            }
        }
    }

    if (arc_estado == ARC_ESPERANDO)
    {
        Arc_TextoCentrado(arc_capa_fija, 4, "* O # INICIA");
    }
}

// Arma el cuadro: capa fija más la barra y la bola (solo mientras hay partida)
static void Arc_Componer(void)
{
    memcpy(arc_cuadro, arc_capa_fija, sizeof(arc_cuadro));

    if (arc_estado == ARC_ESPERANDO || arc_estado == ARC_JUGANDO)
    {
        Arc_Rect(arc_cuadro, arc_px, ARC_PALETA_Y, ARC_PALETA_W, ARC_PALETA_H);
        Arc_Rect(arc_cuadro, arc_bx >> ARC_SUB, arc_by >> ARC_SUB, ARC_BOLA, ARC_BOLA);
    }
}


// Potenciómetro (ADC1 ya configurado por Analog_InitADC: canal 1, 640.5 ciclos, conversión única)

// Promedia ARC_ADC_MUESTRAS conversiones de 12 bits (unos 41 us cada una). Devuelve 1 y el
// promedio en *valor, o 0 si el ADC no está habilitado o no termina. Como también la usa POT2SEG
// desde el tick y VIN bloquea el tick, nunca coincide con otra conversión.
static uint8_t Arc_LeerPot(uint32_t *valor)
{
    uint32_t suma = 0;

    if (!(ADC1->CR & ADC_CR_ADEN))
    {
        return 0;
    }

    ADC1->CFGR &= ~ADC_CFGR_RES;                    // RES = 00: 12 bits

    for (uint8_t i = 0; i < ARC_ADC_MUESTRAS; i++)
    {
        uint32_t restante = ARC_ADC_TIMEOUT;

        ADC1->ISR = (ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR);     // Limpia banderas (se escribe 1)
        ADC1->CR |= ADC_CR_ADSTART;

        while (!(ADC1->ISR & ADC_ISR_EOC))
        {
            if (restante == 0)
            {
                return 0;
            }
            restante--;
        }
        suma += ADC1->DR;                           // Leer DR limpia EOC
    }

    *valor = suma / ARC_ADC_MUESTRAS;
    return 1;
}


// Teclado: columnas en PA1 y PA8, filas en U6 GPB4-GPB7

// Deja una columna como salida en drenaje abierto, soltada (ODR = 1: alta impedancia). Se escribe
// ODR antes de pasar a salida para que el pin nunca baje por accidente.
static void Arc_ConfigColumna(uint8_t pin)
{
    uint32_t pos2 = (uint32_t)pin * 2U;
    uint32_t pos4 = ((uint32_t)pin & 7U) * 4U;

    GPIOA->BSRR = (1U << pin);                      // ODR = 1
    GPIOA->MODER &= ~(3U << pos2);                  // Entrada mientras se configura
    GPIOA->OTYPER |= (1U << pin);                   // Drenaje abierto
    GPIOA->OSPEEDR &= ~(3U << pos2);                // Velocidad baja
    GPIOA->PUPDR &= ~(3U << pos2);                  // Sin pull interno (las filas ya tienen pull-up)
    GPIOA->AFR[pin >> 3] &= ~(0xFU << pos4);
    GPIOA->MODER |= (GPIO_MODO_SALIDA << pos2);     // Al final, salida
}

// Fases 7 y 8: baja la columna (0 = columna 1 y tecla *, 1 = columna 3 y tecla #), lee la fila 4
// y la suelta. Una tecla cuenta como presionada después de ARC_REBOTE_LECTURAS lecturas seguidas
// y solo genera un inicio cuando se presiona (hay que soltarla para volver a contar).
static void Arc_EscanearColumna(uint8_t col)
{
    uint8_t pin_elegido = (col == 0) ? ARC_PIN_COL1 : ARC_PIN_COL3;
    uint8_t pin_otro = (col == 0) ? ARC_PIN_COL3 : ARC_PIN_COL1;
    uint8_t filas = 0xFF;
    uint8_t leido;

    // Suelta la otra columna y baja la elegida en una sola escritura
    GPIOA->BSRR = (1U << pin_otro) | (1U << (pin_elegido + 16U));
    leido = (MCP23017_ReadRegs(MCP_U6_ADDR, MCP_GPIOB, &filas, 1) == I2C1_OK);
    GPIOA->BSRR = (1U << pin_elegido);              // Suelta la columna

    if (leido && !(filas & ARC_FILA4_MASCARA))
    {
        if (arc_tecla_cnt[col] < ARC_REBOTE_LECTURAS)
        {
            arc_tecla_cnt[col]++;
            if (arc_tecla_cnt[col] == ARC_REBOTE_LECTURAS)
            {
                arc_inicio = 1;
            }
        }
    }
    else
    {
        arc_tecla_cnt[col] = 0;
    }
}

// ---------------------------------------------------------------------------------------------
// Juego

static int32_t Arc_Rapidez(void)
{
    int32_t r = ARC_RAPIDEZ_INICIAL + (arc_destruidos / ARC_LAD_COLS);  // +1 por cada 7 ladrillos

    return (r > ARC_RAPIDEZ_MAXIMA) ? ARC_RAPIDEZ_MAXIMA : r;
}

// Pone la bola sobre el centro de la barra (antes del saque)
static void Arc_BolaSobreBarra(void)
{
    arc_bx = (arc_px + (ARC_PALETA_W / 2) - 1) << ARC_SUB;
    arc_by = (ARC_PALETA_Y - ARC_BOLA) << ARC_SUB;
    arc_vx = 0;
    arc_vy = 0;
}

static void Arc_NuevoJuego(void)
{
    for (uint8_t fila = 0; fila < ARC_LAD_FILAS; fila++)
    {
        arc_ladrillos[fila] = (uint8_t)((1U << ARC_LAD_COLS) - 1U);
    }
    arc_puntos = 0;
    arc_vidas = ARC_VIDAS;
    arc_destruidos = 0;
    arc_estado = ARC_ESPERANDO;
    Arc_BolaSobreBarra();
    arc_fija_sucia = 1;
}

// Revisa si la bola, con su esquina superior izquierda en el píxel (px, py), toca un ladrillo. Si
// toca uno lo destruye y suma sus puntos. Devuelve 1 si hubo choque.
static uint8_t Arc_ChocaLadrillo(int32_t px, int32_t py)
{
    for (uint8_t i = 0; i < 4; i++)
    {
        int32_t x = px + (i & 1);               // Las cuatro esquinas de la bola
        int32_t y = py + (i >> 1);
        int32_t dy = y - ARC_LAD_Y0;
        int32_t fila;
        int32_t col;

        if (x < 0 || x >= ARC_ANCHO || dy < 0)
        {
            continue;
        }

        fila = dy / ARC_LAD_PASO_Y;
        col = x / ARC_LAD_PASO_X;
        if (fila >= ARC_LAD_FILAS || col >= ARC_LAD_COLS)
        {
            continue;
        }
        if ((dy % ARC_LAD_PASO_Y) >= ARC_LAD_H || (x % ARC_LAD_PASO_X) >= ARC_LAD_W)
        {
            continue;                           // Cayó en el espacio entre ladrillos
        }

        if (arc_ladrillos[fila] & (1U << col))
        {
            arc_ladrillos[fila] &= (uint8_t)~(1U << col);
            arc_puntos += arc_puntos_fila[fila];
            arc_destruidos++;
            arc_fija_sucia = 1;
            return 1;
        }
    }
    return 0;
}

// Mueve la bola un paso: primero en X y luego en Y, para saber de qué lado fue cada choque.
// Con la rapidez máxima la bola avanza poco más de 1 píxel por paso, menos que su tamaño y que
// el alto de un ladrillo, así que nunca atraviesa nada. Devuelve 1 si la bola se perdió.
static uint8_t Arc_MoverBola(void)
{
    int32_t nbx = arc_bx + arc_vx;
    int32_t nby;
    int32_t bxp;
    int32_t byp;

    // Horizontal: paredes laterales y ladrillos
    if (nbx < 0 || nbx > ((ARC_ANCHO - ARC_BOLA) << ARC_SUB))
    {
        arc_vx = -arc_vx;
    }
    else if (Arc_ChocaLadrillo(nbx >> ARC_SUB, arc_by >> ARC_SUB))
    {
        arc_vx = -arc_vx;
    }
    else
    {
        arc_bx = nbx;
    }

    // Vertical: techo y ladrillos
    nby = arc_by + arc_vy;
    if (nby < (ARC_CAMPO_Y << ARC_SUB))
    {
        arc_vy = -arc_vy;
    }
    else if (Arc_ChocaLadrillo(arc_bx >> ARC_SUB, nby >> ARC_SUB))
    {
        arc_vy = -arc_vy;
    }
    else
    {
        arc_by = nby;
    }

    // La rapidez crece con los ladrillos destruidos; el signo de vy no cambia
    arc_vy = (arc_vy < 0) ? -Arc_Rapidez() : Arc_Rapidez();

    // Barra: solo cuando la bola baja. El ángulo de salida depende de dónde pega: en el centro sale
    // casi vertical y en los extremos sale más inclinada hacia ese lado.
    bxp = arc_bx >> ARC_SUB;
    byp = arc_by >> ARC_SUB;
    if (arc_vy > 0 &&
        (byp + ARC_BOLA) >= ARC_PALETA_Y && byp <= (ARC_PALETA_Y + ARC_PALETA_H - 1) &&
        (bxp + ARC_BOLA) > arc_px && bxp < (arc_px + ARC_PALETA_W))
    {
        int32_t rel = (bxp + 1) - (arc_px + (ARC_PALETA_W / 2));    // -8 a +8

        arc_by = (ARC_PALETA_Y - ARC_BOLA) << ARC_SUB;
        arc_vy = -Arc_Rapidez();
        arc_vx = rel * 2;
        if (arc_vx > 16)
        {
            arc_vx = 16;
        }
        else if (arc_vx < -16)
        {
            arc_vx = -16;
        }
        if (arc_vx > -3 && arc_vx < 3)          // Nunca vertical puro: se quedaría en un bucle
        {
            arc_vx = (arc_vx < 0 || (arc_vx == 0 && arc_lado)) ? -3 : 3;
        }
        return 0;
    }

    // Perdida: la bola ya bajó del borde inferior de la pantalla
    return (arc_vy > 0 && (arc_by >> ARC_SUB) >= (ARC_ALTO - 1));
}

// Fase 0: un paso del juego y armado del cuadro
static void Arc_Paso(void)
{
    uint32_t pot;

    // Barra: posición absoluta con el potenciómetro (si falla la lectura se queda donde estaba)
    if (Arc_LeerPot(&pot))
    {
        if (pot > 4095U)
        {
            pot = 4095U;
        }
#if ARC_POT_INVERTIDO
        pot = 4095U - pot;
#endif
        arc_px = (int32_t)((pot * (uint32_t)(ARC_ANCHO - ARC_PALETA_W)) / 4095U);
    }

    switch (arc_estado)
    {
        case ARC_ESPERANDO:
            Arc_BolaSobreBarra();
            if (arc_inicio)
            {
                arc_vy = -Arc_Rapidez();
                arc_vx = arc_lado ? -6 : 6;
                arc_lado ^= 1U;
                arc_estado = ARC_JUGANDO;
                arc_fija_sucia = 1;             // Quita el aviso de saque
            }
            break;

        case ARC_JUGANDO:
            if (Arc_MoverBola())
            {
                arc_vidas--;
                arc_estado = (arc_vidas == 0) ? ARC_GAMEOVER : ARC_ESPERANDO;
                Arc_BolaSobreBarra();
                arc_fija_sucia = 1;
            }
            else if (arc_destruidos >= ARC_LAD_TOTAL)
            {
                arc_estado = ARC_GANO;
                arc_fija_sucia = 1;
            }
            break;

        case ARC_GAMEOVER:
        case ARC_GANO:
        default:
            if (arc_inicio)
            {
                Arc_NuevoJuego();
            }
            break;
    }
    arc_inicio = 0;                             // Una tecla solo vale para el paso siguiente

    if (arc_fija_sucia)
    {
        Arc_RedibujarFija();
        arc_fija_sucia = 0;
    }

    Arc_Componer();

    // Marca los bancos que cambiaron respecto de lo último enviado; las fases 1 a 6 los mandan
    for (uint8_t b = 0; b < ARC_BANCOS; b++)
    {
        if (memcmp(arc_cuadro[b], arc_enviado[b], ARC_ANCHO) != 0)
        {
            arc_pendientes |= (uint8_t)(1U << b);
        }
    }
}

// Tarea de fondo (desde SysTick_Handler, cada tick)
void Arcade_Actualizar(void)
{
    if (!arc_activo)
    {
        return;
    }

    if (arc_fase == 0)
    {
        Arc_Paso();
    }
    else if (arc_fase <= ARC_BANCOS)
    {
        Arc_EnviarBanco();
    }
    else if (arc_fase == ARC_FASE_COL1)
    {
        Arc_EscanearColumna(0);
    }
    else if (arc_fase == ARC_FASE_COL3)
    {
        Arc_EscanearColumna(1);
    }

    arc_fase++;
    if (arc_fase >= ARC_PERIODO_TICKS)
    {
        arc_fase = 0;
    }
}

uint8_t Arcade_Activo(void)
{
    return arc_activo;
}

// Detiene el juego y deja PA1 y PA8 como los dejó Setup_Seguridad (analógico, sin manejar nada).
// No toca la pantalla: quien llama decide qué mostrar después.
void Arcade_Detener(void)
{
    arc_activo = 0;
    GPIO_ConfigAnalogico(GPIOA, ARC_PIN_COL1);
    GPIO_ConfigAnalogico(GPIOA, ARC_PIN_COL3);
}

// Comando: ARCADE breakout
// Arranca el breakout en la LCD, en paralelo con el monitor (no bloquea). La barra sigue al
// potenciómetro y * o # inician cada jugada. "ARCADE breakout" con el juego en marcha lo reinicia.
// Extra: "ARCADE off" detiene el juego y borra la pantalla.
void Cmd_ARCADE(char *args)
{
    uint8_t largo;
    uint8_t apagar;
    char *ptr = args;

    if ((largo = Texto_EsPalabra(ptr, "breakout")) != 0)
    {
        apagar = 0;
    }
    else if ((largo = Texto_EsPalabra(ptr, "off")) != 0)
    {
        apagar = 1;
    }
    else
    {
        UART_Print("Error de sintaxis. Uso correcto: ARCADE breakout\r\n");
        return;
    }

    ptr += largo;
    while (*ptr == ' ')
    {
        ptr++;
    }
    if (*ptr != '\0')
    {
        UART_Print("Error de sintaxis. Uso correcto: ARCADE breakout\r\n");
        return;
    }

    if (apagar)
    {
        if (!arc_activo)
        {
            UART_Print("ARCADE: no esta en juego.\r\n");
            return;
        }
        Tick_Bloquear();
        Arcade_Detener();
        LCD_Borrar();
        Tick_Desbloquear();
        UART_Print("ARCADE: off\r\n");
        return;
    }

    // Las filas del teclado están en U6: debe estar en línea. Segment_Asegurar pausa y reanuda el
    // tick por su cuenta, por eso va antes de bloquearlo aquí.
    if (!Segment_Asegurar())
    {
        UART_Print("Error: el teclado no responde (U6, I2C 0x21). Revise la fuente de 9 V.\r\n");
        return;
    }

    // Todo lo demás con el tick pausado: SPI1, el ADC, los pines y el estado del juego se comparten
    Tick_Bloquear();

    if (!(ADC1->CR & ADC_CR_ADEN) && !Analog_InitADC())
    {
        Tick_Desbloquear();
        UART_Print("Error: el ADC no respondio al inicializarse.\r\n");
        return;
    }

    if (Watch_Activo())
    {
        Watch_Detener();
        UART_Print("LCDWATCH detenido.\r\n");
    }

    Arc_ConfigColumna(ARC_PIN_COL1);
    Arc_ConfigColumna(ARC_PIN_COL3);

    LCD_Borrar();                                   // Pantalla en blanco = lo que 'enviado' supone
    memset(arc_enviado, 0, sizeof(arc_enviado));
    memset(arc_tecla_cnt, 0, sizeof(arc_tecla_cnt));
    arc_pendientes = 0;
    arc_inicio = 0;
    arc_fase = 0;
    Arc_NuevoJuego();

    arc_activo = 1;
    Tick_Desbloquear();

    UART_Print("ARCADE: breakout iniciado. Potenciometro = barra, * o # = iniciar jugada. "
               "ARCADE off para salir.\r\n");
}