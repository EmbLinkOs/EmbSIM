/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* startup.c -- the example's start-up on the nRF52840: the vector table
 * (the core's 16 entries and the part's 48 interrupts, each a weak alias of
 * a handler that stops), .data copied from flash, .bss cleared, the FPU
 * enabled, then main. Freestanding: no C library. */
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
WEAK(nmi_handler); WEAK(hardfault_handler); WEAK(memmanage_handler); WEAK(busfault_handler);
WEAK(usagefault_handler); WEAK(svc_handler); WEAK(debugmon_handler); WEAK(pendsv_handler);
WEAK(systick_handler); WEAK(power_clock_irq); WEAK(radio_irq); WEAK(uarte0_irq); WEAK(spim0_irq);
WEAK(spim1_irq); WEAK(nfct_irq); WEAK(gpiote_irq); WEAK(saadc_irq); WEAK(timer0_irq); WEAK(timer1_irq);
WEAK(timer2_irq); WEAK(rtc0_irq); WEAK(temp_irq); WEAK(rng_irq); WEAK(ecb_irq); WEAK(ccm_irq);
WEAK(wdt_irq); WEAK(rtc1_irq); WEAK(qdec_irq); WEAK(comp_irq); WEAK(egu0_irq); WEAK(egu1_irq);
WEAK(egu2_irq); WEAK(egu3_irq); WEAK(egu4_irq); WEAK(egu5_irq); WEAK(timer3_irq); WEAK(timer4_irq);
WEAK(pwm0_irq); WEAK(pdm_irq); WEAK(mwu_irq); WEAK(pwm1_irq); WEAK(pwm2_irq); WEAK(spim2_irq);
WEAK(rtc2_irq); WEAK(i2s_irq); WEAK(fpu_irq); WEAK(usbd_irq); WEAK(uarte1_irq); WEAK(qspi_irq);
WEAK(cryptocell_irq); WEAK(pwm3_irq); WEAK(spim3_irq);

__attribute__((section(".vectors"), used)) void (*const vectors[])(void) = {
    (void (*)(void))(uintptr_t)&__stack_top, reset_handler, nmi_handler, hardfault_handler,
    memmanage_handler, busfault_handler, usagefault_handler, 0, 0, 0, 0, svc_handler,
    debugmon_handler, 0, pendsv_handler, systick_handler,
    power_clock_irq, radio_irq, uarte0_irq, spim0_irq, spim1_irq, nfct_irq, gpiote_irq, saadc_irq,
    timer0_irq, timer1_irq, timer2_irq, rtc0_irq, temp_irq, rng_irq, ecb_irq, ccm_irq, wdt_irq,
    rtc1_irq, qdec_irq, comp_irq, egu0_irq, egu1_irq, egu2_irq, egu3_irq, egu4_irq, egu5_irq,
    timer3_irq, timer4_irq, pwm0_irq, pdm_irq, 0, 0, mwu_irq, pwm1_irq, pwm2_irq, spim2_irq,
    rtc2_irq, i2s_irq, fpu_irq, usbd_irq, uarte1_irq, qspi_irq, cryptocell_irq, 0, 0, pwm3_irq, 0,
    spim3_irq,
};

void reset_handler(void)
{
    uint32_t *src = &__data_load, *dst = &__data_start;
    *(volatile uint32_t *)0xe000ed88u |= 0xfu << 20; /* CPACR: CP10 and CP11, full access */
    __asm__ volatile("dsb\n\tisb");
    while (dst < &__data_end)
        *dst++ = *src++;
    for (dst = &__bss_start; dst < &__bss_end;)
        *dst++ = 0;
    main();
    for (;;)
        ;
}
