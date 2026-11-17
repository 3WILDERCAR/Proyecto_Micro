#ifndef RGB_H
#define RGB_H

// Salidas PWM del LED RGB (TIM3_CH1 en PB4, TIM3_CH2 en PA7, TIM3_CH3 en PB0).

// Inicialización del temporizador y de los pines PWM (deja el LED apagado)
void RGB_Init(void);

// Comandos. args apunta al texto que sigue al nombre del comando, ya sin espacios iniciales
// (mismo convenio que Cmd_MD, Cmd_MM y Cmd_BF en memory.h)
void Cmd_RGBOUT(char *args);   // RGBOUT color

#endif /* RGB_H */
