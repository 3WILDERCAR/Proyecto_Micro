#include "hw_comun.h"
#include "watch.h"
#include "display.h"
#include "tick.h"
#include "monitor.h"   // UART_Print, UART_PrintHex32, UART_PrintDec, ParseHex

// Monitoreo de direcciones de memoria en la pantalla LCD (LCDWATCH).
//
// Distribución de la pantalla (84 x 48, 6 bancos de 8 píxeles):
//  - Línea 0: título "LCDWATCH" con la fuente normal.
//  - Línea 1: en blanco.
//  - Líneas 2 a 5: un watch por línea con la fuente chica (21 caracteres), por ejemplo
//        "1 20000000:       FFh"     (hex, byte)
//        "2 20000004:4294967295"     (dec, word)
//    Número del watch, dirección en hex de 8 dígitos, ':' y el valor alineado a la derecha en
//    10 caracteres. El hex lleva 2, 4 u 8 dígitos según el tamaño y termina en 'h' (igual que
//    POT2SEG en el display); el decimal es sin signo.
//  La fuente normal (14 caracteres por línea) no alcanza: dirección + valor necesitan hasta 19.
//
// Tiempo real sin bloquear: SysTick_Handler llama a Watch_Actualizar() cada WATCH_PERIODO_TICKS.
// Cada llamada lee UN watch y redibuja su línea solo si el valor cambió (unos 0.4 ms de SPI1),
// así cada watch se revisa cada 4 x 10 ms = 40 ms con cuatro watches.
//
// Uso compartido de SPI1: el tick y el programa principal (comandos LCD y LCDWATCH) usan la LCD.
// El programa principal la usa solo con el tick bloqueado (Tick_Bloquear), igual que el bus I2C1.
//
// Direcciones válidas: una lectura de una dirección que no existe (ej. 0x30000000) produce un
// BusFault; dentro del tick eso dejaría al monitor en HardFault. Por eso el comando prueba la
// dirección una vez antes de agregarla (Watch_Probar) y exige alineación para hw y w: una lectura
// desalineada en la zona de periféricos produce un UsageFault (PM0214, "Unaligned access").

#define WATCH_MAX               4
#define WATCH_LINEA_TITULO      0
#define WATCH_PRIMERA_LINEA     2       // Watch n (0 a 3) en la línea 2 + n
#define WATCH_ANCHO_VALOR       10      // Caracteres del valor: 4294967295 es el más largo
#define WATCH_LARGO_LINEA       21      // "n AAAAAAAA:" (11) + valor (10)

typedef struct
{
    uint32_t direccion;
    uint32_t mostrado;      // Último valor dibujado en la pantalla
    uint8_t tamano;         // 1 (b), 2 (hw) o 4 (w) bytes
    uint8_t hex;            // 1 = hexadecimal, 0 = decimal sin signo
    uint8_t usado;          // 1 si la ranura tiene un watch
} Watch_t;

// Solo se modifican con el tick bloqueado; el tick las lee
static Watch_t watches[WATCH_MAX];
static volatile uint8_t watch_activo = 0;   // 1 si la pantalla está en modo LCDWATCH
static uint8_t watch_siguiente = 0;         // Ranura del próximo watch (la más vieja)
static uint8_t watch_revisar = 0;           // Ranura que revisa el tick (solo la usa el tick)

// Lee el valor de la dirección con el tamaño de acceso indicado
static uint32_t Watch_Leer(uint32_t direccion, uint8_t tamano)
{
    if (tamano == 4)
    {
        return *((volatile uint32_t *)direccion);
    }
    if (tamano == 2)
    {
        return *((volatile uint16_t *)direccion);
    }
    return *((volatile uint8_t *)direccion);
}

// Prueba si la dirección se puede leer sin un BusFault. Devuelve 1 si sí.
// Técnica de PM0214 (registro CCR, bit BFHFNMIGN): con FAULTMASK = 1 el procesador corre con
// prioridad -1 y, con BFHFNMIGN = 1, un BusFault de una lectura no genera excepción: la lectura
// termina con un valor cualquiera y el error queda anotado en BFSR (parte de SCB->CFSR).
// BFSR se limpia escribiendo 1 en sus bits.
static uint8_t Watch_Probar(uint32_t direccion, uint8_t tamano)
{
    uint32_t errores;

    SCB->CFSR = SCB_CFSR_BUSFAULTSR_Msk;        // Limpia errores anteriores del bus

    __disable_fault_irq();                      // FAULTMASK = 1
    SCB->CCR |= SCB_CCR_BFHFNMIGN_Msk;
    __DSB();
    __ISB();

    (void)Watch_Leer(direccion, tamano);
    __DSB();

    errores = SCB->CFSR & SCB_CFSR_BUSFAULTSR_Msk;

    SCB->CCR &= ~SCB_CCR_BFHFNMIGN_Msk;
    __DSB();
    __ISB();
    __enable_fault_irq();                       // FAULTMASK = 0

    if (errores != 0)
    {
        SCB->CFSR = errores;                    // Deja BFSR limpio
        return 0;
    }
    return 1;
}

