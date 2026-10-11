#ifndef CONNECT_H
#define CONNECT_H

// Declaración de las funciones de comunicación serial
void UART_Init(void);
void UART_Tx(char c);
char UART_Rx(void);

#endif /* CONNECT_H */