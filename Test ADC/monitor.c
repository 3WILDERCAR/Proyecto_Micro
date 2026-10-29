#include <string.h>   // Para strncmp (string comparator)
#include <stdint.h>

#define BUFFER_SIZE 64

char cmd_buffer[BUFFER_SIZE];
uint8_t buffer_index = 0;


// Función de apoyo para imprimir textos completos fácilmente
void UART_Print(const char *str) 
{
    while (*str) 
    {
        UART_Tx(*str++);
    }
}

// Para imprimir valores de 32 bits en formato hexadecimal
void UART_PrintHex32(uint32_t val) 
{
    const char hex_chars[] = "0123456789ABCDEF";
    UART_Print("0x");
    for (int i = 28; i >= 0; i -= 4) 
    {
        UART_Tx(hex_chars[(val >> i) & 0xF]);
    }
}

// Convertidor de texto Hexadecimal a uint32_t
uint32_t ParseHex(const char *str) 
{
    uint32_t val = 0;
    // Ignorar prefijo 0x si el usuario lo escribe

    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2; // Para no entorpecer el envio de los hexadecimales
    
    while (*str) 
    {
        if (*str >= '0' && *str <= '9') 
        {
            val = (val << 4) | (*str - '0');
        } else if (*str >= 'A' && *str <= 'F') 
        {
            val = (val << 4) | (*str - 'A' + 10);
        } else if (*str >= 'a' && *str <= 'f') 
        {
            val = (val << 4) | (*str - 'a' + 10);
        } else 
        {
            break; // Nos detenemos si encontramos un espacio o el fin de la cadena
        }
        str++;
    }
    return val;
}