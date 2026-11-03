/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the Pico SDK's PIO and DMA, as its examples use them:
 *
 *   - PIO0 SM0 runs serial_tx (serial.pio) on GP2 at 115200 baud: a line
 *     written to its TX FIFO goes out as a UART would send it;
 *   - PIO0 SM1 runs square on GP3, its clock divided to 64 kHz: a 1 kHz
 *     square wave, its edges counted through the SIO for 20 ms;
 *   - a DMA channel paced by PIO0 SM0's TX request feeds serial_tx a line;
 *   - DMA copies 1 KiB word by word; another paced by UART0's TX request
 *     sends a line to the console; a chain of two copies; a channel's
 *     completion interrupt on DMA_IRQ_0;
 *   - the sniffer's CRC-32 (bit-reversed data, the result reversed and
 *     inverted) of "123456789": the standard check value 0xcbf43926;
 *   - the ADC's FIFO read by DMA paced by its request: GP28's 1.0 V. */
#include <stdio.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "serial.pio.h"

#define TX_PIN 2
#define SQ_PIN 3

static uint32_t src[256], dst[256], mid[256], end[256];
static volatile unsigned done_irqs;
static int irq_chan;

static void dma_done(void)
{
    if (dma_channel_get_irq0_status((uint)irq_chan)) {
        dma_channel_acknowledge_irq0((uint)irq_chan);
        done_irqs++;
    }
}

