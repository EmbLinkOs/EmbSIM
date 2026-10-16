/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the nRF52840 DK example, on the registers (no nrfx or SDK):
 *
 *   - CLOCK starts the 32 MHz crystal (HFXO) and the 32.768 kHz crystal
 *     (LFXO);
 *   - RTC1 counts the 32.768 kHz clock; its COMPARE0 every 16384 counts
 *     (half a second) toggles LED1 (P0.13, active low): it blinks at 1 Hz;
 *   - a button from D2 (P1.03) to ground, with the pin's pull-up, is
 *     GPIOTE channel 0's falling edge: each press is counted and printed;
 *   - UARTE0 at 115200 baud on the virtual COM port's pins (TXD P0.06, RXD
 *     P0.08), with EasyDMA: the banner and the replies go out of a RAM
 *     buffer, and each byte comes into a one-byte buffer that ENDRX's short
 *     to STARTRX keeps receiving; what arrives comes back in upper case;
 *   - the FPU computes sqrt(2) by Newton's method for the banner, TIMER0
 *     timing it in microseconds;
 *   - between interrupts the core waits in WFI. */
#include <stdint.h>

#include "nrf52840.h"

static volatile uint32_t presses;
static volatile uint8_t rx_byte;
static volatile uint8_t rx_buf[64];
static volatile uint32_t rx_head, rx_tail;
static char tx_buf[64];

/* EasyDMA reads RAM only: the bytes go through tx_buf */
static void uart_write(const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && i < sizeof tx_buf; i++)
        tx_buf[i] = s[i];
    REG(UARTE_ENDTX) = 0;
    REG(UARTE_TXD_PTR) = (uint32_t)(uintptr_t)tx_buf;
    REG(UARTE_TXD_MAXCNT) = i;
    REG(UARTE_STARTTX) = 1;
    while (!REG(UARTE_ENDTX))
        ;
}

static void puts_(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    uart_write(s, n);
}

/* v in decimal, at least `width` digits */
static void put_dec(uint32_t v, int width)
{
    char b[12];
    int i = 12;
    do {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v || 12 - i < width);
    uart_write(b + i, (uint32_t)(12 - i));
}

static void put_num(uint32_t v) { put_dec(v, 1); }

static void put_hex(uint32_t v)
{
    char b[8];
    int i = 8;
    do {
        b[--i] = "0123456789abcdef"[v & 15u];
        v >>= 4;
    } while (v);
    uart_write(b + i, (uint32_t)(8 - i));
}

static float newton_sqrt(float x)
{
    float r = x;
    int i;
    for (i = 0; i < 8; i++)
        r = 0.5f * (r + x / r);
    return r;
}

void rtc1_irq(void)
{
    REG(RTC_COMPARE0) = 0;
    if (REG(P0 + 0x504u) & (1u << 13))
        REG(GPIO_OUTCLR(P0)) = 1u << 13; /* on */
    else
        REG(GPIO_OUTSET(P0)) = 1u << 13; /* off */
    REG(RTC_CC0) = (REG(RTC_CC0) + 16384u) & 0xffffffu;
}

void gpiote_irq(void)
{
    if (REG(GPIOTE_IN(0))) {
        REG(GPIOTE_IN(0)) = 0;
        presses++;
    }
}

void uarte0_irq(void)
{
    if (REG(UARTE_ENDRX)) {
        REG(UARTE_ENDRX) = 0;
        rx_buf[rx_head % 64] = rx_byte;
        rx_head++;
    }
}

int main(void)
{
    uint32_t shown = 0, us, fixed;
    float r;

    REG(CLOCK_HFCLKSTARTED) = 0;
    REG(CLOCK_HFCLKSTART) = 1;
    while (!REG(CLOCK_HFCLKSTARTED))
        ;
    REG(CLOCK_LFCLKSRC) = 1; /* the crystal */
    REG(CLOCK_LFCLKSTARTED) = 0;
    REG(CLOCK_LFCLKSTART) = 1;
    while (!REG(CLOCK_LFCLKSTARTED))
        ;

    REG(GPIO_OUTSET(P0)) = 1u << 13; /* LED1 off */
    REG(GPIO_DIRSET(P0)) = 1u << 13;

    REG(GPIO_PIN_CNF(P1, 3)) = PIN_CNF_PULLUP; /* D2: input, pulled up */
    REG(GPIOTE_CONFIG(0)) = 1u | (3u << 8) | (1u << 13) | (2u << 16); /* event, P1.03, HiToLo */
    REG(GPIOTE_INTENSET) = 1u;

    REG(UARTE_PSEL_TXD) = 6;
    REG(UARTE_PSEL_RXD) = 8;
    REG(UARTE_BAUDRATE) = 0x01d7e000u; /* 115200 */
    REG(UARTE_CONFIG) = 0;
    REG(UARTE_ENABLE) = 8;
    REG(UARTE_RXD_PTR) = (uint32_t)(uintptr_t)&rx_byte;
    REG(UARTE_RXD_MAXCNT) = 1;
    REG(UARTE_SHORTS) = 1u << 5; /* ENDRX_STARTRX */
    REG(UARTE_INTENSET) = 1u << 4; /* ENDRX */
    REG(UARTE_STARTRX) = 1;

    REG(TIMER_BITMODE) = 3; /* 32 bits */
    REG(TIMER_PRESCALER) = 4; /* 1 MHz */
    REG(TIMER_CLEAR) = 1;
    REG(TIMER_START) = 1;
    r = newton_sqrt(2.0f);
    REG(TIMER_CAPTURE0) = 1;
    us = REG(TIMER_CC0);
    fixed = (uint32_t)(r * 100000.0f + 0.5f);

    puts_("nrf52840: part ");
    put_hex(REG(FICR_INFO_PART));
    puts_(", sqrt(2) = ");
    put_num(fixed / 100000u);
    puts_(".");
    put_dec(fixed % 100000u, 5);
    puts_(" by the FPU in ");
    put_num(us);
    puts_(" us\r\n");

    REG(RTC_PRESCALER) = 0;
    REG(RTC_CC0) = 16384;
    REG(RTC_INTENSET) = 1u << 16; /* COMPARE0 */
    REG(RTC_START) = 1;

    REG(NVIC_ISER0) = (1u << IRQ_UARTE0) | (1u << IRQ_GPIOTE) | (1u << IRQ_RTC1);

    for (;;) {
        __asm__ volatile("cpsid i");
        if (presses == shown && rx_tail == rx_head)
            __asm__ volatile("wfi"); /* a pending interrupt still wakes it */
        __asm__ volatile("cpsie i");
        if (presses != shown) {
            shown = presses;
            puts_("button ");
            put_num(shown);
            puts_("\r\n");
        }
        while (rx_tail != rx_head) {
            char c = (char)rx_buf[rx_tail % 64];
            rx_tail++;
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 32);
            uart_write(&c, 1);
        }
    }
}
