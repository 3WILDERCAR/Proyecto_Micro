#include "hw_comun.h"
#include "analog.h"
#include "display.h"
#include "tick.h"
#include "../monitor.h"   // UART_Print, UART_PrintDec, ParseDec

// Conversiones analógico-digital y digital-analógico, y monitoreo continuo del potenciómetro.
//
// Entrada analógica (AyudaProgra, tabla de la NUCLEO-G431KB):
//  - POT: PA0 (CN3-12, A0), modo analógico, ADC1_IN1 (canal 1 de ADC1).
//
// ADC1 (RM0440 cap. 21):
//  - Reloj síncrono adc_hclk/1 (CKMODE = 01) = 16 MHz. Solo se permite con el prescaler de AHB
//    sin dividir, que es el valor tras reset (ver hw_comun.h).
//  - Conversión única por software (CONT = 0, EXTEN = 00), canal 1 en modo single-ended.
//  - Tiempo de muestreo de 640.5 ciclos (SMP = 111, 40 us), el más largo, porque no se conoce la
//    impedancia del potenciómetro. Conversión total = muestreo + 12.5 ciclos (unos 41 us).
//  - Resolución por conversión: RES solo se puede cambiar con ADSTART = 0 (sin conversión en
//    curso), lo cual se cumple porque en modo único ADSTART se limpia solo al terminar.
//  - Voltaje: VREF+ = VDDA = 3.3 V en la NUCLEO-G431KB.
//
// Salida analógica (AyudaProgra, tabla de la NUCLEO-G431KB):
//  - DAC_OUT: PA4 (CN3-9, A3), modo analógico, DAC1_OUT1 (canal 1 de DAC1).
//
// DAC1 (RM0440 cap. 22):
//  - MODE1 = 000: modo normal con el buffer de salida encendido, conectado al pin externo.
//    MODE1 solo se puede escribir con EN1 = 0, así que se configura antes de habilitar.
//  - Sin disparo (TEN1 = 0): el dato escrito en DHR pasa a DOR un ciclo después.
//  - V = VREF+ * DOR / 4096. En 8 bits el dato ocupa los bits 11:4 de DOR, así que el fondo de
//    escala es 2^res (256 o 4096), no 2^res - 1.
//  - Con el buffer encendido la salida solo es válida entre 0.2 V y VREF+ - 0.2 V con carga de
//    5 kOhm o más (AyudaProgra, "DAC"; DS12589 5.3.19): VOUT limita el valor a ese rango.
//  - El canal se habilita con el primer VOUT; hasta entonces PA4 queda en analógico (alta impedancia).
//
// POT2SEG (no bloqueante): Pot2Seg_Actualizar() corre en el tick cada POT_PERIODO_TICKS (20 ms),
// hace una conversión de 12 bits (unos 41 us) y la manda al display: en decimal (0-4095) o en
// hexadecimal (000h-FFFh). Como el tick también usa el ADC, VIN bloquea el tick durante su conversión.

#define PIN_POT             0       // PA0
#define CANAL_POT           1       // ADC1_IN1

#define ADC_SMP_640_5       7U      // SMPx = 111: 640.5 ciclos de ADC

// Referencia del ADC en décimas de milivoltio (3.3 V = 33000), para imprimir 4 decimales
#define VREF_DECIMAS_MV     33000U

// Arranque del regulador del ADC: el tiempo máximo está en la hoja de datos (tADCVREG_STUP,
// unas decenas de microsegundos). Se espera con amplio margen porque solo ocurre una vez.
#define ADC_ARRANQUE_US     100U

// Límite de iteraciones para las esperas del ADC (calibración, ADRDY, EOC). Una conversión
// tarda unos 41 us; el límite equivale a varios milisegundos.
#define ADC_TIMEOUT         100000U

#define PIN_DAC             4       // PA4 (DAC1_OUT1)

// Rango válido de la salida con el buffer encendido, en décimas de milivoltio
#define DAC_MIN_DECIMAS_MV  2000U                               // 0.2 V
#define DAC_MAX_DECIMAS_MV  (VREF_DECIMAS_MV - 2000U)           // 3.1 V

