#include <stdint.h>
#include "hw_comun.h"
#include "display.h"
#include "i2c1.h"
#include "tick.h"
#include "analog.h"
#include "watch.h"
#include "monitor.h"   // UART_Print, UART_PrintDec, ParseDec

// Display de 7 segmentos multi-dígito (DS1, 4 dígitos, cátodo común con transistores Q1-Q4)
// manejado por el expansor U6 (MCP23017, dirección 0x21) en el bus I2C1.
//
// Conexiones (AyudaProgra, "Pines de los MCP23017"):
//  - U6 puerto A: GPA0-GPA6 = segmentos A-G (bit 0 = A ... bit 6 = G), 1 = segmento encendido.
//                 GPA7 = KP_C2 del teclado: entrada con pull-up (reservado para ARCADE).
//  - U6 puerto B: GPB0-GPB3 = DIG1_SEL-DIG4_SEL, 1 = dígito encendido, uno a la vez.
//                 GPB4-GPB7 = filas del teclado: entradas con pull-up, OLATB bits 4-7 siempre en 0.
//  - DIG1 es el dígito de la izquierda. El punto decimal no está conectado.
//
// Multiplexado (sin bloquear el monitor): SysTick_Handler llama a Segment_Refresh() cada
// TICK_MS. Cada llamada enciende un solo dígito con UNA transacción I2C que escribe tres
// registros consecutivos a partir de GPIOA (0x12):
//    GPIOA = 0x00          -> se apagan los segmentos (escribir GPIOx modifica OLATx, DS20001952E 3.5.10)
//    GPIOB = dígito        -> se elige el dígito nuevo (bits 4-7 en 0)
//    OLATA = segmentos     -> se encienden sus segmentos
// Es el orden que pide AyudaProgra ("Display: ... se apagan los segmentos, luego se elige el
// dígito en OLATB y se escriben los segmentos nuevos").

#define SEG_DIGITOS     4
#define SEG_MAXIMO      9999
#define SEG_MINIMO      (-999)      // El signo ocupa un dígito

// Configuración inicial de U6 (AyudaProgra, tabla de U6)
#define U6_IODIRA       0x80        // GPA0-GPA6 salidas, GPA7 entrada
#define U6_IODIRB       0xF0        // GPB0-GPB3 salidas, GPB4-GPB7 entradas
#define U6_GPPUA        0x80        // Pull-up solo en GPA7
#define U6_GPPUB        0xF0        // Pull-up en GPB4-GPB7

// Patrones de los dígitos 0-9 y A-F (bit 0 = A ... bit 6 = G)
static const uint8_t seg_tabla[16] =
{
    0x3F,   // 0: A B C D E F
    0x06,   // 1: B C
    0x5B,   // 2: A B D E G
    0x4F,   // 3: A B C D G
    0x66,   // 4: B C F G
    0x6D,   // 5: A C D F G
    0x7D,   // 6: A C D E F G
    0x07,   // 7: A B C
    0x7F,   // 8: todos
    0x6F,   // 9: A B C D F G
    0x77,   // A: A B C E F G
    0x7C,   // b: C D E F G
    0x39,   // C: A D E F
    0x5E,   // d: B C D E G
    0x79,   // E: A D E F G
    0x71    // F: A E F G
};

#define SEG_GUION       0x40        // Solo el segmento G, para el signo negativo
#define SEG_H           0x74        // "h" (C E F G), sufijo de los valores hexadecimales
#define SEG_HEX_MAXIMO  0xFFF       // 3 dígitos hexadecimales + la "h"

// Estado compartido entre el monitor y la interrupción del tick
static volatile uint8_t seg_patrones[SEG_DIGITOS];  // Índice 0 = DIG1 (izquierda)
static volatile uint8_t seg_activo = 0;             // 1 cuando hay algo que mostrar
static volatile uint8_t seg_en_linea = 0;           // 1 si U6 respondió en la última transferencia
static volatile uint8_t seg_blanquear = 0;          // 1 si el tick debe apagar el display y detenerse
static uint8_t seg_digito = 0;                      // Dígito que toca refrescar (solo lo usa el tick)