// Escribe el valor en texto, alineado a la derecha en WATCH_ANCHO_VALOR caracteres (con espacios
// a la izquierda) y terminado en '\0'
static void Watch_FormatearValor(uint32_t valor, uint8_t tamano, uint8_t hex, char texto[WATCH_ANCHO_VALOR + 1])
{
    const char digitos_hex[] = "0123456789ABCDEF";
    int8_t pos = WATCH_ANCHO_VALOR - 1;

    for (uint8_t i = 0; i < WATCH_ANCHO_VALOR; i++)
    {
        texto[i] = ' ';
    }
    texto[WATCH_ANCHO_VALOR] = '\0';

    if (hex)
    {
        texto[pos--] = 'h';
        for (uint8_t i = 0; i < (uint8_t)(tamano * 2); i++)
        {
            texto[pos--] = digitos_hex[valor & 0xF];
            valor >>= 4;
        }
    }
    else
    {
        do
        {
            texto[pos--] = (char)('0' + (valor % 10U));
            valor /= 10U;
        } while (valor != 0 && pos >= 0);
    }
}

// Dibuja la línea completa de la ranura n con el valor dado
static void Watch_Dibujar(uint8_t n, uint32_t valor)
{
    const char digitos_hex[] = "0123456789ABCDEF";
    const Watch_t *w = &watches[n];
    char linea[WATCH_LARGO_LINEA + 1];

    linea[0] = (char)('1' + n);
    linea[1] = ' ';
    for (uint8_t i = 0; i < 8; i++)
    {
        linea[2 + i] = digitos_hex[(w->direccion >> (28 - (4 * i))) & 0xF];
    }
    linea[10] = ':';
    Watch_FormatearValor(valor, w->tamano, w->hex, &linea[11]);

    LCD_EscribirLineaChica((uint8_t)(WATCH_PRIMERA_LINEA + n), linea);
}

// Lee una dirección escrita en hexadecimal (con o sin 0x, de 1 a 8 dígitos) que termina en
// espacio o en fin de cadena. Devuelve cuántos caracteres usó, o 0 si no es válida.
static uint8_t Watch_LeerDireccion(const char *str, uint32_t *direccion)
{
    uint8_t usados = 0;
    uint8_t digitos = 0;

    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
    {
        usados = 2;
    }

    while (str[usados + digitos] != '\0' && str[usados + digitos] != ' ')
    {
        char c = str[usados + digitos];

        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
        {
            return 0;
        }
        digitos++;
        if (digitos > 8)
        {
            return 0;
        }
    }

    if (digitos == 0)
    {
        return 0;
    }

    *direccion = ParseHex(str);     // ParseHex ignora el 0x y se detiene en el espacio
    return (uint8_t)(usados + digitos);
}

// Inicialización: lista de watches vacía. LCDWATCH no configura periféricos propios: usa la LCD
// que ya inicializó LCD_Init.
void Watch_Init(void)
{
    for (uint8_t i = 0; i < WATCH_MAX; i++)
    {
        watches[i].usado = 0;
    }
    watch_siguiente = 0;
    watch_revisar = 0;
    watch_activo = 0;
}

// Tarea de fondo (desde SysTick_Handler): revisa el siguiente watch en uso y redibuja su línea si
// el valor cambió. A lo sumo una línea por llamada.
void Watch_Actualizar(void)
{
    if (!watch_activo)
    {
        return;
    }

    for (uint8_t intentos = 0; intentos < WATCH_MAX; intentos++)
    {
        uint8_t n = watch_revisar;

        watch_revisar = (uint8_t)((watch_revisar + 1) % WATCH_MAX);

        if (watches[n].usado)
        {
            uint32_t valor = Watch_Leer(watches[n].direccion, watches[n].tamano);

            if (valor != watches[n].mostrado)
            {
                Watch_Dibujar(n, valor);
                watches[n].mostrado = valor;
            }
            return;
        }
    }
}

