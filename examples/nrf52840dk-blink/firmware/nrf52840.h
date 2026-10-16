/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* nrf52840.h -- the nRF52840 registers the example uses, by address (the
 * nRF52840 Product Specification v1.1). Tasks are triggered by writing 1,
 * events cleared by writing 0. */
#ifndef NRF52840_H
#define NRF52840_H

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define CLOCK 0x40000000u
#define CLOCK_HFCLKSTART (CLOCK + 0x000u)
#define CLOCK_LFCLKSTART (CLOCK + 0x008u)
#define CLOCK_HFCLKSTARTED (CLOCK + 0x100u)
#define CLOCK_LFCLKSTARTED (CLOCK + 0x104u)
#define CLOCK_LFCLKSRC (CLOCK + 0x518u)

#define P0 0x50000000u
#define P1 0x50000300u
#define GPIO_OUTSET(p) ((p) + 0x508u)
#define GPIO_OUTCLR(p) ((p) + 0x50cu)
#define GPIO_IN(p) ((p) + 0x510u)
#define GPIO_DIRSET(p) ((p) + 0x518u)
#define GPIO_PIN_CNF(p, n) ((p) + 0x700u + 4u * (n))
#define PIN_CNF_PULLUP (3u << 2)

#define GPIOTE 0x40006000u
#define GPIOTE_IN(n) (GPIOTE + 0x100u + 4u * (n))
#define GPIOTE_INTENSET (GPIOTE + 0x304u)
#define GPIOTE_CONFIG(n) (GPIOTE + 0x510u + 4u * (n))

#define RTC1 0x40011000u
#define RTC_START (RTC1 + 0x000u)
#define RTC_COMPARE0 (RTC1 + 0x140u)
#define RTC_INTENSET (RTC1 + 0x304u)
#define RTC_COUNTER (RTC1 + 0x504u)
#define RTC_PRESCALER (RTC1 + 0x508u)
#define RTC_CC0 (RTC1 + 0x540u)

#define TIMER0 0x40008000u
#define TIMER_START (TIMER0 + 0x000u)
#define TIMER_CLEAR (TIMER0 + 0x00cu)
#define TIMER_CAPTURE0 (TIMER0 + 0x040u)
#define TIMER_BITMODE (TIMER0 + 0x508u)
#define TIMER_PRESCALER (TIMER0 + 0x510u)
#define TIMER_CC0 (TIMER0 + 0x540u)

#define UARTE0 0x40002000u
#define UARTE_STARTRX (UARTE0 + 0x000u)
#define UARTE_STARTTX (UARTE0 + 0x008u)
#define UARTE_ENDRX (UARTE0 + 0x110u)
#define UARTE_ENDTX (UARTE0 + 0x120u)
#define UARTE_SHORTS (UARTE0 + 0x200u)
#define UARTE_INTENSET (UARTE0 + 0x304u)
#define UARTE_ENABLE (UARTE0 + 0x500u)
#define UARTE_PSEL_TXD (UARTE0 + 0x50cu)
#define UARTE_PSEL_RXD (UARTE0 + 0x514u)
#define UARTE_BAUDRATE (UARTE0 + 0x524u)
#define UARTE_RXD_PTR (UARTE0 + 0x534u)
#define UARTE_RXD_MAXCNT (UARTE0 + 0x538u)
#define UARTE_TXD_PTR (UARTE0 + 0x544u)
#define UARTE_TXD_MAXCNT (UARTE0 + 0x548u)
#define UARTE_CONFIG (UARTE0 + 0x56cu)

#define FICR_INFO_PART 0x10000100u

#define SCB_CPACR 0xe000ed88u
#define NVIC_ISER0 0xe000e100u

/* the interrupts the example takes */
#define IRQ_UARTE0 2
#define IRQ_GPIOTE 6
#define IRQ_RTC1 17

#endif
