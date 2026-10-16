/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the Pico example, on the registers (no SDK):
 *
 *   - the clocks as the SDK sets them up: the 12 MHz crystal, PLL_SYS at
 *     1500 MHz / 6 / 2 = 125 MHz for clk_sys and clk_peri, clk_ref from the
 *     crystal, and the watchdog's tick at 1 us; the frequency counter
 *     measures clk_sys for the banner;
 *   - the LED on GPIO25 toggles every 500 ms from the timer's alarm 0, so
 *     it blinks at 1 Hz;
 *   - a button from GP15 to ground (the pad's pull-up) interrupts on its
 *     falling edge through IO_BANK0, and each press is counted;
 *   - UART0 at 115200 baud on GP0 (TX) and GP1 (RX): what arrives comes
 *     back in upper case; numbers are printed with the SIO's divider (the
 *     M0+ has no divide instruction);
 *   - between interrupts the core waits in WFI. */
#include <stdint.h>

#include "rp2040.h"

static volatile uint32_t presses;
static volatile uint8_t rx_buf[64];
static volatile uint32_t rx_head, rx_tail;
static uint32_t next_alarm;

static void putc_(char c)
{
    while (REG(UART_FR) & FR_TXFF)
        ;
    REG(UART_DR) = (uint8_t)c;
}

static void puts_(const char *s)
{
    while (*s)
        putc_(*s++);
}

/* n / d by the SIO's divider: its result is ready 8 cycles after the
 * divisor is written */
static uint32_t udiv(uint32_t n, uint32_t d, uint32_t *rem)
{
    REG(SIO_DIV_UDIVIDEND) = n;
    REG(SIO_DIV_UDIVISOR) = d;
    while (!(REG(SIO_DIV_CSR) & 1u))
        ;
    *rem = REG(SIO_DIV_REMAINDER);
    return REG(SIO_DIV_QUOTIENT);
}

static void put_num(uint32_t v)
{
    char b[12];
    int i = 0;
    do {
        uint32_t r;
        v = udiv(v, 10, &r);
        b[i++] = (char)('0' + r);
    } while (v);
    while (i)
        putc_(b[--i]);
}

static void unreset(uint32_t bits)
{
    REG(RESETS_RESET + 0x3000u) = bits; /* the clear alias */
    while ((REG(RESETS_DONE) & bits) != bits)
        ;
}

static void clocks_init(void)
{
    REG(XOSC_CTRL) = 0xaa0u;            /* 1-15 MHz */
    REG(XOSC_STARTUP) = 47;             /* about 1 ms */
    SET(XOSC_CTRL) = 0xfabu << 12;      /* ENABLE */
    while (!(REG(XOSC_STATUS) & (1u << 31)))
        ;
    REG(CLK_REF_CTRL) = 2;              /* clk_ref from the crystal */
    while (REG(CLK_REF_SELECTED) != (1u << 2))
        ;
    REG(CLK_SYS_CTRL) = 0;              /* clk_sys from clk_ref while the PLL starts */
    while (REG(CLK_SYS_SELECTED) != 1u)
        ;
    unreset(RESET_PLL_SYS);
    REG(PLL_SYS_CS) = 1;                /* REFDIV 1 */
    REG(PLL_SYS_FBDIV) = 125;           /* VCO 1500 MHz */
    CLR(PLL_SYS_PWR) = (1u << 0) | (1u << 5); /* PD, VCOPD */
    while (!(REG(PLL_SYS_CS) & (1u << 31)))
        ;
    REG(PLL_SYS_PRIM) = (6u << 16) | (2u << 12);
    CLR(PLL_SYS_PWR) = 1u << 3;         /* POSTDIVPD */
    REG(CLK_SYS_CTRL) = 0;              /* AUXSRC pll_sys */
    REG(CLK_SYS_CTRL) = 1;              /* SRC the aux source */
    while (REG(CLK_SYS_SELECTED) != (1u << 1))
        ;
    REG(CLK_PERI_CTRL) = (1u << 11);    /* ENABLE, from clk_sys */
    REG(WATCHDOG_TICK) = (1u << 9) | 12; /* 1 us from 12 MHz */
}

static uint32_t clk_sys_khz(void)
{
    REG(FC0_REF_KHZ) = 12000;
    REG(FC0_INTERVAL) = 10;
    REG(FC0_MIN_KHZ) = 0;
    REG(FC0_MAX_KHZ) = 0x1ffffffu;
    REG(FC0_SRC) = 9;                   /* clk_sys */
    while (!(REG(FC0_STATUS) & (1u << 4)))
        ;
    return REG(FC0_RESULT) >> 5;
}

void timer_irq_0(void)
{
    REG(TIMER_INTR) = 1;
    REG(SIO_GPIO_OUT_XOR) = 1u << 25;
    next_alarm += 500000;
    REG(TIMER_ALARM0) = next_alarm;
}

void io_irq_bank0(void)
{
    if (REG(IO_INTR(1)) & (1u << 30)) { /* GPIO15 EDGE_LOW */
        REG(IO_INTR(1)) = 1u << 30;
        presses++;
    }
}

void uart0_irq(void)
{
    while (!(REG(UART_FR) & FR_RXFE)) {
        rx_buf[rx_head % 64] = (uint8_t)REG(UART_DR);
        rx_head++;
    }
}

int main(void)
{
    uint32_t shown = 0;
    clocks_init();
    unreset(RESET_IO_BANK0 | RESET_PADS_BANK0 | RESET_UART0 | RESET_TIMER | RESET_SYSINFO);

    REG(GPIO_CTRL(25)) = FUNC_SIO;
    REG(SIO_GPIO_OUT_CLR) = 1u << 25;
    REG(SIO_GPIO_OE_SET) = 1u << 25;

    CLR(PAD(15)) = PAD_PDE;
    SET(PAD(15)) = PAD_PUE | PAD_IE;
    REG(GPIO_CTRL(15)) = FUNC_SIO;
    SET(IO_PROC0_INTE(1)) = 1u << 30;

    REG(GPIO_CTRL(0)) = FUNC_UART;
    REG(GPIO_CTRL(1)) = FUNC_UART;
    REG(UART_IBRD) = 67;                /* 125 MHz / (16 x 115200) = 67.8 */
    REG(UART_FBRD) = 52;
    REG(UART_LCR_H) = (3u << 5) | (1u << 4); /* 8 bits, FIFOs */
    REG(UART_CR) = (1u << 0) | (1u << 8) | (1u << 9);
    REG(UART_IMSC) = (1u << 4) | (1u << 6); /* RX, RX timeout */

    puts_("pico: RP2040 B");
    put_num(REG(SYSINFO_CHIP_ID) >> 28);
    puts_(", clk_sys ");
    put_num(clk_sys_khz());
    puts_(" kHz\r\n");

    next_alarm = REG(TIMER_RAWL) + 500000;
    REG(TIMER_ALARM0) = next_alarm;
    REG(TIMER_INTE) = 1;
    REG(NVIC_ISER) = (1u << IRQ_TIMER0) | (1u << IRQ_IO_BANK0) | (1u << IRQ_UART0);

    for (;;) {
        __asm__ volatile("cpsid i");
        if (presses == shown && rx_tail == rx_head)
            __asm__ volatile("wfi");    /* a pending interrupt still wakes it */
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
            putc_(c);
        }
    }
}
