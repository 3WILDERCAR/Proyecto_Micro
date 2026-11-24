#ifndef RGB_H
#define RGB_H

/* LED RGB por PWM (TIM3) */

void RGB_Init(void);

/* Comando */
void Cmd_RGBOUT(char *args);   /* RGBOUT color */

#endif /* RGB_H */
