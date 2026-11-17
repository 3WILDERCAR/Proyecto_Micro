#ifndef I2C1_H
#define I2C1_H

#include <stdint.h>

// Driver del bus I2C1 (PA15 = SCL, PB7 = SDA, AF4) a 100 kHz, y escritura de registros de los
// expansores MCP23017 U5 y U6 de Green_Card (AyudaProgra, "Pines de los MCP23017").
//
// IMPORTANTE: I2C1_Write no es reentrante. Después de la inicialización, todo el tráfico del bus
// debe hacerse desde un solo contexto (el tick de SysTick, ver tick.c), o el programa principal
// debe detener el tick antes de usar el bus.

// Direcciones de 7 bits de los expansores (AyudaProgra, tabla de U5 y U6)
#define MCP_U5_ADDR     0x20    // LEDs (puerto A) y switches (puerto B)
#define MCP_U6_ADDR     0x21    // Segmentos (puerto A) y selección de dígitos (puerto B)

// Registros del MCP23017 con IOCON.BANK = 0, valor tras reset (DS20001952E 3.5, Tabla 3-5)
#define MCP_IODIRA      0x00
#define MCP_IODIRB      0x01
#define MCP_GPPUA       0x0C
#define MCP_GPPUB       0x0D
#define MCP_GPIOA       0x12
#define MCP_GPIOB       0x13
#define MCP_OLATA       0x14
#define MCP_OLATB       0x15

// Resultado de las transferencias
#define I2C1_OK         0
#define I2C1_ERROR      1

// Máximo de registros consecutivos que se escriben en una sola transacción
#define MCP_MAX_REGS    8

void I2C1_Init(void);
uint8_t I2C1_Write(uint8_t addr7, const uint8_t *data, uint8_t len);
uint8_t MCP23017_WriteRegs(uint8_t addr7, uint8_t reg, const uint8_t *vals, uint8_t len);

#endif /* I2C1_H */
