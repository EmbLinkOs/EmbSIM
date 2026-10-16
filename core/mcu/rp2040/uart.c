/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* uart.c -- the RP2040's UART0 and UART1, ARM's PL011 (its datasheet,
 * 4.2), over the register file, at their baud rate.
 *
 *   - a write to UARTDR goes into the transmit FIFO (32 deep with
 *     UARTLCR_H.FEN, else one; a write to a full one is lost); while
 *     UARTCR's UARTEN and TXE are set and clk_peri runs, the FIFO drains a
 *     frame at a time: a start bit, WLEN's 5 to 8 data bits, the parity bit
 *     with PEN, one stop bit or two with STP2, each bit 16 x (IBRD +
 *     FBRD / 64) cycles of clk_peri. Each frame goes to the MCU runtime
 *     with its bit time in core cycles; the TX pin, where IO_BANK0 gives
 *     one the UART's function, carries the waveform;
 *   - the receiver: bytes arrive from the session's serial input or the RX
 *     pin while UARTEN and RXE are set and the receive FIFO has room, and a
 *     read of UARTDR takes one (its error bits 0);
 *   - UARTFR says how full each FIFO is (TXFE, TXFF, RXFE, RXFF; BUSY while
 *     a frame is on the line or waits to go);
 *   - the interrupts, UARTRIS: TXRIS when a frame leaves the FIFO for the
 *     line and the FIFO is then at or below its trigger level (UARTIFLS;
 *     empty, without the FIFO), cleared by a write that fills it above;
 *     RXRIS while the receive FIFO is at or above its level (holds a byte,
 *     without the FIFO); RTRIS, the receive timeout, when a byte has waited
 *     32 bit times with none after it, cleared when the FIFO empties.
 *     UARTICR clears them; UARTMIS is UARTRIS & UARTIMSC, and its bits
 *     raise UART0_IRQ or UART1_IRQ, when one sets and when an access
 *     leaves one set.
 * Not modelled: the line's break, the modem lines, the error flags, IrDA,
 * DMA, loopback. */
#include <string.h>

#include "mcu/rp2040/rp2040.h"

#define FIFO 32
#define fld esk_rp_fld

struct uart {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *dr, *fr, *ibrd, *fbrd, *lcrh, *cr, *ifls, *imsc, *ris, *mis, *icr;
    u64 m_fen, m_uarten, m_txe, m_rxe, m_txfe, m_txff, m_rxfe, m_rxff, m_busy;
    u64 m_txris, m_rxris, m_rtris, m_txsel, m_rxsel, m_wlen, m_pen, m_stp2;
    int irq;
    u8 rx[FIFO], tx[FIFO];
    int nrx, rxh, ntx;
    int busy;     /* a frame is on the line */
    u64 left;     /* its cycles to go */
    int rt_armed; /* the receive timeout counts */
    u64 rt_left;
    int running;  /* its clock */
    u64 since;    /* the cycle its clock started */
};

static int depth(const struct uart *u) { return (u->lcrh->value & u->m_fen) ? FIFO : 1; }

/* a trigger level: 1/8, 1/4, 1/2, 3/4 or 7/8 of the FIFO */
static int level(u32 sel)
{
    static const int l[] = { 4, 8, 16, 24, 28 };
    return sel < 5 ? l[sel] : 16;
}

/* a bit's time in core cycles, at the divisor and clk_peri now */
static u32 bit_cycles(const struct uart *u)
{
    u64 div = ((u64)u->ibrd->value & 0xffffu) * 64u + (u->fbrd->value & 0x3fu);
    u64 sys = esk_rp2040_hz(u->s, RP_CLK_SYS), peri = esk_rp2040_hz(u->s, RP_CLK_PERI), c;
    if (!div || !peri)
        return 0;
    c = (16u * div * sys + 32u * peri) / (64u * peri);
    return c ? (u32)c : 1u;
}

static int frame_bits(const struct uart *u)
{
    return 1 + 5 + (int)fld(u->lcrh, u->m_wlen) + (fld(u->lcrh, u->m_pen) ? 1 : 0) +
           (fld(u->lcrh, u->m_stp2) ? 2 : 1);
}

static void flags(struct uart *u)
{
    u64 v = u->fr->value & ~(u->m_txfe | u->m_txff | u->m_rxfe | u->m_rxff | u->m_busy);
    if (!u->ntx)
        v |= u->m_txfe;
    if (u->ntx || u->busy)
        v |= u->m_busy;
    if (u->ntx >= depth(u))
        v |= u->m_txff;
    if (!u->nrx)
        v |= u->m_rxfe;
    if (u->nrx >= depth(u))
        v |= u->m_rxff;
    if (v != u->fr->value)
        esk_rf_hw(u->fr, v);
}

