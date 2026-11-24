#include <stdint.h>
#include "hw_comun.h"
#include "display.h"
#include "i2c1.h"
#include "tick.h"
#include "analog.h"
#include "watch.h"
#include "Arcade.h"
#include "monitor.h"

/* Display de 7 segmentos en U6: GPA = segmentos A-G, GPB0-3 = dígitos, GPB4-7 = filas del teclado.
   El tick enciende un dígito por vez. */

#define SEG_DIGITOS     4
#define SEG_MAXIMO      9999
#define SEG_MINIMO      (-999)

/* Configuración de U6 */
#define U6_IODIRA       0x80        /* GPA7 entrada */
#define U6_IODIRB       0xF0        /* GPB4-GPB7 entradas */
#define U6_GPPUA        0x80
#define U6_GPPUB        0xF0

/* Patrones 0-9 y A-F (bit 0 = A) */
static const uint8_t seg_tabla[16] =
{
    0x3F,   /* 0 */
    0x06,   /* 1 */
    0x5B,   /* 2 */
    0x4F,   /* 3 */
    0x66,   /* 4 */
    0x6D,   /* 5 */
    0x7D,   /* 6 */
    0x07,   /* 7 */
    0x7F,   /* 8 */
    0x6F,   /* 9 */
    0x77,   /* A */
    0x7C,   /* b */
    0x39,   /* C */
    0x5E,   /* d */
    0x79,   /* E */
    0x71    /* F */
};

#define SEG_GUION       0x40        /* Signo menos */
#define SEG_H           0x74        /* "h" */
#define SEG_HEX_MAXIMO  0xFFF

/* Estado compartido con el tick */
static volatile uint8_t seg_patrones[SEG_DIGITOS];  /* Índice 0 = izquierda */
static volatile uint8_t seg_activo = 0;
static volatile uint8_t seg_en_linea = 0;
static volatile uint8_t seg_blanquear = 0;
static uint8_t seg_digito = 0;

/* Inicialización de U6: OLAT, pull-ups y al final IODIR */
uint8_t Segment_Init(void)
{
    const uint8_t olat[2]  = {0x00, 0x00};
    const uint8_t gppu[2]  = {U6_GPPUA, U6_GPPUB};
    const uint8_t iodir[2] = {U6_IODIRA, U6_IODIRB};

    seg_en_linea = 0;

    if (MCP23017_WriteRegs(MCP_U6_ADDR, MCP_OLATA, olat, 2) != I2C1_OK)
    {
        return 0;
    }
    if (MCP23017_WriteRegs(MCP_U6_ADDR, MCP_GPPUA, gppu, 2) != I2C1_OK)
    {
        return 0;
    }
    if (MCP23017_WriteRegs(MCP_U6_ADDR, MCP_IODIRA, iodir, 2) != I2C1_OK)
    {
        return 0;
    }

    seg_digito = 0;
    seg_en_linea = 1;
    return 1;
}


/* Refresca un dígito */
void Segment_Refresh(void)
{
    uint8_t trama[3];

    if (!seg_activo || !seg_en_linea)
    {
        return;
    }

    /* Apagado del display */
    if (seg_blanquear)
    {
        trama[0] = 0x00;
        trama[1] = 0x00;
        if (MCP23017_WriteRegs(MCP_U6_ADDR, MCP_GPIOA, trama, 2) != I2C1_OK)
        {
            seg_en_linea = 0;
        }
        seg_blanquear = 0;
        seg_activo = 0;
        return;
    }

    trama[0] = 0x00;                                /* Segmentos apagados */
    trama[1] = (uint8_t)(1U << seg_digito);         /* Dígito */
    trama[2] = seg_patrones[seg_digito];            /* Segmentos */

    if (MCP23017_WriteRegs(MCP_U6_ADDR, MCP_GPIOA, trama, 3) != I2C1_OK)
    {
        seg_en_linea = 0;
        return;
    }

    seg_digito++;
    if (seg_digito >= SEG_DIGITOS)
    {
        seg_digito = 0;
    }
}

