#include "hw_comun.h"
#include "watch.h"
#include "display.h"
#include "tick.h"
#include "Arcade.h"
#include "monitor.h"

/* LCDWATCH: título en la línea 0 y un watch por línea (2 a 5) con la fuente chica */

#define WATCH_MAX               4
#define WATCH_LINEA_TITULO      0
#define WATCH_PRIMERA_LINEA     2       /* Watch n en la línea 2 + n */
#define WATCH_ANCHO_VALOR       10
#define WATCH_LARGO_LINEA       21

typedef struct
{
    uint32_t direccion;
    uint32_t mostrado;      /* Último valor dibujado */
    uint8_t tamano;         /* 1, 2 o 4 bytes */
    uint8_t hex;
    uint8_t usado;
} Watch_t;

/* Lista de watches (se cambia con el tick bloqueado) */
static Watch_t watches[WATCH_MAX];
static volatile uint8_t watch_activo = 0;
static uint8_t watch_siguiente = 0;         /* Ranura más vieja */
static uint8_t watch_revisar = 0;           /* Ranura que revisa el tick */

/* Lee la dirección con el tamaño indicado */
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

/* Prueba si la dirección se puede leer sin BusFault: 1 si sí */
static uint8_t Watch_Probar(uint32_t direccion, uint8_t tamano)
{
    uint32_t errores;

    SCB->CFSR = SCB_CFSR_BUSFAULTSR_Msk;

    __disable_fault_irq();
    SCB->CCR |= SCB_CCR_BFHFNMIGN_Msk;
    __DSB();
    __ISB();

    (void)Watch_Leer(direccion, tamano);
    __DSB();

    errores = SCB->CFSR & SCB_CFSR_BUSFAULTSR_Msk;

    SCB->CCR &= ~SCB_CCR_BFHFNMIGN_Msk;
    __DSB();
    __ISB();
    __enable_fault_irq();

    if (errores != 0)
    {
        SCB->CFSR = errores;
        return 0;
    }
    return 1;
}

/* Valor en texto, alineado a la derecha */
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

/* Dibuja la línea de un watch */
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

/* Lee una dirección hex; devuelve los caracteres usados o 0 */
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

    *direccion = ParseHex(str);
    return (uint8_t)(usados + digitos);
}

/* Inicialización: lista vacía */
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

/* Tarea de fondo: revisa un watch y redibuja su línea si cambió */
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

/* Borra la lista de watches */
void Watch_Detener(void)
{
    Watch_Init();
}

/* Comando: LCDWATCH addr [{dec|hex}] [{b|hw|w}] */
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

    /* Opciones */
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

    /* Pantalla y lista con el tick bloqueado */
    Tick_Bloquear();

    if (!watch_activo)
    {
        /* Primer watch: pantalla limpia y título */
        if (Arcade_Activo())
        {
            Arcade_Detener();
            UART_Print("ARCADE detenido.\r\n");
        }
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

    /* Confirmación */
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
