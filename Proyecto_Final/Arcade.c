#include <stdint.h>
#include <string.h>
#include "hw_comun.h"
#include "Arcade.h"
#include "display.h"
#include "analog.h"
#include "watch.h"
#include "tick.h"
#include "monitor.h"

/* ARCADE: breakout en la LCD. La barra sigue al potenciómetro (PA0) y * (PA1) o # (PA8) sacan la bola.
   Cada paso del juego dura 10 ticks: fase 0 = juego, fases 1 a 6 = un banco de la LCD. */

/* ---------- Parámetros ---------- */

#define ARC_POT_INVERTIDO       0       /* 1 si el potenciómetro está al revés */

#define ARC_PIN_COL1            1       /* PA1: tecla * */
#define ARC_PIN_COL3            8       /* PA8: tecla # */
#define ARC_FILA_TECLAS         4       /* Fila de * y # */
#define ARC_REBOTE_LECTURAS     2       /* Lecturas para aceptar una tecla */

#define ARC_RAPIDEZ_INICIAL     13      /* 13/16 = 0.81 píxeles por paso de 20 ms (unos 41 píxeles/s) */
#define ARC_RAPIDEZ_MAXIMA      17      /* 17/16 = 1.06 píxeles por paso */


/* ---------- Constantes ---------- */

#define ARC_ANCHO               LCD_ANCHO
#define ARC_ALTO                48
#define ARC_BANCOS              LCD_BANCOS

#define ARC_PERIODO_TICKS       10      /* 10 ticks x 2 ms = 20 ms por paso (50 pasos por segundo) */

#define ARC_SUB                 4       /* 2^4 = 16: posiciones en 1/16 de píxel (píxel = valor >> 4) */

#define ARC_CAMPO_Y             8       /* Inicio del campo de juego */

#define ARC_LAD_COLS            7
#define ARC_LAD_FILAS           4
#define ARC_LAD_TOTAL           (ARC_LAD_COLS * ARC_LAD_FILAS)
#define ARC_LAD_W               11
#define ARC_LAD_H               3
#define ARC_LAD_PASO_X          12
#define ARC_LAD_PASO_Y          4
#define ARC_LAD_Y0              10

#define ARC_PALETA_W            14
#define ARC_PALETA_H            2
#define ARC_PALETA_Y            45

#define ARC_BOLA                2

#define ARC_ADC_MUESTRAS        4

typedef enum
{
    ARC_ESPERANDO = 0,      /* Bola sobre la barra */
    ARC_JUGANDO
} ArcEstado_t;

/* ---------- Estado (se cambia desde el tick o con el tick bloqueado) ---------- */

static volatile uint8_t arc_activo = 0;
static uint8_t arc_fase = 0;                /* Fase del paso */
static ArcEstado_t arc_estado = ARC_ESPERANDO;

static uint8_t arc_ladrillos[ARC_LAD_FILAS];    /* Bit n = ladrillo n en pie */
static uint8_t arc_destruidos = 0;

static int32_t arc_px = 0;                  /* Barra (borde izquierdo) */
static int32_t arc_bx = 0, arc_by = 0;      /* Bola en 1/16 de píxel */
static int32_t arc_vx = 0, arc_vy = 0;      /* Velocidad en 1/16 de píxel */
static uint8_t arc_lado = 0;                /* Dirección del próximo saque */

static uint8_t arc_tecla_cnt[2];            /* Lecturas de * y # */
static uint8_t arc_inicio = 0;

static uint8_t arc_fija_sucia = 0;
static uint8_t arc_pendientes = 0;          /* Bancos por enviar */
static uint8_t arc_capa_fija[ARC_BANCOS][ARC_ANCHO];    /* Ladrillos y mensaje */
static uint8_t arc_cuadro[ARC_BANCOS][ARC_ANCHO];       /* Capa fija + barra + bola */
static uint8_t arc_enviado[ARC_BANCOS][ARC_ANCHO];      /* Último cuadro enviado */


/* ---------- LCD ---------- */

/* Manda el siguiente banco pendiente */
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
    LCD_EscribirBanco(b, arc_cuadro[b]);
}


/* ---------- Dibujo ---------- */

/* Rectángulo relleno en (x, y) de w x h */
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
        int32_t hi = (y1 < ((b + 1) * 8)) ? (y1 - (b * 8)) : 8;
        uint8_t mascara = (uint8_t)(((1U << hi) - 1U) & ~((1U << lo) - 1U));

        for (int32_t cx = x0; cx < x1; cx++)
        {
            buf[b][cx] |= mascara;
        }
    }
}

