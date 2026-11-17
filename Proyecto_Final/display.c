#include <stdint.h>
#include "display.h"
#include "i2c1.h"
#include "../monitor.h"   // UART_Print, UART_PrintDec, ParseDec

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

// Patrones de los dígitos 0-9 (bit 0 = A ... bit 6 = G)
static const uint8_t seg_tabla[10] =
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
    0x6F    // 9: A B C D F G
};

#define SEG_GUION       0x40        // Solo el segmento G, para el signo negativo

// Estado compartido entre el monitor y la interrupción del tick
static volatile uint8_t seg_patrones[SEG_DIGITOS];  // Índice 0 = DIG1 (izquierda)
static volatile uint8_t seg_activo = 0;             // 1 cuando hay algo que mostrar
static volatile uint8_t seg_en_linea = 0;           // 1 si U6 respondió en la última transferencia
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

// Inicialización de la pantalla LCD.
// Pendiente de implementar (comando LCD, paso 7).
void LCD_Init(void)
{
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
// la izquierda (ej. 42 -> "  42", -7 -> "  -7", 0 -> "   0")
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

// Comando: SEGMENT num
// Despliega permanentemente el número decimal num (-999 a 9999) en el display de 7 segmentos.
// El valor se queda mostrando hasta el siguiente SEGMENT o un reset.
void Cmd_SEGMENT(char *args)
{
    int32_t num;
    uint8_t usados;
    uint8_t nuevos[SEG_DIGITOS];
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
    if (!seg_en_linea)
    {
        if (!Segment_Init())
        {
            UART_Print("Error: el display no responde (U6, I2C 0x21). Revise la fuente de 9 V.\r\n");
            return;
        }
    }

    // Copiamos los patrones nuevos. Si el tick interrumpe a la mitad, a lo sumo un dígito
    // muestra el valor anterior durante un refresco (2 ms), lo cual no se nota.
    Segment_Convertir(num, nuevos);
    for (uint8_t i = 0; i < SEG_DIGITOS; i++)
    {
        seg_patrones[i] = nuevos[i];
    }
    seg_activo = 1;

    UART_Print("Display: ");
    UART_PrintDec(num);
    UART_Print("\r\n");
}

// Comando: LCD string
// Envía el mensaje string a la pantalla LCD.
void Cmd_LCD(char *args)
{
    (void)args;   // Sin usar hasta que se implemente el comando
    UART_Print("LCD: comando pendiente de implementar.\r\n");
}
