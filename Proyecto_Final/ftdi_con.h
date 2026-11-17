#ifndef FTDI_CON_H
#define FTDI_CON_H

// Comunicación serial del monitor por el cable FTDI (USART1 en PA9/PA10)
// Reemplaza a connect.h que se conserva solo como registro
void FTDI_Init(void);
void FTDI_Tx(char c);
char FTDI_Rx(void);

#endif /* FTDI_CON_H */
