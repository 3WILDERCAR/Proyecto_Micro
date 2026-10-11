#include <stdint.h>
#include "connect.h"

// Macro para un solo bit (ej. activar relojes o flags)
#define MASK(x) (1 << (x))

// Macro para insertar un valor de varios bits en una posición
#define MASK_VAL(pos, val) ((val) << (pos))

// Direcciones base
#define RCC_BASE      0x40021000
#define GPIOA_BASE    0x48000000
#define USART2_BASE   0x40004400

// Registros de Reloj
#define RCC_AHB2ENR   (*((volatile uint32_t *)(RCC_BASE + 0x4C)))
#define RCC_APB1ENR1  (*((volatile uint32_t *)(RCC_BASE + 0x58)))

// Registros de GPIOA
#define GPIOA_MODER   (*((volatile uint32_t *)(GPIOA_BASE + 0x00)))
#define GPIOA_AFRL    (*((volatile uint32_t *)(GPIOA_BASE + 0x20)))

// Registros de USART2
#define USART2_CR1    (*((volatile uint32_t *)(USART2_BASE + 0x00)))
#define USART2_BRR    (*((volatile uint32_t *)(USART2_BASE + 0x0C)))
#define USART2_ISR    (*((volatile uint32_t *)(USART2_BASE + 0x1C)))
#define USART2_RDR    (*((volatile uint32_t *)(USART2_BASE + 0x24)))
#define USART2_TDR    (*((volatile uint32_t *)(USART2_BASE + 0x28)))

void UART_Init(void) {
    // Habilitamos el reloj de GPIOA (Bit 0) y USART2 (Bit 17)
    RCC_AHB2ENR &= ~(MASK(0));
    RCC_AHB2ENR |= MASK(0);

    RCC_APB1ENR1 &= ~(MASK(17));
    RCC_APB1ENR1 |= MASK(17);

    // Configuramos PA2 y PA3 como Función Alterna

    // El pin PA2 inicia en bit 4 y el pin PA3 inicia en bit 6
    // Primero limpiamos usando el valor 3 (11 binario) para abarcar los 2 bits
    GPIOA_MODER &= ~(MASK_VAL(4, 3) | MASK_VAL(6, 3));
    // Luego escribimos el valor 2 (10 para configurar función alterna)
    GPIOA_MODER |= (MASK_VAL(4, 2) | MASK_VAL(6, 2));

    // Mapeamos PA2 y PA3 a AF7 (USART2) (0111)

    // El AF de PA2 inicia en bit 8 y el AF de PA3 inicia en bit 12 (AF es alternative function)
    // Primero limpiamos usando el valor 15 (F) para abarcar los 4 bits
    GPIOA_AFRL &= ~(MASK_VAL(8, 15) | MASK_VAL(12, 15));
    // Luego escribimos el valor 7 (0111 binario)
    GPIOA_AFRL |= (MASK_VAL(8, 7) | MASK_VAL(12, 7));

    // Configuramos Baudrate a 115200 (16MHz / 115200 = 139)

    USART2_BRR = 139; 

    // Habilitamos USART, TX y RX (Bits 0, 3 y 2)
    USART2_CR1 &= ~(MASK(0) | MASK(2) | MASK(3));
    USART2_CR1 |= (MASK(0) | MASK(2) | MASK(3));
}

void UART_Tx(char c) {
    // Esperar a que el registro de transmisión esté vacío (TXE, Bit 7)
    while (!(USART2_ISR & MASK(7)));
    USART2_TDR = c;
}

char UART_Rx(void) {
    // Esperar hasta que el registro de recepción indique un nuevo dato (RXNE, Bit 5)
    while (!(USART2_ISR & MASK(5)));
    return (char)(USART2_RDR & 0xFF);
}