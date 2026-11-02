/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the Pico SDK on the Pico, as its examples use it: stdio on
 * UART0 at 115200 baud (both ways), the clocks as runtime_init sets them,
 * a repeating timer toggling the LED on GP25 every 250 ms, a GPIO
 * interrupt from a button on GP15, core 1 launched and answering through
 * the FIFO, float and double arithmetic and functions (the bootrom's
 * tables), the hardware divider, and the flash erased and programmed
 * through the bootrom's functions and read back through the XIP. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#define BUTTON 15
#define SPARE (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

static volatile unsigned presses, toggles;
static volatile uint32_t last_edge;

static bool blink(repeating_timer_t *t)
{
    (void)t;
    gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
    toggles++;
    return true;
}

static void pressed(uint gpio, uint32_t events)
{
    /* the button's contacts chatter for a few milliseconds when pressed and
     * when released: a press is a falling edge after 20 ms without an edge */
    if (gpio == BUTTON) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((events & GPIO_IRQ_EDGE_FALL) && !gpio_get(BUTTON) && now - last_edge >= 20u)
            presses++;
        last_edge = now;
    }
}

static void core1(void)
{
    for (;;) {
        uint32_t v = multicore_fifo_pop_blocking();
        multicore_fifo_push_blocking(v * v + 1);
    }
}

static void flash_check(void)
{
    static uint8_t page[FLASH_PAGE_SIZE];
    const uint8_t *xip = (const uint8_t *)(XIP_BASE + SPARE);
    uint32_t irq;
    int i, ok = 1;
    for (i = 0; i < FLASH_PAGE_SIZE; i++)
        page[i] = (uint8_t)(i * 7 + 3);
    irq = save_and_disable_interrupts();
    flash_range_erase(SPARE, FLASH_SECTOR_SIZE);
    flash_range_program(SPARE, page, FLASH_PAGE_SIZE);
    restore_interrupts(irq);
    for (i = 0; i < FLASH_PAGE_SIZE; i++)
        ok &= xip[i] == page[i];
    ok &= xip[FLASH_PAGE_SIZE] == 0xff;
    printf("flash %s\n", ok ? "ok" : "BAD");
}

int main(void)
{
    repeating_timer_t timer;
    volatile float f = 2.0f;
    volatile double d = 2.0;
    volatile int32_t num = 1000003, den = 97;
    int i;
    stdio_init_all();
    printf("EmbSIM Pico SDK tour, clk_sys %lu Hz, clk_peri %lu Hz\n", (unsigned long)clock_get_hz(clk_sys),
           (unsigned long)clock_get_hz(clk_peri));
    printf("float %.6f %.6f, double %.9f %.9f\n", (double)sqrtf(f), (double)sinf(f / 4), sqrt(d), exp(d));
    printf("divider %ld r %ld\n", (long)(num / den), (long)(num % den));
    multicore_launch_core1(core1);
    multicore_fifo_push_blocking(12);
    printf("core1 says %lu\n", (unsigned long)multicore_fifo_pop_blocking());
    flash_check();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_init(BUTTON);
    gpio_pull_up(BUTTON);
    gpio_set_irq_enabled_with_callback(BUTTON, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, pressed);
    add_repeating_timer_ms(-250, blink, NULL, &timer);
    for (i = 0;; i++) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT)
            printf("echo %c\n", c);
        if (i % 10 == 0)
            printf("t=%llu ms toggles=%u presses=%u\n", (unsigned long long)(time_us_64() / 1000), toggles,
                   presses);
        sleep_ms(50);
    }
}
