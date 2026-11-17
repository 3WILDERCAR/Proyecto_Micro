#include "io.h"
#include "../monitor.h"   // UART_Print, UART_PrintHex32, ParseHex

// Entradas y salidas digitales GPIO (switches y LEDs).
// ESQUELETO: todavía no se configura ningún periférico ni pin. Los registros, direcciones y
// pines se agregarán al implementar cada comando, verificados contra el manual y el esquemático.

// Inicialización de los GPIO de entrada (switches) y de salida (LEDs).
// Pendiente de implementar.
void IO_Init(void)
{
}

// Comando: IOMAP in out [not] [all]
// Asigna el estado de la entrada digital in a la salida digital out. No debe bloquear al monitor.
void Cmd_IOMAP(char *args)
{
    (void)args;   // Sin usar hasta que se implemente el comando
    UART_Print("IOMAP: comando pendiente de implementar.\r\n");
}

// Comando: IOUNMAP
// Revierte todas las asignaciones realizadas con IOMAP.
void Cmd_IOUNMAP(char *args)
{
    (void)args;   // Sin usar hasta que se implemente el comando
    UART_Print("IOUNMAP: comando pendiente de implementar.\r\n");
}
