#include "memory.h"
#include "monitor.h"
#include "connect.h"
#include <string.h>

#define SRAM1_BASE 0x20000000
#define DEFAULT_RANGE 0x40 // 64 bytes (4 líneas de 16 bytes), se utilizará cuando no se especifique un rango.

// Función auxiliar para imprimir 1 byte en Hex (2 caracteres)
void UART_PrintHex8(uint8_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    UART_Tx(hex_chars[(val >> 4) & 0xF]);
    UART_Tx(hex_chars[val & 0xF]);
}

// Rutina para desplegar la memoria (MD)
void Print_Memory(uint32_t start, uint32_t end) {
    uint8_t *ptr = (uint8_t *)start;
    uint32_t count = end - start;

    UART_Print("\r\n");
    for (uint32_t i = 0; i < count; i += 16) {
        UART_PrintHex32((uint32_t)(ptr + i));
        UART_Print(" : ");
        
        // Imprimir hasta 16 bytes por línea en Little Endian
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < count) {
                UART_PrintHex8(*(ptr + i + j));
                UART_Print(" ");
            } else {
                UART_Print("   "); // Espacios para alinear la línea si está incompleta
            }
            
            // Separador colocado arbitrariamente a la mitad para facilitar legibilidad
            if (j == 7) UART_Print("- "); 
        }
        
        UART_Print("  |");
        
        // Imprimir representación en caracteres ASCII
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < count) {
                char c = (char)*(ptr + i + j);
                // Mostrar el cárácter solo si es imprimible (ASCII 32 al 126)
                if (c >= 32 && c <= 126) {
                    UART_Tx(c);
                } else {
                    UART_Tx('.'); // Imprimir un punto para los que no son imprimibles
                }
            }

        }
        UART_Print("|\r\n");
    }
    UART_Print("\r\n");
}

// Lógica principal para el comando MD
void Cmd_MD(char *args) {
    // Si se enviaron argumentos de forma correcta (ej. "MD 0x20000000 0x20000040")
    if (*args != '\0' && *args != ' ') {
        uint32_t start = ParseHex(args);
        
        // Buscar el segundo argumento (end)
        char *ptr_end = strchr(args, ' '); 
        uint32_t end;
        if (ptr_end != NULL) {
            while (*ptr_end == ' ') ptr_end++;
            end = ParseHex(ptr_end);
        } else {
            end = start + DEFAULT_RANGE; // Si no hay fin, sumamos el rango por defecto
        }
        
        Print_Memory(start, end);
        return;
    }

    // Si solamente se envió "MD", entramos al sub-menú interactivo
    char sub_buffer[32];
    uint8_t sub_idx = 0;
    char c;

    UART_Print("\r\nOpciones MD:\r\n");
    UART_Print("  def                  (Rango por defecto)\r\n");
    UART_Print("  def [rango] [inicio] (Rango especificado)\r\n");
    UART_Print("  back                 (Cancelar)\r\n\r\n");
    UART_Print("MD> ");

    // Bucle while para leer la opción del sub-menú y retornar al while original con returns
    while (1) {
        c = UART_Rx();
        if (c == '\r') { // Cuando detectamos enter, procesamos la opción
            UART_Print("\r\n");
            sub_buffer[sub_idx] = '\0';
            
            // Evaluar la opción ingresada
            if (strncmp(sub_buffer, "back", 4) == 0) { // Salir del submenú y volver al prompt principal
                UART_Print("Operacion cancelada.\r\n");
                return;
            } 
            else if (strncmp(sub_buffer, "def", 3) == 0) { // Verificamos si el def viene acompañado con argumentos
                if (sub_buffer[3] == '\0') {
                    // Si solo viene "def" mostraremos el rango por defecto 0x2000 0000 a 0x2000 0040
                    Print_Memory(SRAM1_BASE, SRAM1_BASE + DEFAULT_RANGE);
                } else {
                    // Si viene la instrucción completa "def [rango] [inicio] "convertir" los valores a Hex
                    char *ptr = sub_buffer + 4;
                    uint32_t range = ParseHex(ptr);
                    
                    char *ptr_inicio = strchr(ptr, ' ');
                    uint32_t inicio = SRAM1_BASE; // Si no se especifica el "inicio", comenzará en 0x2000 0000 y llegará hasta el rango específicado
                    if (ptr_inicio != NULL) {
                        while (*ptr_inicio == ' ') ptr_inicio++;
                        inicio = ParseHex(ptr_inicio);
                    }
                    Print_Memory(inicio, inicio + range);
                }
                return;
            } 
            else {
                UART_Print("Opción no valida.\r\n");
                return;
            }
        }
        else if (c == '\b' || c == 127) { // Si detectamos un backspace, borramos el último caracter del buffer y de la pantalla
            if (sub_idx > 0) {
                sub_idx--;
                UART_Print("\b \b");
            }
        }
        else {
            if (sub_idx < 31) { // Si el mensaje es muy largo el buffer se desbordaría, por lo que lo limitamos a 31 carácteres + 1 para el '\0'
                sub_buffer[sub_idx++] = c;
                UART_Tx(c);
            }
        }
    }
}

