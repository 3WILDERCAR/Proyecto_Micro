#ifndef TICK_H
#define TICK_H

/* Tick de SysTick para las tareas de fondo */

#define TICK_MS             2
#define IO_PERIODO_TICKS    5       /* 5 ticks x 2 ms = 10 ms entre lecturas de IOMAP */
#define POT_PERIODO_TICKS   10      /* 10 ticks x 2 ms = 20 ms entre lecturas de POT2SEG */
#define WATCH_PERIODO_TICKS 5       /* 5 ticks x 2 ms = 10 ms entre revisiones de LCDWATCH */

void Tick_Init(void);

/* Pausa y reanuda el tick (uso exclusivo de I2C1, ADC o SPI1) */
void Tick_Bloquear(void);
void Tick_Desbloquear(void);

#endif /* TICK_H */
