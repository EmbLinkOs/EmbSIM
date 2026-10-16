/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the Pico SDK's hardware_spi and hardware_i2c on the Pico,
 * against two EEPROMs from the catalog: a 25LC040 on SPI0 (GP4 RX, GP6
 * SCK, GP7 TX; its CS a GPIO on GP5, as the SDK's examples have it; mode
 * 0 at about 2 MHz) written with WREN and WRITE, polled with RDSR through
 * its write cycle and read back; a 24C02 at 0x50 on I2C0 (GP20 SDA, GP21
 * SCL, 400 kHz) written with i2c_write_blocking, polled through its write
 * cycle with address-only writes that it NACKs, read back with a write
 * without STOP and a read (a repeated START), and an absent address.
 * Each line goes out on UART0 at 115200 baud. */
#include <stdio.h>
#include <string.h>

#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#define EE_CS 5

static void ee_spi(const uint8_t *out, uint8_t *in, size_t n)
{
    gpio_put(EE_CS, 0);
    spi_write_read_blocking(spi0, out, in, n);
    gpio_put(EE_CS, 1);
}

static void spi_eeprom(void)
{
    static const uint8_t wren[1] = { 0x06 };
    uint8_t wr[2 + 6] = { 0x02, 0x30, 'R', 'P', '2', '0', '4', '0' };
    uint8_t rd[2 + 6] = { 0x03, 0x30 }, in[8], rdsr[2] = { 0x05, 0 }, sr[2];
    unsigned polls = 0, wel;
    uint baud = spi_init(spi0, 2000000);
    gpio_set_function(4, GPIO_FUNC_SPI);
    gpio_set_function(6, GPIO_FUNC_SPI);
    gpio_set_function(7, GPIO_FUNC_SPI);
    gpio_init(EE_CS);
    gpio_put(EE_CS, 1);
    gpio_set_dir(EE_CS, GPIO_OUT);
    printf("spi: %u Hz\r\n", baud);
    ee_spi(wren, in, 1);
    ee_spi(rdsr, sr, 2);
    wel = sr[1];
    ee_spi(wr, in, sizeof wr);
    do {
        ee_spi(rdsr, sr, 2);
        polls++;
    } while (sr[1] & 1u);
    ee_spi(rd, in, sizeof rd);
    printf("spi: status %02x after WREN, write cycle over after %u polls, read %.6s\r\n", wel, polls,
           (const char *)in + 2);
    /* spi_write_blocking leaves no overrun behind */
    ee_spi(rdsr, sr, 2);
    gpio_put(EE_CS, 0);
    spi_write_blocking(spi0, wr, 2);
    gpio_put(EE_CS, 1);
    printf("spi: busy %d, readable %d after spi_write_blocking\r\n", spi_is_busy(spi0), spi_is_readable(spi0));
}

static void i2c_eeprom(void)
{
    static const uint8_t page[1 + 5] = { 0x40, 'S', 'D', 'K', '2', '4' };
    uint8_t at = 0x40, one = 0, five[6] = { 0 };
    unsigned busy = 0;
    uint32_t t0;
    int n;
    uint baud = i2c_init(i2c0, 400000);
    gpio_set_function(20, GPIO_FUNC_I2C);
    gpio_set_function(21, GPIO_FUNC_I2C);
    gpio_pull_up(20);
    gpio_pull_up(21);
    printf("i2c: %u Hz\r\n", baud);
    n = i2c_write_blocking(i2c0, 0x50, page, sizeof page, false);
    t0 = time_us_32();
    printf("i2c: wrote %d\r\n", n);
    while (i2c_write_blocking(i2c0, 0x50, &at, 1, false) < 0)
        busy++;
    printf("i2c: ready again after %lu us (%u NACKs)\r\n", (unsigned long)((time_us_32() - t0 + 50u) / 100u * 100u),
           busy);
    /* the pointer is at 0x40: a current-address read, then a random read */
    n = i2c_read_blocking(i2c0, 0x50, &one, 1, false);
    printf("i2c: current %d %c\r\n", n, one);
    at = 0x41;
    i2c_write_blocking(i2c0, 0x50, &at, 1, true);
    n = i2c_read_blocking(i2c0, 0x50, five, 4, false);
    printf("i2c: random %d %.4s\r\n", n, (const char *)five);
    n = i2c_read_blocking(i2c0, 0x51, &one, 1, false);
    printf("i2c: 0x51 %s (%d)\r\n", n == PICO_ERROR_GENERIC ? "does not answer" : "answers", n);
}

int main(void)
{
    stdio_init_all();
    printf("EmbSIM Pico SPI and I2C\r\n");
    spi_eeprom();
    i2c_eeprom();
    printf("done\r\n");
    for (;;)
        tight_loop_contents();
}
