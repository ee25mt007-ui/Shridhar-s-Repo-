
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include "tm4c123gh6pm.h"
#include "arm_math.h"

#define SAMPLE_COUNT    1024
#define SAMPLING_FREQ   10240.0f
#define FUND_FREQ       50.0f
#define SYSCLK          16000000UL
#define KNOWN_SEC_PEAK  17.0f     /* Secondary peak voltage */

volatile uint16_t adcBuffer[SAMPLE_COUNT];
volatile int sampleIndex = 0;
volatile int samplingDone = 0;

float32_t input_adc_volts[SAMPLE_COUNT];
float32_t input_grid_volts[SAMPLE_COUNT];
float32_t fft_output[SAMPLE_COUNT];
float32_t magnitude[SAMPLE_COUNT/2];

void UART0_Init(void)
{
    SYSCTL_RCGCUART_R |= 1;
    SYSCTL_RCGCGPIO_R |= 1;
    while ((SYSCTL_PRGPIO_R & 1) == 0) {}

    UART0_CTL_R &= ~1;
    UART0_IBRD_R = 8;       /* 115200 baud */
    UART0_FBRD_R = 44;
    UART0_LCRH_R = 0x70;    /* 8N1 */
    UART0_CTL_R = 0x301;    /* Enable UART */

    GPIO_PORTA_AFSEL_R |= 0x03;
    GPIO_PORTA_PCTL_R = (GPIO_PORTA_PCTL_R & ~0xFF) | 0x11;
    GPIO_PORTA_DEN_R |= 0x03;
}

void UART0_SendChar(char c)
{
    while (UART0_FR_R & 0x20) {}
    UART0_DR_R = c;
}

void UART0_SendString(const char *s)
{
    while (*s) UART0_SendChar(*s++);
}


void Debug_GPIO_Init(void)
{
    SYSCTL_RCGCGPIO_R |= (1 << 5);
    while (!(SYSCTL_PRGPIO_R & (1 << 5))) {}
    GPIO_PORTF_DIR_R |= (1 << 1);
    GPIO_PORTF_DEN_R |= (1 << 1);
}

void Debug_Toggle(void)
{
    GPIO_PORTF_DATA_R ^= (1 << 1);
}

void ADC0_Init_TimerTrigger(void)
{
    SYSCTL_RCGCGPIO_R |= (1 << 4);
    SYSCTL_RCGCADC_R |= 1;
    while (!(SYSCTL_PRGPIO_R & (1 << 4))) {}

    GPIO_PORTE_AFSEL_R |= (1 << 3);
    GPIO_PORTE_DEN_R &= ~(1 << 3);
    GPIO_PORTE_AMSEL_R |= (1 << 3);

    ADC0_PC_R = 1;
    ADC0_SSPRI_R = 0x0123;

    ADC0_ACTSS_R &= ~0x08;
    ADC0_EMUX_R = (ADC0_EMUX_R & ~0xF000) | 0x5000; /* Timer trigger */

    ADC0_SSMUX3_R = 0;
    ADC0_SSCTL3_R = 0x06;

    ADC0_IM_R |= (1 << 3);
    NVIC_EN0_R |= (1 << 17);

    ADC0_ACTSS_R |= 0x08;
}


void Timer0A_Init(void)
{
    SYSCTL_RCGCTIMER_R |= 1;
    while (!(SYSCTL_PRTIMER_R & 1)) {}

    TIMER0_CTL_R &= ~1;
    TIMER0_CFG_R = 0;
    TIMER0_TAMR_R = 0x02; /* Periodic */

    TIMER0_TAILR_R = (SYSCLK / (uint32_t)SAMPLING_FREQ) - 1;
    TIMER0_TAPR_R = 0;

    TIMER0_CTL_R |= (1 << 5); /* ADC trigger */
    TIMER0_CTL_R |= 1;        /* Start */
}


