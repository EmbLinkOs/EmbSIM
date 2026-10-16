/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the Blue Pill acceptance firmware: the LED on PC13 follows the
 * button on PA0, by interrupt, and USART1 says so.
 *
 *   - the clock: HSE (the board's 8 MHz crystal) through the PLL x9: 72 MHz,
 *     APB1 at 36 MHz;
 *   - PA0: input with the internal pull-up; the button pulls it to ground.
 *     EXTI line 0 on both edges raises EXTI0_IRQn;
 *   - PC13: push-pull output, high while the button is pressed. The external
 *     LED (C13 -> 1 k -> LED -> GND) lights then; the board's own LED, lit by
 *     a low PC13, goes out;
 *   - USART1 on PA9 (TX) and PA10 (RX) at 115200 baud: a banner, then
 *     "pressed" and "released" as the main loop sees the changes; each byte
 *     received is echoed back in upper case by the receive interrupt;
 *   - between events the core sleeps in WFI.
 * Freestanding: no C library, no vendor HAL. */
#include <stdint.h>
#include "stm32f103.h"

static volatile int changed, pressed;

static void putc_(char c)
{
    while (!(USART1->SR & (1u << 7))) /* TXE */
        ;
    USART1->DR = (uint8_t)c;
}

static void puts_(const char *s)
{
    while (*s)
        putc_(*s++);
}

static void clock_72mhz(void)
{
    FLASH->ACR = (FLASH->ACR & ~7u) | 2u;           /* two wait states */
    RCC->CR |= 1u << 16;                            /* HSEON */
    while (!(RCC->CR & (1u << 17)))                 /* HSERDY */
        ;
    RCC->CFGR = (4u << 8) | (1u << 16) | (7u << 18); /* APB1 /2, PLL from HSE, x9 */
    RCC->CR |= 1u << 24;                            /* PLLON */
    while (!(RCC->CR & (1u << 25)))                 /* PLLRDY */
        ;
    RCC->CFGR |= 2u;                                /* SW: PLL */
    while ((RCC->CFGR & 0xcu) != 0x8u)              /* SWS: PLL */
        ;
}

static void led(int on)
{
    GPIOC->BSRR = on ? 1u << 13 : 1u << (16 + 13);
}

void USART1_IRQHandler(void)
{
    if (USART1->SR & (1u << 5)) {                   /* RXNE */
        char c = (char)USART1->DR;
        putc_(c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c);
    }
}

void EXTI0_IRQHandler(void)
{
    EXTI->PR = 1u;                                  /* rc_w1 */
    pressed = !(GPIOA->IDR & 1u);                   /* the button pulls PA0 low */
    led(pressed);
    changed = 1;
}

int main(void)
{
    clock_72mhz();
    RCC->APB2ENR |= (1u << 0) | (1u << 2) | (1u << 4) | (1u << 14); /* AFIO, GPIOA, GPIOC, USART1 */

    /* PA0: input with pull-up (CNF 10, ODR 1); PA9: AF push-pull 50 MHz */
    GPIOA->CRL = (GPIOA->CRL & ~0xfu) | 0x8u;
    GPIOA->BSRR = 1u;
    GPIOA->CRH = (GPIOA->CRH & ~(0xfu << 4)) | (0xbu << 4);
    /* PC13: output push-pull 2 MHz, low */
    GPIOC->CRH = (GPIOC->CRH & ~(0xfu << 20)) | (0x2u << 20);
    led(0);

    USART1->BRR = 0x271;                            /* 72 MHz / 115200 */
    USART1->CR1 = (1u << 13) | (1u << 5) | (1u << 3) | (1u << 2); /* UE, RXNEIE, TE, RE */
    puts_("EmbSIM Blue Pill demo\r\n");
    nvic_enable(USART1_IRQn);

    /* EXTI0 from PA0, both edges */
    AFIO->EXTICR[0] &= ~0xfu;
    EXTI->RTSR |= 1u;
    EXTI->FTSR |= 1u;
    EXTI->IMR |= 1u;
    nvic_enable(EXTI0_IRQn);

    for (;;) {
        __asm__ volatile("cpsid i");
        if (changed) {
            int p = pressed;
            changed = 0;
            __asm__ volatile("cpsie i");
            puts_(p ? "pressed\r\n" : "released\r\n");
            continue;
        }
        __asm__ volatile("wfi\n\tcpsie i");
    }
}
