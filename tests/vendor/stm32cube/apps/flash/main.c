/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- ST's STM32F1 HAL programming the Blue Pill's flash, as an
 * application keeping its settings does: the option bytes read with
 * HAL_FLASHEx_OBGetConfig; the last page erased with HAL_FLASHEx_Erase,
 * then a halfword, a word and a doubleword programmed with
 * HAL_FLASH_Program and read back, each operation timed on the DWT's
 * cycle counter at 72 MHz (the core runs from the flash, so it waits out
 * each one); a halfword over a programmed one refused with
 * HAL_FLASH_ERROR_PROG. Then the option bytes erased and the user data
 * byte 0 programmed with HAL_FLASHEx_OBProgram, and HAL_FLASH_OB_Launch's
 * reset: the second boot finds Data0 loaded and the page kept. */
#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"

#define PAGE 0x0800FC00u /* the last 1 KiB page of the C8's 64 KiB */

static UART_HandleTypeDef uart;

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

static void say(const char *s)
{
    HAL_UART_Transmit(&uart, (const uint8_t *)s, (uint16_t)strlen(s), 100);
}

/* microseconds since `c0` on the cycle counter at 72 MHz */
static unsigned long us_since(uint32_t c0) { return (unsigned long)((DWT->CYCCNT - c0) / 72u); }

static void program(uint32_t type, uint32_t a, uint64_t v, const char *what)
{
    char line[96];
    uint32_t c0 = DWT->CYCCNT;
    HAL_StatusTypeDef st = HAL_FLASH_Program(type, a, v);
    snprintf(line, sizeof line, "%s %s in %lu us\r\n", what, st == HAL_OK ? "ok" : "failed", us_since(c0));
    say(line);
}

int main(void)
{
    char line[128];
    FLASH_OBProgramInitTypeDef ob = { 0 };
    FLASH_EraseInitTypeDef er = { 0 };
    uint32_t bad = 0, c0;
    HAL_StatusTypeDef st;
    HAL_Init();
    clocks();
    serial();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    HAL_FLASHEx_OBGetConfig(&ob);
    snprintf(line, sizeof line, "EmbSIM STM32F1 flash: RDP 0x%02lx, USER 0x%02lx, WRP 0x%08lx, Data0 0x%02lx\r\n",
             (unsigned long)ob.RDPLevel, (unsigned long)ob.USERConfig, (unsigned long)ob.WRPPage,
             (unsigned long)HAL_FLASHEx_OBGetUserData(OB_DATA_ADDRESS_DATA0));
    say(line);
    if (HAL_FLASHEx_OBGetUserData(OB_DATA_ADDRESS_DATA0) == 0x42u) {
        snprintf(line, sizeof line, "second boot: Data0 0x42, the page kept: %04x %08lx %08lx%08lx\r\n",
                 *(volatile uint16_t *)PAGE, (unsigned long)*(volatile uint32_t *)(PAGE + 4),
                 (unsigned long)*(volatile uint32_t *)(PAGE + 12), (unsigned long)*(volatile uint32_t *)(PAGE + 8));
        say(line);
        say("done\r\n");
        for (;;) {
        }
    }
    HAL_FLASH_Unlock();
    er.TypeErase = FLASH_TYPEERASE_PAGES;
    er.PageAddress = PAGE;
    er.NbPages = 1;
    c0 = DWT->CYCCNT;
    st = HAL_FLASHEx_Erase(&er, &bad);
    snprintf(line, sizeof line, "erase %s in %lu us, first word %08lx\r\n", st == HAL_OK ? "ok" : "failed",
             us_since(c0), (unsigned long)*(volatile uint32_t *)PAGE);
    say(line);
    program(FLASH_TYPEPROGRAM_HALFWORD, PAGE, 0xBEEFu, "halfword");
    program(FLASH_TYPEPROGRAM_WORD, PAGE + 4, 0x12345678u, "word");
    program(FLASH_TYPEPROGRAM_DOUBLEWORD, PAGE + 8, 0x0123456789ABCDEFull, "doubleword");
    snprintf(line, sizeof line, "read back %04x %08lx %08lx%08lx\r\n", *(volatile uint16_t *)PAGE,
             (unsigned long)*(volatile uint32_t *)(PAGE + 4), (unsigned long)*(volatile uint32_t *)(PAGE + 12),
             (unsigned long)*(volatile uint32_t *)(PAGE + 8));
    say(line);
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, PAGE, 0x1234u);
    snprintf(line, sizeof line, "over a programmed halfword: %s, error 0x%lx, still %04x\r\n",
             st == HAL_OK ? "ok" : "refused", (unsigned long)HAL_FLASH_GetError(), *(volatile uint16_t *)PAGE);
    say(line);
    HAL_FLASH_OB_Unlock();
    c0 = DWT->CYCCNT;
    st = HAL_FLASHEx_OBErase();
    snprintf(line, sizeof line, "option bytes erased %s in %lu us\r\n", st == HAL_OK ? "ok" : "failed",
             us_since(c0));
    say(line);
    memset(&ob, 0, sizeof ob);
    ob.OptionType = OPTIONBYTE_DATA;
    ob.DATAAddress = OB_DATA_ADDRESS_DATA0;
    ob.DATAData = 0x42u;
    st = HAL_FLASHEx_OBProgram(&ob);
    snprintf(line, sizeof line, "Data0 programmed %s; launching\r\n", st == HAL_OK ? "ok" : "failed");
    say(line);
    HAL_Delay(2); /* the line's last frame out before the reset */
    HAL_FLASH_OB_Launch();
    for (;;) {
    }
}

/* ---- the HAL's MSP hooks, and the vectors ---------------------- */

void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_UART_MspInit(UART_HandleTypeDef *h)
{
    GPIO_InitTypeDef g = { 0 };
    (void)h;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    g.Pin = GPIO_PIN_9; /* TX */
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_10; /* RX */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);
}

void SysTick_Handler(void) { HAL_IncTick(); }
