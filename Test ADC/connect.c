#include <stdint.h>
#include "connect.h"

// Comunicación serial del monitor: USART2 en PA2 (TX) y PA3 (RX) hacia el VCP del ST-LINK
// (UM2397 6.6.4, Tabla 8). 115200 baudios, 8 bits de datos, sin paridad y 1 bit de parada.
//
// Cambios respecto a la versión de la pre-entrega: se hicieron para incluir el nuevo convenio
// de no asumir nada sobre el estado de los registros (no confiar en sus valores de reset).
// La versión anterior funcionaba porque, después de un reset, el USART2 está deshabilitado,
// en formato 8N1 y con un reloj de 16 MHz; ahora todo eso se escribe de forma explícita:
//  - Se reinicia el USART2 desde el RCC para dejar todos sus registros limpios.
//  - Se fija el reloj del USART2 en HSI16 (16 MHz) para que BRR = 139 siga siendo válido
//    aunque después cambie el reloj del sistema.
//  - Se limpian todos los registros de configuración de PA2 y PA3, no solo MODER y AFRL.
//  - Se escribe el formato de la trama y se respeta el orden de habilitación del manual.
// UART_Init() se llama después de Setup_Seguridad() (ver main.c).

// Macro para un solo bit (ej. activar relojes o flags)
// Usamos 1U (sin signo) para poder desplazar hasta el bit 31 sin desbordar un int con signo
#define MASK(x) (1U << (x))

// Macro para insertar un valor de varios bits en una posición
#define MASK_VAL(pos, val) ((uint32_t)(val) << (pos))

// Direcciones base (RM0440 2.2.2)
#define RCC_BASE      0x40021000
#define GPIOA_BASE    0x48000000
#define USART2_BASE   0x40004400

// Registros de Reloj
#define RCC_CR          (*((volatile uint32_t *)(RCC_BASE + 0x00)))   // RM0440 7.4.1
#define RCC_APB1RSTR1   (*((volatile uint32_t *)(RCC_BASE + 0x38)))   // RM0440 7.4.11
#define RCC_AHB2ENR     (*((volatile uint32_t *)(RCC_BASE + 0x4C)))   // RM0440 7.4.15
#define RCC_APB1ENR1    (*((volatile uint32_t *)(RCC_BASE + 0x58)))   // RM0440 7.4.17
#define RCC_CCIPR       (*((volatile uint32_t *)(RCC_BASE + 0x88)))   // RM0440 7.4.26

// Registros de GPIOA (RM0440 9.4)
#define GPIOA_MODER     (*((volatile uint32_t *)(GPIOA_BASE + 0x00)))  // 9.4.1
#define GPIOA_OTYPER    (*((volatile uint32_t *)(GPIOA_BASE + 0x04)))  // 9.4.2
#define GPIOA_OSPEEDR   (*((volatile uint32_t *)(GPIOA_BASE + 0x08)))  // 9.4.3
#define GPIOA_PUPDR     (*((volatile uint32_t *)(GPIOA_BASE + 0x0C)))  // 9.4.4
#define GPIOA_AFRL      (*((volatile uint32_t *)(GPIOA_BASE + 0x20)))  // 9.4.9

// Registros de USART2 (RM0440 40.8)
#define USART2_CR1      (*((volatile uint32_t *)(USART2_BASE + 0x00)))  // 40.8.1
#define USART2_CR2      (*((volatile uint32_t *)(USART2_BASE + 0x04)))  // 40.8.3
#define USART2_BRR      (*((volatile uint32_t *)(USART2_BASE + 0x0C)))  // 40.8.5
#define USART2_ISR      (*((volatile uint32_t *)(USART2_BASE + 0x1C)))  // 40.8.9
#define USART2_RDR      (*((volatile uint32_t *)(USART2_BASE + 0x24)))  // 40.8.12
#define USART2_TDR      (*((volatile uint32_t *)(USART2_BASE + 0x28)))  // 40.8.13
#define USART2_PRESC    (*((volatile uint32_t *)(USART2_BASE + 0x2C)))  // 40.8.14

// Bits de RCC que utilizamos
#define RCC_GPIOAEN       0    // Reloj de GPIOA en RCC_AHB2ENR
#define RCC_USART2EN      17   // Reloj de USART2 en RCC_APB1ENR1
#define RCC_USART2RST     17   // Reset de USART2 en RCC_APB1RSTR1
#define RCC_HSION         8    // Encendido de HSI16 en RCC_CR
#define RCC_HSIRDY        10   // Bandera de HSI16 listo en RCC_CR
#define RCC_USART2SEL     2    // Selección del reloj de USART2 en RCC_CCIPR (bits 3:2)
#define USART2SEL_HSI16   2    // Valor 10 (binario): HSI16 como reloj del USART2

// Bits de USART_CR1 que utilizamos (RM0440 40.8.1)
#define USART_UE          0    // Habilitación del USART
#define USART_RE          2    // Habilitación del receptor
#define USART_TE          3    // Habilitación del transmisor
#define USART_PCE         10   // Habilitación de la paridad
#define USART_M0          12   // Largo de la palabra (bit 0)
#define USART_OVER8       15   // Modo de sobremuestreo
#define USART_M1          28   // Largo de la palabra (bit 1)
#define USART_FIFOEN      29   // Habilitación de la FIFO

// Campo de bits de parada STOP en USART_CR2, bits 13:12 (RM0440 40.8.3)
#define USART_STOP        12

// Banderas de USART_ISR con la FIFO deshabilitada (RM0440 40.8.10)
#define USART_RXNE        5    // Dato recibido listo para leer
#define USART_TXE         7    // Registro de transmisión vacío