/* Número a patrones, alineado a la derecha */
static void Segment_Convertir(int32_t num, uint8_t patrones[SEG_DIGITOS])
{
    uint32_t magnitud;
    int8_t pos = SEG_DIGITOS - 1;

    for (uint8_t i = 0; i < SEG_DIGITOS; i++)
    {
        patrones[i] = 0x00;
    }

    magnitud = (num < 0) ? (uint32_t)(-num) : (uint32_t)num;

    do
    {
        patrones[pos] = seg_tabla[magnitud % 10];
        magnitud /= 10;
        pos--;
    } while (magnitud > 0 && pos >= 0);

    if (num < 0)
    {
        patrones[pos] = SEG_GUION;
    }
}

/* Publica los patrones para el tick */
static void Segment_Publicar(const uint8_t nuevos[SEG_DIGITOS])
{
    for (uint8_t i = 0; i < SEG_DIGITOS; i++)
    {
        seg_patrones[i] = nuevos[i];
    }
    seg_blanquear = 0;
    seg_activo = 1;
}

/* Inicializa U6 si hace falta */
uint8_t Segment_Asegurar(void)
{
    uint8_t resultado;

    if (seg_en_linea)
    {
        return 1;
    }

    Tick_Bloquear();
    resultado = Segment_Init();
    Tick_Desbloquear();

    return resultado;
}

/* Muestra un número decimal */
void Segment_MostrarDecimal(int32_t num)
{
    uint8_t nuevos[SEG_DIGITOS];

    if (num > SEG_MAXIMO)
    {
        num = SEG_MAXIMO;
    }
    else if (num < SEG_MINIMO)
    {
        num = SEG_MINIMO;
    }

    Segment_Convertir(num, nuevos);
    Segment_Publicar(nuevos);
}

/* Muestra un valor hex con "h" al final */
void Segment_MostrarHex(uint32_t valor)
{
    uint8_t nuevos[SEG_DIGITOS];
    int8_t pos = SEG_DIGITOS - 2;

    if (valor > SEG_HEX_MAXIMO)
    {
        valor = SEG_HEX_MAXIMO;
    }

    for (uint8_t i = 0; i < SEG_DIGITOS; i++)
    {
        nuevos[i] = 0x00;
    }
    nuevos[SEG_DIGITOS - 1] = SEG_H;

    do
    {
        nuevos[pos] = seg_tabla[valor & 0xFU];
        valor >>= 4;
        pos--;
    } while (valor > 0 && pos >= 0);

    Segment_Publicar(nuevos);
}

/* Apaga el display */
void Segment_Apagar(void)
{
    if (seg_activo && seg_en_linea)
    {
        seg_blanquear = 1;
    }
    else
    {
        seg_activo = 0;
    }
}

/* Elige una fila del teclado (0 = ninguna) */
uint8_t Segment_ElegirFilaTeclado(uint8_t fila)
{
    uint8_t iodirb = U6_IODIRB;

    if (fila >= 1 && fila <= 4)
    {
        iodirb &= (uint8_t)~(1U << (3U + fila));    /* Fila 1 = GPB4 */
    }

    if (MCP23017_WriteRegs(MCP_U6_ADDR, MCP_IODIRB, &iodirb, 1) != I2C1_OK)
    {
        seg_en_linea = 0;
        return 0;
    }
    return 1;
}

/* Comando: SEGMENT num */
void Cmd_SEGMENT(char *args)
{
    int32_t num;
    uint8_t usados;
    char *ptr;

    usados = ParseDec(args, &num);
    if (usados == 0)
    {
        UART_Print("Error de sintaxis. Uso correcto: SEGMENT num (decimal)\r\n");
        return;
    }

    ptr = args + usados;
    while (*ptr == ' ')
    {
        ptr++;
    }
    if (*ptr != '\0')
    {
        UART_Print("Error de sintaxis. Uso correcto: SEGMENT num (decimal)\r\n");
        return;
    }

    if (num < SEG_MINIMO || num > SEG_MAXIMO)
    {
        UART_Print("Error: el display tiene 4 digitos. Rango permitido: -999 a 9999.\r\n");
        return;
    }

    /* Reintenta U6 si hace falta */
    if (!Segment_Asegurar())
    {
        UART_Print("Error: el display no responde (U6, I2C 0x21). Revise la fuente de 9 V.\r\n");
        return;
    }

    if (Pot2Seg_Activo())
    {
        Pot2Seg_Detener();
        UART_Print("POT2SEG detenido.\r\n");
    }

    Segment_MostrarDecimal(num);

    UART_Print("Display: ");
    UART_PrintDec(num);
    UART_Print("\r\n");
}

