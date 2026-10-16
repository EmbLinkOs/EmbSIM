/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- ST's STM32F1 HAL's SPI and I2C masters on the Blue Pill, as a
 * CubeMX project uses them, against two EEPROMs from the catalog: a
 * 25LC040 on SPI2 (mode 0, 36 MHz / 16, its CS a GPIO on PB12) written
 * with WREN and WRITE, polled with RDSR until its write cycle ends, and
 * read back; a 24C02 at 0x50 on I2C1 (100 kHz) written with
 * HAL_I2C_Mem_Write, polled with HAL_I2C_IsDeviceReady through its write
 * cycle, and read back in reads of one, two and five bytes (the HAL's
 * three receive sequences), and an absent address. Results go out on
 * USART1 at 115200 baud. */
#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"

static UART_HandleTypeDef uart;
static SPI_HandleTypeDef spi;
static I2C_HandleTypeDef i2c;

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

static void buses(void)
{
    GPIO_InitTypeDef g = { 0 };
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_12; /* the EEPROM's CS */
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);

    spi.Instance = SPI2;
    spi.Init.Mode = SPI_MODE_MASTER;
    spi.Init.Direction = SPI_DIRECTION_2LINES;
    spi.Init.DataSize = SPI_DATASIZE_8BIT;
    spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    spi.Init.NSS = SPI_NSS_SOFT;
    spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    spi.Init.TIMode = SPI_TIMODE_DISABLE;
    spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    spi.Init.CRCPolynomial = 10;
    if (HAL_SPI_Init(&spi) != HAL_OK)
        fail();

    i2c.Instance = I2C1;
    i2c.Init.ClockSpeed = 100000;
    i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    i2c.Init.OwnAddress1 = 0;
    i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    i2c.Init.OwnAddress2 = 0;
    i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&i2c) != HAL_OK)
        fail();
}

static void say(const char *s)
{
    HAL_UART_Transmit(&uart, (const uint8_t *)s, (uint16_t)strlen(s), 100);
}

/* one SPI transaction with CS low */
static void ee_spi(const uint8_t *out, uint8_t *in, uint16_t n)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
    if (HAL_SPI_TransmitReceive(&spi, (uint8_t *)out, in, n, 100) != HAL_OK)
        fail();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
}

static void spi_eeprom(void)
{
    static const uint8_t wren[1] = { 0x06 };
    uint8_t wr[2 + 6] = { 0x02, 0x10, 'E', 'm', 'b', 'S', 'I', 'M' };
    uint8_t rd[2 + 6] = { 0x03, 0x10 }, in[8], rdsr[2] = { 0x05, 0 }, sr[2];
    char line[80];
    unsigned polls = 0, wel;
    ee_spi(wren, in, 1);
    ee_spi(rdsr, sr, 2);
    wel = sr[1];
    ee_spi(wr, in, sizeof wr);
    do {
        ee_spi(rdsr, sr, 2);
        polls++;
    } while (sr[1] & 1u);
    ee_spi(rd, in, sizeof rd);
    snprintf(line, sizeof line, "spi: status %02x after WREN, write cycle over after %u polls, read %.6s\r\n", wel,
             polls, (const char *)in + 2);
    say(line);
}

static void i2c_eeprom(void)
{
    static const uint8_t data[5] = { 'H', 'A', 'L', '2', '4' };
    uint8_t one[1] = { 0 }, two[2] = { 0 }, five[6] = { 0 };
    char line[96];
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(&i2c, 0x50 << 1, 0x20, I2C_MEMADD_SIZE_8BIT, (uint8_t *)data, 5, 100);
    uint32_t t0 = HAL_GetTick(), busy = 0;
    snprintf(line, sizeof line, "i2c: write %s\r\n", st == HAL_OK ? "ok" : "failed");
    say(line);
    while (HAL_I2C_IsDeviceReady(&i2c, 0x50 << 1, 1, 10) != HAL_OK)
        busy++;
    snprintf(line, sizeof line, "i2c: ready again after %lu ms (%lu NACKs)\r\n", (unsigned long)(HAL_GetTick() - t0),
             (unsigned long)busy);
    say(line);
    HAL_I2C_Mem_Read(&i2c, 0x50 << 1, 0x20, I2C_MEMADD_SIZE_8BIT, one, 1, 100);
    HAL_I2C_Mem_Read(&i2c, 0x50 << 1, 0x21, I2C_MEMADD_SIZE_8BIT, two, 2, 100);
    HAL_I2C_Mem_Read(&i2c, 0x50 << 1, 0x20, I2C_MEMADD_SIZE_8BIT, five, 5, 100);
    snprintf(line, sizeof line, "i2c: read %c, %c%c, %.5s\r\n", one[0], two[0], two[1], (const char *)five);
    say(line);
    st = HAL_I2C_IsDeviceReady(&i2c, 0x51 << 1, 2, 10);
    snprintf(line, sizeof line, "i2c: 0x51 %s\r\n", st == HAL_OK ? "answers" : "does not answer");
    say(line);
}

int main(void)
{
    HAL_Init();
    clocks();
    serial();
    buses();
    say("EmbSIM STM32 SPI and I2C\r\n");
    spi_eeprom();
    i2c_eeprom();
    say("done\r\n");
    for (;;) {
    }
}

/* ---- the HAL's MSP hooks and the vectors -------------------------------------- */

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
    g.Pin = GPIO_PIN_9;
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_10;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *h)
{
    GPIO_InitTypeDef g = { 0 };
    (void)h;
    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin = GPIO_PIN_13 | GPIO_PIN_15; /* SCK, MOSI */
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_14; /* MISO */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *h)
{
    GPIO_InitTypeDef g = { 0 };
    (void)h;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7; /* SCL, SDA */
    g.Mode = GPIO_MODE_AF_OD;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
    __HAL_RCC_I2C1_CLK_ENABLE();
}

void SysTick_Handler(void) { HAL_IncTick(); }
