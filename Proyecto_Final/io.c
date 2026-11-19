#include <stdint.h>
#include "hw_comun.h"
#include "io.h"
#include "i2c1.h"
#include "tick.h"
#include "monitor.h"   // UART_Print, UART_PrintDec, ParseDec

// Entradas y salidas digitales en el expansor U5 (MCP23017, dirección 0x20) del bus I2C1
// (AyudaProgra, "Pines de los MCP23017"):
//  - Puerto A: GPA0-GPA7 = OUT0-OUT7, LEDs D3-D10 (bit n = OUTn). Activos en bajo: 0 = LED encendido.
//  - Puerto B: GPB0-GPB7 = IN0-IN7, switches SW2-SW9 (bit n = INn). Switch hacia GND = 0;
//    hacia el tiro 1 = 1, que lo da el pull-up interno (GPPUB = 0xFF es obligatorio, porque el
//    tiro 1 del SS-12E17 quedó libre).
//
// Lógica: el estado de una salida es el de su entrada (1 = LED encendido), o el contrario con
// "not". Como los LEDs son activos en bajo, el valor que se escribe en OLATA es el complemento.
//
// Funcionamiento no bloqueante: IOMAP e IOUNMAP solo modifican la tabla de mapeos. La tarea
// IO_Actualizar() corre en el tick (cada 10 ms): lee GPIOB, calcula los LEDs y escribe OLATA
// solo si cambiaron. Si U5 deja de responder se marca fuera de línea y la tarea deja de usar el
// bus; el siguiente IOMAP lo vuelve a inicializar.
//
// Cada salida tiene como máximo una entrada asignada (un IOMAP nuevo sobre la misma salida
// reemplaza al anterior). Una misma entrada puede manejar varias salidas.

#define IO_CANALES          8

// Configuración de U5 (AyudaProgra, tabla de U5)
#define U5_IODIRA           0x00    // LEDs: salidas
#define U5_IODIRB           0xFF    // Switches: entradas
#define U5_GPPUA            0x00    // Sin pull-up en los LEDs
#define U5_GPPUB            0xFF    // Pull-up en los switches (obligatorio)
#define U5_LEDS_APAGADOS    0xFF    // OLATA: 1 = LED apagado

// Un mapeo por salida
typedef struct
{
    uint8_t activo;         // 1 si la salida tiene una entrada asignada
    uint8_t entrada;        // 0 a 7
    uint8_t invertido;      // 1 con el modificador "not"
} IO_Mapeo_t;

// Estado compartido entre el monitor y la interrupción del tick
static volatile IO_Mapeo_t io_mapa[IO_CANALES];
static volatile uint8_t io_activo = 0;          // 1 si hay al menos un mapeo
static volatile uint8_t io_en_linea = 0;        // 1 si U5 respondió en la última transferencia
static uint8_t io_olat_actual = U5_LEDS_APAGADOS;   // Último valor escrito en OLATA

// Inicialización de U5. Primero OLATA en 0xFF para que los LEDs nazcan apagados, luego las
// pull-ups y al final IODIR (AyudaProgra: "se escribe OLAT antes que IODIR").
// Si el tick ya está corriendo, quien llama debe haberlo bloqueado.
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

// Calcula el valor de OLATA (activo en bajo) a partir de los switches y la tabla de mapeos.
// Las salidas sin mapeo quedan apagadas.
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

    return (uint8_t)~encendidos;    // LEDs activos en bajo
}

// Tarea de fondo (desde SysTick_Handler): switches -> LEDs
void IO_Actualizar(void)
{
    uint8_t switches;
    uint8_t olat;

    if (!io_activo || !io_en_linea)
    {
        return;
    }

    // Leemos el puerto de los switches (GPIOB refleja el nivel de los pines)
    if (MCP23017_ReadRegs(MCP_U5_ADDR, MCP_GPIOB, &switches, 1) != I2C1_OK)
    {
        io_en_linea = 0;
        return;
    }

    olat = IO_CalcularLeds(switches);

    // Solo escribimos si algún LED cambió, para no ocupar el bus de más
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

// Si U5 no está en línea, lo inicializa con el tick bloqueado (el tick también usa el bus).
// Devuelve 1 si U5 quedó en línea.
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

// Comando: IOMAP in out [not] [all]
// Asigna el estado de la entrada digital in (switch 0-7) a la salida digital out (LED 0-7).
// "not" invierte el mapeo; "all" mapea todos los switches a sus LEDs (INn -> OUTn).
// El mapeo opera en paralelo con el monitor (tarea del tick), no bloquea.
void Cmd_IOMAP(char *args)
{
    int32_t numeros[2];
    uint8_t cantidad = 0;
    uint8_t invertido = 0;
    uint8_t todos = 0;
    char *ptr = args;

    // Recorremos las palabras: números (in, out) y los modificadores not / all
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

    // Sin "all" se necesitan in y out; con "all" se permiten 0 o 2 números (se ignoran)
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

    // Actualizamos la tabla con el tick bloqueado para que no lea un mapeo a medias
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

// Comando: IOUNMAP
// Revierte todas las asignaciones de IOMAP y apaga los LEDs.
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

    // Apagamos los LEDs (el tick ya no los toca porque no hay mapeos)
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
