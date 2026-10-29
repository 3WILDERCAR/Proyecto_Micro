#include <stdint.h>
#include "stm32g431xx.h" 
#include "setup_seguridad.h"
#include "adc.h"
#include "monitor.h"

int main(void) 
{
    // Configuración de seguridad y liberación de pines (esquemático Setup_Seguridad)
    // Debe ir antes de cualquier otra inicialización
    Setup_Seguridad();

    // Inicializa el hardware y relojes
    UART_Init();

    char buffer[40];
    uint32_t dato;
    uint32_t mv;
    volatile uint32_t i;

    adc_config();
 
    while (1)
    {
        main_adc(buffer, dato, mv, i);
    }
}