// Inicialización de U6. Se escriben primero los OLAT en 0 para que las salidas nazcan apagadas,
// luego las pull-ups y al final IODIR (AyudaProgra: "se escribe OLAT antes que IODIR").
// Cada par A/B está en registros consecutivos y se escribe en una sola transacción.
uint8_t Segment_Init(void)
{
    const uint8_t olat[2]  = {0x00, 0x00};
    const uint8_t gppu[2]  = {U6_GPPUA, U6_GPPUB};
    const uint8_t iodir[2] = {U6_IODIRA, U6_IODIRB};

    seg_en_linea = 0;   // El tick no toca el bus mientras se inicializa

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


// Refresca un dígito. Si U6 deja de responder se marca fuera de línea y se deja de usar el bus,
// para que la interrupción no gaste tiempo en esperas; Cmd_SEGMENT lo vuelve a inicializar.
void Segment_Refresh(void)
{
    uint8_t trama[3];

    if (!seg_activo || !seg_en_linea)
    {
        return;
    }

    // Apagado pedido por Segment_Apagar: segmentos y dígitos en 0 y se detiene el multiplexado
    if (seg_blanquear)
    {
        trama[0] = 0x00;                            // GPIOA: segmentos apagados
        trama[1] = 0x00;                            // GPIOB: ningún dígito (bits 4-7 en 0)
        if (MCP23017_WriteRegs(MCP_U6_ADDR, MCP_GPIOA, trama, 2) != I2C1_OK)
        {
            seg_en_linea = 0;
        }
        seg_blanquear = 0;
        seg_activo = 0;
        return;
    }

    trama[0] = 0x00;                                // GPIOA: segmentos apagados
    trama[1] = (uint8_t)(1U << seg_digito);         // GPIOB: dígito actual (bits 4-7 en 0)
    trama[2] = seg_patrones[seg_digito];            // OLATA: segmentos del dígito

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

// Convierte un número a los patrones de los 4 dígitos, alineado a la derecha y sin ceros a
// la izquierda (ej. 42 -> "  42", -7 -> "  -7", 0 -> "   0"). Rango: SEG_MINIMO a SEG_MAXIMO.
static void Segment_Convertir(int32_t num, uint8_t patrones[SEG_DIGITOS])
{
    uint32_t magnitud;
    int8_t pos = SEG_DIGITOS - 1;

    for (uint8_t i = 0; i < SEG_DIGITOS; i++)
    {
        patrones[i] = 0x00;     // Dígito apagado
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
        patrones[pos] = SEG_GUION;  // Con |num| <= 999 siempre queda un dígito libre
    }
}

// Copia los patrones nuevos al buffer que lee el tick y activa el multiplexado. Si el tick
// interrumpe a la mitad, a lo sumo un dígito muestra el valor anterior durante un refresco (2 ms).
static void Segment_Publicar(const uint8_t nuevos[SEG_DIGITOS])
{
    for (uint8_t i = 0; i < SEG_DIGITOS; i++)
    {
        seg_patrones[i] = nuevos[i];
    }
    seg_blanquear = 0;
    seg_activo = 1;
}

// Se asegura de que U6 esté en línea; si no, lo inicializa con el tick bloqueado (el tick también
// usa el bus). Se llama desde el programa principal. Devuelve 1 si el display quedó listo.
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

// Muestra un número decimal (se limita a SEG_MINIMO..SEG_MAXIMO). Se puede llamar desde el tick.
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

// Muestra un valor en hexadecimal con 3 dígitos a la derecha y una "h" al final
// (ej. 0x7A3 -> "7A3h", 0x00F -> "  Fh"). Se limita a SEG_HEX_MAXIMO. Se puede llamar desde el tick.
void Segment_MostrarHex(uint32_t valor)
{
    uint8_t nuevos[SEG_DIGITOS];
    int8_t pos = SEG_DIGITOS - 2;   // El último dígito es la "h"

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

// Apaga el display. El tick escribe los segmentos y dígitos en 0 y deja de multiplexar.
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

// Comando: SEGMENT num
// Despliega permanentemente el número decimal num (-999 a 9999) en el display de 7 segmentos.
// El valor se queda mostrando hasta el siguiente SEGMENT, POT2SEG o un reset. Si POT2SEG estaba
// activo se detiene, porque si no sobrescribiría el número en el siguiente refresco.
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

    // No debe haber nada más después del número
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

    // Si U6 no respondió antes (por ejemplo, la fuente de 9 V estaba apagada), reintentamos
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

// ---------------------------------------------------------------------------------------------
// Pantalla LCD 84 x 48 con controlador PCD8544, en SPI1 (AyudaProgra, tabla de la NUCLEO-G431KB):
//  - PB3  (CN3-15, D13): SPI1_SCK,  AF5, push-pull
//  - PB5  (CN4-14, D11): SPI1_MOSI, AF5, push-pull
//  - PA11 (CN4-13, D10): LCD_CE, salida push-pull, activo en 0, inicia en 1
//  - PA12 (CN4-5,  D2):  LCD_DC, salida push-pull: 0 = comando, 1 = dato
//  - RES de la LCD va a NRST: no se maneja por software. El PCD8544 necesita RES en bajo cuando
//    sube su VDD, así que hay que encender la fuente de 9 V antes del USB o presionar SW1 después.
//  - PB4 (MISO del Arduino) es RGB_R: SPI1 solo transmite y nunca se configura MISO.
//
// SPI1 (RM0440 cap. 42): maestro, 8 bits, MSB primero, modo 0 (CPOL = 0, CPHA = 0): el PCD8544
// lee SDIN en el flanco de subida de SCLK. Reloj PCLK2 / 8 = 2 MHz (el PCD8544 acepta hasta 4 MHz).
// NSS por software (SSM = 1, SSI = 1) porque CE es un GPIO. Modo bidireccional de una línea con
// la salida habilitada (BIDIMODE = 1, BIDIOE = 1): solo transmite, sin datos de recepción ni OVR.
// El DR se escribe con acceso de 8 bits: un acceso de 16 bits mandaría dos tramas.
//
// Reglas del PCD8544 (AyudaProgra, "LCD"): bajar CE antes de enviar y subirlo al terminar; DC debe
// estar listo antes del octavo flanco de SCLK de cada byte, así que solo se cambia con SPI1 libre
// (FIFO de transmisión vacía y BSY = 0).
//
// Memoria de la pantalla: 6 bancos (filas de 8 píxeles) x 84 columnas; cada byte es una columna
// de 8 píxeles con el bit 0 arriba. Con direccionamiento horizontal (V = 0) la columna avanza sola.
// Texto: fuente de 5 x 7 en celdas de 6 x 8 -> 14 caracteres por línea y 6 líneas.

#define PIN_LCD_SCK         3       // PB3
#define PIN_LCD_MOSI        5       // PB5
#define PIN_LCD_CE          11      // PA11
#define PIN_LCD_DC          12      // PA12
#define AF5_SPI1            5

#define LCD_ANCHO           84      // Columnas
#define LCD_BANCOS          6       // Filas de 8 píxeles
#define LCD_CHAR_ANCHO      6       // 5 columnas de la letra + 1 de separación
#define LCD_COLUMNAS_TEXTO  (LCD_ANCHO / LCD_CHAR_ANCHO)    // 14
#define LCD_LINEAS_TEXTO    LCD_BANCOS                      // 6

// Instrucciones del PCD8544 (datasheet, tabla del set de instrucciones)
#define PCD_FUNCION_BASICA  0x20    // 0010 0PVH: PD = 0, V = 0 (horizontal), H = 0
#define PCD_FUNCION_EXTENDIDA 0x21  // H = 1: set de instrucciones extendido
#define PCD_PANTALLA_NORMAL 0x0C    // 0000 1D0E: D = 1, E = 0 -> modo normal
#define PCD_SET_Y           0x40    // 0100 0Y2Y1Y0 (banco 0 a 5)
#define PCD_SET_X           0x80    // 1X6...X0     (columna 0 a 83)
#define PCD_TEMPERATURA     0x04    // 0000 01TC1TC0: TC = 0
#define PCD_BIAS            0x14    // 0001 0BS2BS1BS0: BS = 100 (1:48, el valor usual)
#define PCD_VOP             0xB1    // 1VOP6..VOP0: contraste. Si se ve muy claro u oscuro,
                                    // ajustar entre 0xA8 y 0xC0 según el módulo

#define LCD_DR8             (*((__IO uint8_t *)&SPI1->DR))  // Acceso de 8 bits al registro de datos
#define LCD_TIMEOUT         100000U

// Fuente 5 x 7 para ASCII 0x20 (espacio) a 0x7E (~). Cada carácter son 5 columnas; en cada byte
// el bit 0 es el píxel de arriba.
static const uint8_t lcd_fuente[95][5] =
{
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, // ' ' ! "
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, // # $ %
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00}, // & ' (
    {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08}, // ) * +
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00}, // , - .
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, // / 0 1
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10}, // 2 3 4
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, // 5 6 7
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00}, // 8 9 :
    {0x00,0x56,0x36,0x00,0x00}, {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14}, // ; < =
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E}, // > ? @
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22}, // A B C
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, // D E F
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, // G H I
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40}, // J K L
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E}, // M N O
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, // P Q R
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F}, // S T U
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63}, // V W X
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00}, // Y Z [
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04}, // \ ] ^
    {0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78}, // _ ` a
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F}, // b c d
    {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E}, // e f g
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00}, // h i j
    {0x7F,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78}, // k l m
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08}, // n o p
    {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20}, // q r s
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C}, // t u v
    {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C}, // w x y
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00}, // z { |
    {0x00,0x41,0x36,0x08,0x00}, {0x10,0x08,0x08,0x10,0x08}                              // } ~
};

