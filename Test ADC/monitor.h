#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>

void UART_Print(const char *str);
void UART_PrintHex32(uint32_t val);
uint32_t ParseHex(const char *str);

#endif /* MONITOR_H */