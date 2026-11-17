#include <stdint.h>
#include "ftdi_con.h"

// Comunicación serial del monitor por el cable FTDI: USART1 en PA9 (TX) y PA10 (RX).
// PA9 = D1 (CN4-1) y PA10 = D0 (CN4-2) (UM2397 7.2, Tabla 11), ambos en AF7 (DS12589 4.11, Tabla 13).
// 115200 baudios, 8 bits de datos, sin paridad y 1 bit de parada, igual que la versión con USART2.
//
// Este archivo reemplaza a connect.c (USART2 hacia el VCP del ST-LINK), que se conserva en el
// proyecto solo como registro. Pues es obligatoria la implementación de la interfaz FTDI
//
// Se sigue el mismo convenio de no asumir nada sobre el estado de los registros:
//  - Se reinicia el USART1 desde el RCC para dejar todos sus registros limpios.
//  - Se fija el reloj del USART1 en HSI16 (16 MHz) para que BRR = 139 siga siendo válido
//    aunque después cambie el reloj del sistema.
//  - Se limpian todos los registros de configuración de PA9 y PA10 antes de escribirlos.
// FTDI_Init() se llama después de Setup_Seguridad() (ver main.c). Setup_Seguridad() ya
// deshabilitó los pull-down "dead battery" que un nivel alto en PA9/PA10 activaría en PB6/PB4
// (DS12589 4.10, Tabla 12, nota 6).
//
// Hardware (esquemático Green_Card, conector J3 para el cable SparkFun DEV-09718):
//  - El cable maneja niveles de 5 V, por eso su TXD llega a PA10 por un divisor: R28 (1k) en serie
//    y R29 (4.7k) en paralelo con R30 (10k) hacia GND. Con el TXD en su máximo (4.9 V) PA10 recibe
//    unos 3.7 V, menos que el máximo absoluto de 4.0 V con la Nucleo apagada (DS12589 Tabla 14).
//  - Como PA10 puede quedar arriba de VDD + 0.3 V, sus resistencias internas de pull-up y pull-down
//    deben quedar deshabilitadas (DS12589 5.3.1, Tabla 17, nota 3): PUPDR = 00.
//  - PA9 sale por R31 (1k) en serie hacia el RXD del cable.
//  - Si el cable se desconecta, el divisor deja PA10 en 0 y el USART lo recibe como un "break"
//    (error de trama). FTDI_Rx() descarta esos datos.

// Macro para un solo bit (ej. activar relojes o flags)
// Usamos 1U (sin signo) para poder desplazar hasta el bit 31 sin desbordar un int con signo
#define MASK(x) (1U << (x))

// Macro para insertar un valor de varios bits en una posición
#define MASK_VAL(pos, val) ((uint32_t)(val) << (pos))

// Direcciones base (RM0440 2.2.2)
#define RCC_BASE      0x40021000
#define GPIOA_BASE    0x48000000
#define USART1_BASE   0x40013800

// Registros de Reloj
#define RCC_CR          (*((volatile uint32_t *)(RCC_BASE + 0x00)))   // RM0440 7.4.1
#define RCC_APB2RSTR    (*((volatile uint32_t *)(RCC_BASE + 0x40)))   // RM0440 7.4.13
#define RCC_AHB2ENR     (*((volatile uint32_t *)(RCC_BASE + 0x4C)))   // RM0440 7.4.15
#define RCC_APB2ENR     (*((volatile uint32_t *)(RCC_BASE + 0x60)))   // RM0440 7.4.19
#define RCC_CCIPR       (*((volatile uint32_t *)(RCC_BASE + 0x88)))   // RM0440 7.4.26

// Registros de GPIOA (RM0440 9.4)
#define GPIOA_MODER     (*((volatile uint32_t *)(GPIOA_BASE + 0x00)))  // 9.4.1
#define GPIOA_OTYPER    (*((volatile uint32_t *)(GPIOA_BASE + 0x04)))  // 9.4.2
#define GPIOA_OSPEEDR   (*((volatile uint32_t *)(GPIOA_BASE + 0x08)))  // 9.4.3
#define GPIOA_PUPDR     (*((volatile uint32_t *)(GPIOA_BASE + 0x0C)))  // 9.4.4
#define GPIOA_AFRH      (*((volatile uint32_t *)(GPIOA_BASE + 0x24)))  // 9.4.10

