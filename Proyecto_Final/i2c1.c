#include "hw_comun.h"
#include "i2c1.h"

/* I2C1 maestro a 100 kHz: PA15 = SCL, PB7 = SDA (AF4, open-drain) */

#define PIN_SCL           15   /* PA15 */
#define PIN_SDA           7    /* PB7 */
#define AF4_I2C1          4

/* TIMINGR para 100 kHz con reloj de 16 MHz:
   PRESC = 3   -> tPRESC = (3 + 1) / 16 MHz = 250 ns
   SCLL  = 19  -> SCL en bajo (19 + 1) x 250 ns = 5.0 us
   SCLH  = 15  -> SCL en alto (15 + 1) x 250 ns = 4.0 us
   Periodo = 5.0 + 4.0 us + tiempos de sincronización = unos 10 us -> 100 kHz
   SDADEL = 2  -> retardo de SDA 2 x 250 ns = 0.5 us
   SCLDEL = 4  -> preparación de SDA (4 + 1) x 250 ns = 1.25 us */
#define I2C_TIMING_100KHZ ((0x3U  << I2C_TIMINGR_PRESC_Pos)  | \
                           (0x4U  << I2C_TIMINGR_SCLDEL_Pos) | \
                           (0x2U  << I2C_TIMINGR_SDADEL_Pos) | \
                           (0x0FU << I2C_TIMINGR_SCLH_Pos)   | \
                           (0x13U << I2C_TIMINGR_SCLL_Pos))

#define I2C_ERRORES       (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO)
#define I2C_LIMPIAR_TODO  (I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF)

/* Límite de cada espera */
#define I2C_TIMEOUT       4000U

/* Reinicio por software (PE = 0 y luego 1) */
static void I2C1_Reset(void)
{
    volatile uint32_t lectura;

    I2C1->CR1 &= ~I2C_CR1_PE;
    while (I2C1->CR1 & I2C_CR1_PE)
    {
        /* Espera */
    }

    /* PE en 0 al menos 3 ciclos */
    lectura = I2C1->CR1;
    lectura = I2C1->CR1;
    lectura = I2C1->CR1;
    (void)lectura;

    I2C1->CR1 |= I2C_CR1_PE;
}

/* Espera una bandera de ISR: 1 si llegó, 0 si hubo error o se agotó el tiempo */
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

/* Recuperación después de un error */
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

    I2C1->ICR = I2C_LIMPIAR_TODO;
    I2C1_Reset();
    return I2C1_ERROR;
}

void I2C1_Init(void)
{
    /* Reloj y reset del I2C1 */
    RCC_ActivarAPB1(RCC_APB1ENR1_I2C1EN, RCC_APB1RSTR1_I2C1RST);

    /* HSI16 como reloj del I2C1 */
    RCC_HSI16_Encender();
    RCC->CCIPR &= ~RCC_CCIPR_I2C1SEL;
    RCC->CCIPR |= RCC_CCIPR_I2C1SEL_1;

    /* Pines */
    GPIO_ConfigAF(GPIOA, PIN_SCL, AF4_I2C1, GPIO_OPEN_DRAIN);
    GPIO_ConfigAF(GPIOB, PIN_SDA, AF4_I2C1, GPIO_OPEN_DRAIN);

    /* Filtros y tiempos con PE = 0 */
    I2C1->CR1 &= ~I2C_CR1_PE;
    I2C1->CR1 &= ~(I2C_CR1_ANFOFF | I2C_CR1_DNF);

    I2C1->TIMINGR = I2C_TIMING_100KHZ;

    I2C1->CR1 |= I2C_CR1_PE;
}