static void serial_tx_init(PIO pio, uint sm, uint offset, uint pin, uint baud)
{
    pio_sm_config c = serial_tx_program_get_default_config(offset);
    pio_sm_set_pins_with_mask(pio, sm, 1u << pin, 1u << pin);
    pio_sm_set_pindirs_with_mask(pio, sm, 1u << pin, 1u << pin);
    pio_gpio_init(pio, pin);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (8.0f * (float)baud));
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(10);
    printf("pio_dma\n");

    /* PIO: a serial line out of GP2 */
    uint tx_off = pio_add_program(pio0, &serial_tx_program);
    serial_tx_init(pio0, 0, tx_off, TX_PIN, 115200);
    for (const char *p = "pio says hello\n"; *p; p++)
        pio_sm_put_blocking(pio0, 0, (uint32_t)*p);

    /* PIO: a square wave on GP3, its edges counted */
    uint sq_off = pio_add_program(pio0, &square_program);
    pio_sm_config c = square_program_get_default_config(sq_off);
    sm_config_set_set_pins(&c, SQ_PIN, 1);
    pio_gpio_init(pio0, SQ_PIN);
    pio_sm_set_consecutive_pindirs(pio0, 1, SQ_PIN, 1, true);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 64000.0f);
    pio_sm_init(pio0, 1, sq_off, &c);
    pio_sm_set_enabled(pio0, 1, true);
    {
        absolute_time_t until = make_timeout_time_ms(20);
        bool last = gpio_get(SQ_PIN);
        unsigned edges = 0;
        while (!time_reached(until)) {
            bool now = gpio_get(SQ_PIN);
            if (now != last)
                edges++;
            last = now;
        }
        printf("square edges %u in 20 ms\n", edges);
    }

    /* DMA: a copy */
    for (int i = 0; i < 256; i++)
        src[i] = 0x01010101u * (uint32_t)i ^ 0x5a5a0000u;
    int ch = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, true);
    dma_channel_configure((uint)ch, &dc, dst, src, 256, true);
    dma_channel_wait_for_finish_blocking((uint)ch);
    printf("dma copy %s\n", memcmp(src, dst, sizeof src) == 0 ? "ok" : "bad");

    /* DMA: a chain of two copies, the second's completion an interrupt */
    int a = dma_claim_unused_channel(true), b = dma_claim_unused_channel(true);
    irq_chan = b;
    irq_set_exclusive_handler(DMA_IRQ_0, dma_done);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_set_irq0_enabled((uint)b, true);
    dma_channel_config cb = dma_channel_get_default_config((uint)b);
    channel_config_set_read_increment(&cb, true);
    channel_config_set_write_increment(&cb, true);
    dma_channel_configure((uint)b, &cb, end, mid, 256, false);
    dma_channel_config ca = dma_channel_get_default_config((uint)a);
    channel_config_set_read_increment(&ca, true);
    channel_config_set_write_increment(&ca, true);
    channel_config_set_chain_to(&ca, (uint)b);
    dma_channel_configure((uint)a, &ca, mid, src, 256, true);
    while (!done_irqs)
        tight_loop_contents();
    printf("dma chain %s, irqs %u\n", memcmp(src, end, sizeof src) == 0 ? "ok" : "bad", done_irqs);

    /* DMA: the sniffer's CRC-32 of "123456789" */
    {
        static const char check[] = "123456789";
        static uint8_t sink;
        int sc = dma_claim_unused_channel(true);
        dma_channel_config cc = dma_channel_get_default_config((uint)sc);
        channel_config_set_transfer_data_size(&cc, DMA_SIZE_8);
        channel_config_set_read_increment(&cc, true);
        channel_config_set_write_increment(&cc, false);
        channel_config_set_sniff_enable(&cc, true);
        dma_sniffer_enable((uint)sc, DMA_SNIFF_CTRL_CALC_VALUE_CRC32R, true);
        dma_sniffer_set_output_reverse_enabled(true);
        dma_sniffer_set_output_invert_enabled(true);
        dma_sniffer_set_data_accumulator(0xffffffffu);
        dma_channel_configure((uint)sc, &cc, &sink, check, 9, true);
        dma_channel_wait_for_finish_blocking((uint)sc);
        printf("crc32 %08lx\n", (unsigned long)dma_sniffer_get_data_accumulator());
        dma_sniffer_disable();
    }

    /* DMA paced by UART0's TX request: a line to the console */
    {
        static const char line[] = "dma to the uart\n";
        int uc = dma_claim_unused_channel(true);
        dma_channel_config cu = dma_channel_get_default_config((uint)uc);
        channel_config_set_transfer_data_size(&cu, DMA_SIZE_8);
        channel_config_set_read_increment(&cu, true);
        channel_config_set_write_increment(&cu, false);
        channel_config_set_dreq(&cu, uart_get_dreq(uart0, true));
        dma_channel_configure((uint)uc, &cu, &uart_get_hw(uart0)->dr, line, sizeof line - 1, true);
        dma_channel_wait_for_finish_blocking((uint)uc);
        uart_tx_wait_blocking(uart0);
    }

    /* DMA paced by PIO0 SM0's TX request: serial_tx fed a line */
    {
        static uint32_t words[32];
        static const char line[] = "dma feeds the pio\n";
        size_t n = strlen(line);
        for (size_t i = 0; i < n; i++)
            words[i] = (uint32_t)line[i];
        int pc = dma_claim_unused_channel(true);
        dma_channel_config cp = dma_channel_get_default_config((uint)pc);
        channel_config_set_transfer_data_size(&cp, DMA_SIZE_32);
        channel_config_set_read_increment(&cp, true);
        channel_config_set_write_increment(&cp, false);
        channel_config_set_dreq(&cp, pio_get_dreq(pio0, 0, true));
        dma_channel_configure((uint)pc, &cp, &pio0->txf[0], words, (uint)n, true);
        dma_channel_wait_for_finish_blocking((uint)pc);
        printf("pio fed by dma\n");
    }

    /* DMA paced by the ADC's request: eight samples of GP28 */
    {
        static uint16_t samples[8];
        adc_init();
        adc_gpio_init(28);
        adc_select_input(2);
        adc_fifo_setup(true, true, 1, false, false);
        int ac = dma_claim_unused_channel(true);
        dma_channel_config cd = dma_channel_get_default_config((uint)ac);
        channel_config_set_transfer_data_size(&cd, DMA_SIZE_16);
        channel_config_set_read_increment(&cd, false);
        channel_config_set_write_increment(&cd, true);
        channel_config_set_dreq(&cd, DREQ_ADC);
        dma_channel_configure((uint)ac, &cd, samples, &adc_hw->fifo, 8, true);
        adc_run(true);
        dma_channel_wait_for_finish_blocking((uint)ac);
        adc_run(false);
        unsigned sum = 0;
        for (int i = 0; i < 8; i++)
            sum += samples[i];
        printf("adc by dma %u\n", sum / 8u);
    }
    printf("done\n");
    for (;;)
        tight_loop_contents();
}
