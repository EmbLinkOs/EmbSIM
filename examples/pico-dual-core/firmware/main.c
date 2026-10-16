/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the Pico's two cores, on the registers (no SDK):
 *
 *   - core 0 brings the clocks up as the SDK does (clk_sys 125 MHz from
 *     PLL_SYS), sets UART0 up at 115200 baud on GP0/GP1, and launches core
 *     1 as the SDK's multicore_launch_core1 does: core 1 reset through the
 *     PSM, then the sequence 0, 0, 1, the vector table, a stack and the
 *     entry, each word sent through the FIFO and echoed by core 1's
 *     bootrom;
 *   - both cores add 1000 to one counter, each addition under spinlock 31;
 *   - core 1 blinks the LED on GPIO25 at 1 Hz from the timer's alarm 1, takes
 *     the button on GP15 through IO_BANK0's PROC1 interrupts, and answers
 *     the numbers core 0 sends it with n * 1000 / 7 from its own divider
 *     (SIO_IRQ_PROC1), while core 0 prints with its own;
 *   - core 0 prints what core 1 sends back (SIO_IRQ_PROC0) and echoes the
 *     terminal in upper case; between interrupts both cores wait in WFI. */
#include <stdint.h>

#include "rp2040.h"

extern void (*const vectors[])(void);

#define MSG_RESULT 0x01000000u
#define MSG_BUTTON 0x02000000u

static volatile uint32_t shared;
static volatile uint32_t core1_cpuid = 99, core1_done;
static volatile uint32_t presses;
static uint32_t core1_stack[256];
static uint32_t next_alarm;

static volatile uint8_t rx_buf[64];
static volatile uint32_t rx_head, rx_tail;
static volatile uint32_t msg_buf[16];
static volatile uint32_t msg_head, msg_tail;

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

/* n / d by the calling core's divider */
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

static void lock(void)
{
    while (!REG(SIO_SPINLOCK(31)))
        ;
}

static void unlock(void) { REG(SIO_SPINLOCK(31)) = 1; }

static void fifo_push(uint32_t v)
{
    while (!(REG(SIO_FIFO_ST) & FIFO_RDY))
        ;
    REG(SIO_FIFO_WR) = v;
    __asm__ volatile("sev");
}

static uint32_t fifo_pop(void)
{
    while (!(REG(SIO_FIFO_ST) & FIFO_VLD))
        __asm__ volatile("wfe");
    return REG(SIO_FIFO_RD);
}

