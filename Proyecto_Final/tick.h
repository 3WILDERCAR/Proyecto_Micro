#ifndef TICK_H
#define TICK_H

// Tick del sistema con SysTick para las tareas que deben correr en paralelo con el monitor.
// SysTick_Handler se ejecuta cada TICK_MS milisegundos y llama a las tareas de fondo
// (por ahora, el multiplexado del display de 7 segmentos).

#define TICK_MS     2

void Tick_Init(void);

#endif /* TICK_H */
