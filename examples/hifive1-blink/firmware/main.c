/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the HiFive1 example: the core clock moved to the 16 MHz
 * crystal (the PLL bypassed), the RGB LED's green blinking at 1 Hz from the
 * CLINT's timer interrupt (mtimecmp every 16384 ticks of the 32.768 kHz
 * RTC), a button on D2 (GPIO18, pulled up, falling edge through the PLIC)
 * counted and printed, and what UART0 receives echoed in upper case, at
 * 115200 baud. Between interrupts the core waits in WFI. */
#include "fe310.h"

#define LED_GREEN (1u << 19)
#define BUTTON (1u << 18)
#define UART_PINS ((1u << 16) | (1u << 17))
#define HALF_SECOND 16384u

static volatile uint32_t presses, ticks;
static volatile uint8_t rx_buf[16];
static volatile uint32_t rx_head, rx_tail;

static void put(char c)
{
    while ((int32_t)UART0_TXDATA < 0) /* full */
        ;
    UART0_TXDATA = (uint8_t)c;
}

static void puts_(const char *s)
{
    while (*s)
        put(*s++);
}

static void put_num(uint32_t v)
{
    char b[10];
    int n = 0;
    do {
        b[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        put(b[--n]);
}

static void next_tick(void)
{
    uint64_t t = ((uint64_t)CLINT_MTIMECMP_HI << 32 | CLINT_MTIMECMP_LO) + HALF_SECOND;
    CLINT_MTIMECMP_HI = 0xFFFFFFFFu; /* no spurious match while the halves change */
    CLINT_MTIMECMP_LO = (uint32_t)t;
    CLINT_MTIMECMP_HI = (uint32_t)(t >> 32);
}

__attribute__((interrupt("machine"), aligned(4))) void trap_entry(void)
{
    uint32_t cause;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    if (cause == 0x80000007u) { /* the machine timer */
        GPIO_OUTPUT_VAL ^= LED_GREEN;
        ticks++;
        next_tick();
    } else if (cause == 0x8000000Bu) { /* the PLIC */
        uint32_t src = PLIC_CLAIM;
        if (src == PLIC_SRC_GPIO(18)) {
            GPIO_FALL_IP = BUTTON;
            presses++;
        } else if (src == PLIC_SRC_UART0) {
            int32_t c;
            while ((c = (int32_t)UART0_RXDATA) >= 0) {
                rx_buf[rx_head % 16u] = (uint8_t)c;
                rx_head++;
            }
        }
        PLIC_CLAIM = src;
    }
}

int main(void)
{
    uint32_t seen = 0;
    /* the core clock: HFXOSC through the bypassed PLL, undivided */
    PRCI_PLLCFG = (1u << 17) | (1u << 18); /* pllrefsel, pllbypass */
    PRCI_PLLCFG |= 1u << 16;                /* pllsel */
    PRCI_PLLOUTDIV = 1u << 8;               /* divby1 */
    /* UART0 at 115200 (16 MHz / 139), its pins on IOF0, RX interrupt at one byte */
    GPIO_IOF_SEL &= ~UART_PINS;
    GPIO_IOF_EN |= UART_PINS;
    UART0_DIV = 138;
    UART0_TXCTRL = 1;
    UART0_RXCTRL = 1;
    UART0_IE = 2; /* rxwm */
    /* the green LED off (active low), the button's pull-up and falling edge */
    GPIO_OUTPUT_VAL |= LED_GREEN;
    GPIO_OUTPUT_EN |= LED_GREEN;
    GPIO_PUE |= BUTTON;
    GPIO_INPUT_EN |= BUTTON;
    GPIO_FALL_IP = BUTTON;
    GPIO_FALL_IE |= BUTTON;
    /* the PLIC: the button and UART0 at priority 1 */
    PLIC_PRIORITY(PLIC_SRC_GPIO(18)) = 1;
    PLIC_PRIORITY(PLIC_SRC_UART0) = 1;
    PLIC_ENABLE0 = (1u << PLIC_SRC_UART0) | (1u << PLIC_SRC_GPIO(18));
    PLIC_ENABLE1 = 0;
    PLIC_THRESHOLD = 0;
    /* the timer: the first half second from now */
    CLINT_MTIMECMP_HI = CLINT_MTIME_HI;
    CLINT_MTIMECMP_LO = CLINT_MTIME_LO;
    next_tick();
    __asm__ volatile("csrs mie, %0" ::"r"((1u << 7) | (1u << 11))); /* MTIE, MEIE */
    __asm__ volatile("csrsi mstatus, 8");                           /* MIE */
    puts_("EmbSIM HiFive1\r\n");
    for (;;) {
        __asm__ volatile("wfi");
        if (presses != seen) {
            seen = presses;
            puts_("button ");
            put_num(seen);
            puts_("\r\n");
        }
        while (rx_tail != rx_head) {
            char c = (char)rx_buf[rx_tail % 16u];
            rx_tail++;
            put(c >= 'a' && c <= 'z' ? (char)(c - 32) : c);
        }
    }
}