#define DAC_TIMEOUT         100000U

static uint8_t adc_listo = 0;       // 1 si Analog_InitADC terminó bien
static uint8_t dac_listo = 0;       // 1 si Analog_InitDAC ya configuró el DAC
static uint8_t dac_encendido = 0;   // 1 después de habilitar el canal (primer VOUT)

// Estado de POT2SEG, compartido con la interrupción del tick
static volatile uint8_t pot_activo = 0;     // 1 con POT2SEG on
static volatile uint8_t pot_hex = 0;        // 1 = hexadecimal, 0 = decimal

#define ADC_RES_12_BITS     0U      // Código RES[1:0] = 00

// Espera a que (ADC1->CR & mascara) sea igual a 'valor'. Devuelve 1 si se cumplió, 0 si se agotó el tiempo.
static uint8_t ADC_EsperarCR(uint32_t mascara, uint32_t valor)
{
    uint32_t restante = ADC_TIMEOUT;

    while ((ADC1->CR & mascara) != valor)
    {
        if (restante == 0)
        {
            return 0;
        }
        restante--;
    }
    return 1;
}

// Espera a que se encienda una bandera de ADC1->ISR. Devuelve 1 si se encendió, 0 si se agotó el tiempo.
static uint8_t ADC_EsperarISR(uint32_t bandera)
{
    uint32_t restante = ADC_TIMEOUT;

    while (!(ADC1->ISR & bandera))
    {
        if (restante == 0)
        {
            return 0;
        }
        restante--;
    }
    return 1;
}

// Inicialización del ADC1 para el potenciómetro, en el orden de RM0440 cap. 21:
// reloj, salir de deep-power-down, regulador, calibración, habilitación y configuración del canal.
uint8_t Analog_InitADC(void)
{
    adc_listo = 0;

    // Reloj de ADC1/ADC2 (bus AHB2) y reinicio desde el RCC
    RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN;
    RCC->AHB2RSTR |= RCC_AHB2RSTR_ADC12RST;
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_ADC12RST;

    // Reloj síncrono adc_hclk/1: CKMODE (bits 17:16 de ADC12_CCR) = 01
    ADC12_COMMON->CCR &= ~ADC_CCR_CKMODE;
    ADC12_COMMON->CCR |= ADC_CCR_CKMODE_0;

    // PA0 en analógico
    GPIO_ConfigAnalogico(GPIOA, PIN_POT);

    // Salimos de deep-power-down (DEEPPWD vale 1 tras reset) y encendemos el regulador
    ADC1->CR &= ~ADC_CR_DEEPPWD;
    ADC1->CR |= ADC_CR_ADVREGEN;
    Retardo_us(ADC_ARRANQUE_US);

    // Canal 1 en modo single-ended. DIFSEL solo se puede escribir con ADEN = 0
    ADC1->DIFSEL &= ~(1U << CANAL_POT);

    // Calibración single-ended (ADCALDIF = 0) con el ADC deshabilitado (ADEN = 0)
    ADC1->CR &= ~ADC_CR_ADCALDIF;
    ADC1->CR |= ADC_CR_ADCAL;
    if (!ADC_EsperarCR(ADC_CR_ADCAL, 0))
    {
        return 0;
    }

    // ADEN no se puede setear durante 4 ciclos de ADC después de que ADCAL baja
    Retardo_us(1);

    // Habilitación: limpiamos ADRDY (se escribe 1), ADEN = 1 y esperamos ADRDY
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    if (!ADC_EsperarISR(ADC_ISR_ADRDY))
    {
        return 0;
    }
    ADC1->ISR = ADC_ISR_ADRDY;

    // CFGR: alineado a la derecha, conversión única, disparo por software; si hubiera overrun
    // se conserva el dato más nuevo (OVRMOD = 1). La resolución se escribe en cada conversión.
    ADC1->CFGR &= ~(ADC_CFGR_RES | ADC_CFGR_ALIGN | ADC_CFGR_CONT | ADC_CFGR_EXTEN | ADC_CFGR_OVRMOD);
    ADC1->CFGR |= ADC_CFGR_OVRMOD;

    // Tiempo de muestreo del canal 1 (SMP1, bits 5:3 de SMPR1)
    ADC1->SMPR1 &= ~ADC_SMPR1_SMP1;
    ADC1->SMPR1 |= (ADC_SMP_640_5 << ADC_SMPR1_SMP1_Pos);

    // Secuencia regular de una sola conversión (L = 0) con el canal 1 en la primera posición
    ADC1->SQR1 &= ~(ADC_SQR1_L | ADC_SQR1_SQ1);
    ADC1->SQR1 |= ((uint32_t)CANAL_POT << ADC_SQR1_SQ1_Pos);

    adc_listo = 1;
    return 1;
}

