/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* startup.c -- the example's start-up on the STM32F446RE: the vector table
 * (the core's 16 entries and the part's 97 interrupts, each a weak alias of
 * a handler that stops), the FPU enabled as CMSIS's SystemInit does, .data
 * copied from flash, .bss cleared, then main. Freestanding: no C library. */
#include <stdint.h>

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
int main(void);

void Reset_Handler(void);
void Default_Handler(void)
{
    for (;;)
        ;
}

#define WEAK(name) void name(void) __attribute__((weak, alias("Default_Handler")))
WEAK(NMI_Handler); WEAK(HardFault_Handler); WEAK(MemManage_Handler); WEAK(BusFault_Handler);
WEAK(UsageFault_Handler); WEAK(SVC_Handler); WEAK(DebugMon_Handler); WEAK(PendSV_Handler);
WEAK(SysTick_Handler); WEAK(USART3_IRQHandler); WEAK(EXTI15_10_IRQHandler);

#define IRQ(n) (16 + (n))
__attribute__((section(".isr_vector"), used)) void (*const vectors[16 + 97])(void) = {
    [0] = (void (*)(void))(uintptr_t)&_estack,
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
    [4] = MemManage_Handler,
    [5] = BusFault_Handler,
    [6] = UsageFault_Handler,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [IRQ(39)] = USART3_IRQHandler,
    [IRQ(40)] = EXTI15_10_IRQHandler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    /* CP10 and CP11 full access: the FPU, as SystemInit enables it */
    *(volatile uint32_t *)0xE000ED88u |= 0xFu << 20;
    __asm volatile("dsb\n\tisb");
    while (dst < &_edata)
        *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss;)
        *dst++ = 0;
    main();
    for (;;)
        ;
}
