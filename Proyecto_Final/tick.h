#ifndef TICK_H
#define TICK_H

// Tick del sistema con SysTick para las tareas que deben correr en paralelo con el monitor.
// SysTick_Handler se ejecuta cada TICK_MS milisegundos y llama a las tareas de fondo:
//  - Multiplexado del display de 7 segmentos: cada tick.
//  - IOMAP (switches -> LEDs): cada IO_PERIODO_TICKS ticks.
//  - POT2SEG (potenciómetro -> display): cada POT_PERIODO_TICKS ticks.
//  - LCDWATCH (un watch por vez -> LCD): cada WATCH_PERIODO_TICKS ticks, desfasado de IOMAP.

#define TICK_MS             2
#define IO_PERIODO_TICKS    5       // 5 ticks = 10 ms entre lecturas de los switches
#define POT_PERIODO_TICKS   10      // 10 ticks = 20 ms entre conversiones de POT2SEG
#define WATCH_PERIODO_TICKS 5       // 5 ticks = 10 ms entre revisiones de LCDWATCH

void Tick_Init(void);

// Pausan y reanudan la interrupción del tick. El programa principal las usa para tener el bus
// I2C1, el ADC o SPI1 (LCD) en exclusiva (ej. reinicializar U5 o U6, o la conversión de VIN).
void Tick_Bloquear(void);
void Tick_Desbloquear(void);

#endif /* TICK_H */
