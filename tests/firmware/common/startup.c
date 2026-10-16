/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* startup.c -- the test firmware's start-up for an STM32F103: the vector
 * table (the core's 16 entries and the part's 60 interrupts, each a weak
 * alias of a handler that stops), .data copied from flash, .bss cleared,
 * then main. Freestanding: no C library. */
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
WEAK(SysTick_Handler);
WEAK(WWDG_IRQHandler); WEAK(PVD_IRQHandler); WEAK(TAMPER_IRQHandler); WEAK(RTC_IRQHandler);
WEAK(FLASH_IRQHandler); WEAK(RCC_IRQHandler); WEAK(EXTI0_IRQHandler); WEAK(EXTI1_IRQHandler);
WEAK(EXTI2_IRQHandler); WEAK(EXTI3_IRQHandler); WEAK(EXTI4_IRQHandler);
WEAK(DMA1_Channel1_IRQHandler); WEAK(DMA1_Channel2_IRQHandler); WEAK(DMA1_Channel3_IRQHandler);
WEAK(DMA1_Channel4_IRQHandler); WEAK(DMA1_Channel5_IRQHandler); WEAK(DMA1_Channel6_IRQHandler);
WEAK(DMA1_Channel7_IRQHandler); WEAK(ADC1_2_IRQHandler); WEAK(USB_HP_CAN_TX_IRQHandler);
WEAK(USB_LP_CAN_RX0_IRQHandler); WEAK(CAN_RX1_IRQHandler); WEAK(CAN_SCE_IRQHandler);
WEAK(EXTI9_5_IRQHandler); WEAK(TIM1_BRK_IRQHandler); WEAK(TIM1_UP_IRQHandler);
WEAK(TIM1_TRG_COM_IRQHandler); WEAK(TIM1_CC_IRQHandler); WEAK(TIM2_IRQHandler);
WEAK(TIM3_IRQHandler); WEAK(TIM4_IRQHandler); WEAK(I2C1_EV_IRQHandler); WEAK(I2C1_ER_IRQHandler);
WEAK(I2C2_EV_IRQHandler); WEAK(I2C2_ER_IRQHandler); WEAK(SPI1_IRQHandler); WEAK(SPI2_IRQHandler);
WEAK(USART1_IRQHandler); WEAK(USART2_IRQHandler); WEAK(USART3_IRQHandler);
WEAK(EXTI15_10_IRQHandler); WEAK(RTCAlarm_IRQHandler); WEAK(USBWakeup_IRQHandler);

__attribute__((section(".isr_vector"), used)) void (*const vectors[])(void) = {
    (void (*)(void))(uintptr_t)&_estack, Reset_Handler, NMI_Handler, HardFault_Handler,
    MemManage_Handler, BusFault_Handler, UsageFault_Handler, 0, 0, 0, 0, SVC_Handler,
    DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler,
    WWDG_IRQHandler, PVD_IRQHandler, TAMPER_IRQHandler, RTC_IRQHandler, FLASH_IRQHandler,
    RCC_IRQHandler, EXTI0_IRQHandler, EXTI1_IRQHandler, EXTI2_IRQHandler, EXTI3_IRQHandler,
    EXTI4_IRQHandler, DMA1_Channel1_IRQHandler, DMA1_Channel2_IRQHandler,
    DMA1_Channel3_IRQHandler, DMA1_Channel4_IRQHandler, DMA1_Channel5_IRQHandler,
    DMA1_Channel6_IRQHandler, DMA1_Channel7_IRQHandler, ADC1_2_IRQHandler,
    USB_HP_CAN_TX_IRQHandler, USB_LP_CAN_RX0_IRQHandler, CAN_RX1_IRQHandler,
    CAN_SCE_IRQHandler, EXTI9_5_IRQHandler, TIM1_BRK_IRQHandler, TIM1_UP_IRQHandler,
    TIM1_TRG_COM_IRQHandler, TIM1_CC_IRQHandler, TIM2_IRQHandler, TIM3_IRQHandler,
    TIM4_IRQHandler, I2C1_EV_IRQHandler, I2C1_ER_IRQHandler, I2C2_EV_IRQHandler,
    I2C2_ER_IRQHandler, SPI1_IRQHandler, SPI2_IRQHandler, USART1_IRQHandler,
    USART2_IRQHandler, USART3_IRQHandler, EXTI15_10_IRQHandler, RTCAlarm_IRQHandler,
    USBWakeup_IRQHandler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss;)
        *dst++ = 0;
    main();
    for (;;)
        ;
}

/* what the compiler may call */
void *memset(void *d, int c, unsigned n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}
