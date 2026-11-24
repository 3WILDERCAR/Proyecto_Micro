#include <stdint.h>
#include "hw_comun.h"
#include "io.h"
#include "i2c1.h"
#include "tick.h"
#include "monitor.h"

/* Switches (GPB, IN0-7) y LEDs (GPA, OUT0-7, activos en bajo) en U5 */

#define IO_CANALES          8

/* Configuración de U5 */
#define U5_IODIRA           0x00    /* LEDs: salidas */
#define U5_IODIRB           0xFF    /* Switches: entradas */
#define U5_GPPUA            0x00
#define U5_GPPUB            0xFF    /* Pull-up en los switches */
#define U5_LEDS_APAGADOS    0xFF

/* Un mapeo por salida */
typedef struct
{
    uint8_t activo;
    uint8_t entrada;
    uint8_t invertido;      /* "not" */
} IO_Mapeo_t;

/* Estado compartido con el tick */
static volatile IO_Mapeo_t io_mapa[IO_CANALES];
static volatile uint8_t io_activo = 0;
static volatile uint8_t io_en_linea = 0;
static uint8_t io_olat_actual = U5_LEDS_APAGADOS;

/* Inicialización de U5: OLAT, pull-ups y al final IODIR */
uint8_t IO_Init(void)
{
    const uint8_t olata    = U5_LEDS_APAGADOS;
    const uint8_t gppu[2]  = {U5_GPPUA, U5_GPPUB};
    const uint8_t iodir[2] = {U5_IODIRA, U5_IODIRB};

    io_en_linea = 0;

    if (MCP23017_WriteRegs(MCP_U5_ADDR, MCP_OLATA, &olata, 1) != I2C1_OK)
    {
        return 0;
    }
    if (MCP23017_WriteRegs(MCP_U5_ADDR, MCP_GPPUA, gppu, 2) != I2C1_OK)
    {
        return 0;
    }
    if (MCP23017_WriteRegs(MCP_U5_ADDR, MCP_IODIRA, iodir, 2) != I2C1_OK)
    {
        return 0;
    }

    io_olat_actual = U5_LEDS_APAGADOS;
    io_en_linea = 1;
    return 1;
}

/* Cálculo de los LEDs a partir de los switches */
static uint8_t IO_CalcularLeds(uint8_t switches)
{
    uint8_t encendidos = 0;

    for (uint8_t salida = 0; salida < IO_CANALES; salida++)
    {
        if (io_mapa[salida].activo)
        {
            uint8_t estado = (uint8_t)((switches >> io_mapa[salida].entrada) & 1U);

            if (io_mapa[salida].invertido)
            {
                estado ^= 1U;
            }
            encendidos |= (uint8_t)(estado << salida);
        }
    }

    return (uint8_t)~encendidos;    /* Activos en bajo */
}

/* Tarea de fondo: switches -> LEDs */
void IO_Actualizar(void)
{
    uint8_t switches;
    uint8_t olat;

    if (!io_activo || !io_en_linea)
    {
        return;
    }

    /* Lectura de los switches */
    if (MCP23017_ReadRegs(MCP_U5_ADDR, MCP_GPIOB, &switches, 1) != I2C1_OK)
    {
        io_en_linea = 0;
        return;
    }

    olat = IO_CalcularLeds(switches);

    /* Escritura solo si cambió */
    if (olat != io_olat_actual)
    {
        if (MCP23017_WriteRegs(MCP_U5_ADDR, MCP_OLATA, &olat, 1) != I2C1_OK)
        {
            io_en_linea = 0;
            return;
        }
        io_olat_actual = olat;
    }
}

/* Inicializa U5 si hace falta */
static uint8_t IO_AsegurarU5(void)
{
    uint8_t resultado;

    if (io_en_linea)
    {
        return 1;
    }

    Tick_Bloquear();
    resultado = IO_Init();
    Tick_Desbloquear();

    return resultado;
}

