#include "hw_comun.h"
#include "analog.h"
#include "display.h"
#include "tick.h"
#include "monitor.h"

/* ADC1 (potenciómetro en PA0), DAC1 (PA4) y POT2SEG */

#define PIN_POT             0       /* PA0 */
#define CANAL_POT           1       /* ADC1_IN1 */

/* Muestreo de 640.5 ciclos a 16 MHz = 40 us; con los 12.5 ciclos de conversión
   son 653 ciclos = unos 41 us por lectura */
#define ADC_SMP_640_5       7U

/* Referencia de 3.3 V en décimas de mV: 3.3 V = 3300 mV = 33000 décimas (4 decimales al imprimir) */
#define VREF_DECIMAS_MV     33000U

/* Espera del regulador del ADC */
#define ADC_ARRANQUE_US     100U

/* Límite de las esperas */
#define ADC_TIMEOUT         100000U

#define PIN_DAC             4       /* PA4 */

/* Rango del DAC con buffer, en décimas de mV: 0.2 V = 2000; 3.3 V - 0.2 V = 3.1 V = 31000 */
#define DAC_MIN_DECIMAS_MV  2000U
#define DAC_MAX_DECIMAS_MV  (VREF_DECIMAS_MV - 2000U)

#define DAC_TIMEOUT         100000U

static uint8_t adc_listo = 0;
static uint8_t dac_listo = 0;
static uint8_t dac_encendido = 0;

/* Estado de POT2SEG */
static volatile uint8_t pot_activo = 0;
static volatile uint8_t pot_hex = 0;        /* 1 = hexadecimal */

#define ADC_RES_12_BITS     0U

/* Espera un valor en ADC1->CR: 1 si se cumplió */
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

/* Espera una bandera de ADC1->ISR: 1 si se encendió */
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

/* Inicialización del ADC1 */
uint8_t Analog_InitADC(void)
{
    adc_listo = 0;

    /* Reloj y reset del ADC */
    RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN;
    RCC->AHB2RSTR |= RCC_AHB2RSTR_ADC12RST;
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_ADC12RST;

    /* Reloj síncrono del ADC */
    ADC12_COMMON->CCR &= ~ADC_CCR_CKMODE;
    ADC12_COMMON->CCR |= ADC_CCR_CKMODE_0;

    /* Pin */
    GPIO_ConfigAnalogico(GPIOA, PIN_POT);

    /* Regulador del ADC */
    ADC1->CR &= ~ADC_CR_DEEPPWD;
    ADC1->CR |= ADC_CR_ADVREGEN;
    Retardo_us(ADC_ARRANQUE_US);

    /* Canal single-ended */
    ADC1->DIFSEL &= ~(1U << CANAL_POT);

    /* Calibración */
    ADC1->CR &= ~ADC_CR_ADCALDIF;
    ADC1->CR |= ADC_CR_ADCAL;
    if (!ADC_EsperarCR(ADC_CR_ADCAL, 0))
    {
        return 0;
    }

    Retardo_us(1);

    /* Habilitación */
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    if (!ADC_EsperarISR(ADC_ISR_ADRDY))
    {
        return 0;
    }
    ADC1->ISR = ADC_ISR_ADRDY;

    /* Conversión única por software */
    ADC1->CFGR &= ~(ADC_CFGR_RES | ADC_CFGR_ALIGN | ADC_CFGR_CONT | ADC_CFGR_EXTEN | ADC_CFGR_OVRMOD);
    ADC1->CFGR |= ADC_CFGR_OVRMOD;

    /* Tiempo de muestreo */
    ADC1->SMPR1 &= ~ADC_SMPR1_SMP1;
    ADC1->SMPR1 |= (ADC_SMP_640_5 << ADC_SMPR1_SMP1_Pos);

    /* Secuencia de un canal */
    ADC1->SQR1 &= ~(ADC_SQR1_L | ADC_SQR1_SQ1);
    ADC1->SQR1 |= ((uint32_t)CANAL_POT << ADC_SQR1_SQ1_Pos);

    adc_listo = 1;
    return 1;
}

/* Código RES de la resolución */
static uint8_t ADC_CodigoResolucion(int32_t bits, uint32_t *codigo)
{
    switch (bits)
    {
        case 12: *codigo = 0U; return 1;
        case 10: *codigo = 1U; return 1;
        case 8:  *codigo = 2U; return 1;
        case 6:  *codigo = 3U; return 1;
        default: return 0;
    }
}

/* Una conversión con la resolución indicada */
static uint8_t ADC_Convertir(uint32_t codigo_res, uint32_t *lectura)
{
    ADC1->CFGR &= ~ADC_CFGR_RES;
    ADC1->CFGR |= (codigo_res << ADC_CFGR_RES_Pos);

    /* Banderas y arranque */
    ADC1->ISR = (ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR);
    ADC1->CR |= ADC_CR_ADSTART;

    if (!ADC_EsperarISR(ADC_ISR_EOC))
    {
        return 0;
    }

    *lectura = ADC1->DR;
    return 1;
}