/* Texto en un banco */
static void Arc_Texto(uint8_t (*buf)[ARC_ANCHO], uint8_t banco, int32_t x, const char *texto)
{
    while (*texto != '\0')
    {
        const uint8_t *glifo = LCD_Glifo(*texto++);

        for (uint8_t j = 0; j < 6; j++)
        {
            int32_t col = x + j;

            if (col >= 0 && col < ARC_ANCHO)
            {
                buf[banco][col] = (j < 5) ? glifo[j] : 0x00;
            }
        }
        x += 6;
    }
}

static void Arc_TextoCentrado(uint8_t (*buf)[ARC_ANCHO], uint8_t banco, const char *texto)
{
    Arc_Texto(buf, banco, (ARC_ANCHO - (6 * (int32_t)strlen(texto))) / 2, texto);
}

/* Capa fija: ladrillos y aviso de saque */
static void Arc_RedibujarFija(void)
{
    memset(arc_capa_fija, 0, sizeof(arc_capa_fija));

    /* Ladrillos */
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

/* Cuadro: capa fija + barra + bola */
static void Arc_Componer(void)
{
    memcpy(arc_cuadro, arc_capa_fija, sizeof(arc_cuadro));

    Arc_Rect(arc_cuadro, arc_px, ARC_PALETA_Y, ARC_PALETA_W, ARC_PALETA_H);
    Arc_Rect(arc_cuadro, arc_bx >> ARC_SUB, arc_by >> ARC_SUB, ARC_BOLA, ARC_BOLA);
}


/* ---------- Entradas ---------- */

/* Promedio de las lecturas del potenciómetro */
static uint8_t Arc_LeerPot(uint32_t *valor)
{
    uint32_t suma = 0;

    for (uint8_t i = 0; i < ARC_ADC_MUESTRAS; i++)
    {
        uint32_t lectura;

        if (!Analog_LeerPot12(&lectura))
        {
            return 0;
        }
        suma += lectura;
    }

    *valor = suma / ARC_ADC_MUESTRAS;
    return 1;
}

/* Antirrebote de una tecla */
static void Arc_ContarTecla(uint8_t tecla, uint8_t presionada)
{
    if (!presionada)
    {
        arc_tecla_cnt[tecla] = 0;
        return;
    }

    if (arc_tecla_cnt[tecla] < ARC_REBOTE_LECTURAS)
    {
        arc_tecla_cnt[tecla]++;
        if (arc_tecla_cnt[tecla] == ARC_REBOTE_LECTURAS)
        {
            arc_inicio = 1;
        }
    }
}

/* Lee * y # (0 = presionada) */
static void Arc_LeerTeclas(void)
{
    uint32_t idr = GPIOA->IDR;

    Arc_ContarTecla(0, (uint8_t)((idr & (1U << ARC_PIN_COL1)) == 0U));
    Arc_ContarTecla(1, (uint8_t)((idr & (1U << ARC_PIN_COL3)) == 0U));
}


/* ---------- Juego ---------- */

/* Cálculo de la rapidez: 13 + 1 por cada 7 ladrillos destruidos, hasta 17 */
static int32_t Arc_Rapidez(void)
{
    int32_t r = ARC_RAPIDEZ_INICIAL + (arc_destruidos / ARC_LAD_COLS);

    return (r > ARC_RAPIDEZ_MAXIMA) ? ARC_RAPIDEZ_MAXIMA : r;
}

/* Bola sobre la barra */
static void Arc_BolaSobreBarra(void)
{
    arc_bx = (arc_px + (ARC_PALETA_W / 2) - 1) << ARC_SUB;
    arc_by = (ARC_PALETA_Y - ARC_BOLA) << ARC_SUB;
    arc_vx = 0;
    arc_vy = 0;
}

/* Ladrillos completos y bola sobre la barra */
static void Arc_NuevoJuego(void)
{
    for (uint8_t fila = 0; fila < ARC_LAD_FILAS; fila++)
    {
        arc_ladrillos[fila] = (uint8_t)((1U << ARC_LAD_COLS) - 1U);
    }
    arc_destruidos = 0;
    arc_estado = ARC_ESPERANDO;
    Arc_BolaSobreBarra();
    arc_fija_sucia = 1;
}

/* Choque con ladrillos: 1 si hubo */
static uint8_t Arc_ChocaLadrillo(int32_t px, int32_t py)
{
    for (uint8_t i = 0; i < 4; i++)
    {
        int32_t x = px + (i & 1);               /* Esquinas de la bola */
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
            continue;
        }

        if (arc_ladrillos[fila] & (1U << col))
        {
            arc_ladrillos[fila] &= (uint8_t)~(1U << col);
            arc_destruidos++;
            arc_fija_sucia = 1;
            return 1;
        }
    }
    return 0;
}

