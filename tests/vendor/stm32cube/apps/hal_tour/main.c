/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- ST's STM32F1 HAL on the Blue Pill, as a CubeMX project uses it:
 * the clock tree from the 8 MHz crystal through the PLL to 72 MHz (two
 * flash wait states, APB1 at 36 MHz), HAL_Delay on SysTick (the L LED on
 * PC13 at 2 Hz), a button on PA0 through EXTI0, TIM2's update interrupt
 * at 10 Hz, and USART1 at 115200 baud: lines out with HAL_UART_Transmit,
 * characters in with HAL_UART_Receive_IT, echoed. */
#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"

static UART_HandleTypeDef uart;
static TIM_HandleTypeDef tim;
static volatile unsigned presses, ticks;
static volatile uint32_t last_edge;
static volatile int got = -1;
static uint8_t rx;

static void fail(void)
{
    for (;;) {
    }
}

static void clocks(void)
{
    RCC_OscInitTypeDef osc = { 0 };
    RCC_ClkInitTypeDef clk = { 0 };
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
}

static void pins(void)
{
    GPIO_InitTypeDef g = { 0 };
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_13;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_0;
    g.Mode = GPIO_MODE_IT_RISING_FALLING; /* both edges: the debounce sees the line settle */
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_NVIC_SetPriority(EXTI0_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
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
    HAL_UART_Receive_IT(&uart, &rx, 1);
}

static void timer(void)
{
    /* TIM2 on APB1's doubled clock, 72 MHz: 7200 / 1000 is 10 Hz */
    tim.Instance = TIM2;
    tim.Init.Prescaler = 7200 - 1;
    tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    tim.Init.Period = 1000 - 1;
    tim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&tim) != HAL_OK)
        fail();
    HAL_TIM_Base_Start_IT(&tim);
}

static void say(const char *s)
{
    HAL_UART_Transmit(&uart, (const uint8_t *)s, (uint16_t)strlen(s), 100);
}

int main(void)
{
    char line[96];
    HAL_Init();
    clocks();
    pins();
    serial();
    timer();
    snprintf(line, sizeof line, "EmbSIM STM32 HAL tour, SYSCLK %lu Hz, PCLK1 %lu Hz\r\n",
             (unsigned long)HAL_RCC_GetSysClockFreq(), (unsigned long)HAL_RCC_GetPCLK1Freq());
    say(line);
    for (;;) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(250);
        snprintf(line, sizeof line, "tick=%lu tim2=%u presses=%u\r\n", (unsigned long)HAL_GetTick(), ticks,
                 presses);
        say(line);
        if (got >= 0) {
            snprintf(line, sizeof line, "echo %c\r\n", got);
            got = -1;
            say(line);
        }
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
    g.Pin = GPIO_PIN_9; /* TX */
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_10; /* RX */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *h)
{
    (void)h;
    __HAL_RCC_TIM2_CLK_ENABLE();
    HAL_NVIC_SetPriority(TIM2_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    /* the button's contacts chatter for a few milliseconds when pressed and
     * when released: a press is a falling edge after 20 ms without an edge */
    if (pin == GPIO_PIN_0) {
        uint32_t now = HAL_GetTick();
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET && now - last_edge >= 20u)
            presses++;
        last_edge = now;
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *h)
{
    if (h->Instance == TIM2)
        ticks++;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    got = rx;
    HAL_UART_Receive_IT(h, &rx, 1);
}

void SysTick_Handler(void) { HAL_IncTick(); }
void EXTI0_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0); }
void TIM2_IRQHandler(void) { HAL_TIM_IRQHandler(&tim); }
void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&uart); }