// Registros de USART1 (RM0440 40.8)
#define USART1_CR1      (*((volatile uint32_t *)(USART1_BASE + 0x00)))  // 40.8.1
#define USART1_CR2      (*((volatile uint32_t *)(USART1_BASE + 0x04)))  // 40.8.3
#define USART1_BRR      (*((volatile uint32_t *)(USART1_BASE + 0x0C)))  // 40.8.5
#define USART1_ISR      (*((volatile uint32_t *)(USART1_BASE + 0x1C)))  // 40.8.9
#define USART1_ICR      (*((volatile uint32_t *)(USART1_BASE + 0x20)))  // 40.8.11
#define USART1_RDR      (*((volatile uint32_t *)(USART1_BASE + 0x24)))  // 40.8.12
#define USART1_TDR      (*((volatile uint32_t *)(USART1_BASE + 0x28)))  // 40.8.13
#define USART1_PRESC    (*((volatile uint32_t *)(USART1_BASE + 0x2C)))  // 40.8.14

// Bits de RCC que utilizamos
#define RCC_GPIOAEN       0    // Reloj de GPIOA en RCC_AHB2ENR
#define RCC_USART1EN      14   // Reloj de USART1 en RCC_APB2ENR
#define RCC_USART1RST     14   // Reset de USART1 en RCC_APB2RSTR
#define RCC_HSION         8    // Encendido de HSI16 en RCC_CR
#define RCC_HSIRDY        10   // Bandera de HSI16 listo en RCC_CR
#define RCC_USART1SEL     0    // Selección del reloj de USART1 en RCC_CCIPR (bits 1:0)
#define USART1SEL_HSI16   2    // Valor 10 (binario): HSI16 como reloj del USART1

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
#define USART_FE          1    // Error de trama o "break"
#define USART_NE          2    // Ruido detectado
#define USART_ORE         3    // Sobrecarga (llegó un dato sin haber leído el anterior)
#define USART_RXNE        5    // Dato recibido listo para leer
#define USART_TXE         7    // Registro de transmisión vacío

// Bits de USART_ICR para limpiar las banderas de error (RM0440 40.8.11)
#define USART_FECF        1
#define USART_NECF        2
#define USART_ORECF       3

// Baudrate a 115200: 16 MHz / 115200 = 138.9, redondeado a 139 (RM0440 40.5.7)
#define UART_BRR_115200   139