static void irq(struct uart *u, int again)
{
    u64 was = u->mis->value, m = u->ris->value & u->imsc->value;
    if (m != was)
        esk_rf_hw(u->mis, m);
    if (m && (!was || again))
        esk_rf_interrupt(u->p, u->irq);
}

static void ris(struct uart *u, u64 set, u64 clr)
{
    u64 v = (u->ris->value | set) & ~clr;
    if (v != u->ris->value)
        esk_rf_hw(u->ris, v);
}

static int sending(const struct uart *u)
{
    return !esk_rf_gated(u->p) && (u->cr->value & u->m_uarten) && (u->cr->value & u->m_txe) &&
           bit_cycles(u);
}

static void uart_clock(struct uart *u)
{
    int was = u->running;
    esk_rf_clock(u->p, &u->running, u->busy || u->rt_armed);
    if (u->running && !was)
        u->since = u->s->cycles;
}

/* the next frame goes onto the line, if one waits and it can */
static void start(struct uart *u)
{
    struct esk_mach *s = u->s;
    u32 bit;
    u8 c;
    if (u->busy || !u->ntx || !sending(u))
        return;
    bit = bit_cycles(u);
    c = u->tx[0];
    memmove(u->tx, u->tx + 1, (size_t)(FIFO - 1));
    u->ntx--;
    u->busy = 1;
    u->left = (u64)bit * (u64)frame_bits(u);
    if ((u->lcrh->value & u->m_fen) ? u->ntx <= level(fld(u->ifls, u->m_txsel)) : !u->ntx)
        ris(u, u->m_txris, 0);
    uart_clock(u);
    if (s->host.uart_tx)
        s->host.uart_tx(s->host.ctx, u->p->name, c, bit);
}

static void rx_levels(struct uart *u)
{
    int full = (u->lcrh->value & u->m_fen) ? u->nrx >= level(fld(u->ifls, u->m_rxsel)) : u->nrx >= 1;
    ris(u, full ? u->m_rxris : 0, (full ? 0 : u->m_rxris) | (u->nrx ? 0 : u->m_rtris));
    if (!u->nrx && u->rt_armed) {
        u->rt_armed = 0;
        uart_clock(u);
    }
}

static u32 uart_read(void *ctx, u32 off, int n)
{
    struct uart *u = (struct uart *)ctx;
    flags(u);
    if (off == esk_rf_off(u->dr)) {
        u32 v = u->nrx ? u->rx[u->rxh] : 0;
        if (!u->s->bus.debug && u->nrx) {
            u->rxh = (u->rxh + 1) % FIFO;
            u->nrx--;
            rx_levels(u);
            flags(u);
            irq(u, 1);
        }
        esk_rf_hw(u->dr, v);
        return esk_rf_read(u->p, off, n) & 0xffu;
    }
    return esk_rf_read(u->p, off, n);
}

static void uart_write(void *ctx, u32 off, int n, u32 v)
{
    struct uart *u = (struct uart *)ctx;
    esk_rf_write(u->p, off, n, v);
    if (off == esk_rf_off(u->dr)) {
        if (u->ntx < depth(u))
            u->tx[u->ntx++] = (u8)v;
        if (u->ntx > ((u->lcrh->value & u->m_fen) ? level(fld(u->ifls, u->m_txsel)) : 0))
            ris(u, 0, u->m_txris);
    }
    if (off == esk_rf_off(u->icr))
        ris(u, 0, v & 0x7ffu);
    start(u);
    rx_levels(u);
    flags(u);
    irq(u, 1);
}

static void uart_tick(void *ctx, u32 cycles)
{
    struct uart *u = (struct uart *)ctx;
    struct esk_mach *s = u->s;
    int changed = 0;
    if (s->cycles - cycles < u->since)
        cycles = (u32)(s->cycles - u->since); /* the instruction that started it */
    if (u->busy) {
        if (u->left > cycles) {
            u->left -= cycles;
        } else {
            u->left = 0;
            u->busy = 0;
            changed = 1;
        }
    }
    if (u->rt_armed) {
        if (u->rt_left > cycles) {
            u->rt_left -= cycles;
        } else {
            u->rt_armed = 0;
            if (u->nrx)
                ris(u, u->m_rtris, 0);
            changed = 1;
        }
    }
    if (!changed)
        return;
    start(u);
    uart_clock(u);
    flags(u);
    irq(u, 0);
}