/* ---------- LCD (PCD8544 en SPI1) ----------
   PB3 = SCK, PB5 = MOSI, PA11 = CE, PA12 = DC */

#define PIN_LCD_SCK         3       /* PB3 */
#define PIN_LCD_MOSI        5       /* PB5 */
#define PIN_LCD_CE          11      /* PA11 */
#define PIN_LCD_DC          12      /* PA12 */
#define AF5_SPI1            5

#define LCD_CHAR_ANCHO      6       /* 5 columnas + 1 de separación */
#define LCD_COLUMNAS_TEXTO  (LCD_ANCHO / LCD_CHAR_ANCHO)    /* 84 / 6 = 14 caracteres */
#define LCD_LINEAS_TEXTO    LCD_BANCOS                      /* 6 */

/* Comandos del PCD8544 */
#define PCD_FUNCION_BASICA  0x20
#define PCD_FUNCION_EXTENDIDA 0x21
#define PCD_PANTALLA_NORMAL 0x0C
#define PCD_SET_Y           0x40
#define PCD_SET_X           0x80
#define PCD_TEMPERATURA     0x04
#define PCD_BIAS            0x14
#define PCD_VOP             0xB1    /* Contraste */

#define LCD_DR8             (*((__IO uint8_t *)&SPI1->DR))  /* Acceso de 8 bits */
#define LCD_TIMEOUT         100000U

/* Fuente 5 x 7 (ASCII 0x20 a 0x7E) */
static const uint8_t lcd_fuente[95][5] =
{
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, /* ' ' ! " */
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, /* # $ % */
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00}, /* & ' ( */
    {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08}, /* ) * + */
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00}, /* , - . */
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, /* / 0 1 */
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10}, /* 2 3 4 */
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, /* 5 6 7 */
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00}, /* 8 9 : */
    {0x00,0x56,0x36,0x00,0x00}, {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14}, /* ; < = */
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E}, /* > ? @ */
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22}, /* A B C */
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, /* D E F */
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, /* G H I */
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40}, /* J K L */
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E}, /* M N O */
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, /* P Q R */
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F}, /* S T U */
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63}, /* V W X */
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00}, /* Y Z [ */
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04}, /* \ ] ^ */
    {0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78}, /* _ ` a */
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F}, /* b c d */
    {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E}, /* e f g */
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00}, /* h i j */
    {0x7F,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78}, /* k l m */
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08}, /* n o p */
    {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20}, /* q r s */
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C}, /* t u v */
    {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C}, /* w x y */
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00}, /* z { | */
    {0x00,0x41,0x36,0x08,0x00}, {0x10,0x08,0x08,0x10,0x08}                              /* } ~ */
};

/* Fuente chica 3 x 5 para LCDWATCH */
#define LCD_CHICA_ANCHO     4
#define LCD_COLUMNAS_CHICA  (LCD_ANCHO / LCD_CHICA_ANCHO)   /* 84 / 4 = 21 caracteres */

static const char lcd_chica_caracteres[] = "0123456789ABCDEFh:-";

static const uint8_t lcd_chica_fuente[][3] =
{
    {0x3E,0x22,0x3E}, {0x24,0x3E,0x20}, {0x3A,0x2A,0x2E}, {0x2A,0x2A,0x3E}, /* 0 1 2 3 */
    {0x0E,0x08,0x3E}, {0x2E,0x2A,0x3A}, {0x3E,0x2A,0x3A}, {0x02,0x02,0x3E}, /* 4 5 6 7 */
    {0x3E,0x2A,0x3E}, {0x2E,0x2A,0x3E}, {0x3C,0x0A,0x3C}, {0x3E,0x2A,0x14}, /* 8 9 A B */
    {0x1C,0x22,0x22}, {0x3E,0x22,0x1C}, {0x3E,0x2A,0x22}, {0x3E,0x0A,0x02}, /* C D E F */
    {0x3E,0x08,0x30}, {0x00,0x14,0x00}, {0x08,0x08,0x08}                    /* h : - */
};

