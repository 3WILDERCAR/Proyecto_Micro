#ifndef SOUND_H
#define SOUND_H

/* Buzzer (TIM4_CH1 en PB6) */

void Sound_Init(void);

/* Comandos */
void Cmd_SOUND(char *args);   /* SOUND freq */
void Cmd_MUTE(char *args);    /* MUTE */

#endif /* SOUND_H */
