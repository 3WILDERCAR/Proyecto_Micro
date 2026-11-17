#ifndef SOUND_H
#define SOUND_H

// Generación de señales cuadradas en el buzzer (TIM4_CH1 en PB6).

// Inicialización del temporizador y del pin del buzzer (deja el buzzer en silencio)
void Sound_Init(void);

// Comandos. args apunta al texto que sigue al nombre del comando, ya sin espacios iniciales
// (mismo convenio que Cmd_MD, Cmd_MM y Cmd_BF en memory.h)
void Cmd_SOUND(char *args);   // SOUND freq
void Cmd_MUTE(char *args);    // MUTE

#endif /* SOUND_H */