/* Espera a que SPI1 termine */
static void LCD_EsperarFin(void)
{
    uint32_t restante = LCD_TIMEOUT;

    while ((SPI1->SR & SPI_SR_FTLVL) && (restante > 0))
    {
        restante--;
    }
    while ((SPI1->SR & SPI_SR_BSY) && (restante > 0))
    {
        restante--;
    }
}

/* Envía n bytes como comando (dato = 0) o dato (dato = 1) */
static void LCD_Enviar(uint8_t dato, const uint8_t *bytes, uint16_t n, uint8_t repetir)
{
    LCD_EsperarFin();
    GPIO_Escribir(GPIOA, PIN_LCD_DC, dato);
    GPIO_Escribir(GPIOA, PIN_LCD_CE, 0);            /* CE en 0 */

    for (uint16_t i = 0; i < n; i++)
    {
        uint32_t restante = LCD_TIMEOUT;

        while (!(SPI1->SR & SPI_SR_TXE) && (restante > 0))
        {
            restante--;
        }
        LCD_DR8 = repetir ? bytes[0] : bytes[i];
    }

    LCD_EsperarFin();
    GPIO_Escribir(GPIOA, PIN_LCD_CE, 1);            /* CE en 1 */
}

/* Posición en la pantalla */
static void LCD_IrA(uint8_t columna, uint8_t banco)
{
    uint8_t comandos[2];

    comandos[0] = (uint8_t)(PCD_SET_X | (columna % LCD_ANCHO));
    comandos[1] = (uint8_t)(PCD_SET_Y | (banco % LCD_BANCOS));
    LCD_Enviar(0, comandos, 2, 0);
}

/* Borra la pantalla */
static void LCD_Limpiar(void)
{
    const uint8_t cero = 0x00;

    LCD_IrA(0, 0);
    LCD_Enviar(1, &cero, LCD_ANCHO * LCD_BANCOS, 1);
    LCD_IrA(0, 0);
}

/* Escribe un carácter */
static void LCD_Caracter(char c)
{
    uint8_t columnas[LCD_CHAR_ANCHO];
    uint8_t indice;

    if (c < 0x20 || c > 0x7E)
    {
        c = '?';
    }
    indice = (uint8_t)(c - 0x20);

    for (uint8_t i = 0; i < 5; i++)
    {
        columnas[i] = lcd_fuente[indice][i];
    }
    columnas[5] = 0x00;

    LCD_Enviar(1, columnas, LCD_CHAR_ANCHO, 0);
}

/* Escribe una línea con la fuente normal */
void LCD_EscribirLinea(uint8_t linea, const char *texto)
{
    uint8_t columnas[LCD_ANCHO];
    uint8_t terminado = 0;

    for (uint8_t i = 0; i < LCD_COLUMNAS_TEXTO; i++)
    {
        char c = ' ';

        if (!terminado && texto[i] != '\0')
        {
            c = texto[i];
        }
        else
        {
            terminado = 1;
        }

        if (c < 0x20 || c > 0x7E)
        {
            c = '?';
        }

        for (uint8_t j = 0; j < 5; j++)
        {
            columnas[(i * LCD_CHAR_ANCHO) + j] = lcd_fuente[c - 0x20][j];
        }
        columnas[(i * LCD_CHAR_ANCHO) + 5] = 0x00;
    }

    LCD_EscribirBanco(linea, columnas);
}

/* Escribe una línea con la fuente chica */
void LCD_EscribirLineaChica(uint8_t linea, const char *texto)
{
    uint8_t columnas[LCD_ANCHO];
    uint8_t terminado = 0;

    for (uint8_t i = 0; i < LCD_COLUMNAS_CHICA; i++)
    {
        int8_t indice = -1;     /* -1 = en blanco */

        if (!terminado && texto[i] != '\0')
        {
            for (uint8_t k = 0; lcd_chica_caracteres[k] != '\0'; k++)
            {
                if (lcd_chica_caracteres[k] == texto[i])
                {
                    indice = (int8_t)k;
                    break;
                }
            }
        }
        else
        {
            terminado = 1;
        }

        for (uint8_t j = 0; j < 3; j++)
        {
            columnas[(i * LCD_CHICA_ANCHO) + j] = (indice >= 0) ? lcd_chica_fuente[indice][j] : 0x00;
        }
        columnas[(i * LCD_CHICA_ANCHO) + 3] = 0x00;
    }

    LCD_EscribirBanco(linea, columnas);
}

