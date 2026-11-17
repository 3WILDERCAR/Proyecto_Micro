#include "hw_comun.h"
#include "i2c1.h"

// Driver del bus I2C1 en modo maestro, solo escritura, 100 kHz (modo estándar).
//
// Pines (AyudaProgra, tabla de la NUCLEO-G431KB; DS12589 4.11, Tabla 13):
//  - PA15 = I2C1_SCL (CN3-7, A5), AF4, open-drain, sin pull-up ni pull-down interno
//  - PB7  = I2C1_SDA (CN3-8, A4), AF4, open-drain, sin pull-up ni pull-down interno
//  Las pull-ups son externas (R25 y R26, 4.7k a +3V3_IO).
//  PA6 (unido a A5 por SB3) y PA5 (unido a A4 por SB2) ya quedaron como entradas flotantes
//  en Setup_Seguridad(), así que no interfieren con el bus.
//
// Reloj: HSI16 (16 MHz) seleccionado en RCC_CCIPR.I2C1SEL, igual que el USART1 de ftdi_con.c,
// para que el valor de TIMINGR no dependa del reloj del sistema.
//
// Manejo de errores: cada espera tiene un límite de iteraciones. Si el esclavo no responde
// (NACK), hay error de bus o arbitraje, o se agota el tiempo, se reinicia el periférico por
// software y la función devuelve I2C1_ERROR. Así un expansor apagado (fuente de 9 V sin
// conectar) nunca deja al monitor colgado.

// Pines del bus y su función alterna
#define PIN_SCL           15   // PA15
#define PIN_SDA           7    // PB7
#define AF4_I2C1          4    // AF4 = I2C1_SCL / I2C1_SDA en PA15 y PB7

// Temporización para 100 kHz con fI2CCLK = 16 MHz (RM0440, Tabla 360):
// PRESC = 0x3, SCLDEL = 0x4, SDADEL = 0x2, SCLH = 0x0F, SCLL = 0x13 -> TIMINGR = 0x30420F13
#define I2C_TIMING_100KHZ ((0x3U  << I2C_TIMINGR_PRESC_Pos)  | \
                           (0x4U  << I2C_TIMINGR_SCLDEL_Pos) | \
                           (0x2U  << I2C_TIMINGR_SDADEL_Pos) | \
                           (0x0FU << I2C_TIMINGR_SCLH_Pos)   | \
                           (0x13U << I2C_TIMINGR_SCLL_Pos))

#define I2C_ERRORES       (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO)
#define I2C_LIMPIAR_TODO  (I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF)

// Límite de iteraciones de cada espera. Un byte a 100 kHz tarda unos 90 us; con el núcleo a
// 16 MHz este límite equivale a algunos milisegundos, de sobra para cualquier paso normal.
#define I2C_TIMEOUT       4000U

// Reinicio por software del I2C1 (RM0440, I2C software reset): PE = 0 libera SCL y SDA y reinicia
// las máquinas de estado y las banderas. PE debe quedar en 0 al menos 3 ciclos de APB.
static void I2C1_Reset(void)
{
    volatile uint32_t lectura;

    I2C1->CR1 &= ~I2C_CR1_PE;
    while (I2C1->CR1 & I2C_CR1_PE)
    {
        // Esperamos a que PE se lea en 0
    }

    // Lecturas extra para asegurar los 3 ciclos de APB con PE en 0
    lectura = I2C1->CR1;
    lectura = I2C1->CR1;
    lectura = I2C1->CR1;
    (void)lectura;

    I2C1->CR1 |= I2C_CR1_PE;
}

// Espera a que se encienda la bandera indicada en I2C_ISR.
// Devuelve 1 si se encendió; 0 si antes apareció un error (NACK, BERR, ARLO) o se agotó el tiempo.
// Los errores se revisan primero: así un NACK en el último byte no se confunde con un STOP normal.
static uint8_t I2C1_WaitFlag(uint32_t bandera)
{
    uint32_t restante = I2C_TIMEOUT;

    while (restante > 0)
    {
        uint32_t estado = I2C1->ISR;

        if (estado & I2C_ERRORES)
        {
            return 0;
        }
        if (estado & bandera)
        {
            return 1;
        }
        restante--;
    }
    return 0;
}

// Recuperación después de un error. Con un NACK el hardware genera el STOP solo (RM0440, I2C
// controller transmitter: "a STOP condition is automatically sent"), así que esperamos un
// momento a que termine antes de reiniciar.
static uint8_t I2C1_Fallo(void)
{
    uint32_t restante = I2C_TIMEOUT;

    if (I2C1->ISR & I2C_ISR_NACKF)
    {
        while (!(I2C1->ISR & I2C_ISR_STOPF) && (restante > 0))
        {
            restante--;
        }
    }

    I2C1->ICR = I2C_LIMPIAR_TODO;   // ICR solo se escribe: 1 limpia la bandera, 0 no hace nada
    I2C1_Reset();
    return I2C1_ERROR;
}