void FTDI_Init(void) {
    // Habilitamos el reloj de GPIOA (Bit 0 de RCC_AHB2ENR) y USART1 (Bit 14 de RCC_APB2ENR)
    RCC_AHB2ENR &= ~(MASK(RCC_GPIOAEN));
    RCC_AHB2ENR |= MASK(RCC_GPIOAEN);

    RCC_APB2ENR &= ~(MASK(RCC_USART1EN));
    RCC_APB2ENR |= MASK(RCC_USART1EN);

    // Reiniciamos el USART1 desde el RCC (Bit 14 de RCC_APB2RSTR, RM0440 7.4.13)
    // Esto deja todos sus registros (CR1, CR2, CR3, BRR, PRESC) en su valor de reset y el
    // USART deshabilitado, sin importar lo que hubiera antes
    RCC_APB2RSTR |= MASK(RCC_USART1RST);       // Activamos el reset
    RCC_APB2RSTR &= ~(MASK(RCC_USART1RST));    // Lo liberamos

    // Seleccionamos HSI16 (16 MHz) como reloj del USART1

    // Primero nos aseguramos de que HSI16 esté encendido y estable (RM0440 7.4.1)
    // Aquí solo seteamos el bit, no lo limpiamos, porque apagar HSI16 detendría el reloj del sistema
    RCC_CR |= MASK(RCC_HSION);
    while (!(RCC_CR & MASK(RCC_HSIRDY)));       // Esperamos a que HSI16 esté listo

    // Luego lo elegimos en USART1SEL, bits 1:0 de RCC_CCIPR (RM0440 7.4.26)
    // Primero limpiamos usando el valor 3 (11 binario) para abarcar los 2 bits
    RCC_CCIPR &= ~(MASK_VAL(RCC_USART1SEL, 3));
    // Luego escribimos el valor 2 (10 binario = HSI16)
    RCC_CCIPR |= MASK_VAL(RCC_USART1SEL, USART1SEL_HSI16);

    // Configuramos PA9 y PA10 como Función Alterna

    // El pin PA9 inicia en bit 18 y el pin PA10 inicia en bit 20 (campos de 2 bits)
    // Primero ponemos MODER en 00 (entrada) para que los pines dejen de manejar la línea mientras se configuran
    GPIOA_MODER   &= ~(MASK_VAL(18, 3) | MASK_VAL(20, 3));

    // Limpiamos el resto de su configuración
    GPIOA_OTYPER  &= ~(MASK(9) | MASK(10));                   // Salida push-pull
    GPIOA_OSPEEDR &= ~(MASK_VAL(18, 3) | MASK_VAL(20, 3));    // Velocidad baja, suficiente para 115200
    GPIOA_PUPDR   &= ~(MASK_VAL(18, 3) | MASK_VAL(20, 3));    // Sin pull-up ni pull-down (obligatorio en PA10, ver arriba)

    // Mapeamos PA9 y PA10 a AF7 (USART1) (0111) (DS12589 4.11, Tabla 13)

    // PA9 y PA10 están en AFRH: el AF de PA9 inicia en bit 4 y el AF de PA10 inicia en bit 8
    // Primero limpiamos usando el valor 15 (F) para abarcar los 4 bits
    GPIOA_AFRH &= ~(MASK_VAL(4, 15) | MASK_VAL(8, 15));
    // Luego escribimos el valor 7 (0111 binario)
    GPIOA_AFRH |= (MASK_VAL(4, 7) | MASK_VAL(8, 7));

    // Al final escribimos el valor 2 (10 binario) en MODER para configurar la función alterna
    GPIOA_MODER |= (MASK_VAL(18, 2) | MASK_VAL(20, 2));

    // Configuramos el USART1 siguiendo el orden del manual (RM0440 40.5.5)

    // Nos aseguramos de que el USART esté deshabilitado (UE = 0), porque BRR y PRESC
    // solo se pueden escribir así (RM0440 40.8.5 y 40.8.14)
    USART1_CR1 &= ~(MASK(USART_UE));

    // Formato de 8 bits sin paridad, sobremuestreo x16 y sin FIFO: limpiamos M1, M0, PCE, OVER8 y FIFOEN
    // También limpiamos TE y RE para habilitarlos hasta el final
    USART1_CR1 &= ~(MASK(USART_M1) | MASK(USART_M0) | MASK(USART_PCE) |
                    MASK(USART_OVER8) | MASK(USART_FIFOEN) | MASK(USART_TE) | MASK(USART_RE));

    // Prescaler del reloj del USART en /1 (PRESCALER = 0000)
    USART1_PRESC &= ~(MASK_VAL(0, 15));

    // Configuramos Baudrate a 115200 (16MHz / 115200 = 139)
    USART1_BRR &= ~(MASK_VAL(0, 0xFFFF));
    USART1_BRR |= UART_BRR_115200;

    // 1 bit de parada (STOP = 00)
    USART1_CR2 &= ~(MASK_VAL(USART_STOP, 3));

    // Habilitamos primero el USART (Bit 0) y después TX y RX (Bits 3 y 2)
    USART1_CR1 |= MASK(USART_UE);
    USART1_CR1 |= (MASK(USART_TE) | MASK(USART_RE));
}

void FTDI_Tx(char c) {
    // Esperar a que el registro de transmisión esté vacío (TXE, Bit 7)
    while (!(USART1_ISR & MASK(USART_TXE)));
    USART1_TDR = c;
}

char FTDI_Rx(void) {
    uint32_t estado;
    char dato;

    while (1) {
        // Esperar hasta que el registro de recepción indique un nuevo dato (RXNE, Bit 5)
        while (!(USART1_ISR & MASK(USART_RXNE)));

        // Guardamos las banderas antes de leer el dato; leer RDR limpia RXNE (RM0440 40.8.10)
        estado = USART1_ISR;
        dato = (char)(USART1_RDR & 0xFF);

        // Si hubo error de trama (FE), ruido (NE) o sobrecarga (ORE), limpiamos esas banderas.
        // USART_ICR solo se escribe: un 1 limpia la bandera y un 0 no hace nada, por eso aquí se
        // escribe directo en lugar de limpiar y luego setear (RM0440 40.8.11)
        if (estado & (MASK(USART_FE) | MASK(USART_NE) | MASK(USART_ORE))) {
            USART1_ICR = MASK(USART_FECF) | MASK(USART_NECF) | MASK(USART_ORECF);
        }

        // Un dato con error de trama no es válido (por ejemplo, el "break" que aparece si se
        // desconecta el cable), así que lo descartamos y esperamos el siguiente
        if (estado & MASK(USART_FE)) {
            continue;
        }

        return dato;
    }
}
