/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* usart.c -- an STM32 USART or UART of the SR/DR layout (F1, F2, F4), over
 * the register file, at its baud rate (RM0008 27).
 *
 *   - SR resets to 0x000000c0, TXE and TC, as the reference manual says;
 *     its CTS, LBD, TC and RXNE are rc_w0;
 *   - a write to DR with CR1's UE and TE set moves the byte to the shift
 *     register if it is empty (TXE stays set, TC clears) or holds it in DR
 *     (TXE clears) until the shift register empties. A frame is a start
 *     bit, 8 or 9 data bits (CR1.M) and the stop bits (CR2.STOP: 1, 0.5, 2
 *     or 1.5), each bit BRR clocks of the USART's bus clock (PCLK2 for
 *     USART1, PCLK1 for the others); at the end of a frame the held byte,
 *     if any, starts, else TC sets. Each frame goes to the MCU runtime
 *     (esk_mach_host.uart_tx) with its bit time, which writes the console
 *     stream and drives the TX pin's waveform;
 *   - the interrupt is made pending when an access or a frame's end
 *     leaves TXE with TXEIE, TC with TCIE or RXNE with RXNEIE set;
 *   - the receiver takes a byte from the runtime's serial input while UE
 *     and RE are set and DR holds none: RXNE sets (with RXNEIE, the
 *     interrupt), a read of DR takes the byte and clears RXNE.
 * Without UE and TE nothing is sent.
 *
 * Not modelled: the error flags (a byte waits for room, so ORE never
 * sets), parity, LIN, smartcard and IrDA modes, hardware flow control,
 * DMA, reception from the RX pin's waveform (bytes come from the session's
 * serial input).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/stm32-usart.c, which
 * sent a byte in no time; the frame timing is EmbSIM's. */
#include <string.h>

#include "mcu/stm32/stm32.h"

struct usart {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *sr, *dr, *cr1, *cr2, *brr;
    u64 txe, tc, rxne, ue, te, re, txeie, tcie, rxneie, mbit, stop;
    int irq, apb, running;
    u8 rx;      /* the byte received */
    int held;   /* a byte waits in DR for the shift register */
    u8 hold;
    u64 left;   /* core cycles until the frame in the shift register ends */
};

static void update_irq(struct usart *u)
{
    u64 sr = u->sr->value, cr1 = u->cr1->value;
    if (((sr & u->txe) && (cr1 & u->txeie)) || ((sr & u->tc) && (cr1 & u->tcie)) ||
        ((sr & u->rxne) && (cr1 & u->rxneie)))
        esk_rf_interrupt(u->p, u->irq);
}

/* core cycles per bit: BRR clocks of the bus clock, at the core clock */
static u32 bit_cycles(const struct usart *u)
{
    u32 brr = (u32)u->brr->value & 0xffffu, hclk = esk_mach_core_hz(u->s);
    u32 pclk = esk_stm32f1_pclk(u->s, u->apb);
    u64 c;
    if (!brr || !pclk)
        return 1;
    c = ((u64)brr * hclk + pclk / 2) / pclk;
    return c ? (u32)(c > 0xffffffu ? 0xffffffu : c) : 1;
}

/* a frame's length in core cycles */
static u64 frame_cycles(const struct usart *u)
{
    u64 b = bit_cycles(u), stop2;
    u32 stop = u->stop ? (u32)((u->cr2->value & u->stop) >> 12) : 0;
    /* stop bits in halves: 1, 0.5, 2, 1.5 */
    static const u32 halves[4] = { 2, 1, 4, 3 };
    stop2 = halves[stop & 3];
    return b * (1u + ((u->cr1->value & u->mbit) ? 9u : 8u)) + (b * stop2) / 2u;
}

static void start_frame(struct usart *u, u8 c)
{
    struct esk_mach *s = u->s;
    u->left = frame_cycles(u);
    esk_rf_clock(u->p, &u->running, 1);
    esk_rf_hw(u->sr, u->sr->value & ~u->tc);
    if (s->host.uart_tx)
        s->host.uart_tx(s->host.ctx, u->p->name, c, bit_cycles(u));
}

static u32 usart_read(void *ctx, u32 off, int n)
{
    struct usart *u = (struct usart *)ctx;
    if (off == esk_rf_off(u->dr) && (u->sr->value & u->rxne)) {
        if (!u->s->bus.debug)
            esk_rf_hw(u->sr, u->sr->value & ~u->rxne);
        return u->rx;
    }
    return esk_rf_read(u->p, off, n);
}

