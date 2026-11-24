#ifndef I2C1_H
#define I2C1_H

#include <stdint.h>

/* Bus I2C1 (PA15 = SCL, PB7 = SDA) y expansores MCP23017.
   Desde el programa principal se usa con el tick bloqueado. */

/* Direcciones de los expansores */
#define MCP_U5_ADDR     0x20    /* LEDs y switches */
#define MCP_U6_ADDR     0x21    /* Segmentos y dígitos */

/* Registros del MCP23017 (BANK = 0) */
#define MCP_IODIRA      0x00
#define MCP_IODIRB      0x01
#define MCP_GPPUA       0x0C
#define MCP_GPPUB       0x0D
#define MCP_GPIOA       0x12
#define MCP_GPIOB       0x13
#define MCP_OLATA       0x14
#define MCP_OLATB       0x15

/* Resultado */
#define I2C1_OK         0
#define I2C1_ERROR      1

/* Máximo de registros por escritura */
#define MCP_MAX_REGS    8

void I2C1_Init(void);
uint8_t I2C1_Write(uint8_t addr7, const uint8_t *data, uint8_t len);
uint8_t I2C1_WriteRead(uint8_t addr7, uint8_t reg, uint8_t *buf, uint8_t len);
uint8_t MCP23017_WriteRegs(uint8_t addr7, uint8_t reg, const uint8_t *vals, uint8_t len);
uint8_t MCP23017_ReadRegs(uint8_t addr7, uint8_t reg, uint8_t *vals, uint8_t len);

#endif /* I2C1_H */
