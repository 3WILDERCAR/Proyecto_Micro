#include <stdint.h>
#include "stm32g431xx.h"
#include "setup_seguridad.h"
#include "ftdi_con.h"
#include "monitor.h"

/* Comandos adicionales */
#include "i2c1.h"
#include "display.h"
#include "tick.h"
#include "sound.h"
#include "rgb.h"
#include "analog.h"
#include "io.h"
#include "watch.h"

void Test_Run(void) {
    while(1) {
        UART_Print("\r\n--- BUCLE DE RUN ---\r\n");
        UART_Print("Se ha transferido el control exitosamente!\r\n");

        /* Retardo */
        for (volatile uint32_t i = 0; i < 1000000; i++);
    }
}

void Test_Call(void) {
    UART_Print("\r\n--- SUBRUTINA DE CALL ---\r\n");
    UART_Print("Ejecucion completada. Volviendo al monitor!\r\n\r\n");
}

int main(void) {

    /* Seguridad y pines libres (primero) */
    Setup_Seguridad();

    /* Serial por el cable FTDI (USART1) */
    FTDI_Init();

    /* Bus I2C1 */
    I2C1_Init();

    /* Display de 7 segmentos (U6) */
    if (!Segment_Init())
    {
        UART_Print("\r\nAviso: el display (U6) no responde. Revise la fuente de 9 V.\r\n");
    }

    /* LCD y LCDWATCH */
    LCD_Init();
    Watch_Init();

    /* Switches y LEDs (U5) */
    if (!IO_Init())
    {
        UART_Print("\r\nAviso: los switches/LEDs (U5) no responden. Revise la fuente de 9 V.\r\n");
    }

    /* Buzzer */
    Sound_Init();

    /* LED RGB */
    RGB_Init();

    /* ADC */
    if (!Analog_InitADC())
    {
        UART_Print("\r\nAviso: el ADC no respondio al inicializarse.\r\n");
    }

    /* DAC */
    Analog_InitDAC();

    /* Tick (después de todo lo que usa el bus) */
    Tick_Init();

    /* Direcciones de prueba para RUN y CALL */
    UART_Print("\r\nDirección de Test_Run: ");
    UART_PrintHex32((uint32_t)&Test_Run);
    UART_Print("\r\nDirección de Test_Call: ");
    UART_PrintHex32((uint32_t)&Test_Call);
    UART_Print("\r\n");

    /* Monitor */
    Monitor_Run();

    while(1) {
        /* No se llega aquí */
    }
}
