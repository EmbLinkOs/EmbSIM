/* SPDX-License-Identifier: Apache-2.0 */
/* main.c -- the Explorer 16/32 with its PIC32MX795F512L PIM: the 8 MHz
 * crystal through the PLL to an 80 MHz SYSCLK and a 40 MHz PBCLK (the
 * configuration words below), Timer1's interrupt every 100 ms blinking D3
 * at 1 Hz, S3 (RD6) counted through the change notice and shown on D4 to
 * D10, and UART2 at 115200 baud on the USB-UART bridge echoing what it
 * receives in upper case. Between interrupts the core waits (WAIT). */
#include <stdint.h>

#include "pic32mx.h"

/* the configuration words, in boot flash at 0xBFC02FF0:
 *   DEVCFG3: FSRSSEL 7 (priority 7 alone uses the shadow set)
 *   DEVCFG2: FPLLIDIV /2, FPLLMUL x20, FPLLODIV /1: 8 MHz -> 80 MHz
 *   DEVCFG1: FNOSC PRIPLL, POSCMOD HS, FPBDIV /2, the watchdog off
 *   DEVCFG0: the debugger off, no code protection */
__attribute__((section(".config"), used)) static const uint32_t devcfg[4] = {
    0xffffffffu,
    0xfff8ffd9u,  /* FPLLODIV 000, FPLLMUL 101, FPLLIDIV 001 */
    0xff7fdefbu,  /* FWDTEN 0, FPBDIV 01, POSCMOD 10, FNOSC 011 */
    0x7fffffffu,
};

static volatile uint32_t ticks;
static volatile uint32_t presses;

static void putc2(char c)
{
    while (U2STA & (1u << 9)) /* UTXBF */
        ;
    U2TXREG = (uint8_t)c;
}

static void puts2(const char *s)
{
    while (*s)
        putc2(*s++);
}

/* a number in decimal, without a divide instruction's library */
static void putu(uint32_t v)
{
    char b[12];
    int n = 0;
    do {
        b[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        putc2(b[--n]);
}

static void puthex(uint32_t v)
{
    int i;
    puts2("0x");
    for (i = 28; i >= 0; i -= 4)
        putc2("0123456789abcdef"[(v >> i) & 15u]);
}

/* SYSCLK from the oscillator's registers: the primary oscillator (8 MHz)
 * through the PLL, as COSC says */
static uint32_t sysclk(void)
{
    static const uint8_t idiv[8] = { 1, 2, 3, 4, 5, 6, 10, 12 };
    static const uint8_t mul[8] = { 15, 16, 17, 18, 19, 20, 21, 24 };
    uint32_t o = OSCCON, cosc = (o >> 12) & 7u;
    if (cosc != 3)
        return cosc == 2 ? 8000000u : 0;
    return 8000000u / idiv[DEVCFG2 & 7u] * mul[(o >> 16) & 7u] >> ((o >> 27) & 7u);
}

static void show_presses(void)
{
    /* D4 to D10 (RA1-RA7): the count; D3 (RA0) is the blink's */
    uint32_t a = LATA;
    LATA = (a & ~0xfeu) | ((presses << 1) & 0xfeu);
}

void isr(uint32_t vector)
{
    if (vector == VEC_T1) {
        CLR(IFS0) = BIT(IRQ_T1);
        if (++ticks % 5u == 0)
            INV(LATA) = 1u; /* D3 */
    } else if (vector == VEC_CN) {
        uint32_t d = PORTD; /* the read ends the mismatch */
        CLR(IFS1) = BIT(IRQ_CN);
        if (!(d & (1u << 6))) {
            presses++;
            show_presses();
            puts2("S3 pressed ");
            putu(presses);
            puts2("\r\n");
        }
    } else if (vector == VEC_UART2) {
        while (U2STA & 1u) { /* URXDA */
            char c = (char)U2RXREG;
            putc2(c >= 'a' && c <= 'z' ? (char)(c - 32) : c);
        }
        CLR(IFS1) = BIT(IRQ_U2RX);
    }
}

int main(void)
{
    uint32_t hz;
    CHECON = 2;            /* two flash wait states at 80 MHz */
    DDPCON = 0;            /* JTAG off: RA0, RA1, RA4, RA5 are the LEDs' */
    TRISA = 0xff00u;       /* RA0-RA7 out: D3 to D10 */
    LATA = 0;
    hz = sysclk();
    /* UART2: 115200 baud at PBCLK 40 MHz, BRGH (4 clocks a bit):
     * 40 MHz / (4 x 87) */
    U2BRG = 86;
    U2MODE = (1u << 15) | (1u << 3);
    U2STA = (1u << 12) | (1u << 10); /* URXEN, UTXEN */
    puts2("explorer16: PIC32MX795F512L, SYSCLK ");
    putu(hz / 1000u);
    puts2(" kHz, PBCLK ");
    putu(hz / (1u << ((OSCCON >> 19) & 3u)) / 1000u);
    puts2(" kHz, DEVID ");
    puthex(DEVID);
    puts2("\r\n");
    /* Timer1: PBCLK / 256 = 156250 Hz, 15625 counts: 100 ms */
    T1CON = 3u << 4;
    TMR1 = 0;
    PR1 = 15625u - 1u;
    SET(IPC1) = 3u << 2;           /* T1IP 3 */
    SET(IEC0) = BIT(IRQ_T1);
    T1CON |= 1u << 15;
    /* S3 on RD6 (CN15): the change notice */
    TRISD |= 1u << 6;
    CNEN = 1u << 15;
    CNCON = 1u << 15;
    (void)PORTD;
    SET(IPC6) = 2u << 18;          /* CNIP 2 */
    CLR(IFS1) = BIT(IRQ_CN);
    SET(IEC1) = BIT(IRQ_CN);
    /* UART2's receiver */
    SET(IPC8) = 4u << 2;           /* U2IP 4 */
    SET(IEC1) = BIT(IRQ_U2RX);
    /* multi-vector mode, then interrupts on */
    INTCON = 1u << 12;
    __asm__ volatile("ei");
    for (;;)
        __asm__ volatile("wait");
}