// Fuente chica de 3 x 5 para las líneas de LCDWATCH, en celdas de 4 x 8 -> 21 caracteres por
// línea. Solo tiene los caracteres que usa un watch; cualquier otro se dibuja en blanco. Las letras
// ocupan las filas 1 a 5 del banco (bit 1 a bit 5), así queda una fila libre arriba y dos abajo.
#define LCD_CHICA_ANCHO     4                               // 3 columnas + 1 de separación
#define LCD_COLUMNAS_CHICA  (LCD_ANCHO / LCD_CHICA_ANCHO)   // 21

static const char lcd_chica_caracteres[] = "0123456789ABCDEFh:-";

static const uint8_t lcd_chica_fuente[][3] =
{
    {0x3E,0x22,0x3E}, {0x24,0x3E,0x20}, {0x3A,0x2A,0x2E}, {0x2A,0x2A,0x3E}, // 0 1 2 3
    {0x0E,0x08,0x3E}, {0x2E,0x2A,0x3A}, {0x3E,0x2A,0x3A}, {0x02,0x02,0x3E}, // 4 5 6 7
    {0x3E,0x2A,0x3E}, {0x2E,0x2A,0x3E}, {0x3C,0x0A,0x3C}, {0x3E,0x2A,0x14}, // 8 9 A B
    {0x1C,0x22,0x22}, {0x3E,0x22,0x1C}, {0x3E,0x2A,0x22}, {0x3E,0x0A,0x02}, // C D E F
    {0x3E,0x08,0x30}, {0x00,0x14,0x00}, {0x08,0x08,0x08}                    // h : -
};

