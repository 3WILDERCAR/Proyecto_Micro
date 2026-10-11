#include "memory.h"
#include "monitor.h"
#include "connect.h"
#include <string.h>   // Para strncmp (string comparator)
#include <stdint.h>

extern void Cmd_RM(uint32_t reg_idx, uint32_t val);
extern void Cmd_RUN(uint32_t addr);
extern void Cmd_CALL(uint32_t addr);

#define BUFFER_SIZE 64

char cmd_buffer[BUFFER_SIZE];
uint8_t buffer_index = 0;


// Función de apoyo para imprimir textos completos fácilmente
void UART_Print(const char *str) {
    while (*str) {
        UART_Tx(*str++);
    }
}

// Array global donde desde el ensamblador guardamos el estado de los registros
uint32_t cpu_regs[17]; 

// Obtenemos la función que realizamos en ensamblador
extern void Cmd_RD(void);

// Para imprimir valores de 32 bits en formato hexadecimal
void UART_PrintHex32(uint32_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    UART_Print("0x");
    for (int i = 28; i >= 0; i -= 4) {
        UART_Tx(hex_chars[(val >> i) & 0xF]);
    }
}

// Convertidor de texto Hexadecimal a uint32_t
uint32_t ParseHex(const char *str) {
    uint32_t val = 0;
    // Ignorar prefijo 0x si el usuario lo escribe
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2; // Para no entorpecer el envio de los hexadecimales
    
    while (*str) {
        if (*str >= '0' && *str <= '9') {
            val = (val << 4) | (*str - '0');
        } else if (*str >= 'A' && *str <= 'F') {
            val = (val << 4) | (*str - 'A' + 10);
        } else if (*str >= 'a' && *str <= 'f') {
            val = (val << 4) | (*str - 'a' + 10);
        } else {
            break; // Nos detenemos si encontramos un espacio o el fin de la cadena
        }
        str++;
    }
    return val;
}


// Función en C llamada desde Ensamblador para mostrar los registros
void Print_RD(void) {
    const char *reg_names[] = {
        "R0 ", "R1 ", "R2 ", "R3 ", "R4 ", "R5 ", "R6 ", "R7 ",
        "R8 ", "R9 ", "R10", "R11", "R12", "SP ", "LR ", "PC ", "PSR"
    };
    
    UART_Print("\r\n=== ESTADO DE REGISTROS (CPU) ===\r\n");
    // Imprimimos desde el registro 0 (R0) hasta el registro 16 (PSR) 
    for (int i = 0; i < 17; i++) { 
        UART_Print(reg_names[i]);
        UART_Print(": ");
        UART_PrintHex32(cpu_regs[i]);
        
        // Imprimir en dos columnas para lograr una mayor legibilidad
        if (i % 2 == 0) {
            UART_Print("\t");
        } else {
            UART_Print("\r\n");
        }
    }
    UART_Print("\r\n\r\n");
}



