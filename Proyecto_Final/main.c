#include <stdint.h>
#include "stm32g431xx.h" 
#include "setup_seguridad.h"
#include "connect.h"
#include "monitor.h"

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

    // Inicializa el hardware y relojes
    UART_Init();
    
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