// Convierte la resolución en bits (12, 10, 8 o 6) a su código RES[1:0].
// Devuelve 0 si la resolución no es válida.
static uint8_t ADC_CodigoResolucion(int32_t bits, uint32_t *codigo)
{
    switch (bits)
    {
        case 12: *codigo = 0U; return 1;   // 00
        case 10: *codigo = 1U; return 1;   // 01
        case 8:  *codigo = 2U; return 1;   // 10
        case 6:  *codigo = 3U; return 1;   // 11
        default: return 0;
    }
}

// Hace una conversión del potenciómetro con la resolución indicada (código RES[1:0]).
// Bloquea hasta que termina (unos 41 us). Devuelve 1 y el valor en *lectura, o 0 si falla.
static uint8_t ADC_Convertir(uint32_t codigo_res, uint32_t *lectura)
{
    ADC1->CFGR &= ~ADC_CFGR_RES;
    ADC1->CFGR |= (codigo_res << ADC_CFGR_RES_Pos);

    // Limpiamos banderas anteriores (se escribe 1) y arrancamos la conversión
    ADC1->ISR = (ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR);
    ADC1->CR |= ADC_CR_ADSTART;

    if (!ADC_EsperarISR(ADC_ISR_EOC))
    {
        return 0;
    }

    *lectura = ADC1->DR;                // Leer DR limpia EOC
    return 1;
}

// Inicialización del DAC1 canal 1 (PA4). Deja todo configurado pero el canal apagado (EN1 = 0).
void Analog_InitDAC(void)
{
    dac_encendido = 0;

    // Reloj del DAC1 (bus AHB2) y reinicio desde el RCC
    RCC->AHB2ENR |= RCC_AHB2ENR_DAC1EN;
    RCC->AHB2RSTR |= RCC_AHB2RSTR_DAC1RST;
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_DAC1RST;

    // PA4 en analógico antes de habilitar el canal
    GPIO_ConfigAnalogico(GPIOA, PIN_DAC);

    // Canal apagado mientras se configura
    DAC1->CR &= ~DAC_CR_EN1;

    // MCR: MODE1 = 000 (buffer encendido, pin externo) y HFSEL = 00 (AHB a menos de 80 MHz)
    DAC1->MCR &= ~(DAC_MCR_MODE1 | DAC_MCR_HFSEL);

    // CR: sin disparo, sin generador de ondas, sin DMA y fuera de modo calibración
    DAC1->CR &= ~(DAC_CR_TEN1 | DAC_CR_TSEL1 | DAC_CR_WAVE1 | DAC_CR_MAMP1 |
                  DAC_CR_DMAEN1 | DAC_CR_DMAUDRIE1 | DAC_CR_CEN1);

    // Dato inicial en 0
    DAC1->DHR12R1 = 0;

    dac_listo = 1;
}

// Habilita el canal 1 (EN1 = 1) y espera a que la interfaz acepte datos (DAC1RDY).
// Devuelve 1 si quedó listo, 0 si se agotó el tiempo.
static uint8_t DAC_Encender(void)
{
    uint32_t restante = DAC_TIMEOUT;

    DAC1->CR |= DAC_CR_EN1;
    while (!(DAC1->SR & DAC_SR_DAC1RDY))
    {
        if (restante == 0)
        {
            return 0;
        }
        restante--;
    }

    dac_encendido = 1;
    return 1;
}

