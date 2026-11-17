#include "watch.h"
#include "../monitor.h"   // UART_Print, UART_PrintHex32, ParseHex

// Monitoreo de direcciones de memoria en la pantalla LCD.
// ESQUELETO: todavía no se configura ningún periférico ni pin. Los registros, direcciones y
// pines se agregarán al implementar cada comando, verificados contra el manual y el esquemático.

// Inicialización de la lista de watches (hasta cuatro).
// Pendiente de implementar.
void Watch_Init(void)
{
}

// Comando: LCDWATCH addr [{dec|hex}] [{b|hw|w}]
// Agrega un watch a la LCD (hasta cuatro; el quinto sobreescribe el más viejo). No debe bloquear al monitor.
void Cmd_LCDWATCH(char *args)
{
    (void)args;   // Sin usar hasta que se implemente el comando
    UART_Print("LCDWATCH: comando pendiente de implementar.\r\n");
}
