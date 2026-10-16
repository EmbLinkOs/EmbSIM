/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* startup.c -- the example's start-up on the RP2040's core 0: the vector
 * table right after boot2 (the core's 16 entries and the part's 26
 * interrupts, each a weak alias of a handler that stops), .data copied
 * from flash, .bss cleared, then main. Freestanding: no C library. */
#include <stdint.h>

extern uint32_t __stack_top, __data_load, __data_start, __data_end, __bss_start, __bss_end;
int main(void);

void reset_handler(void);
void default_handler(void)
{
    for (;;)
        ;
}

#define WEAK(name) void name(void) __attribute__((weak, alias("default_handler")))
WEAK(nmi_handler); WEAK(hardfault_handler); WEAK(svc_handler); WEAK(pendsv_handler); WEAK(systick_handler);
WEAK(timer_irq_0); WEAK(timer_irq_1); WEAK(timer_irq_2); WEAK(timer_irq_3); WEAK(pwm_irq_wrap);
WEAK(usbctrl_irq); WEAK(xip_irq); WEAK(pio0_irq_0); WEAK(pio0_irq_1); WEAK(pio1_irq_0); WEAK(pio1_irq_1);
WEAK(dma_irq_0); WEAK(dma_irq_1); WEAK(io_irq_bank0); WEAK(io_irq_qspi); WEAK(sio_irq_proc0);
WEAK(sio_irq_proc1); WEAK(clocks_irq); WEAK(spi0_irq); WEAK(spi1_irq); WEAK(uart0_irq); WEAK(uart1_irq);
WEAK(adc_irq_fifo); WEAK(i2c0_irq); WEAK(i2c1_irq); WEAK(rtc_irq);

__attribute__((section(".vectors"), used)) void (*const vectors[])(void) = {
    (void (*)(void))(uintptr_t)&__stack_top, reset_handler, nmi_handler, hardfault_handler,
    0, 0, 0, 0, 0, 0, 0, svc_handler, 0, 0, pendsv_handler, systick_handler,
    timer_irq_0, timer_irq_1, timer_irq_2, timer_irq_3, pwm_irq_wrap, usbctrl_irq, xip_irq,
    pio0_irq_0, pio0_irq_1, pio1_irq_0, pio1_irq_1, dma_irq_0, dma_irq_1, io_irq_bank0,
    io_irq_qspi, sio_irq_proc0, sio_irq_proc1, clocks_irq, spi0_irq, spi1_irq, uart0_irq,
    uart1_irq, adc_irq_fifo, i2c0_irq, i2c1_irq, rtc_irq,
};

void reset_handler(void)
{
    uint32_t *src = &__data_load, *dst = &__data_start;
    while (dst < &__data_end)
        *dst++ = *src++;
    for (dst = &__bss_start; dst < &__bss_end;)
        *dst++ = 0;
    main();
    for (;;)
        ;
}