// Espera a que SPI1 termine de transmitir: FIFO de transmisión vacía (FTLVL = 00) y BSY = 0
// (RM0440 cap. 42, procedimiento para terminar una transmisión). Solo después se puede cambiar
// DC o subir CE.
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

// Envía n bytes a la LCD como comandos (dato = 0) o como datos de la pantalla (dato = 1).
// Si 'repetir' es 1 envía n veces el primer byte (para limpiar sin un arreglo de 504 bytes).
static void LCD_Enviar(uint8_t dato, const uint8_t *bytes, uint16_t n, uint8_t repetir)
{
    LCD_EsperarFin();                               // SPI1 libre antes de tocar DC o CE
    GPIO_Escribir(GPIOA, PIN_LCD_DC, dato);
    GPIO_Escribir(GPIOA, PIN_LCD_CE, 0);            // CE en bajo: empieza la transmisión

    for (uint16_t i = 0; i < n; i++)
    {
        uint32_t restante = LCD_TIMEOUT;

        while (!(SPI1->SR & SPI_SR_TXE) && (restante > 0))
        {
            restante--;
        }
        LCD_DR8 = repetir ? bytes[0] : bytes[i];
    }

    LCD_EsperarFin();                               // Último byte completo antes de subir CE
    GPIO_Escribir(GPIOA, PIN_LCD_CE, 1);
}

// Coloca el puntero de la memoria de la pantalla en una columna (0-83) y banco (0-5)
static void LCD_IrA(uint8_t columna, uint8_t banco)
{
    uint8_t comandos[2];

    comandos[0] = (uint8_t)(PCD_SET_X | (columna % LCD_ANCHO));
    comandos[1] = (uint8_t)(PCD_SET_Y | (banco % LCD_BANCOS));
    LCD_Enviar(0, comandos, 2, 0);
}

// Borra toda la pantalla (504 bytes en 0) y deja el puntero en (0, 0)
static void LCD_Limpiar(void)
{
    const uint8_t cero = 0x00;

    LCD_IrA(0, 0);
    LCD_Enviar(1, &cero, LCD_ANCHO * LCD_BANCOS, 1);
    LCD_IrA(0, 0);
}