// Evaluador principal de comandos
void Monitor_Parse(void) {
    // Comparamos los primeros caracteres del buffer
    // Comando "RD" para mostrar los registros
    if (strncmp(cmd_buffer, "RD", 2) == 0) {
        UART_Print("Ejecutando: Register Display...\r\n");
        Cmd_RD();
    } 

    // Comando "RM" para modificar un registro
    else if (strncmp(cmd_buffer, "RM", 2) == 0) {
        // Validar que después de RM haya un espacio
        if (cmd_buffer[2] != ' ') {
            UART_Print("Error de sintaxis. Uso correcto: RM R{X} data\r\n");
        } 
        else {
            char *ptr = cmd_buffer + 3;
            uint32_t reg_idx = 0xFF;
            
            while (*ptr == ' ') ptr++; // Ignorar espacios extra
            
            // Detectar el registro
            if (*ptr == 'R' || *ptr == 'r') { // Detectar el registro (Definimos casos para R0-R12, SP, LR, PC y PSR con y sin mayúsculas)
                ptr++;
                reg_idx = 0;
                while (*ptr >= '0' && *ptr <= '9') {
                    reg_idx = reg_idx * 10 + (*ptr - '0'); // Convertimos el dígito a número, 01, 02, 03, etc.
                    ptr++;
                }
            } 
            else if (strncmp(ptr, "SP", 2) == 0 || strncmp(ptr, "sp", 2) == 0) { reg_idx = 13; ptr += 2; }
            else if (strncmp(ptr, "LR", 2) == 0 || strncmp(ptr, "lr", 2) == 0) { reg_idx = 14; ptr += 2; }
            else if (strncmp(ptr, "PC", 2) == 0 || strncmp(ptr, "pc", 2) == 0) { reg_idx = 15; ptr += 2; }
            else if (strncmp(ptr, "PSR", 3) == 0 || strncmp(ptr, "psr", 3) == 0) { reg_idx = 16; ptr += 3; }
            
            while (*ptr == ' ') ptr++; // Ignorar espacios antes del dato
            
            // Verificamos que el registro sea válido y que además haya un valor a escribir
            if (reg_idx <= 16 && *ptr != '\0') { 
                uint32_t val = ParseHex(ptr);
                Cmd_RM(reg_idx, val); // Llamada a Ensamblador
                UART_Print("Registro modificado exitosamente.\r\n");
                Cmd_RD();
            } else {
                UART_Print("Error de sintaxis. Uso correcto: RM R{X} data\r\n");
            }
        }
    }
    
    // Comando "MD" para desplegar memoria
    else if (strncmp(cmd_buffer, "MD", 2) == 0) {
        char *ptr = cmd_buffer + 2;
        while (*ptr == ' ') ptr++; // Saltamos los espacios después de MD
        Cmd_MD(ptr);
    }

    // Comando "MM" para modificar memoria
    else if (strncmp(cmd_buffer, "MM", 2) == 0) {
        char *ptr = cmd_buffer + 2;
        while (*ptr == ' ') ptr++;
        Cmd_MM(ptr);
    }

    // Comando "BF" para llenar bloques de memoria
    else if (strncmp(cmd_buffer, "BF", 2) == 0) {
        // Validar que haya un espacio después de BF (Entiéndase, argumentos)
        if (cmd_buffer[2] != ' ') {
            UART_Print("Error de sintaxis. Uso correcto: BF start end data [size]\r\n");
        } else {
            char *ptr = cmd_buffer + 3;
            while (*ptr == ' ') ptr++;
            Cmd_BF(ptr);
        }
    }

    // Comando "RUN" para ejecutar código en una dirección absoluta
    else if (strncmp(cmd_buffer, "RUN", 3) == 0) {
        if (cmd_buffer[3] != ' ') {
            UART_Print("Error de sintaxis. Uso correcto: RUN addr\r\n");
        } 
        else {
            char *ptr = cmd_buffer + 4; 
            while (*ptr == ' ') ptr++; // Movemos el puntero hasta encontrar "addr"
            
            if (*ptr != '\0') {
                uint32_t addr = ParseHex(ptr);
                
                // SUPER IMPORTANTE: Debido a la cuestión de Thumb que se viene arrastrando desde entonces, nos aseguramos de que el LSB sea 1
                // De lo contrario, Hard fault xd
                addr = addr | 1; 
                
                UART_Print("Transfiriendo control absoluto del CPU...\r\n\r\n");
                Cmd_RUN(addr);
            } else {
                UART_Print("Error de sintaxis. Uso correcto: RUN addr\r\n");
            }
        }
    }

    // Comando "CALL" para ejecutar una subrutina y regresar
    else if (strncmp(cmd_buffer, "CALL", 4) == 0) {
        if (cmd_buffer[4] != ' ') {
            UART_Print("Error de sintaxis. Uso correcto: CALL addr\r\n");
        } 
        else {
            char *ptr = cmd_buffer + 5;
            while (*ptr == ' ') ptr++; 
            
            if (*ptr != '\0') {
                uint32_t addr = ParseHex(ptr);
                
                // SUPER IMPORTANTE: Debido a la cuestión de Thumb que se viene arrastrando desde entonces, nos aseguramos de que el LSB sea 1
                addr = addr | 1; 
                
                UART_Print("Ejecutando subrutina externa...\r\n");
                Cmd_CALL(addr);
                // Si la función regresa correctamente, se ejecutará esta línea:
                UART_Print("Retorno exitoso al programa monitor.\r\n\r\n");
            } else {
                UART_Print("Error de sintaxis. Uso correcto: CALL addr\r\n\r\n");
            }
        }
    }

    // Si se presiona Enter sin escribir nada, lo ignoramos
    else if (buffer_index > 0) {
        UART_Print("Error: Comando no reconocido.\r\n");
    }
}

// Bucle principal de la terminal
void Monitor_Run(void) {
    char c;
    UART_Print("\r\n--- STM32G4 Monitor Iniciado ---\r\n");
    UART_Print("Wisho> "); // Simulación para el prompt de la terminal

    while(1) {
        c = UART_Rx();
        
        if (c == '\r') {
            // Enter detectado: finalizar cadena, saltar línea y evaluar
            UART_Print("\r\n");
            cmd_buffer[buffer_index] = '\0'; 
            Monitor_Parse();
            
            // Limpiar variables y mostrar prompt de nuevo
            buffer_index = 0; 
            UART_Print("Wisho> ");
        } 
        else if (c == '\b' || c == 127) { //127 porque algunas terminales envían DELETE en lugar de Backspace, 127 es su ASCII
            // Backspace o Delete detectado: borrar último carácter de la pantalla y del buffer
            if (buffer_index > 0) {
                buffer_index--;
                UART_Print("\b \b"); 
            }
        } 
        else {
            // Guardar el carácter escrito si aún hay espacio en el buffer
            if (buffer_index < BUFFER_SIZE - 1) {
                cmd_buffer[buffer_index++] = c;
                UART_Tx(c); // Eco del carácter para que el usuario vea lo que escribe
            }
        }
    }
}