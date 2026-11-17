#ifndef CONNECT_H
#define CONNECT_H

// OBSOLETO (deprecated): reemplazado por ftdi_con.h (USART1 por el cable FTDI).
// Se conserva solo como registro de la implementación con USART2 (VCP).

// Declaración de las funciones de comunicación serial
void UART_Init(void);
void UART_Tx(char c);
char UART_Rx(void);

#endif /* CONNECT_H */