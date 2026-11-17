#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>

void Monitor_Run(void);
void UART_Print(const char *str);
void UART_PrintHex32(uint32_t val);
void UART_PrintDec(int32_t val);
uint32_t ParseHex(const char *str);
uint8_t ParseDec(const char *str, int32_t *val);

#endif /* MONITOR_H */
