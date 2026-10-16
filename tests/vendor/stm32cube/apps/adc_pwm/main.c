/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- ST's STM32F1 HAL's ADC and timer PWM on the Blue Pill, as a
 * CubeMX project uses them: 72 MHz from the crystal, the ADC at 12 MHz
 * (PCLK2 / 6) and calibrated; TIM3's channels 1 and 2 as PWM at 1 kHz on
 * PA6 and PA7 (25 % and 75 %); ADC1 reading channel 4 (PA4, the board's
 * 1.0 V source) by polling, and channel 1 (PA1, PA6's PWM through an RC
 * low-pass) by its end-of-conversion interrupt; after a second, channel
 * 1's duty becomes 75 %. Each reading goes out on USART1 at 115200 baud,
 * the filtered voltage to 0.1 V. */
#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"

static UART_HandleTypeDef uart;
static TIM_HandleTypeDef pwm;
static ADC_HandleTypeDef adc;
static volatile int done;
static volatile uint32_t value;

static void fail(void)
{
    for (;;) {
    }
}

static void clocks(void)
{
    RCC_OscInitTypeDef osc = { 0 };
    RCC_ClkInitTypeDef clk = { 0 };
    RCC_PeriphCLKInitTypeDef per = { 0 };
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        fail();
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK)
        fail();
    per.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    per.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&per) != HAL_OK)
        fail();
}

static void serial(void)
{
    uart.Instance = USART1;
    uart.Init.BaudRate = 115200;
    uart.Init.WordLength = UART_WORDLENGTH_8B;
    uart.Init.StopBits = UART_STOPBITS_1;
    uart.Init.Parity = UART_PARITY_NONE;
    uart.Init.Mode = UART_MODE_TX_RX;
    uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&uart) != HAL_OK)
        fail();
}

static void pwm_start(void)
{
    TIM_OC_InitTypeDef oc = { 0 };
    /* TIM3 on APB1's doubled clock, 72 MHz: 72 to 1 MHz, 1000 counts a period */
    pwm.Instance = TIM3;
    pwm.Init.Prescaler = 72 - 1;
    pwm.Init.CounterMode = TIM_COUNTERMODE_UP;
    pwm.Init.Period = 1000 - 1;
    pwm.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    pwm.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&pwm) != HAL_OK)
        fail();
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.Pulse = 250;
    if (HAL_TIM_PWM_ConfigChannel(&pwm, &oc, TIM_CHANNEL_1) != HAL_OK)
        fail();
    oc.Pulse = 750;
    if (HAL_TIM_PWM_ConfigChannel(&pwm, &oc, TIM_CHANNEL_2) != HAL_OK)
        fail();
    HAL_TIM_PWM_Start(&pwm, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&pwm, TIM_CHANNEL_2);
}

static void adc_init(void)
{
    adc.Instance = ADC1;
    adc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    adc.Init.ContinuousConvMode = DISABLE;
    adc.Init.DiscontinuousConvMode = DISABLE;
    adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    adc.Init.NbrOfConversion = 1;
    if (HAL_ADC_Init(&adc) != HAL_OK)
        fail();
    if (HAL_ADCEx_Calibration_Start(&adc) != HAL_OK)
        fail();
}

static void channel(uint32_t ch)
{
    ADC_ChannelConfTypeDef c = { 0 };
    c.Channel = ch;
    c.Rank = ADC_REGULAR_RANK_1;
    c.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&adc, &c) != HAL_OK)
        fail();
}

static uint32_t polled(uint32_t ch)
{
    channel(ch);
    HAL_ADC_Start(&adc);
    if (HAL_ADC_PollForConversion(&adc, 10) != HAL_OK)
        fail();
    return HAL_ADC_GetValue(&adc);
}

static uint32_t by_interrupt(uint32_t ch)
{
    channel(ch);
    done = 0;
    HAL_ADC_Start_IT(&adc);
    while (!done)
        __WFI();
    return value;
}

static void say(const char *s)
{
    HAL_UART_Transmit(&uart, (const uint8_t *)s, (uint16_t)strlen(s), 100);
}

int main(void)
{
    char line[96];
    unsigned n = 0;
    HAL_Init();
    clocks();
    serial();
    pwm_start();
    adc_init();
    snprintf(line, sizeof line, "EmbSIM STM32 ADC and PWM, ADC clock %lu Hz\r\n",
             (unsigned long)HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADC));
    say(line);
    for (;;) {
        uint32_t direct = polled(ADC_CHANNEL_4), rc = by_interrupt(ADC_CHANNEL_1);
        unsigned dv = (unsigned)((rc * 3300u + 2048u) / 4096u + 50u) / 100u; /* 0.1 V, rounded */
        snprintf(line, sizeof line, "t=%lu pa4=%lu rc=%u.%uV\r\n", (unsigned long)HAL_GetTick(),
                 (unsigned long)direct, dv / 10u, dv % 10u);
        say(line);
        if (++n == 10)
            __HAL_TIM_SET_COMPARE(&pwm, TIM_CHANNEL_1, 750);
        HAL_Delay(100);
    }
}

/* ---- the HAL's MSP hooks and callbacks, and the vectors ---------------------- */

void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_UART_MspInit(UART_HandleTypeDef *h)
{
    GPIO_InitTypeDef g = { 0 };
    (void)h;
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin = GPIO_PIN_9; /* TX */
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_10; /* RX */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);
}

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *h)
{
    GPIO_InitTypeDef g = { 0 };
    (void)h;
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7; /* TIM3_CH1, TIM3_CH2 */
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *h)
{
    GPIO_InitTypeDef g = { 0 };
    (void)h;
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin = GPIO_PIN_1 | GPIO_PIN_4;
    g.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_NVIC_SetPriority(ADC1_2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(ADC1_2_IRQn);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h)
{
    value = HAL_ADC_GetValue(h);
    done = 1;
}

void SysTick_Handler(void) { HAL_IncTick(); }
void ADC1_2_IRQHandler(void) { HAL_ADC_IRQHandler(&adc); }