uint8_t Watch_Activo(void)
{
    return watch_activo;
}

// Borra la lista; el próximo LCDWATCH vuelve a preparar la pantalla
void Watch_Detener(void)
{
    Watch_Init();
}

// Comando: LCDWATCH addr [{dec|hex}] [{b|hw|w}]
// Agrega un watch a la LCD (hasta cuatro; el quinto sobreescribe el más viejo). Las opciones se
// aceptan en cualquier orden y sin distinguir mayúsculas. Por omisión: dec y b (las primeras de
// cada lista, igual que POT2SEG).
void Cmd_LCDWATCH(char *args)
{
    uint32_t direccion;
    uint32_t valor;
    uint8_t tamano = 1;
    uint8_t hex = 0;
    uint8_t formato_dado = 0;
    uint8_t tamano_dado = 0;
    uint8_t reemplazado = 0;
    uint8_t n;
    uint8_t largo;
    char texto_valor[WATCH_ANCHO_VALOR + 1];

    largo = Watch_LeerDireccion(args, &direccion);
    if (largo == 0)
    {
        UART_Print("Error de sintaxis. Uso correcto: LCDWATCH addr [{dec|hex}] [{b|hw|w}]\r\n");
        return;
    }
    args += largo;

    // Opciones
    while (1)
    {
        while (*args == ' ')
        {
            args++;
        }
        if (*args == '\0')
        {
            break;
        }

        if (!formato_dado && (largo = Texto_EsPalabra(args, "dec")) != 0)
        {
            hex = 0;
            formato_dado = 1;
        }
        else if (!formato_dado && (largo = Texto_EsPalabra(args, "hex")) != 0)
        {
            hex = 1;
            formato_dado = 1;
        }
        else if (!tamano_dado && (largo = Texto_EsPalabra(args, "b")) != 0)
        {
            tamano = 1;
            tamano_dado = 1;
        }
        else if (!tamano_dado && (largo = Texto_EsPalabra(args, "hw")) != 0)
        {
            tamano = 2;
            tamano_dado = 1;
        }
        else if (!tamano_dado && (largo = Texto_EsPalabra(args, "w")) != 0)
        {
            tamano = 4;
            tamano_dado = 1;
        }
        else
        {
            UART_Print("Error de sintaxis. Uso correcto: LCDWATCH addr [{dec|hex}] [{b|hw|w}]\r\n");
            return;
        }
        args += largo;
    }

    if ((direccion % tamano) != 0)
    {
        UART_Print("Error: con hw la direccion debe ser multiplo de 2 y con w multiplo de 4.\r\n");
        return;
    }

    if (!Watch_Probar(direccion, tamano))
    {
        UART_Print("Error: la direccion ");
        UART_PrintHex32(direccion);
        UART_Print(" no se puede leer.\r\n");
        return;
    }

    // Pantalla y lista con el tick bloqueado (SPI1 y la lista se comparten con el tick)
    Tick_Bloquear();

    if (!watch_activo)
    {
        // Primer watch: la pantalla deja de mostrar lo que hubiera (ej. el texto de LCD)
        Watch_Init();
        LCD_Borrar();
        LCD_EscribirLinea(WATCH_LINEA_TITULO, "LCDWATCH");
    }

    n = watch_siguiente;
    reemplazado = watches[n].usado;

    valor = Watch_Leer(direccion, tamano);
    watches[n].direccion = direccion;
    watches[n].tamano = tamano;
    watches[n].hex = hex;
    watches[n].mostrado = valor;
    watches[n].usado = 1;
    Watch_Dibujar(n, valor);

    watch_siguiente = (uint8_t)((watch_siguiente + 1) % WATCH_MAX);
    watch_activo = 1;

    Tick_Desbloquear();

    // Confirmación en la terminal, ej. "Watch 1: 0x20000000 (b, hex) = FFh"
    Watch_FormatearValor(valor, tamano, hex, texto_valor);
    largo = 0;
    while (texto_valor[largo] == ' ')
    {
        largo++;
    }

    UART_Print("Watch ");
    UART_PrintDec(n + 1);
    UART_Print(": ");
    UART_PrintHex32(direccion);
    UART_Print((tamano == 1) ? " (b, " : ((tamano == 2) ? " (hw, " : " (w, "));
    UART_Print(hex ? "hex) = " : "dec) = ");
    UART_Print(&texto_valor[largo]);
    if (reemplazado)
    {
        UART_Print("  (reemplaza al watch mas viejo)");
    }
    UART_Print("\r\n");
}