// Lee un voltaje decimal positivo con hasta 4 decimales (ej. "2", "1.5", "0.25", "3.1000") y lo
// devuelve en décimas de milivoltio. Termina en espacio o fin de cadena.
// Devuelve cuántos caracteres se usaron, o 0 si el texto no es válido.
static uint8_t Analog_LeerVoltaje(const char *str, uint32_t *decimas_mv)
{
    const char *inicio = str;
    uint32_t enteros = 0;
    uint32_t fraccion = 0;
    uint8_t digitos_ent = 0;
    uint8_t digitos_dec = 0;

    while (*str >= '0' && *str <= '9')
    {
        if (digitos_ent >= 2)
        {
            return 0;   // Más de 99 V no tiene sentido y evita desbordes
        }
        enteros = (enteros * 10U) + (uint32_t)(*str - '0');
        digitos_ent++;
        str++;
    }

    if (*str == '.')
    {
        str++;
        while (*str >= '0' && *str <= '9')
        {
            if (digitos_dec >= 4)
            {
                return 0;   // Más de 4 decimales
            }
            fraccion = (fraccion * 10U) + (uint32_t)(*str - '0');
            digitos_dec++;
            str++;
        }
    }

    if ((digitos_ent + digitos_dec) == 0 || (*str != '\0' && *str != ' '))
    {
        return 0;
    }

    // Completamos la fracción a 4 decimales (ej. ".5" -> 5000)
    while (digitos_dec < 4)
    {
        fraccion *= 10U;
        digitos_dec++;
    }

    *decimas_mv = (enteros * 10000U) + fraccion;
    return (uint8_t)(str - inicio);
}

// Imprime un valor en décimas de milivoltio como voltios con 4 decimales (ej. 16502 -> "1.6502")
static void Analog_PrintVoltaje(uint32_t decimas_mv)
{
    uint32_t enteros = decimas_mv / 10000U;
    uint32_t decimales = decimas_mv % 10000U;

    UART_PrintDec((int32_t)enteros);
    UART_Print(".");

    // Ceros a la izquierda para completar los 4 decimales
    if (decimales < 1000U)
    {
        UART_Print("0");
    }
    if (decimales < 100U)
    {
        UART_Print("0");
    }
    if (decimales < 10U)
    {
        UART_Print("0");
    }
    UART_PrintDec((int32_t)decimales);
}

// Comando: VIN [res]
// Conversión AD del potenciómetro con resolución res (6, 8, 10 o 12 bits; default: 8 bits).
// Imprime el voltaje a 4 dígitos de detalle (V con 4 decimales). Blocking.
void Cmd_VIN(char *args)
{
    int32_t bits = 8;
    uint32_t codigo;
    uint32_t lectura;
    uint32_t maximo;
    uint32_t decimas_mv;

    // Resolución opcional
    if (*args != '\0')
    {
        uint8_t usados = ParseDec(args, &bits);
        char *ptr;

        if (usados == 0)
        {
            UART_Print("Error de sintaxis. Uso correcto: VIN [res] (res = 6, 8, 10 o 12)\r\n");
            return;
        }

        ptr = args + usados;
        while (*ptr == ' ')
        {
            ptr++;
        }
        if (*ptr != '\0')
        {
            UART_Print("Error de sintaxis. Uso correcto: VIN [res] (res = 6, 8, 10 o 12)\r\n");
            return;
        }
    }

    if (!ADC_CodigoResolucion(bits, &codigo))
    {
        UART_Print("Error: resolucion no valida. Use 6, 8, 10 o 12 bits.\r\n");
        return;
    }

    // El tick también usa el ADC (POT2SEG), así que se bloquea mientras VIN lo usa.
    // Si el ADC no quedó listo al arrancar, lo reintentamos una vez
    Tick_Bloquear();
    if (!adc_listo && !Analog_InitADC())
    {
        Tick_Desbloquear();
        UART_Print("Error: el ADC no respondio al inicializarse.\r\n");
        return;
    }
    if (!ADC_Convertir(codigo, &lectura))
    {
        Tick_Desbloquear();
        UART_Print("Error: la conversion del ADC no termino.\r\n");
        return;
    }
    Tick_Desbloquear();

    // V = lectura * VREF / (2^res - 1), en décimas de milivoltio y redondeado
    maximo = (1U << bits) - 1U;
    decimas_mv = ((lectura * VREF_DECIMAS_MV) + (maximo / 2U)) / maximo;

    UART_Print("VIN: ");
    Analog_PrintVoltaje(decimas_mv);
    UART_Print(" V  (lectura ");
    UART_PrintDec((int32_t)lectura);
    UART_Print(" de ");
    UART_PrintDec((int32_t)maximo);
    UART_Print(", ");
    UART_PrintDec(bits);
    UART_Print(" bits)\r\n");
}

