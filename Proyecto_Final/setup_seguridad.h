#ifndef SETUP_SEGURIDAD_H
#define SETUP_SEGURIDAD_H

// Configuración de seguridad y liberación de pines (Esquemático: Setup_Seguridad)
// Al final bloquea la configuración de PA13/PA14 (SWD) hasta el siguiente reset (GPIOA_LCKR)
void Setup_Seguridad(void);

#endif /* SETUP_SEGURIDAD_H */