void ADC0Seq3_Handler(void)
{
    ADC0_ISC_R = 0x08;
    adcBuffer[sampleIndex++] = ADC0_SSFIFO3_R & 0x0FFF;

    if (sampleIndex >= SAMPLE_COUNT) {
        sampleIndex = 0;
        samplingDone = 1;
    }
}

/* Convert ADC counts to voltage */
static inline float adc_counts_to_volts(uint16_t c)
{
    return (c * 3.3f) / 4096.0f;
}


void ComputeFFT_and_Print(void)
{
    int i, h;
    char buf[128];

    float adc_min = 1e9f, adc_max = -1e9f;

    /* ADC to volts + min/max */
    for (i = 0; i < SAMPLE_COUNT; i++) {
        input_adc_volts[i] = adc_counts_to_volts(adcBuffer[i]);
        if (input_adc_volts[i] < adc_min) adc_min = input_adc_volts[i];
        if (input_adc_volts[i] > adc_max) adc_max = input_adc_volts[i];
    }

    float adc_pp = adc_max - adc_min;
    float adc_peak = adc_pp * 0.5f;
    float adc_offset = (adc_max + adc_min) * 0.5f;
    float attenuation = adc_peak / KNOWN_SEC_PEAK;
    if (attenuation < 1e-9f) attenuation = 1e-9f;

    const float PRIMARY_SCALE = 230.0f / 12.0f;


    for (i = 0; i < SAMPLE_COUNT; i++) {
        float vsec = (input_adc_volts[i] - adc_offset) / attenuation;
        input_grid_volts[i] = vsec * PRIMARY_SCALE;
    }

  
    arm_rfft_fast_instance_f32 S;
    arm_rfft_fast_init_f32(&S, SAMPLE_COUNT);
    arm_rfft_fast_f32(&S, input_grid_volts, fft_output, 0);


    for (i = 0; i < SAMPLE_COUNT/2; i++) {
        float re = fft_output[2*i];
        float im = fft_output[2*i + 1];
        magnitude[i] = sqrtf(re*re + im*im);
    }


    sprintf(buf, "\r\nADC min=%.3f max=%.3f offset=%.3f\r\n", adc_min, adc_max, adc_offset);
    UART0_SendString(buf);


    float binWidth = SAMPLING_FREQ / SAMPLE_COUNT;
    float scale_to_vrms = (2.0f / SAMPLE_COUNT) / sqrtf(2.0f);

    UART0_SendString("FFT bins:\r\n");
    for (i = 0; i < 50; i++) {
        float vrms = magnitude[i] * scale_to_vrms;
        sprintf(buf, "Bin %d (%.1f Hz): %.5f Vrms\r\n", i, i * binWidth, vrms);
        UART0_SendString(buf);
    }


    UART0_SendString("\r\nHarmonics:\r\n");
    float fundVrms = 0, harmSumSq = 0;

    for (h = 1; h <= 25; h++) {
        int bin = (int)((FUND_FREQ * h) / binWidth + 0.5f);
        float Vrms = magnitude[bin] * scale_to_vrms;
        sprintf(buf, "%d: %.1f Hz = %.5f Vrms\r\n", h, FUND_FREQ*h, Vrms);
        UART0_SendString(buf);
        if (h == 1) fundVrms = Vrms; else harmSumSq += Vrms*Vrms;
    }

    sprintf(buf, "THD = %.2f %%\r\n", (sqrtf(harmSumSq)/fundVrms)*100);
    UART0_SendString(buf);
}


int main(void)
{
    UART0_Init();
    Debug_GPIO_Init();
    ADC0_Init_TimerTrigger();
    Timer0A_Init();

    UART0_SendString("Grid FFT Analyzer Ready\r\n");

    while (1) {
        if (samplingDone) {
            samplingDone = 0;
            Debug_Toggle();
            ComputeFFT_and_Print();
            Debug_Toggle();
        }
    }
}