// Comando: VOUT voltage [res]
// Conversión DA para sacar el nivel voltage (V, hasta 4 decimales) con resolución res
// (8 o 12 bits; default: 8 bits). Limita el valor a 0.2 V - 3.1 V. Blocking.
void Cmd_VOUT(char *args)
{
    uint32_t pedido;
    int32_t bits = 8;
    uint32_t escala;
    uint32_t codigo;
    uint32_t codigo_min;
    uint32_t codigo_max;
    uint32_t salida;
    uint8_t usados;
    char *ptr;

    usados = Analog_LeerVoltaje(args, &pedido);
    if (usados == 0)
    {
        UART_Print("Error de sintaxis. Uso correcto: VOUT voltage [res] (ej. VOUT 1.65 12)\r\n");
        return;
    }

    // Resolución opcional
    ptr = args + usados;
    while (*ptr == ' ')
    {
        ptr++;
    }
    if (*ptr != '\0')
    {
        usados = ParseDec(ptr, &bits);
        if (usados == 0)
        {
            UART_Print("Error de sintaxis. Uso correcto: VOUT voltage [res] (ej. VOUT 1.65 12)\r\n");
            return;
        }
        ptr += usados;
        while (*ptr == ' ')
        {
            ptr++;
        }
        if (*ptr != '\0')
        {
            UART_Print("Error de sintaxis. Uso correcto: VOUT voltage [res] (ej. VOUT 1.65 12)\r\n");
            return;
        }
    }

    if (bits != 8 && bits != 12)
    {
        UART_Print("Error: el DAC solo acepta resolucion de 8 o 12 bits.\r\n");
        return;
    }

    if (!dac_listo)
    {
        Analog_InitDAC();
    }
    if (!dac_encendido && !DAC_Encender())
    {
        UART_Print("Error: el DAC no quedo listo (DAC1RDY).\r\n");
        return;
    }

    // Código = V * 2^res / VREF, redondeado. Los límites se calculan en códigos para que la
    // salida real nunca quede fuera de 0.2 V - 3.1 V: el mínimo se redondea hacia arriba y
    // el máximo hacia abajo.
    escala = 1U << bits;
    codigo = ((pedido * escala) + (VREF_DECIMAS_MV / 2U)) / VREF_DECIMAS_MV;
    codigo_min = ((DAC_MIN_DECIMAS_MV * escala) + VREF_DECIMAS_MV - 1U) / VREF_DECIMAS_MV;
    codigo_max = (DAC_MAX_DECIMAS_MV * escala) / VREF_DECIMAS_MV;

    // Solo se avisa si el voltaje pedido está fuera del rango. Si está justo en el borde (ej. 0.2 V
    // en 12 bits), el redondeo puede dar un código un paso afuera y se corrige sin aviso.
    if (pedido < DAC_MIN_DECIMAS_MV || pedido > DAC_MAX_DECIMAS_MV)
    {
        UART_Print("Aviso: con el buffer encendido el DAC solo da 0.2 V a 3.1 V; se limito el valor.\r\n");
    }
    if (codigo < codigo_min)
    {
        codigo = codigo_min;
    }
    else if (codigo > codigo_max)
    {
        codigo = codigo_max;
    }

    if (bits == 8)
    {
        DAC1->DHR8R1 = codigo;
    }
    else
    {
        DAC1->DHR12R1 = codigo;
    }

    // Voltaje que realmente sale: V = VREF * código / 2^res
    salida = ((codigo * VREF_DECIMAS_MV) + (escala / 2U)) / escala;

    UART_Print("VOUT: ");
    Analog_PrintVoltaje(salida);
    UART_Print(" V  (codigo ");
    UART_PrintDec((int32_t)codigo);
    UART_Print(" de ");
    UART_PrintDec((int32_t)escala);
    UART_Print(", ");
    UART_PrintDec(bits);
    UART_Print(" bits)\r\n");
}