// Lógica principal para el comando MM
void Cmd_MM(char *args) {
    // Si no hay argumentos o solo hay espacios, mostramos error
    if (*args == '\0' || *args == ' ') {
        UART_Print("Error de sintaxis. Uso correcto: MM addr data [size]\r\n");
        return;
    }

    // Extraemos la dirección
    uint32_t addr = ParseHex(args);
    
    // Extraemos el valor que queremos escribir en memoria
    char *ptr = strchr(args, ' ');
    if (ptr == NULL) {
        UART_Print("Error de sintaxis. Uso correcto: MM addr data [size]\r\n");
        return;
    }
    while (*ptr == ' ') ptr++;
    uint32_t data = ParseHex(ptr);

    // Extraemos el tamaño en el que queremos escribir (Opcional, default a 1)
    // Es importante verificar el tamaño para evitar comportamiento no determinado
    uint8_t size = 1;
    char *ptr_size = strchr(ptr, ' ');
    if (ptr_size != NULL) {
        while (*ptr_size == ' ') ptr_size++;
        if (*ptr_size != '\0') {
            size = (uint8_t)ParseHex(ptr_size);
        }
    }

    // Validamos el size y realizar la escritura con el cast (Tratar el valor addr como puntero) de puntero correspondiente
    // Importante utilizar volatile para obligar al compilador a acceder a la memoria en cada ocasión
    switch (size) {
        case 1:
            *((volatile uint8_t *)addr) = (uint8_t)data; // Utilizamos desreferencia para modificar el valor guardado en la dirección
            break;
        case 2:
            // Asegurar alineación a media palabra si fuera estrictamente necesario
            *((volatile uint16_t *)addr) = (uint16_t)data;
            break;
        case 4:
            // Asegurar alineación a palabra si fuera estrictamente necesario
            *((volatile uint32_t *)addr) = (uint32_t)data;
            break;
        default:
            UART_Print("Error: Tamaños de acceso permitidos: 1, 2 o 4 bytes.\r\n");
            return;
    }

    UART_Print("Memoria modificada exitosamente.\r\n");
    // Confirmamos visualmente que se haya escrito correctamente (Desplegando el área modificada)
    Print_Memory(addr, addr + size);
}

// Lógica principal para el comando BF
void Cmd_BF(char *args) {
    // Si no hay argumentos o solo hay espacios, mostrar el uso correcto
    if (*args == '\0' || *args == ' ') {
        UART_Print("Error de sintaxis. Uso correcto: BF start end data [size]\r\n");
        return;
    }

    // Extraemos el start
    uint32_t start = ParseHex(args);
    
    // Extraemos el end
    char *ptr = strchr(args, ' ');
    if (ptr == NULL) {
        UART_Print("Error de sintaxis. Uso correcto: BF start end data [size]\r\n");
        return;
    }
    while (*ptr == ' ') ptr++; // Avanzamos el puntero cuando hay blankspaces
    uint32_t end = ParseHex(ptr);

    // Extraemos el data a escribir en memoria de forma repetitiva
    ptr = strchr(ptr, ' ');
    if (ptr == NULL) {
        UART_Print("Error de sintaxis. Uso correcto: BF start end data [size]\r\n");
        return;
    }
    while (*ptr == ' ') ptr++;
    uint32_t data = ParseHex(ptr);

    // Extraemos el size (Único opcional para esta instrucción, por defecto es 1)
    uint8_t size = 1; 
    char *ptr_size = strchr(ptr, ' ');
    if (ptr_size != NULL) {
        while (*ptr_size == ' ') ptr_size++;
        if (*ptr_size != '\0') {
            size = (uint8_t)ParseHex(ptr_size);
        }
    }

    // Verificamos que las direcciones de memoria tengan sentido (Escribir de menor a mayor)
    if (start > end) {
        UART_Print("Error: La direccion start debe ser menor o igual a end.\r\n");
        return;
    }

    // Lógica de llenado dependiente del size usando aritmética de punteros
    switch (size) {
        case 1: {
            uint8_t *ptr8 = (uint8_t *)start; // Primero apuntamos el puntero hacia la dirección inicial
            while ((uint32_t)ptr8 <= end) { // Hasta que el puntero encuentre la dirección final
                *ptr8 = (uint8_t)data; // Desreferenciamos para meter el valor a la dirección apuntada actualmente
                ptr8++;
            }
            break;
        }
        case 2: {
            uint16_t *ptr16 = (uint16_t *)start; // Lo mismo que arriba pero para 2 bytes
            while ((uint32_t)ptr16 <= end) {
                *ptr16 = (uint16_t)data;
                ptr16++;
            }
            break;
        }
        case 4: {
            uint32_t *ptr32 = (uint32_t *)start; // Lo mismo que arriba pero para 4 bytes
            while ((uint32_t)ptr32 <= end) {
                *ptr32 = (uint32_t)data;
                ptr32++;
            }
            break;
        }
        default:
            UART_Print("Error: Tamanos de acceso permitidos: 1, 2 o 4 bytes.\r\n");
            return;
    }

    UART_Print("Bloque llenado exitosamente.\r\n");
    // Confirmación visual desplegando el área llenada más el último tamaño escrito
    Print_Memory(start, end + size);
}