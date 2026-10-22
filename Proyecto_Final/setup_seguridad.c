#include <stdint.h>
#include "setup_seguridad.h"

// Setup_Seguridad
// Configuraciones mínimas para operar la NUCLEO-G431KB de forma segura y dejar
// libres (en un estado conocido) todos los pines que usaremos en Green_Card y Arcade.
//
// Criterios:
//  - Se asume el PEOR caso de los solder bridges, que coincide con la configuración
//    por defecto de la tarjeta (UM2397 6.7 Tabla 9).
//  - No se confía en los valores de reset de los registros: cada pin se LIMPIA por
//    completo (MODER, OTYPER, OSPEEDR, PUPDR y AFR, etc) y luego se configura.
//
// Alimentación (esquemático Setup_Seguridad): la tarjeta se alimenta por su micro-USB
// CN1 (UM2397 6.4, opción 1); los periféricos por el dominio externo de 9 V.
//
// Pines que esta rutina NO reconfigura:
//   PA2  / PA3  -> USART2 TX / RX hacia el VCP del ST-LINK, los configura UART_Init()
//                  (UM2397 6.6.4 Tabla 8, SB1 y SB12 en ON)
//   PG10-NRST   -> Se mantiene como NRST, no se modifican option bytes (RM0440 9.3.16)
// PA13 / PA14 (SWD) solo se restauran a su configuración de reset y se bloquean.

// Macro para un solo bit (ej. activar relojes o flags)
// Se usa 1U (sin signo) para poder desplazar hasta el bit 31 sin desbordar un int con signo
#define MASK(x) (1U << (x))

// Macro para insertar un valor de varios bits en una posición
// Se castea a uint32_t por la misma razón (campos en los bits 31:30, ej. MODE15)
#define MASK_VAL(pos, val) ((uint32_t)(val) << (pos))

// Posición del campo de un pin: 2 bits en MODER/OSPEEDR/PUPDR y 4 bits en AFRL/AFRH
#define POS2(pin) ((pin) * 2)
#define POS4(pin) ((pin) * 4)

// Direcciones base (RM0440 2.2.2, mapa de memoria)
#define RCC_BASE      0x40021000
#define PWR_BASE      0x40007000
#define GPIOA_BASE    0x48000000
#define GPIOB_BASE    0x48000400

// Registros de Reloj
#define RCC_AHB2ENR   (*((volatile uint32_t *)(RCC_BASE + 0x4C)))   // RM0440 7.4.15
#define RCC_APB1ENR1  (*((volatile uint32_t *)(RCC_BASE + 0x58)))   // RM0440 7.4.17

// Registro de control 3 del PWR
// FALTA POR VERIFICAR: el capítulo de PWR del RM0440 (registros en 6.4, mapa en 6.4.23) NO está en el
// extracto del proyecto. El offset 0x08 y el bit 14 deben confirmarse en el RM0440 completo o
// comparando con PWR_CR3_UCPD_DBDIS_Pos del archivo stm32g431xx.h que ya incluye main.c
#define PWR_CR3       (*((volatile uint32_t *)(PWR_BASE + 0x08)))

// Registros de GPIO, válidos para cualquier puerto pasando su dirección base (RM0440 9.4)
#define GPIO_MODER(base)    (*((volatile uint32_t *)(uintptr_t)((base) + 0x00)))   // 9.4.1
#define GPIO_OTYPER(base)   (*((volatile uint32_t *)(uintptr_t)((base) + 0x04)))   // 9.4.2
#define GPIO_OSPEEDR(base)  (*((volatile uint32_t *)(uintptr_t)((base) + 0x08)))   // 9.4.3
#define GPIO_PUPDR(base)    (*((volatile uint32_t *)(uintptr_t)((base) + 0x0C)))   // 9.4.4
#define GPIO_ODR(base)      (*((volatile uint32_t *)(uintptr_t)((base) + 0x14)))   // 9.4.6
#define GPIO_LCKR(base)     (*((volatile uint32_t *)(uintptr_t)((base) + 0x1C)))   // 9.4.8
#define GPIO_AFRL(base)     (*((volatile uint32_t *)(uintptr_t)((base) + 0x20)))   // 9.4.9
#define GPIO_AFRH(base)     (*((volatile uint32_t *)(uintptr_t)((base) + 0x24)))   // 9.4.10