/* Manda un banco completo */
void LCD_EscribirBanco(uint8_t banco, const uint8_t columnas[LCD_ANCHO])
{
    LCD_IrA(0, banco);
    LCD_Enviar(1, columnas, LCD_ANCHO, 0);
}

/* Columnas de un carácter */
const uint8_t *LCD_Glifo(char c)
{
    if (c < 0x20 || c > 0x7E)
    {
        c = '?';
    }
    return lcd_fuente[(uint8_t)(c - 0x20)];
}

/* Borra la pantalla */
void LCD_Borrar(void)
{
    LCD_Limpiar();
}

/* Inicialización de SPI1 y de la LCD */
void LCD_Init(void)
{
    const uint8_t arranque[] =
    {
        PCD_FUNCION_EXTENDIDA,
        PCD_BIAS,
        PCD_VOP,
        PCD_TEMPERATURA,
        PCD_FUNCION_BASICA,
        PCD_PANTALLA_NORMAL
    };

    /* CE en 1 y DC en 0 */
    GPIO_ConfigSalida(GPIOA, PIN_LCD_CE, 1);
    GPIO_ConfigSalida(GPIOA, PIN_LCD_DC, 0);

    /* Reloj y reset de SPI1 */
    RCC_ActivarAPB2(RCC_APB2ENR_SPI1EN, RCC_APB2RSTR_SPI1RST);

    /* Maestro, modo 0, solo transmisión.
       Reloj: BR = 2 -> PCLK2 / 8 = 16 MHz / 8 = 2 MHz; un byte tarda 8 / 2 MHz = 4 us,
       una línea de 84 bytes unos 0.34 ms */
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 &= ~(SPI_CR1_CPHA | SPI_CR1_CPOL | SPI_CR1_BR | SPI_CR1_LSBFIRST |
                   SPI_CR1_RXONLY | SPI_CR1_CRCEN | SPI_CR1_CRCL);
    SPI1->CR1 |= (SPI_CR1_MSTR | (2U << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI |
                  SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE);

    /* 8 bits */
    SPI1->CR2 &= ~SPI_CR2_DS;
    SPI1->CR2 |= ((7U << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH);

    /* Pines y habilitación */
    GPIO_ConfigAF(GPIOB, PIN_LCD_SCK, AF5_SPI1, GPIO_PUSH_PULL);
    GPIO_ConfigAF(GPIOB, PIN_LCD_MOSI, AF5_SPI1, GPIO_PUSH_PULL);
    SPI1->CR1 |= SPI_CR1_SPE;

    /* Arranque del PCD8544 */
    LCD_Enviar(0, arranque, sizeof(arranque), 0);
    LCD_Limpiar();
}

/* Comando: LCD string */
void Cmd_LCD(char *args)
{
    uint8_t columna = 0;
    uint8_t linea = 0;
    uint8_t escritos = 0;

    if (*args == '\0')
    {
        UART_Print("Error de sintaxis. Uso correcto: LCD string\r\n");
        return;
    }

    /* Tick bloqueado; se detienen LCDWATCH y ARCADE */
    Tick_Bloquear();

    if (Watch_Activo())
    {
        Watch_Detener();
        UART_Print("LCDWATCH detenido.\r\n");
    }
    if (Arcade_Activo())
    {
        Arcade_Detener();
        UART_Print("ARCADE detenido.\r\n");
    }

    LCD_Limpiar();

    while (*args != '\0' && linea < LCD_LINEAS_TEXTO)
    {
        if (columna == 0)
        {
            LCD_IrA(0, linea);
        }

        LCD_Caracter(*args);
        args++;
        escritos++;

        columna++;
        if (columna >= LCD_COLUMNAS_TEXTO)
        {
            columna = 0;
            linea++;
        }
    }

    Tick_Desbloquear();

    UART_Print("LCD: ");
    UART_PrintDec(escritos);
    UART_Print(" caracteres escritos");
    if (*args != '\0')
    {
        UART_Print(" (el resto no cabe en la pantalla)");
    }
    UART_Print(".\r\n");
}