/* Mueve la bola un paso: 1 si se perdió */
static uint8_t Arc_MoverBola(void)
{
    int32_t nbx = arc_bx + arc_vx;
    int32_t nby;
    int32_t bxp;
    int32_t byp;

    /* Horizontal */
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

    /* Vertical */
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

    /* Cálculo de la rapidez: se conserva el signo de vy y se aplica la rapidez actual */
    arc_vy = (arc_vy < 0) ? -Arc_Rapidez() : Arc_Rapidez();

    /* Rebote en la barra:
       rel = centro de la bola - centro de la barra (-7 a +7 píxeles)
       vx  = rel x 2 subpíxeles, limitado a -16..+16 (1 píxel por paso como máximo)
       Si |vx| < 3 se fuerza a 3 para que la bola nunca suba en vertical puro */
    bxp = arc_bx >> ARC_SUB;
    byp = arc_by >> ARC_SUB;
    if (arc_vy > 0 &&
        (byp + ARC_BOLA) >= ARC_PALETA_Y && byp <= (ARC_PALETA_Y + ARC_PALETA_H - 1) &&
        (bxp + ARC_BOLA) > arc_px && bxp < (arc_px + ARC_PALETA_W))
    {
        int32_t rel = (bxp + 1) - (arc_px + (ARC_PALETA_W / 2));

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
        if (arc_vx > -3 && arc_vx < 3)
        {
            arc_vx = (arc_vx < 0 || (arc_vx == 0 && arc_lado)) ? -3 : 3;
        }
        return 0;
    }

    /* Bola perdida */
    return (arc_vy > 0 && (arc_by >> ARC_SUB) >= (ARC_ALTO - 1));
}

/* Fase 0: paso del juego */
static void Arc_Paso(void)
{
    uint32_t pot;

    Arc_LeerTeclas();

    /* Posición de la barra: px = pot x (84 - 14) / 4095
       pot 0 -> píxel 0 (izquierda), pot 4095 -> píxel 70 (derecha) */
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
                arc_fija_sucia = 1;
            }
            break;

        case ARC_JUGANDO:
            if (Arc_MoverBola())
            {
                /* Bola perdida: nueva jugada con los ladrillos que quedan */
                arc_estado = ARC_ESPERANDO;
                Arc_BolaSobreBarra();
                arc_fija_sucia = 1;
            }
            else if (arc_destruidos >= ARC_LAD_TOTAL)
            {
                /* Sin ladrillos: se vuelven a poner todos */
                Arc_NuevoJuego();
            }
            break;

        default:
            break;
    }
    arc_inicio = 0;

    if (arc_fija_sucia)
    {
        Arc_RedibujarFija();
        arc_fija_sucia = 0;
    }

    Arc_Componer();

    /* Bancos que cambiaron */
    for (uint8_t b = 0; b < ARC_BANCOS; b++)
    {
        if (memcmp(arc_cuadro[b], arc_enviado[b], ARC_ANCHO) != 0)
        {
            arc_pendientes |= (uint8_t)(1U << b);
        }
    }
}

/* Tarea de fondo */
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

/* Detiene el juego y libera el teclado (con el tick bloqueado) */
void Arcade_Detener(void)
{
    arc_activo = 0;
    (void)Segment_ElegirFilaTeclado(0);
    GPIO_ConfigAnalogico(GPIOA, ARC_PIN_COL1);
    GPIO_ConfigAnalogico(GPIOA, ARC_PIN_COL3);
}

/* Comando: ARCADE breakout | ARCADE off */
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

    /* U6 en línea */
    if (!Segment_Asegurar())
    {
        UART_Print("Error: el teclado no responde (U6, I2C 0x21). Revise la fuente de 9 V.\r\n");
        return;
    }

    /* Inicio con el tick bloqueado */
    Tick_Bloquear();

    if (!Analog_ADCListo() && !Analog_InitADC())
    {
        Tick_Desbloquear();
        UART_Print("Error: el ADC no respondio al inicializarse.\r\n");
        return;
    }

    /* Fila del teclado y columnas con pull-up */
    if (!Segment_ElegirFilaTeclado(ARC_FILA_TECLAS))
    {
        Tick_Desbloquear();
        UART_Print("Error: el teclado no responde (U6, I2C 0x21). Revise la fuente de 9 V.\r\n");
        return;
    }
    GPIO_ConfigEntrada(GPIOA, ARC_PIN_COL1, GPIO_PULL_UP);
    GPIO_ConfigEntrada(GPIOA, ARC_PIN_COL3, GPIO_PULL_UP);

    if (Watch_Activo())
    {
        Watch_Detener();
        UART_Print("LCDWATCH detenido.\r\n");
    }

    LCD_Borrar();
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