// Bits de los registros de reloj
#define RCC_GPIOAEN       0    // RCC_AHB2ENR, RM0440 7.4.15
#define RCC_GPIOBEN       1    // RCC_AHB2ENR, RM0440 7.4.15
#define RCC_PWREN         28   // RCC_APB1ENR1, RM0440 7.4.17

// Bit de PWR_CR3 que deshabilita los pull-down "dead battery" del UCPD (VERIFICAR, ver arriba)
#define PWR_UCPD1_DBDIS   14

// Bit de llave del bloqueo de configuración (GPIOx_LCKR, RM0440 9.4.8)
#define GPIO_LCKK         16

// Valores de los campos MODER (RM0440 9.4.1)
#define MODE_INPUT    0   // 00: Entrada
#define MODE_OUTPUT   1   // 01: Salida de propósito general
#define MODE_AF       2   // 10: Función alterna
#define MODE_ANALOG   3   // 11: Analógico

// Valores de los campos PUPDR (RM0440 9.4.4)
#define PUPD_NONE     0   // 00: Sin pull-up ni pull-down
#define PUPD_UP       1   // 01: Pull-up
#define PUPD_DOWN     2   // 10: Pull-down

// Valor de OSPEEDR "very high speed" (RM0440 9.4.3)
#define OSPEED_VHIGH  3

// Funciones auxiliares

// Limpia TODOS los campos de configuración de un pin:
// MODER = 00, OTYPER = 0, OSPEEDR = 00, PUPDR = 00 y AFSEL = 0000.
// El resultado es una entrada flotante (RM0440 9.3.9). Se limpia MODER primero para que,
// si el pin estaba como salida, deje de manejar la línea antes de tocar lo demás.

static void Pin_Limpiar(uint32_t base, uint8_t pin) {
    GPIO_MODER(base)   &= ~(MASK_VAL(POS2(pin), 3));
    GPIO_OTYPER(base)  &= ~(MASK(pin));
    GPIO_OSPEEDR(base) &= ~(MASK_VAL(POS2(pin), 3));
    GPIO_PUPDR(base)   &= ~(MASK_VAL(POS2(pin), 3));
    if (pin < 8) {
        GPIO_AFRL(base) &= ~(MASK_VAL(POS4(pin), 0xF));       // AFSEL0..7 en AFRL
    } else {
        GPIO_AFRH(base) &= ~(MASK_VAL(POS4(pin - 8), 0xF));   // AFSEL8..15 en AFRH
    }
}

// Deja un pin libre: limpio y en modo analógico sin pull.
// En analógico el buffer de salida y el Schmitt trigger están deshabilitados,
// lo que da consumo nulo y alta impedancia (RM0440 9.3.12).
static void Pin_Analogico(uint32_t base, uint8_t pin) {
    Pin_Limpiar(base, pin);
    GPIO_MODER(base) |= MASK_VAL(POS2(pin), MODE_ANALOG);
}

// 1. Relojes: Siempre habilitar el reloj antes de configurar un periférico

static void Setup_Relojes(void) {
    // Habilitamos el reloj de GPIOA (Bit 0) y GPIOB (Bit 1)
    RCC_AHB2ENR &= ~(MASK(RCC_GPIOAEN) | MASK(RCC_GPIOBEN));
    RCC_AHB2ENR |= (MASK(RCC_GPIOAEN) | MASK(RCC_GPIOBEN));

    // Habilitamos el reloj de la interfaz de potencia PWR (Bit 28), necesario para escribir PWR_CR3
    RCC_APB1ENR1 &= ~(MASK(RCC_PWREN));
    RCC_APB1ENR1 |= MASK(RCC_PWREN);
}


// 2. UCPD "dead battery": PB4 y PB6 tienen un pull-down interno de 5.1 kOhm que se activa
//    con un nivel alto en PA10 y PA9 respectivamente (DS12589 4.10, Tabla 12, nota 6).
//    Como PA9/PA10 pueden usarse como USART1 (línea idle en alto), lo deshabilitamos
//    escribiendo UCPD1_DBDIS = 1 en PWR_CR3.

static void Setup_UCPD(void) {
    PWR_CR3 &= ~(MASK(PWR_UCPD1_DBDIS));
    PWR_CR3 |= MASK(PWR_UCPD1_DBDIS);
}