static void fifo_drain(void)
{
    while (REG(SIO_FIFO_ST) & FIFO_VLD)
        (void)REG(SIO_FIFO_RD);
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

/* ---- core 1 ----------------------------------------------------------------- */

void timer_irq_1(void)
{
    REG(TIMER_INTR) = 1u << 1;
    REG(SIO_GPIO_OUT_XOR) = 1u << 25;
    next_alarm += 500000;
    REG(TIMER_ALARM1) = next_alarm;
}

void io_irq_bank0(void)
{
    if (REG(IO_INTR(1)) & (1u << 30)) { /* GPIO15 EDGE_LOW */
        REG(IO_INTR(1)) = 1u << 30;
        presses++;
        fifo_push(MSG_BUTTON | presses);
    }
}

void sio_irq_proc1(void)
{
    while (REG(SIO_FIFO_ST) & FIFO_VLD) {
        uint32_t n = REG(SIO_FIFO_RD), r;
        fifo_push(MSG_RESULT | udiv(n * 1000u, 7, &r));
    }
}

static void core1_main(void)
{
    int i;
    core1_cpuid = REG(SIO_CPUID);
    for (i = 0; i < 1000; i++) {
        lock();
        shared++;
        unlock();
    }
    core1_done = 1;
    SET(IO_PROC1_INTE(1)) = 1u << 30;
    next_alarm = REG(TIMER_RAWL) + 500000;
    REG(TIMER_ALARM1) = next_alarm;
    SET(TIMER_INTE) = 1u << 1;
    REG(NVIC_ISER) = (1u << IRQ_TIMER1) | (1u << IRQ_IO_BANK0) | (1u << IRQ_SIO_PROC1);
    for (;;)
        __asm__ volatile("wfi");
}

/* ---- core 0 ----------------------------------------------------------------- */

void sio_irq_proc0(void)
{
    while (REG(SIO_FIFO_ST) & FIFO_VLD) {
        msg_buf[msg_head % 16] = REG(SIO_FIFO_RD);
        msg_head++;
    }
}

void uart0_irq(void)
{
    while (!(REG(UART_FR) & FR_RXFE)) {
        rx_buf[rx_head % 64] = (uint8_t)REG(UART_DR);
        rx_head++;
    }
}

/* the SDK's multicore_reset_core1 and multicore_launch_core1_raw */
static void launch_core1(void (*entry)(void), uint32_t *sp)
{
    const uint32_t seq[6] = { 0, 0, 1, (uint32_t)(uintptr_t)vectors, (uint32_t)(uintptr_t)sp,
                              (uint32_t)(uintptr_t)entry };
    uint32_t i = 0;
    SET(PSM_FRCE_OFF) = PSM_PROC1;
    while (!(REG(PSM_FRCE_OFF) & PSM_PROC1))
        ;
    CLR(PSM_FRCE_OFF) = PSM_PROC1;
    do {
        uint32_t cmd = seq[i];
        if (!cmd) {
            fifo_drain();
            __asm__ volatile("sev");
        }
        fifo_push(cmd);
        i = fifo_pop() == cmd ? i + 1 : 0;
    } while (i < 6);
}

int main(void)
{
    static const uint32_t ask[3] = { 10, 100, 1000 };
    int i;
    clocks_init();
    unreset(RESET_IO_BANK0 | RESET_PADS_BANK0 | RESET_UART0 | RESET_TIMER | RESET_SYSINFO);

    REG(GPIO_CTRL(25)) = FUNC_SIO;
    REG(SIO_GPIO_OUT_CLR) = 1u << 25;
    REG(SIO_GPIO_OE_SET) = 1u << 25;
    CLR(PAD(15)) = PAD_PDE;
    SET(PAD(15)) = PAD_PUE | PAD_IE;
    REG(GPIO_CTRL(15)) = FUNC_SIO;

    REG(GPIO_CTRL(0)) = FUNC_UART;
    REG(GPIO_CTRL(1)) = FUNC_UART;
    REG(UART_IBRD) = 67;                /* 125 MHz / (16 x 115200) = 67.8 */
    REG(UART_FBRD) = 52;
    REG(UART_LCR_H) = (3u << 5) | (1u << 4); /* 8 bits, FIFOs */
    REG(UART_CR) = (1u << 0) | (1u << 8) | (1u << 9);
    REG(UART_IMSC) = (1u << 4) | (1u << 6); /* RX, RX timeout */

    puts_("pico: two cores, clk_sys ");
    put_num(clk_sys_khz());
    puts_(" kHz, core 0 CPUID ");
    put_num(REG(SIO_CPUID));
    puts_("\r\n");

    launch_core1(core1_main, core1_stack + 256);
    for (i = 0; i < 1000; i++) {
        lock();
        shared++;
        unlock();
    }
    while (!core1_done)
        ;
    puts_("core 1 CPUID ");
    put_num(core1_cpuid);
    puts_(", shared ");
    put_num(shared);
    puts_("\r\n");

    REG(NVIC_ISER) = (1u << IRQ_UART0) | (1u << IRQ_SIO_PROC0);
    for (i = 0; i < 3; i++)
        fifo_push(ask[i]);

    i = 0;
    for (;;) {
        __asm__ volatile("cpsid i");
        if (msg_tail == msg_head && rx_tail == rx_head)
            __asm__ volatile("wfi");    /* a pending interrupt still wakes it */
        __asm__ volatile("cpsie i");
        while (msg_tail != msg_head) {
            uint32_t m = msg_buf[msg_tail % 16];
            msg_tail++;
            if ((m & 0xff000000u) == MSG_RESULT && i < 3) {
                puts_("core 1: ");
                put_num(ask[i++]);
                puts_(" * 1000 / 7 = ");
                put_num(m & 0xffffffu);
                puts_("\r\n");
            } else if ((m & 0xff000000u) == MSG_BUTTON) {
                puts_("button ");
                put_num(m & 0xffffffu);
                puts_(" (core 1)\r\n");
            }
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