// Escribe un carácter en la posición actual del puntero (6 columnas). Los caracteres fuera de
// ASCII imprimible se muestran como '?'.
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
    columnas[5] = 0x00;     // Separación entre letras

    LCD_Enviar(1, columnas, LCD_CHAR_ANCHO, 0);
}

// Escribe una línea completa de texto (banco 0 a 5) con la fuente normal: 14 caracteres. Si el
// texto es más corto, el resto de la línea se borra; si es más largo, se corta. Toda la línea se
// manda en una sola transmisión de 84 bytes (unos 0.4 ms a 2 MHz).
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

    LCD_IrA(0, linea);
    LCD_Enviar(1, columnas, LCD_ANCHO, 0);
}

// Igual que LCD_EscribirLinea pero con la fuente chica: 21 caracteres por línea
void LCD_EscribirLineaChica(uint8_t linea, const char *texto)
{
    uint8_t columnas[LCD_ANCHO];
    uint8_t terminado = 0;

    for (uint8_t i = 0; i < LCD_COLUMNAS_CHICA; i++)
    {
        int8_t indice = -1;     // -1: carácter en blanco

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

    LCD_IrA(0, linea);
    LCD_Enviar(1, columnas, LCD_ANCHO, 0);
}

// Borra toda la pantalla
void LCD_Borrar(void)
{
    LCD_Limpiar();
}

// Inicialización de SPI1, de los pines CE y DC, y del PCD8544. Deja la pantalla en blanco.
void LCD_Init(void)
{
    const uint8_t arranque[] =
    {
        PCD_FUNCION_EXTENDIDA,      // Function set con H = 1
        PCD_BIAS,                   // Bias system
        PCD_VOP,                    // Set VOP (contraste)
        PCD_TEMPERATURA,            // Temperature control
        PCD_FUNCION_BASICA,         // Function set con H = 0, direccionamiento horizontal
        PCD_PANTALLA_NORMAL         // Display control: modo normal
    };

    // CE en 1 (LCD sin seleccionar) y DC en 0 antes de conectar el SPI
    GPIO_ConfigSalida(GPIOA, PIN_LCD_CE, 1);
    GPIO_ConfigSalida(GPIOA, PIN_LCD_DC, 0);

    // Reloj de SPI1 (bus APB2) y reinicio desde el RCC
    RCC_ActivarAPB2(RCC_APB2ENR_SPI1EN, RCC_APB2RSTR_SPI1RST);

    // CR1 sin SPE: modo 0, maestro, PCLK2 / 8, MSB primero, NSS por software (SSM = SSI = 1),
    // una línea con salida habilitada, sin CRC
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 &= ~(SPI_CR1_CPHA | SPI_CR1_CPOL | SPI_CR1_BR | SPI_CR1_LSBFIRST |
                   SPI_CR1_RXONLY | SPI_CR1_CRCEN | SPI_CR1_CRCL);
    SPI1->CR1 |= (SPI_CR1_MSTR | (2U << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI |
                  SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE);

    // CR2: tramas de 8 bits (DS = 0111) y FRXTH = 1 (umbral de 8 bits)
    SPI1->CR2 &= ~SPI_CR2_DS;
    SPI1->CR2 |= ((7U << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH);

    // Pines del SPI y, al final, SPE
    GPIO_ConfigAF(GPIOB, PIN_LCD_SCK, AF5_SPI1, GPIO_PUSH_PULL);
    GPIO_ConfigAF(GPIOB, PIN_LCD_MOSI, AF5_SPI1, GPIO_PUSH_PULL);
    SPI1->CR1 |= SPI_CR1_SPE;

    // Secuencia de arranque del PCD8544 y pantalla en blanco
    LCD_Enviar(0, arranque, sizeof(arranque), 0);
    LCD_Limpiar();
}

// Comando: LCD string
// Borra la pantalla y escribe el mensaje desde la esquina superior izquierda, con salto de línea
// automático cada 14 caracteres (máximo 6 líneas, 84 caracteres). Blocking: una pantalla
// completa tarda unos milisegundos a 2 MHz. Detiene LCDWATCH si estaba activo.
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

    // LCDWATCH también usa SPI1 desde el tick: se pausa el tick mientras se escribe la pantalla
    // (unos 5 ms para una pantalla llena). Si había watches, se detienen para que no tapen el texto.
    Tick_Bloquear();

    if (Watch_Activo())
    {
        Watch_Detener();
        UART_Print("LCDWATCH detenido.\r\n");
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