/* Escribe len bytes al esclavo (START, datos, STOP) */
uint8_t I2C1_Write(uint8_t addr7, const uint8_t *data, uint8_t len)
{
    uint32_t restante = I2C_TIMEOUT;

    if (len == 0)
    {
        return I2C1_ERROR;
    }

    /* Bus libre */
    while (I2C1->ISR & I2C_ISR_BUSY)
    {
        if (restante == 0)
        {
            return I2C1_Fallo();
        }
        restante--;
    }

    I2C1->ICR = I2C_LIMPIAR_TODO;

    /* Dirección, escritura, número de bytes y AUTOEND */
    I2C1->CR2 &= ~(I2C_CR2_SADD | I2C_CR2_RD_WRN | I2C_CR2_ADD10 |
                   I2C_CR2_NBYTES | I2C_CR2_RELOAD | I2C_CR2_AUTOEND);
    I2C1->CR2 |= ((((uint32_t)addr7 << 1) & I2C_CR2_SADD) |
                  ((uint32_t)len << I2C_CR2_NBYTES_Pos) |
                  I2C_CR2_AUTOEND);

    I2C1->CR2 |= I2C_CR2_START;

    /* Datos */
    for (uint8_t i = 0; i < len; i++)
    {
        if (!I2C1_WaitFlag(I2C_ISR_TXIS))
        {
            return I2C1_Fallo();
        }
        I2C1->TXDR = data[i];
    }

    /* STOP */
    if (!I2C1_WaitFlag(I2C_ISR_STOPF))
    {
        return I2C1_Fallo();
    }

    I2C1->ICR = I2C_ICR_STOPCF;
    return I2C1_OK;
}

/* Lee len bytes desde el registro reg (escritura, RESTART y lectura) */
uint8_t I2C1_WriteRead(uint8_t addr7, uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint32_t restante = I2C_TIMEOUT;

    if (len == 0)
    {
        return I2C1_ERROR;
    }

    /* Bus libre */
    while (I2C1->ISR & I2C_ISR_BUSY)
    {
        if (restante == 0)
        {
            return I2C1_Fallo();
        }
        restante--;
    }

    I2C1->ICR = I2C_LIMPIAR_TODO;

    /* Escritura del número de registro, sin STOP */
    I2C1->CR2 &= ~(I2C_CR2_SADD | I2C_CR2_RD_WRN | I2C_CR2_ADD10 |
                   I2C_CR2_NBYTES | I2C_CR2_RELOAD | I2C_CR2_AUTOEND);
    I2C1->CR2 |= ((((uint32_t)addr7 << 1) & I2C_CR2_SADD) |
                  (1U << I2C_CR2_NBYTES_Pos));
    I2C1->CR2 |= I2C_CR2_START;

    if (!I2C1_WaitFlag(I2C_ISR_TXIS))
    {
        return I2C1_Fallo();
    }
    I2C1->TXDR = reg;

    if (!I2C1_WaitFlag(I2C_ISR_TC))
    {
        return I2C1_Fallo();
    }

    /* Lectura con RESTART y STOP automático */
    I2C1->CR2 &= ~(I2C_CR2_NBYTES | I2C_CR2_AUTOEND);
    I2C1->CR2 |= (I2C_CR2_RD_WRN | ((uint32_t)len << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND);
    I2C1->CR2 |= I2C_CR2_START;

    for (uint8_t i = 0; i < len; i++)
    {
        if (!I2C1_WaitFlag(I2C_ISR_RXNE))
        {
            return I2C1_Fallo();
        }
        buf[i] = (uint8_t)I2C1->RXDR;
    }

    if (!I2C1_WaitFlag(I2C_ISR_STOPF))
    {
        return I2C1_Fallo();
    }

    I2C1->ICR = I2C_ICR_STOPCF;
    return I2C1_OK;
}

/* Lee registros consecutivos del MCP23017 */
uint8_t MCP23017_ReadRegs(uint8_t addr7, uint8_t reg, uint8_t *vals, uint8_t len)
{
    if (len == 0 || len > MCP_MAX_REGS)
    {
        return I2C1_ERROR;
    }
    return I2C1_WriteRead(addr7, reg, vals, len);
}

/* Escribe registros consecutivos del MCP23017 */
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