void I2C1_Init(void)
{
    // Reloj del I2C1 y reinicio desde el RCC
    RCC_ActivarAPB1(RCC_APB1ENR1_I2C1EN, RCC_APB1RSTR1_I2C1RST);

    // HSI16 como reloj del I2C1: I2C1SEL (bits 13:12 de RCC_CCIPR) = 10
    RCC_HSI16_Encender();
    RCC->CCIPR &= ~RCC_CCIPR_I2C1SEL;
    RCC->CCIPR |= RCC_CCIPR_I2C1SEL_1;

    // Pines del bus en open-drain
    GPIO_ConfigAF(GPIOA, PIN_SCL, AF4_I2C1, GPIO_OPEN_DRAIN);
    GPIO_ConfigAF(GPIOB, PIN_SDA, AF4_I2C1, GPIO_OPEN_DRAIN);

    // Configuración del periférico con PE = 0 (TIMINGR, ANFOFF y DNF solo se escriben así)
    I2C1->CR1 &= ~I2C_CR1_PE;
    I2C1->CR1 &= ~(I2C_CR1_ANFOFF | I2C_CR1_DNF);   // Filtro analógico encendido, sin filtro digital

    I2C1->TIMINGR = I2C_TIMING_100KHZ;              // Registro completo (bits 27:24 reservados en 0)

    // Habilitamos el periférico
    I2C1->CR1 |= I2C_CR1_PE;
}

// Escribe len bytes al esclavo addr7 (dirección de 7 bits) en una sola transacción:
// START, dirección + W, los len bytes y STOP automático (AUTOEND = 1).
uint8_t I2C1_Write(uint8_t addr7, const uint8_t *data, uint8_t len)
{
    uint32_t restante = I2C_TIMEOUT;

    if (len == 0)
    {
        return I2C1_ERROR;
    }

    // Esperamos a que el bus esté libre
    while (I2C1->ISR & I2C_ISR_BUSY)
    {
        if (restante == 0)
        {
            return I2C1_Fallo();
        }
        restante--;
    }

    // Limpiamos banderas de una transferencia anterior
    I2C1->ICR = I2C_LIMPIAR_TODO;

    // Dirección, sentido (escritura), número de bytes y AUTOEND. En modo de 7 bits la dirección
    // va corrida un bit: 0x40 para U5 y 0x42 para U6 (AyudaProgra, I2C1)
    I2C1->CR2 &= ~(I2C_CR2_SADD | I2C_CR2_RD_WRN | I2C_CR2_ADD10 |
                   I2C_CR2_NBYTES | I2C_CR2_RELOAD | I2C_CR2_AUTOEND);
    I2C1->CR2 |= ((((uint32_t)addr7 << 1) & I2C_CR2_SADD) |
                  ((uint32_t)len << I2C_CR2_NBYTES_Pos) |
                  I2C_CR2_AUTOEND);

    // Generamos el START (los demás campos ya no se pueden cambiar mientras START = 1)
    I2C1->CR2 |= I2C_CR2_START;

    // Enviamos cada byte cuando TXIS indica que TXDR está libre
    for (uint8_t i = 0; i < len; i++)
    {
        if (!I2C1_WaitFlag(I2C_ISR_TXIS))
        {
            return I2C1_Fallo();
        }
        I2C1->TXDR = data[i];
    }

    // Con AUTOEND el hardware genera el STOP después del último byte
    if (!I2C1_WaitFlag(I2C_ISR_STOPF))
    {
        return I2C1_Fallo();
    }

    I2C1->ICR = I2C_ICR_STOPCF;
    return I2C1_OK;
}

// Escribe len registros consecutivos del MCP23017 a partir de reg.
// Con IOCON.SEQOP = 0 (valor por defecto) el puntero de registro avanza solo después de cada
// byte (DS20001952E 3.5.6), así que basta enviar la dirección del primero.
uint8_t MCP23017_WriteRegs(uint8_t addr7, uint8_t reg, const uint8_t *vals, uint8_t len)
{
    uint8_t trama[MCP_MAX_REGS + 1];

    if (len == 0 || len > MCP_MAX_REGS)
    {
        return I2C1_ERROR;
    }

    trama[0] = reg;
    for (uint8_t i = 0; i < len; i++)
    {
        trama[i + 1] = vals[i];
    }

    return I2C1_Write(addr7, trama, (uint8_t)(len + 1));
}
