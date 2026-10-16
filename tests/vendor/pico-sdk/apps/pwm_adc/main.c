/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the Pico SDK's hardware_pwm and hardware_adc on the Pico:
 * slice 0's channels on GP16 and GP17 at 1 kHz (125 MHz / 125 / 1000;
 * 25 % and 75 %), its wrap interrupt counted; ADC2 (GP28, the board's
 * 1.0 V source) and the temperature sensor read with adc_read; ADC0 (GP16's
 * PWM through an RC low-pass on GP26) read too, its duty raised to 75 %
 * after a second; the FIFO filled by free-running conversions, round robin
 * over ADC0 and ADC2, drained by the ADC's interrupt. Each line goes out on
 * UART0 at 115200 baud. */
#include <stdio.h>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

static volatile unsigned wraps, fifo_reads;
static volatile uint16_t fifo_last[2];

static void on_wrap(void)
{
    pwm_clear_irq(0);
    wraps++;
}

static void on_fifo(void)
{
    while (!adc_fifo_is_empty()) {
        uint16_t v = adc_fifo_get();
        fifo_last[fifo_reads & 1u] = v;
        fifo_reads++;
    }
}

/* 0.1 V, rounded */
static unsigned tenths(uint16_t code) { return ((code * 3300u + 2048u) / 4096u + 50u) / 100u; }

int main(void)
{
    pwm_config cfg = pwm_get_default_config();
    unsigned i;
    stdio_init_all();

    gpio_set_function(16, GPIO_FUNC_PWM);
    gpio_set_function(17, GPIO_FUNC_PWM);
    pwm_config_set_clkdiv_int(&cfg, 125);
    pwm_config_set_wrap(&cfg, 999);
    pwm_init(0, &cfg, false);
    pwm_set_chan_level(0, PWM_CHAN_A, 250);
    pwm_set_chan_level(0, PWM_CHAN_B, 750);
    pwm_clear_irq(0);
    pwm_set_irq_enabled(0, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, on_wrap);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    pwm_set_enabled(0, true);

    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(28);
    adc_set_temp_sensor_enabled(true);
    printf("EmbSIM Pico PWM and ADC, clk_sys %lu Hz\n", (unsigned long)clock_get_hz(clk_sys));

    for (i = 0; i < 10; i++) {
        uint16_t pa, rc, t;
        sleep_ms(100);
        adc_select_input(2);
        pa = adc_read();
        adc_select_input(0);
        rc = adc_read();
        adc_select_input(4);
        t = adc_read();
        printf("ms=%lu wraps=%u adc2=%u temp=%u rc=%u.%uV\n", (unsigned long)to_ms_since_boot(get_absolute_time()),
               wraps, pa, t, tenths(rc) / 10u, tenths(rc) % 10u);
        if (i == 4)
            pwm_set_chan_level(0, PWM_CHAN_A, 750);
    }

    /* free-running conversions, round robin over ADC0 and ADC2, into the
     * FIFO; its interrupt at two entries */
    adc_select_input(0);
    adc_set_round_robin(0x5);
    adc_fifo_setup(true, false, 2, false, false);
    adc_set_clkdiv(47999.0f); /* 1 kHz: 48 MHz / 48000 */
    irq_set_exclusive_handler(ADC_IRQ_FIFO, on_fifo);
    adc_irq_set_enabled(true);
    irq_set_enabled(ADC_IRQ_FIFO, true);
    adc_run(true);
    sleep_ms(50);
    adc_run(false);
    irq_set_enabled(ADC_IRQ_FIFO, false);
    printf("fifo: %u conversions, last rc=%u.%uV adc2=%u\n", fifo_reads, tenths(fifo_last[0]) / 10u,
           tenths(fifo_last[0]) % 10u, fifo_last[1]);
    printf("done\n");
    for (;;)
        tight_loop_contents();
}