/* Comando: IOMAP in out [not] [all] */
void Cmd_IOMAP(char *args)
{
    int32_t numeros[2];
    uint8_t cantidad = 0;
    uint8_t invertido = 0;
    uint8_t todos = 0;
    char *ptr = args;

    /* Lectura de argumentos */
    while (*ptr != '\0')
    {
        uint8_t largo;

        if ((largo = Texto_EsPalabra(ptr, "not")) != 0)
        {
            invertido = 1;
        }
        else if ((largo = Texto_EsPalabra(ptr, "all")) != 0)
        {
            todos = 1;
        }
        else if (cantidad < 2 && (largo = ParseDec(ptr, &numeros[cantidad])) != 0)
        {
            cantidad++;
        }
        else
        {
            UART_Print("Error de sintaxis. Uso correcto: IOMAP in out [not] [all]  o  IOMAP all [not]\r\n");
            return;
        }

        ptr += largo;
        while (*ptr == ' ')
        {
            ptr++;
        }
    }

    /* Validación */
    if ((!todos && cantidad != 2) || (todos && cantidad == 1))
    {
        UART_Print("Error de sintaxis. Uso correcto: IOMAP in out [not] [all]  o  IOMAP all [not]\r\n");
        return;
    }

    if (!todos && (numeros[0] < 0 || numeros[0] >= IO_CANALES ||
                   numeros[1] < 0 || numeros[1] >= IO_CANALES))
    {
        UART_Print("Error: in y out deben estar entre 0 y 7.\r\n");
        return;
    }

    if (!IO_AsegurarU5())
    {
        UART_Print("Error: los switches/LEDs no responden (U5, I2C 0x20). Revise la fuente de 9 V.\r\n");
        return;
    }

    /* Tabla de mapeos */
    Tick_Bloquear();
    if (todos)
    {
        for (uint8_t i = 0; i < IO_CANALES; i++)
        {
            io_mapa[i].entrada = i;
            io_mapa[i].invertido = invertido;
            io_mapa[i].activo = 1;
        }
    }
    else
    {
        io_mapa[numeros[1]].entrada = (uint8_t)numeros[0];
        io_mapa[numeros[1]].invertido = invertido;
        io_mapa[numeros[1]].activo = 1;
    }
    io_activo = 1;
    Tick_Desbloquear();

    UART_Print("IOMAP: ");
    if (todos)
    {
        UART_Print("IN0-7 -> OUT0-7");
    }
    else
    {
        UART_Print("IN");
        UART_PrintDec(numeros[0]);
        UART_Print(" -> OUT");
        UART_PrintDec(numeros[1]);
    }
    if (invertido)
    {
        UART_Print(" (invertido)");
    }
    UART_Print("\r\n");
}

/* Comando: IOUNMAP */
void Cmd_IOUNMAP(char *args)
{
    const uint8_t apagados = U5_LEDS_APAGADOS;
    uint8_t resultado = I2C1_OK;

    if (*args != '\0')
    {
        UART_Print("Error de sintaxis. Uso correcto: IOUNMAP\r\n");
        return;
    }

    Tick_Bloquear();

    for (uint8_t i = 0; i < IO_CANALES; i++)
    {
        io_mapa[i].activo = 0;
    }
    io_activo = 0;

    /* LEDs apagados */
    if (io_en_linea)
    {
        resultado = MCP23017_WriteRegs(MCP_U5_ADDR, MCP_OLATA, &apagados, 1);
        if (resultado == I2C1_OK)
        {
            io_olat_actual = U5_LEDS_APAGADOS;
        }
        else
        {
            io_en_linea = 0;
        }
    }

    Tick_Desbloquear();

    if (resultado != I2C1_OK)
    {
        UART_Print("Mapeos eliminados, pero U5 no respondio al apagar los LEDs.\r\n");
        return;
    }
    UART_Print("IOUNMAP: todos los mapeos eliminados.\r\n");
}