/* Inicialización del DAC1 (canal apagado) */
void Analog_InitDAC(void)
{
    dac_encendido = 0;

    /* Reloj y reset del DAC */
    RCC->AHB2ENR |= RCC_AHB2ENR_DAC1EN;
    RCC->AHB2RSTR |= RCC_AHB2RSTR_DAC1RST;
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_DAC1RST;

    /* Pin */
    GPIO_ConfigAnalogico(GPIOA, PIN_DAC);

    /* Canal apagado */
    DAC1->CR &= ~DAC_CR_EN1;

    /* Modo normal con buffer */
    DAC1->MCR &= ~(DAC_MCR_MODE1 | DAC_MCR_HFSEL);

    /* Sin disparo ni DMA */
    DAC1->CR &= ~(DAC_CR_TEN1 | DAC_CR_TSEL1 | DAC_CR_WAVE1 | DAC_CR_MAMP1 |
                  DAC_CR_DMAEN1 | DAC_CR_DMAUDRIE1 | DAC_CR_CEN1);

    /* Dato inicial */
    DAC1->DHR12R1 = 0;

    dac_listo = 1;
}

/* Habilita el canal del DAC */
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

/* Lee un voltaje con hasta 4 decimales (en décimas de mV) */
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
            return 0;
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
                return 0;
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

    /* Fracción a 4 decimales: ".5" -> 5 -> 5000, así 1.5 V = 1 x 10000 + 5000 = 15000 décimas de mV */
    while (digitos_dec < 4)
    {
        fraccion *= 10U;
        digitos_dec++;
    }

    *decimas_mv = (enteros * 10000U) + fraccion;
    return (uint8_t)(str - inicio);
}

/* Imprime décimas de mV como voltios */
static void Analog_PrintVoltaje(uint32_t decimas_mv)
{
    uint32_t enteros = decimas_mv / 10000U;
    uint32_t decimales = decimas_mv % 10000U;

    UART_PrintDec((int32_t)enteros);
    UART_Print(".");

    /* Ceros de los decimales */
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

/* Comando: VIN [res] */
void Cmd_VIN(char *args)
{
    int32_t bits = 8;
    uint32_t codigo;
    uint32_t lectura;
    uint32_t maximo;
    uint32_t decimas_mv;

    /* Resolución */
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

    /* Conversión con el tick bloqueado */
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

    /* Cálculo del voltaje:
       V = lectura x 3.3 V / (2^res - 1)
       En décimas de mV: lectura x 33000 / maximo; se suma maximo / 2 para redondear.
       Ej. 8 bits, lectura 128: 128 x 33000 / 255 = 16565 -> 1.6565 V */
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

/* Comando: VOUT voltage [res] */
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

    /* Resolución */
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

    /* Cálculo del código del DAC:
       código = V x 2^res / 3.3 V, redondeado (se suma 33000 / 2 antes de dividir).
       Ej. 1.65 V en 12 bits: 16500 x 4096 / 33000 = 2048
       El mínimo (0.2 V) se redondea hacia arriba y el máximo (3.1 V) hacia abajo,
       para que la salida nunca quede fuera del rango. */
    escala = 1U << bits;
    codigo = ((pedido * escala) + (VREF_DECIMAS_MV / 2U)) / VREF_DECIMAS_MV;
    codigo_min = ((DAC_MIN_DECIMAS_MV * escala) + VREF_DECIMAS_MV - 1U) / VREF_DECIMAS_MV;
    codigo_max = (DAC_MAX_DECIMAS_MV * escala) / VREF_DECIMAS_MV;

    /* Límite de 0.2 V a 3.1 V */
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

    /* Voltaje de salida: V = código x 3.3 V / 2^res (en décimas de mV, redondeado).
       Ej. 2048 en 12 bits: 2048 x 33000 / 4096 = 16500 -> 1.6500 V */
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

/* Tarea de fondo de POT2SEG */
void Pot2Seg_Actualizar(void)
{
    uint32_t lectura;

    if (!pot_activo || !adc_listo)
    {
        return;
    }

    if (!ADC_Convertir(ADC_RES_12_BITS, &lectura))
    {
        return;
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

/* Detiene POT2SEG */
void Pot2Seg_Detener(void)
{
    pot_activo = 0;
}

uint8_t Analog_ADCListo(void)
{
    return adc_listo;
}

/* Lectura de 12 bits para otros módulos */
uint8_t Analog_LeerPot12(uint32_t *lectura)
{
    if (!adc_listo)
    {
        return 0;
    }
    return ADC_Convertir(ADC_RES_12_BITS, lectura);
}

/* Comando: POT2SEG {on|off} [{dec|hex}] */
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

    /* Formato */
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

    /* ADC y display listos */
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
