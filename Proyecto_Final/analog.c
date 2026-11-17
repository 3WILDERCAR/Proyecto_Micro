#include "analog.h"
#include "../monitor.h"   // UART_Print, UART_PrintHex32, ParseHex

// Conversiones analógico-digital y digital-analógico, y monitoreo continuo del potenciómetro.
// ESQUELETO: todavía no se configura ningún periférico ni pin. Los registros, direcciones y
// pines se agregarán al implementar cada comando, verificados contra el manual y el esquemático.

// Inicialización del ADC (entrada analógica / potenciómetro).
// Pendiente de implementar.
void Analog_InitADC(void)
{
}

// Inicialización de la salida analógica.
// Pendiente de implementar.
void Analog_InitDAC(void)
{
}

// Comando: VIN [res]
// Conversión AD con resolución res (default: 8 bits). Imprime el voltaje a 4 dígitos de detalle. Blocking.
void Cmd_VIN(char *args)
{
    (void)args;   // Sin usar hasta que se implemente el comando
    UART_Print("VIN: comando pendiente de implementar.\r\n");
}

// Comando: VOUT voltage [res]
// Conversión DA para sacar el nivel voltage con resolución res (default: 8 bits). Blocking.
void Cmd_VOUT(char *args)
{
    (void)args;   // Sin usar hasta que se implemente el comando
    UART_Print("VOUT: comando pendiente de implementar.\r\n");
}

// Comando: POT2SEG {on|off} [{dec|hex}]
// Arranca o para el despliegue en el display de 7 segmentos de la conversión AD del potenciómetro. No debe bloquear al monitor.
void Cmd_POT2SEG(char *args)
{
    (void)args;   // Sin usar hasta que se implemente el comando
    UART_Print("POT2SEG: comando pendiente de implementar.\r\n");
}