static int usart_room(void *ctx)
{
    struct usart *u = (struct usart *)ctx;
    return (u->cr1->value & u->ue) && (u->cr1->value & u->re) && !(u->sr->value & u->rxne);
}

static void usart_put(void *ctx, int c)
{
    struct usart *u = (struct usart *)ctx;
    u->rx = (u8)c;
    esk_rf_hw(u->sr, u->sr->value | u->rxne);
    update_irq(u);
}

static void usart_write(void *ctx, u32 off, int n, u32 v)
{
    struct usart *u = (struct usart *)ctx;
    esk_rf_write(u->p, off, n, v);
    if (off == esk_rf_off(u->dr) && (u->cr1->value & u->ue) && (u->cr1->value & u->te)) {
        if (!u->running) {
            start_frame(u, (u8)v);
        } else {
            u->held = 1;
            u->hold = (u8)v;
            esk_rf_hw(u->sr, u->sr->value & ~u->txe);
        }
    }
    if (!(u->cr1->value & u->ue)) {
        /* disabled: the transmitter stops, the flags are its reset's */
        u->held = 0;
        u->left = 0;
        esk_rf_clock(u->p, &u->running, 0);
        esk_rf_hw(u->sr, u->sr->value | u->txe | u->tc);
    }
    update_irq(u);
}

static void usart_tick(void *ctx, u32 cycles)
{
    struct usart *u = (struct usart *)ctx;
    if (!u->running)
        return;
    if (u->left > cycles) {
        u->left -= cycles;
        return;
    }
    if (u->held) {
        u->held = 0;
        esk_rf_hw(u->sr, u->sr->value | u->txe);
        start_frame(u, u->hold);
    } else {
        u->left = 0;
        esk_rf_clock(u->p, &u->running, 0);
        esk_rf_hw(u->sr, u->sr->value | u->tc);
    }
    update_irq(u);
}

static int usart_next_event(void *ctx, u32 *cycles)
{
    struct usart *u = (struct usart *)ctx;
    if (!u->running)
        return 0;
    *cycles = u->left > 0xffffffffu ? 0xffffffffu : (u32)u->left;
    return 1;
}

static void usart_reset(void *ctx)
{
    struct usart *u = (struct usart *)ctx;
    u->held = 0;
    u->left = 0;
    u->running = 0;
}

const struct esk_dev_ops esk_stm32_usart_ops = {
    "stm32-usart", usart_read, usart_write, usart_reset, usart_tick, usart_next_event,
};

void *esk_stm32_usart_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct usart u, *c;
    memset(&u, 0, sizeof u);
    u.s = s;
    u.p = p;
    u.sr = esk_rf_reg(p, "SR");
    u.dr = esk_rf_reg(p, "DR");
    u.cr1 = esk_rf_reg(p, "CR1");
    u.cr2 = esk_rf_reg(p, "CR2");
    u.brr = esk_rf_reg(p, "BRR");
    if (!u.sr || !u.dr || !u.cr1 || !u.cr2 || !u.brr)
        return 0; /* not this layout (ISR/TDR): registers alone */
    u.txe = esk_rf_mask(u.sr, "TXE");
    u.tc = esk_rf_mask(u.sr, "TC");
    u.rxne = esk_rf_mask(u.sr, "RXNE");
    u.ue = esk_rf_mask(u.cr1, "UE");
    u.te = esk_rf_mask(u.cr1, "TE");
    u.re = esk_rf_mask(u.cr1, "RE");
    u.txeie = esk_rf_mask(u.cr1, "TXEIE");
    u.tcie = esk_rf_mask(u.cr1, "TCIE");
    u.rxneie = esk_rf_mask(u.cr1, "RXNEIE");
    u.mbit = esk_rf_mask(u.cr1, "M");
    u.stop = esk_rf_mask(u.cr2, "STOP");
    if (!u.txe || !u.tc || !u.ue || !u.te)
        return 0;
    u.irq = esk_rf_irq(p, 0);
    u.apb = p->gate_name && strstr(p->gate_name, "APB2") ? 2 : 1;
    esk_rf_set_reset(u.sr, u.txe | u.tc);
    esk_rf_set_mwv(u.sr, esk_rf_mask(u.sr, "CTS") | esk_rf_mask(u.sr, "LBD") | u.tc | u.rxne,
                   ESK_MWV_0CLR);
    c = (struct usart *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = u;
    if (u.rxne && u.re)
        esk_mach_rx_port(s, p->name, usart_room, usart_put, c);
    return c;
}