static int uart_next(void *ctx, u32 *cycles)
{
    struct uart *u = (struct uart *)ctx;
    u64 c = ~UINT64_C(0);
    if (u->busy)
        c = u->left;
    if (u->rt_armed && u->rt_left < c)
        c = u->rt_left;
    if (c == ~UINT64_C(0))
        return 0;
    *cycles = c > 0xffffffffu ? 0xffffffffu : (u32)c;
    return 1;
}

static void uart_reset(void *ctx)
{
    struct uart *u = (struct uart *)ctx;
    u->nrx = u->rxh = u->ntx = 0;
    u->busy = u->rt_armed = 0;
    u->running = 0;
    u->left = u->rt_left = 0;
    flags(u);
}

const struct esk_dev_ops esk_rp2040_uart_ops = {
    "rp2040-uart", uart_read, uart_write, uart_reset, uart_tick, uart_next,
};

static int uart_room(void *ctx)
{
    struct uart *u = (struct uart *)ctx;
    return !esk_rf_gated(u->p) && (u->cr->value & u->m_uarten) && (u->cr->value & u->m_rxe) &&
           u->nrx < depth(u);
}

static void uart_put(void *ctx, int c)
{
    struct uart *u = (struct uart *)ctx;
    u32 bit = bit_cycles(u);
    if (u->nrx >= depth(u))
        return;
    u->rx[(u->rxh + u->nrx++) % FIFO] = (u8)c;
    rx_levels(u);
    if (bit) {
        u->rt_armed = 1;
        u->rt_left = 32u * (u64)bit;
        uart_clock(u);
    }
    flags(u);
    irq(u, 0);
}

static u32 uart_bit_cycles(void *ctx) { return bit_cycles((const struct uart *)ctx); }

/* a clock started: what waited in the FIFO goes */
void esk_rp2040_uart_kick(struct esk_mach *s)
{
    static const char *const names[] = { "UART0", "UART1" };
    int i;
    for (i = 0; i < 2; i++) {
        struct uart *u = (struct uart *)esk_rp_model(s, names[i], &esk_rp2040_uart_ops);
        if (u && !esk_rf_gated(u->p)) {
            start(u);
            flags(u);
            irq(u, 0);
        }
    }
}

void *esk_rp2040_uart_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct uart *u = (struct uart *)esk_mach_alloc(s, sizeof *u);
    if (!u)
        return 0;
    memset(u, 0, sizeof *u);
    u->s = s;
    u->p = p;
    if (!(u->dr = esk_rf_reg(p, "UARTDR")) || !(u->fr = esk_rf_reg(p, "UARTFR")) ||
        !(u->ibrd = esk_rf_reg(p, "UARTIBRD")) || !(u->fbrd = esk_rf_reg(p, "UARTFBRD")) ||
        !(u->lcrh = esk_rf_reg(p, "UARTLCR_H")) || !(u->cr = esk_rf_reg(p, "UARTCR")) ||
        !(u->ifls = esk_rf_reg(p, "UARTIFLS")) || !(u->imsc = esk_rf_reg(p, "UARTIMSC")) ||
        !(u->ris = esk_rf_reg(p, "UARTRIS")) || !(u->mis = esk_rf_reg(p, "UARTMIS")) ||
        !(u->icr = esk_rf_reg(p, "UARTICR")))
        return 0; /* not a PL011's layout */
    u->m_fen = esk_rf_mask(u->lcrh, "FEN");
    u->m_wlen = esk_rf_mask(u->lcrh, "WLEN");
    u->m_pen = esk_rf_mask(u->lcrh, "PEN");
    u->m_stp2 = esk_rf_mask(u->lcrh, "STP2");
    u->m_uarten = esk_rf_mask(u->cr, "UARTEN");
    u->m_txe = esk_rf_mask(u->cr, "TXE");
    u->m_rxe = esk_rf_mask(u->cr, "RXE");
    u->m_txfe = esk_rf_mask(u->fr, "TXFE");
    u->m_txff = esk_rf_mask(u->fr, "TXFF");
    u->m_rxfe = esk_rf_mask(u->fr, "RXFE");
    u->m_rxff = esk_rf_mask(u->fr, "RXFF");
    u->m_busy = esk_rf_mask(u->fr, "BUSY");
    u->m_txris = esk_rf_mask(u->ris, "TXRIS");
    u->m_rxris = esk_rf_mask(u->ris, "RXRIS");
    u->m_rtris = esk_rf_mask(u->ris, "RTRIS");
    u->m_txsel = esk_rf_mask(u->ifls, "TXIFLSEL");
    u->m_rxsel = esk_rf_mask(u->ifls, "RXIFLSEL");
    u->irq = esk_rf_irq(p, "_IRQ");
    esk_mach_rx_port(s, p->name, uart_room, uart_put, uart_bit_cycles, u);
    return u;
}