// 3. SWD (PA13 = SWDIO, PA14 = SWCLK): se restaura su configuración de reset antes de
//    bloquearla, por si fue alterada (RM0440 9.3.1 y valores de reset de 9.4.1, 9.4.3,
//    9.4.4 y 9.4.10):
//      PA13: MODER 10 (AF0), OSPEEDR 11, PUPDR 01 (pull-up)
//      PA14: MODER 10 (AF0), OSPEEDR 00, PUPDR 10 (pull-down)
//    Excepción a la limpieza en dos escrituras: aquí se arma el valor en una variable y se
//    escribe una sola vez por registro, para que SWDIO/SWCLK nunca pasen por un estado
//    intermedio (ej. MODER = 00) mientras el depurador está conectado.

static void Setup_RestaurarSWD(void) {
    uint32_t tmp;

    // MODER: limpiar y escribir 10 (función alterna) en PA13 y PA14
    tmp = GPIO_MODER(GPIOA_BASE);
    tmp &= ~(MASK_VAL(POS2(13), 3) | MASK_VAL(POS2(14), 3));
    tmp |= (MASK_VAL(POS2(13), MODE_AF) | MASK_VAL(POS2(14), MODE_AF));
    GPIO_MODER(GPIOA_BASE) = tmp;

    // OTYPER: push-pull (0) en ambos
    tmp = GPIO_OTYPER(GPIOA_BASE);
    tmp &= ~(MASK(13) | MASK(14));
    GPIO_OTYPER(GPIOA_BASE) = tmp;

    // OSPEEDR: PA13 en 11 y PA14 en 00
    tmp = GPIO_OSPEEDR(GPIOA_BASE);
    tmp &= ~(MASK_VAL(POS2(13), 3) | MASK_VAL(POS2(14), 3));
    tmp |= MASK_VAL(POS2(13), OSPEED_VHIGH);
    GPIO_OSPEEDR(GPIOA_BASE) = tmp;

    // PUPDR: PA13 con pull-up y PA14 con pull-down
    tmp = GPIO_PUPDR(GPIOA_BASE);
    tmp &= ~(MASK_VAL(POS2(13), 3) | MASK_VAL(POS2(14), 3));
    tmp |= (MASK_VAL(POS2(13), PUPD_UP) | MASK_VAL(POS2(14), PUPD_DOWN));
    GPIO_PUPDR(GPIOA_BASE) = tmp;

    // AFRH: AF0 (SWD) en ambos (AFSEL13 y AFSEL14)
    tmp = GPIO_AFRH(GPIOA_BASE);
    tmp &= ~(MASK_VAL(POS4(13 - 8), 0xF) | MASK_VAL(POS4(14 - 8), 0xF));
    GPIO_AFRH(GPIOA_BASE) = tmp;
}

// 4. Pines compartidos por SB2 y SB3 (en ON por defecto, UM2397 6.7 Tabla 9):
//      CN3-7 (A5) = PA6 + PA15  ->  PA6 "must be configured as floating input"
//      CN3-8 (A4) = PA5 + PB7   ->  PA5 "must be configured as floating input"
//    Entrada flotante = todos los campos en 0, que es exactamente lo que deja Pin_Limpiar
//    (RM0440 9.3.9, 9.4.1, 9.4.4).

static void Setup_PinesCompartidos(void) {
    Pin_Limpiar(GPIOA_BASE, 5);
    Pin_Limpiar(GPIOA_BASE, 6);
}


// 5. Liberar los pines de JTAG. Después del reset quedan en función alterna (RM0440 9.3.1):
//      PA15: JTDI con pull-up  |  PB4: NJTRST con pull-up  |  PB3: JTDO flotante
//    La depuración por SWD (PA13/PA14) no se ve afectada. PB3 también tiene T_SWO
//    conectado (SB4 en ON, UM2397 6.7 Tabla 9); la traza SWO deja de estar disponible.

static void Setup_LiberarJTAG(void) {
    Pin_Analogico(GPIOA_BASE, 15);
    Pin_Analogico(GPIOB_BASE, 3);
    Pin_Analogico(GPIOB_BASE, 4);
}


// 6. Pines libres para Green_Card y Arcade: limpios y en analógico sin pull.
//    PA9 y PA10 quedan aquí como "libres" mientras se decide si se usa USART1/FTDI.