// Baudrate a 115200: 16 MHz / 115200 = 138.9, redondeado a 139 (RM0440 40.5.7)
#define UART_BRR_115200   139

void UART_Init(void) {
    // Habilitamos el reloj de GPIOA (Bit 0) y USART2 (Bit 17)
    RCC_AHB2ENR &= ~(MASK(RCC_GPIOAEN));
    RCC_AHB2ENR |= MASK(RCC_GPIOAEN);

    RCC_APB1ENR1 &= ~(MASK(RCC_USART2EN));
    RCC_APB1ENR1 |= MASK(RCC_USART2EN);

    // Reiniciamos el USART2 desde el RCC (Bit 17 de RCC_APB1RSTR1, RM0440 7.4.11)
    // Esto deja todos sus registros (CR1, CR2, CR3, BRR, PRESC) en su valor de reset y el
    // USART deshabilitado, sin importar lo que hubiera antes
    RCC_APB1RSTR1 |= MASK(RCC_USART2RST);      // Activamos el reset
    RCC_APB1RSTR1 &= ~(MASK(RCC_USART2RST));   // Lo liberamos

    // Seleccionamos HSI16 (16 MHz) como reloj del USART2

    // Primero nos aseguramos de que HSI16 esté encendido y estable (RM0440 7.4.1)
    // Aquí solo seteamos el bit, no lo limpiamos, porque apagar HSI16 detendría el reloj del sistema
    RCC_CR |= MASK(RCC_HSION);
    while (!(RCC_CR & MASK(RCC_HSIRDY)));       // Esperamos a que HSI16 esté listo

    // Luego lo elegimos en USART2SEL, bits 3:2 de RCC_CCIPR (RM0440 7.4.26)
    // Primero limpiamos usando el valor 3 (11 binario) para abarcar los 2 bits
    RCC_CCIPR &= ~(MASK_VAL(RCC_USART2SEL, 3));
    // Luego escribimos el valor 2 (10 binario = HSI16)
    RCC_CCIPR |= MASK_VAL(RCC_USART2SEL, USART2SEL_HSI16);

    // Configuramos PA2 y PA3 como Función Alterna

    // El pin PA2 inicia en bit 4 y el pin PA3 inicia en bit 6 (campos de 2 bits)
    // Primero ponemos MODER en 00 (entrada) para que los pines dejen de manejar la línea mientras se configuran
    GPIOA_MODER   &= ~(MASK_VAL(4, 3) | MASK_VAL(6, 3));
    
    // Limpiamos el resto de su configuración
    GPIOA_OTYPER  &= ~(MASK(2) | MASK(3));                    // Salida push-pull
    GPIOA_OSPEEDR &= ~(MASK_VAL(4, 3) | MASK_VAL(6, 3));      // Velocidad baja, suficiente para 115200
    GPIOA_PUPDR   &= ~(MASK_VAL(4, 3) | MASK_VAL(6, 3));      // Sin pull-up ni pull-down

    // Mapeamos PA2 y PA3 a AF7 (USART2) (0111) (DS12589 4.11, Tabla 13)

    // El AF de PA2 inicia en bit 8 y el AF de PA3 inicia en bit 12 (AF es alternative function)
    // Primero limpiamos usando el valor 15 (F) para abarcar los 4 bits
    GPIOA_AFRL &= ~(MASK_VAL(8, 15) | MASK_VAL(12, 15));
    // Luego escribimos el valor 7 (0111 binario)
    GPIOA_AFRL |= (MASK_VAL(8, 7) | MASK_VAL(12, 7));

    // Al final escribimos el valor 2 (10 binario) en MODER para configurar la función alterna
    GPIOA_MODER |= (MASK_VAL(4, 2) | MASK_VAL(6, 2));

    // Configuramos el USART2 siguiendo el orden del manual (RM0440 40.5.5)

    // Nos aseguramos de que el USART esté deshabilitado (UE = 0), porque BRR y PRESC
    // solo se pueden escribir así (RM0440 40.8.5 y 40.8.14)
    USART2_CR1 &= ~(MASK(USART_UE));

    // Formato de 8 bits sin paridad, sobremuestreo x16 y sin FIFO: limpiamos M1, M0, PCE, OVER8 y FIFOEN
    // También limpiamos TE y RE para habilitarlos hasta el final
    USART2_CR1 &= ~(MASK(USART_M1) | MASK(USART_M0) | MASK(USART_PCE) |
                    MASK(USART_OVER8) | MASK(USART_FIFOEN) | MASK(USART_TE) | MASK(USART_RE));

    // Prescaler del reloj del USART en /1 (PRESCALER = 0000)
    USART2_PRESC &= ~(MASK_VAL(0, 15));

    // Configuramos Baudrate a 115200 (16MHz / 115200 = 139)
    USART2_BRR &= ~(MASK_VAL(0, 0xFFFF));
    USART2_BRR |= UART_BRR_115200;

    // 1 bit de parada (STOP = 00)
    USART2_CR2 &= ~(MASK_VAL(USART_STOP, 3));

    // Habilitamos primero el USART (Bit 0) y después TX y RX (Bits 3 y 2)
    USART2_CR1 |= MASK(USART_UE);
    USART2_CR1 |= (MASK(USART_TE) | MASK(USART_RE));
}

void UART_Tx(char c) {
    // Esperar a que el registro de transmisión esté vacío (TXE, Bit 7)
    while (!(USART2_ISR & MASK(USART_TXE)));
    USART2_TDR = c;
}

char UART_Rx(void) {
    // Esperar hasta que el registro de recepción indique un nuevo dato (RXNE, Bit 5)
    while (!(USART2_ISR & MASK(USART_RXNE)));
    return (char)(USART2_RDR & 0xFF);
}
