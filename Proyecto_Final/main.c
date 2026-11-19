#include <stdint.h>
#include "stm32g431xx.h"
#include "setup_seguridad.h"
#include "ftdi_con.h"   // Antes: connect.h. Ahora el monitor usa USART1 por el cable FTDI
#include "monitor.h"

// Drivers de los comandos adicionales (fondo verde)
#include "Green_Commands/i2c1.h"
#include "Green_Commands/display.h"
#include "Green_Commands/tick.h"
#include "Green_Commands/sound.h"
#include "Green_Commands/rgb.h"
#include "Green_Commands/analog.h"
#include "Green_Commands/io.h"
#include "Green_Commands/watch.h"

void Test_Run(void) {
    while(1) {
        UART_Print("\r\n--- BUCLE DE RUN ---\r\n");
        UART_Print("Se ha transferido el control exitosamente!\r\n");

        // Retardo simple para no saturar la terminal de "EL CPU HA...."
        for (volatile uint32_t i = 0; i < 1000000; i++);
    }
}

void Test_Call(void) {
    UART_Print("\r\n--- SUBRUTINA DE CALL ---\r\n");
    UART_Print("Ejecucion completada. Volviendo al monitor!\r\n\r\n");
}

int main(void) {

    // Configuración de seguridad y liberación de pines (esquemático Setup_Seguridad)
    // Debe ir antes de cualquier otra inicialización
    Setup_Seguridad();

    // Inicializa la comunicación serial por el cable FTDI (USART1 en PA9/PA10)
    // Antes: UART_Init() con USART2 hacia el VCP del ST-LINK (connect.c, ya no se usa)
    FTDI_Init();

    // Bus I2C1 (PA15/PB7) hacia los expansores U5 y U6 de Green_Card
    I2C1_Init();

    // Display de 7 segmentos (U6). Si no responde se avisa; SEGMENT lo reintenta después
    if (!Segment_Init())
    {
        UART_Print("\r\nAviso: el display (U6) no responde. Revise la fuente de 9 V.\r\n");
    }

    // Pantalla LCD (PCD8544 en SPI1): queda en blanco
    LCD_Init();
    Watch_Init();   // LCDWATCH: lista de watches vacía

    // Switches y LEDs (U5). Si no responde se avisa; IOMAP lo reintenta después
    if (!IO_Init())
    {
        UART_Print("\r\nAviso: los switches/LEDs (U5) no responden. Revise la fuente de 9 V.\r\n");
    }

    // Buzzer (TIM4_CH1 en PB6): queda en silencio, con PB6 en 0
    Sound_Init();

    // LED RGB (TIM3_CH1-CH3 en PB4/PA7/PB0): queda apagado
    RGB_Init();

    // ADC1 para el potenciómetro (PA0 = ADC1_IN1)
    if (!Analog_InitADC())
    {
        UART_Print("\r\nAviso: el ADC no respondio al inicializarse.\r\n");
    }

    // DAC1 canal 1 (PA4 = DAC1_OUT1): queda configurado y apagado hasta el primer VOUT
    Analog_InitDAC();

    // Tick de SysTick para las tareas en paralelo (multiplexado del display e IOMAP).
    // Va después de las inicializaciones que usan el bus I2C
    Tick_Init();

    // Para mostrar el funcionamiento de RUN y CALL
    // Imprimimos las direcciones de las funciones de prueba para poder usarlas luego
    UART_Print("\r\nDirección de Test_Run: ");
    UART_PrintHex32((uint32_t)&Test_Run);
    UART_Print("\r\nDirección de Test_Call: ");
    UART_PrintHex32((uint32_t)&Test_Call);
    UART_Print("\r\n");

    // Transfiere el control al sistema del Monitor
    Monitor_Run();

    while(1) {
        // Nunca debería llegar aquí por el bucle infinito en Monitor_Run
    }
}