// Tarea de fondo de POT2SEG (desde SysTick_Handler): una conversión de 12 bits al display
void Pot2Seg_Actualizar(void)
{
    uint32_t lectura;

    if (!pot_activo || !adc_listo)
    {
        return;
    }

    if (!ADC_Convertir(ADC_RES_12_BITS, &lectura))
    {
        return;     // Se reintenta en el siguiente periodo
    }

    if (pot_hex)
    {
        Segment_MostrarHex(lectura);
    }
    else
    {
        Segment_MostrarDecimal((int32_t)lectura);
    }
}

uint8_t Pot2Seg_Activo(void)
{
    return pot_activo;
}

// Detiene POT2SEG sin tocar el display (quien llama decide qué mostrar después)
void Pot2Seg_Detener(void)
{
    pot_activo = 0;
}

// Comando: POT2SEG {on|off} [{dec|hex}]
// Arranca o para el despliegue en el display de 7 segmentos de la conversión AD (12 bits) del
// potenciómetro, en decimal (default) o hexadecimal. Opera en paralelo con el monitor (tarea del
// tick cada 20 ms), no bloquea. Con "on" mientras ya está activo, solo cambia el formato.
void Cmd_POT2SEG(char *args)
{
    uint8_t encender;
    uint8_t hex = 0;
    uint8_t largo;
    char *ptr = args;

    if ((largo = Texto_EsPalabra(ptr, "on")) != 0)
    {
        encender = 1;
    }
    else if ((largo = Texto_EsPalabra(ptr, "off")) != 0)
    {
        encender = 0;
    }
    else
    {
        UART_Print("Error de sintaxis. Uso correcto: POT2SEG {on|off} [{dec|hex}]\r\n");
        return;
    }

    ptr += largo;
    while (*ptr == ' ')
    {
        ptr++;
    }

    // Formato opcional (solo con "on")
    if (*ptr != '\0')
    {
        if (encender && (largo = Texto_EsPalabra(ptr, "dec")) != 0)
        {
            hex = 0;
        }
        else if (encender && (largo = Texto_EsPalabra(ptr, "hex")) != 0)
        {
            hex = 1;
        }
        else
        {
            UART_Print("Error de sintaxis. Uso correcto: POT2SEG {on|off} [{dec|hex}]\r\n");
            return;
        }

        ptr += largo;
        while (*ptr == ' ')
        {
            ptr++;
        }
        if (*ptr != '\0')
        {
            UART_Print("Error de sintaxis. Uso correcto: POT2SEG {on|off} [{dec|hex}]\r\n");
            return;
        }
    }

    if (!encender)
    {
        if (pot_activo)
        {
            pot_activo = 0;
            Segment_Apagar();
        }
        UART_Print("POT2SEG: off\r\n");
        return;
    }

    // Encender: el ADC y el display deben estar listos
    if (!adc_listo)
    {
        uint8_t listo;

        Tick_Bloquear();
        listo = Analog_InitADC();
        Tick_Desbloquear();

        if (!listo)
        {
            UART_Print("Error: el ADC no respondio al inicializarse.\r\n");
            return;
        }
    }
    if (!Segment_Asegurar())
    {
        UART_Print("Error: el display no responde (U6, I2C 0x21). Revise la fuente de 9 V.\r\n");
        return;
    }

    pot_hex = hex;
    pot_activo = 1;

    UART_Print("POT2SEG: on (");
    UART_Print(hex ? "hexadecimal" : "decimal");
    UART_Print(", 12 bits, cada 20 ms)\r\n");
}