static void Setup_PinesLibres(void) {
    // Puerto A: PA0, PA1, PA4, PA7, PA8, PA9, PA10, PA11, PA12
    const uint8_t pinesA[] = {0, 1, 4, 7, 8, 9, 10, 11, 12};
    // Puerto B: PB0, PB5, PB6, PB7
    const uint8_t pinesB[] = {0, 5, 6, 7};

    for (uint32_t i = 0; i < sizeof(pinesA); i++) {
        Pin_Analogico(GPIOA_BASE, pinesA[i]);
    }
    for (uint32_t i = 0; i < sizeof(pinesB); i++) {
        Pin_Analogico(GPIOB_BASE, pinesB[i]);
    }
}

// 7. PB8 / BOOT0: maneja el LED de usuario LD2 (SB7 en ON y SB6 en OFF por defecto,
//    UM2397 6.6.1 y 6.7 Tabla 9). No sale a los headers y no lo usaremos.
//    El RM0440 recomienda no dejar PB8 en analógico para limitar el consumo
//    (RM0440 9.3.15 y nota de 9.4.1; DS12589 4.10 Tabla 12, nota 7).
//    Lo dejamos como salida push-pull en 0 (LD2 apagado). En cada reset PB8 vuelve
//    a entrada, por lo que no interfiere con la lectura de BOOT0 (RM0440 9.3.15).

static void Setup_PB8(void) {
    // Limpiamos todo: queda como entrada, sin manejar la línea
    Pin_Limpiar(GPIOB_BASE, 8);

    // Dato de salida en 0 ANTES de pasar a salida, para que no haya un pulso en alto
    GPIO_ODR(GPIOB_BASE) &= ~(MASK(8));

    // Modo salida de propósito general (01); OTYPER = 0 (push-pull) ya quedó limpio
    GPIO_MODER(GPIOB_BASE) |= MASK_VAL(POS2(8), MODE_OUTPUT);
}

// 8. Bloqueo de la configuración de PA13/PA14 (RM0440 9.3.6 y 9.4.8).
//    Protege el SWD de escrituras accidentales con los comandos MM/BF del monitor
//    (ej. sobre GPIOA_MODER). Queda bloqueado hasta el siguiente reset del MCU o del GPIOA.
//
//    IMPORTANTE:
//    - La secuencia exige escrituras de palabra completa con LCK[15:0] constante, por eso
//      aquí NO se usa Read-Modify-Write. Los bits 31:17 son reservados y deben quedar en
//      su valor de reset (0), que es lo que se escribe.
//    - Tras el primer bloqueo, LCKK se lee en 1 hasta el siguiente reset: la secuencia solo
//      puede aplicarse UNA vez por puerto. Si en el futuro se quieren bloquear más pines de
//      GPIOA, deben agregarse a LCK_PINES_A (y mover esta llamada después de su configuración).
//    - Verificación desde el monitor: "MD 4800001C 48000020" debe mostrar 00 60 01 00
//      (GPIOA_LCKR = 0x00016000 en Little-Endian).


#define LCK_PINES_A   (MASK(13) | MASK(14))

static void Setup_BloquearSWD(void) {
    volatile uint32_t lectura;

    // Si LCKK ya está en 1, el puerto ya fue bloqueado desde el último reset
    if (GPIO_LCKR(GPIOA_BASE) & MASK(GPIO_LCKK)) {
        return;
    }

    GPIO_LCKR(GPIOA_BASE) = MASK(GPIO_LCKK) | LCK_PINES_A;   // WR LCKK = 1 + LCK[15:0]
    GPIO_LCKR(GPIOA_BASE) = LCK_PINES_A;                     // WR LCKK = 0 + LCK[15:0]
    GPIO_LCKR(GPIOA_BASE) = MASK(GPIO_LCKK) | LCK_PINES_A;   // WR LCKK = 1 + LCK[15:0]
    lectura = GPIO_LCKR(GPIOA_BASE);                         // RD LCKR (parte de la secuencia)
    (void)lectura;
}

// Rutina principal
// El orden importa: relojes primero; UCPD antes de que PA9/PA10 puedan quedar en alto;
// SWD restaurado; PA5/PA6 en entrada flotante antes de que PB7/PA15 reciban cualquier
// función; y el bloqueo al final, cuando PA13/PA14 ya tienen su configuración correcta.

void Setup_Seguridad(void) {
    Setup_Relojes();
    Setup_UCPD();
    Setup_RestaurarSWD();
    Setup_PinesCompartidos();
    Setup_LiberarJTAG();
    Setup_PinesLibres();
    Setup_PB8();
    Setup_BloquearSWD();
}
