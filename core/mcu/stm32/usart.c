/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* usart.c -- an STM32 USART or UART of the SR/DR layout (F1, F2, F4: RM0008
 * 27, RM0390 25), over the register file, at its baud rate.
 *
 * The frame:
 *   - a start bit, 8 or 9 data bits (CR1.M), the last of them the parity
 *     bit with CR1.PCE (PS: even or odd), and the stop bits (CR2.STOP: 1,
 *     0.5, 2 or 1.5);
 *   - a bit is BRR clocks of the USART's APB clock (PCLK2 for USART1 and
 *     USART6, PCLK1 for the others) with 16 times oversampling; with the
 *     F4's OVER8, DIV_Mantissa x 8 + DIV_Fraction[2:0] clocks.
 * Transmitting:
 *   - a write to DR with UE and TE moves the frame to the shift register if
 *     it is empty (TXE stays set, TC clears) or holds it in DR (TXE clears)
 *     until it empties; at the end of a frame the held one starts, else TC
 *     sets. Setting TE first sends an idle frame (a frame of 1s);
 *   - SBK sends a break after the current frame: 10 or 11 bits of 0 (13 with
 *     LINEN) and a stop bit, SBK clearing as it ends;
 *   - with CR3.CTSE a frame starts only while the nCTS pin is low; nCTS's
 *     changes set SR.CTS (CTSIE: the interrupt);
 *   - each frame goes to the MCU runtime (esk_mach_host.uart_frame) with
 *     its format and bit time: the serial stream, and the TX pin's waveform
 *     when the pin is the USART's (TX idles high while UE is set).
 * Receiving (RE):
 *   - a frame comes from the RX pin's waveform (the runtime's decoder, at
 *     this format) or from the session's serial input; its data goes to DR
 *     and RXNE sets, or with RXNE still set ORE sets and the frame is lost;
 *     a parity mismatch sets PE, a low stop bit FE (a break: FE with 0, and
 *     LBD with LINEN); a line idle for a frame after a reception sets IDLE;
 *     PE, FE, NF, ORE and IDLE clear by reading SR then DR; reading DR
 *     clears RXNE;
 *   - mute mode (RWU): frames are ignored until an idle line (WAKE 0) or an
 *     address frame (WAKE 1: the MSB set and ADD in the low four bits)
 *     wakes it, which clears RWU; an address frame for another node mutes
 *     it again;
 *   - with CR3.RTSE the nRTS pin is low while the receiver can take a
 *     frame (RXNE clear) and high once it cannot;
 *   - half-duplex (HDSEL): the receiver listens on the TX pin, which a
 *     program configures open-drain so that it is let go between frames.
 * Synchronous mode (CR2.CLKEN, master): the CK pin clocks each data bit
 *   (the last with LBCL), idling at CPOL, the data valid at the edge CPHA
 *   picks; RX is sampled at that edge, so the receiver takes a byte with
 *   each one sent, as a synchronous slave answers.
 * DMA (CR3): DMAT requests the transmitter's DMA while TXE is set, DMAR the
 *   receiver's while RXNE is set (USARTn_TX, USARTn_RX); the DMA's access
 *   to DR is the program's.
 * The interrupt is pending while TXE with TXEIE, TC with TCIE, RXNE or ORE
 *   with RXNEIE, IDLE with IDLEIE, PE with PEIE, LBD with LBDIE, CTS with
 *   CTSIE, or FE, NF or ORE with CR3.EIE and DMAR.
 *
 * Not modelled: noise detection (the decoder takes one sample a bit, so NF
 * never sets, and ONEBIT changes nothing), smartcard mode (SCEN, NACK,
 * GTPR) and IrDA (IREN, IRLP), which no catalog part speaks; the synchronous mode's clock as a slave
 * (the STM32's USART is master only, so nothing is missing there).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/stm32-usart.c, which
 * sent a byte in no time; the frame, its timing, the receiver's flags and
 * the modes are EmbSIM's. */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

/* SR */
#define PE (1u << 0)
#define FE (1u << 1)
#define NF (1u << 2)
#define ORE (1u << 3)
#define IDLE (1u << 4)
#define RXNE (1u << 5)
#define TC (1u << 6)
#define TXE (1u << 7)
#define LBD (1u << 8)
#define CTS (1u << 9)
#define ERRS (PE | FE | NF | ORE | IDLE)
/* CR1 */
#define SBK (1u << 0)
#define RWU (1u << 1)
#define RE (1u << 2)
#define TE (1u << 3)
#define IDLEIE (1u << 4)
#define RXNEIE (1u << 5)
#define TCIE (1u << 6)
#define TXEIE (1u << 7)
#define PEIE (1u << 8)
#define PS (1u << 9)
#define PCE (1u << 10)
#define WAKE (1u << 11)
#define M9 (1u << 12)
#define UE (1u << 13)
#define OVER8 (1u << 15)
/* CR2 */
#define LBDL (1u << 5)
#define LBDIE (1u << 6)
#define LBCL (1u << 8)
#define CPHA (1u << 9)
#define CPOL (1u << 10)
#define CLKEN (1u << 11)
#define LINEN (1u << 14)
/* CR3 */
#define EIE (1u << 0)
#define HDSEL (1u << 3)
#define DMAR (1u << 6)
#define DMAT (1u << 7)
#define RTSE (1u << 8)
#define CTSE (1u << 9)
#define CTSIE (1u << 10)

#define CK_MAX 20 /* the clock edges of a synchronous frame */

struct usart {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *sr, *dr, *cr1, *cr2, *cr3, *brr;
    int irq, apb, running;
    /* the transmitter */
    int busy;           /* the shift register holds a frame */
    u64 left;           /* core cycles until it ends */
    int held;           /* a frame waits in DR */
    u16 hold;
    int idle_frame;     /* the frame in the shift register is TE's idle frame */
    int brk;            /* the frame in the shift register is a break */
    /* the receiver */
    u16 rdr;            /* the data DR shows */
    u32 seen;           /* error flags a read of SR saw, cleared by the next DR read */
    u64 idle_left;      /* core cycles of idle line until IDLE, 0 none */
    int muted_idle;     /* mute mode waiting for an idle line */
    /* the pins */
    int cts;            /* nCTS's level, 0 asserted */
    int rts;            /* nRTS's level */
    int ck;             /* CK's level, -1 not driven */
    /* the synchronous frame's clock: edges still to come, from now */
    u64 ck_at[CK_MAX];
    int nck, ck_next;
    int ck_sample[CK_MAX];
    u16 sync_rx;
    int sync_bits;
    char sig_tx[16], sig_rx[16]; /* its DMA requests */
};


static u32 v32(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }

/* the DMA requests: TX while TXE with DMAT, RX while RXNE with DMAR */
static void dma_check(struct usart *u)
{
    u32 sr = v32(u->sr), c3 = v32(u->cr3), c1 = v32(u->cr1);
    esk_stm32_dma_request(u->s, u->sig_tx, (c3 & DMAT) && (sr & TXE) && (c1 & UE) && (c1 & TE));
    esk_stm32_dma_request(u->s, u->sig_rx, (c3 & DMAR) && (sr & RXNE) && (c1 & UE));
}

static void update_irq(struct usart *u)
{
    u32 sr = v32(u->sr), c1 = v32(u->cr1), c2 = v32(u->cr2), c3 = v32(u->cr3);
    if (((sr & TXE) && (c1 & TXEIE)) || ((sr & TC) && (c1 & TCIE)) || ((sr & (RXNE | ORE)) && (c1 & RXNEIE)) ||
        ((sr & IDLE) && (c1 & IDLEIE)) || ((sr & PE) && (c1 & PEIE)) || ((sr & LBD) && (c2 & LBDIE)) ||
        ((sr & CTS) && (c3 & CTSIE)) || ((sr & (FE | NF | ORE)) && (c3 & EIE) && (c3 & DMAR)))
        esk_rf_interrupt(u->p, u->irq);
}

static void sr_set(struct usart *u, u32 set, u32 clear)
{
    esk_rf_hw(u->sr, (u->sr->value & ~(u64)clear) | set);
}

/* core cycles per bit: the bit's clocks of the APB clock, at the core clock */
static u32 bit_cycles(const struct usart *u)
{
    u32 brr = v32(u->brr) & 0xffffu, hclk = esk_mach_core_hz(u->s), pclk = esk_stm32_pclk(u->s, u->apb);
    u64 clocks, c;
    if (v32(u->cr1) & OVER8)
        brr = (brr >> 4 << 3) | (brr & 7u);
    clocks = brr;
    if (!clocks || !pclk)
        return 1;
    c = (clocks * hclk + pclk / 2) / pclk;
    return c ? (u32)(c > 0xffffffu ? 0xffffffu : c) : 1;
}

static int data_bits(const struct usart *u) { return v32(u->cr1) & M9 ? 9 : 8; }

static int stop_halves(const struct usart *u)
{
    static const int halves[4] = { 2, 1, 4, 3 };
    return halves[v32(u->cr2) >> 12 & 3];
}

static u64 frame_cycles(const struct usart *u)
{
    return (u64)bit_cycles(u) * (u64)(2 * (1 + data_bits(u)) + stop_halves(u)) / 2u;
}

/* the frame's data bits with the parity bit in place */
static u32 with_parity(const struct usart *u, u32 v)
{
    int n = data_bits(u), k, ones = 0;
    v &= (1u << n) - 1;
    if (!(v32(u->cr1) & PCE))
        return v;
    for (k = 0; k < n - 1; k++)
        ones += (int)(v >> k & 1);
    v &= ~(1u << (n - 1));
    /* even parity (PS 0): the ones, parity included, even; odd: odd */
    if ((ones & 1) ^ ((v32(u->cr1) & PS) ? 1 : 0))
        v |= 1u << (n - 1);
    return v;
}

static int parity_ok(const struct usart *u, u32 v)
{
    int n = data_bits(u), k, ones = 0;
    if (!(v32(u->cr1) & PCE))
        return 1;
    for (k = 0; k < n; k++)
        ones += (int)(v >> k & 1);
    return (v32(u->cr1) & PS) ? (ones & 1) == 1 : (ones & 1) == 0;
}

static void run_clock(struct usart *u)
{
    esk_rf_clock(u->p, &u->running, u->busy || u->idle_left || u->ck_next < u->nck);
}

static void pins(struct usart *u)
{
    if (u->s->pins_changed)
        u->s->pins_changed(u->s);
}

/* nRTS: low while the receiver can take a frame */
static void rts_update(struct usart *u)
{
    int r = (v32(u->cr3) & RTSE) ? (v32(u->sr) & RXNE) != 0 : 1;
    if (r != u->rts) {
        u->rts = r;
        pins(u);
    }
}

/* ---- the receiver ---------------------------------------------------------- */

/* a frame arrived (from the pin, the session or the synchronous clock) */
static void receive(struct usart *u, u32 bits, int framing_error)
{
    u32 c1 = v32(u->cr1), n = (u32)data_bits(u), v = bits & ((1u << n) - 1);
    int brk = framing_error && v == 0;
    if (!(c1 & UE) || !(c1 & RE))
        return;
    u->idle_left = frame_cycles(u);
    run_clock(u);
    if (brk && (v32(u->cr2) & LINEN))
        sr_set(u, LBD, 0);
    {
        /* address mark (WAKE 1): the data's MSB (below the parity bit) marks
         * an address, ADD in its low four bits */
        u32 msb = (c1 & PCE) ? n - 2 : n - 1;
        int address = (c1 & WAKE) && (v >> msb & 1);
        int ours = address && (v & 15u) == (v32(u->cr2) & 15u);
        if (c1 & RWU) {
            if (!ours) {
                update_irq(u);
                return; /* mute: only an address frame for this node wakes it */
            }
            esk_rf_hw(u->cr1, u->cr1->value & ~(u64)RWU);
        } else if (address && !ours) {
            esk_rf_hw(u->cr1, u->cr1->value | RWU); /* another node's: mute */
            update_irq(u);
            return;
        }
    }
    if (v32(u->sr) & RXNE) {
        sr_set(u, ORE, 0); /* the frame is lost */
    } else {
        u->rdr = (u16)v;
        sr_set(u, RXNE | (framing_error ? FE : 0) | (parity_ok(u, v) ? 0 : PE), 0);
    }
    rts_update(u);
    update_irq(u);
    dma_check(u);
}

static u32 usart_format(void *ctx)
{
    const struct usart *u = (const struct usart *)ctx;
    return (u32)data_bits(u) | (u32)stop_halves(u) << 8;
}

static void usart_put_frame(void *ctx, u32 bits, int framing_error)
{
    receive((struct usart *)ctx, bits, framing_error);
}

static void usart_started(void *ctx)
{
    struct usart *u = (struct usart *)ctx;
    u->idle_left = 0; /* a start bit: the line is not idle */
}

/* the session's serial input: a byte, its parity as the format has it */
static void usart_put(void *ctx, int c)
{
    struct usart *u = (struct usart *)ctx;
    receive(u, with_parity(u, (u32)c), 0);
}

static int usart_room(void *ctx)
{
    struct usart *u = (struct usart *)ctx;
    /* the receiver is on (a frame it cannot take is an overrun, not a
     * wait); a synchronous receiver takes its bits from the clock */
    return (v32(u->cr1) & UE) && (v32(u->cr1) & RE) && !(v32(u->cr2) & CLKEN);
}

static u32 usart_bit_cycles(void *ctx)
{
    return bit_cycles((const struct usart *)ctx);
}

/* ---- the transmitter -------------------------------------------------------- */

static void sync_clock(struct usart *u, u64 bit);

static void start_frame(struct usart *u, u32 v, int brk, int idle)
{
    struct esk_mach *s = u->s;
    int n = data_bits(u);
    u64 b = bit_cycles(u);
    u->busy = 1;
    u->brk = brk;
    u->idle_frame = idle;
    u->left = frame_cycles(u);
    sr_set(u, 0, TC);
    if (brk) {
        int zeros = (v32(u->cr2) & LINEN) ? 12 : n + 1; /* after the start bit */
        u->left = b * (u64)(1 + zeros) + b;
        if (s->host.uart_frame)
            s->host.uart_frame(s->host.ctx, u->p->name, -1, 0, zeros, 2, (u32)b);
    } else if (!idle) {
        u32 bits = with_parity(u, v);
        int c = (int)(bits & ((v32(u->cr1) & PCE) ? (1u << (n - 1)) - 1 : 0xffu));
        if (s->host.uart_frame)
            s->host.uart_frame(s->host.ctx, u->p->name, c, bits, n, stop_halves(u), (u32)b);
        else if (s->host.uart_tx)
            s->host.uart_tx(s->host.ctx, u->p->name, c, (u32)b);
        if (v32(u->cr2) & CLKEN)
            sync_clock(u, b);
    }
    run_clock(u);
}

/* the next frame, if one waits and nCTS lets it go */
static void next_frame(struct usart *u)
{
    if (u->busy)
        return;
    if ((v32(u->cr3) & CTSE) && u->cts)
        return;
    if (v32(u->cr1) & SBK) {
        start_frame(u, 0, 1, 0);
        return;
    }
    if (u->held) {
        u->held = 0;
        sr_set(u, TXE, 0);
        start_frame(u, u->hold, 0, 0);
    }
}

static void frame_end(struct usart *u)
{
    u->busy = 0;
    u->left = 0;
    if (u->brk)
        esk_rf_hw(u->cr1, u->cr1->value & ~(u64)SBK);
    u->brk = u->idle_frame = 0;
    next_frame(u);
    if (!u->busy)
        sr_set(u, TC, 0);
}

/* ---- the synchronous clock --------------------------------------------------- */

/* CK's edges for the frame just started: one pulse a data bit (the last
 * with LBCL), the first edge mid-bit with CPHA 0 or at the bit's start with
 * CPHA 1; RX is sampled at the capture edge (the first with CPHA 0, the
 * second with CPHA 1) */
static void sync_clock(struct usart *u, u64 b)
{
    u32 c2 = v32(u->cr2);
    int n = data_bits(u), k, pulses = (c2 & LBCL) ? n : n - 1;
    u64 t0 = b; /* after the start bit */
    u->nck = 0;
    u->ck_next = 0;
    u->sync_rx = 0;
    u->sync_bits = 0;
    for (k = 0; k < pulses && u->nck + 2 <= CK_MAX; k++) {
        u64 bit0 = t0 + (u64)k * b;
        u64 e1 = (c2 & CPHA) ? bit0 : bit0 + b / 2, e2 = e1 + b / 2;
        u->ck_at[u->nck] = e1;
        u->ck_sample[u->nck++] = !(c2 & CPHA);
        u->ck_at[u->nck] = e2;
        u->ck_sample[u->nck++] = (c2 & CPHA) != 0;
    }
}

/* the RX pin's level, as the pin reads */
static int rx_level(struct usart *u)
{
    int port, bit, lvl = 1;
    if (esk_stm32_usart_pin(u->s, u->p->name, 0, &port, &bit) == 0 && u->s->host.pin_in &&
        !u->s->host.pin_in(u->s->host.ctx, port, bit, &lvl))
        lvl = 1;
    return lvl != 0;
}

/* the clock edges up to `elapsed` cycles into the frame */
static void sync_edges(struct usart *u, u64 elapsed)
{
    u32 c2 = v32(u->cr2);
    int changed = 0;
    while (u->ck_next < u->nck && u->ck_at[u->ck_next] <= elapsed) {
        int first = !(u->ck_next & 1);
        u->ck = first ? !(c2 & CPOL) : (c2 & CPOL) != 0;
        if (u->ck_sample[u->ck_next]) {
            if (rx_level(u))
                u->sync_rx |= (u16)(1u << u->sync_bits);
            u->sync_bits++;
        }
        u->ck_next++;
        changed = 1;
    }
    if (changed)
        pins(u);
}

/* ---- the device ------------------------------------------------------------------ */

static void usart_tick(void *ctx, u32 cycles)
{
    struct usart *u = (struct usart *)ctx;
    if (u->idle_left) {
        if (u->idle_left > cycles) {
            u->idle_left -= cycles;
        } else {
            u->idle_left = 0;
            if ((v32(u->cr1) & UE) && (v32(u->cr1) & RE)) {
                if ((v32(u->cr1) & RWU) && !(v32(u->cr1) & WAKE))
                    esk_rf_hw(u->cr1, u->cr1->value & ~(u64)RWU); /* an idle line wakes it */
                else if (!(v32(u->cr1) & RWU))
                    sr_set(u, IDLE, 0);
                update_irq(u);
            }
        }
    }
    if (u->busy) {
        u64 total = frame_cycles(u);
        if (u->ck_next < u->nck)
            sync_edges(u, total - (u->left > cycles ? u->left - cycles : 0));
        if (u->left > cycles) {
            u->left -= cycles;
        } else {
            if (v32(u->cr2) & CLKEN) {
                u->ck = (v32(u->cr2) & CPOL) != 0;
                u->nck = u->ck_next = 0;
                if (u->sync_bits && !u->idle_frame && !u->brk)
                    receive(u, u->sync_rx, 0);
                pins(u);
            }
            frame_end(u);
            update_irq(u);
            dma_check(u);
        }
    }
    run_clock(u);
}

static int usart_next_event(void *ctx, u32 *cycles)
{
    struct usart *u = (struct usart *)ctx;
    u64 best = 0;
    if (u->busy) {
        best = u->left;
        if (u->ck_next < u->nck) {
            u64 elapsed = frame_cycles(u) - u->left, at = u->ck_at[u->ck_next];
            u64 c = at > elapsed ? at - elapsed : 1;
            if (c < best)
                best = c;
        }
    }
    if (u->idle_left && (!best || u->idle_left < best))
        best = u->idle_left;
    if (!best)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static u32 usart_read(void *ctx, u32 off, int n)
{
    struct usart *u = (struct usart *)ctx;
    u32 v;
    if (off == esk_rf_off(u->dr)) {
        if (u->s->bus.debug)
            return u->rdr;
        v = u->rdr;
        /* RXNE clears; PE, FE, NF, ORE and IDLE too when SR was read first */
        sr_set(u, 0, RXNE | u->seen);
        u->seen = 0;
        rts_update(u);
        update_irq(u);
        dma_check(u);
        return v;
    }
    v = esk_rf_read(u->p, off, n);
    if (off == esk_rf_off(u->sr) && !u->s->bus.debug)
        u->seen = v32(u->sr) & ERRS;
    return v;
}

static void usart_write(void *ctx, u32 off, int n, u32 v)
{
    struct usart *u = (struct usart *)ctx;
    u32 c1 = v32(u->cr1);
    esk_rf_write(u->p, off, n, v);
    u->seen = 0; /* the clearing sequence is a read of SR then of DR */
    if (off == esk_rf_off(u->dr) && (v32(u->cr1) & UE) && (v32(u->cr1) & TE)) {
        u->held = 1;
        u->hold = (u16)(v & 0x1ffu);
        sr_set(u, 0, TXE);
        next_frame(u);
    }
    if (off == esk_rf_off(u->cr1)) {
        if ((v32(u->cr1) & UE) && (v32(u->cr1) & TE) && !(c1 & TE) && !u->busy)
            start_frame(u, 0x1ff, 0, 1); /* TE: an idle frame */
        if ((v32(u->cr1) & SBK) && !(c1 & SBK))
            next_frame(u);
        if ((v32(u->cr1) & UE) != (c1 & UE))
            pins(u); /* TX idles high while UE is set */
    }
    if (!(v32(u->cr1) & UE)) {
        /* disabled: the transmitter stops, the flags are its reset's */
        u->held = u->busy = 0;
        u->left = u->idle_left = 0;
        u->nck = u->ck_next = 0;
        sr_set(u, TXE | TC, 0);
    }
    if (off == esk_rf_off(u->cr2) || off == esk_rf_off(u->cr1)) {
        int ck = (v32(u->cr2) & CLKEN) && (v32(u->cr1) & UE) ? (v32(u->cr2) & CPOL) != 0 : -1;
        if (ck != u->ck && u->ck_next >= u->nck) {
            u->ck = ck;
            pins(u);
        }
    }
    rts_update(u);
    run_clock(u);
    update_irq(u);
    dma_check(u);
}

static void usart_reset(void *ctx)
{
    struct usart *u = (struct usart *)ctx;
    u->held = u->busy = u->brk = u->idle_frame = 0;
    u->left = u->idle_left = 0;
    u->running = 0;
    u->rdr = 0;
    u->seen = 0;
    u->rts = 1;
    u->ck = -1;
    u->nck = u->ck_next = 0;
}

const struct esk_dev_ops esk_stm32_usart_ops = {
    "stm32-usart", usart_read, usart_write, usart_reset, usart_tick, usart_next_event,
};

/* ---- the pins ------------------------------------------------------------------ */

static struct usart *usart_named(struct esk_mach *s, const char *name)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_stm32_usart_ops && !esk_rf_gated(p) ? (struct usart *)p->ctx : 0;
}

/* "USART2_TX" -> USART2 and the function after the underscore */
static struct usart *signal_of(struct esk_mach *s, const char *sig, const char **fn)
{
    char name[16];
    const char *us = strchr(sig, '_');
    size_t l = us ? (size_t)(us - sig) : 0;
    if (!us || l >= sizeof name || (strncmp(sig, "USART", 5) && strncmp(sig, "UART", 4)))
        return 0;
    memcpy(name, sig, l);
    name[l] = 0;
    *fn = us + 1;
    return usart_named(s, name);
}

int esk_stm32_usart_signal(struct esk_mach *s, const char *sig)
{
    const char *fn;
    struct usart *u = signal_of(s, sig, &fn);
    if (!u || !(v32(u->cr1) & UE))
        return -1;
    if (!strcmp(fn, "TX"))
        return (v32(u->cr1) & TE) || (v32(u->cr3) & HDSEL) ? 1 : -1; /* idle high; the waveform is the runtime's */
    if (!strcmp(fn, "RTS"))
        return (v32(u->cr3) & RTSE) ? u->rts : -1;
    if (!strcmp(fn, "CK"))
        return u->ck;
    return -1;
}

void esk_stm32_usart_signal_input(struct esk_mach *s, const char *sig, int level)
{
    const char *fn;
    struct usart *u = signal_of(s, sig, &fn);
    if (!u || strcmp(fn, "CTS"))
        return;
    level = level != 0;
    if (level == u->cts)
        return;
    u->cts = level;
    if (v32(u->cr3) & CTSE) {
        sr_set(u, CTS, 0);
        next_frame(u);
        update_irq(u);
    }
}

int esk_stm32_usart_pin(struct esk_mach *s, const char *unit, int tx, int *port, int *bit)
{
    struct usart *u = usart_named(s, unit);
    char sig[24];
    int at;
    if (u && !tx && (v32(u->cr3) & HDSEL))
        tx = 1; /* half-duplex: the receiver listens on TX */
    if (s->rf && s->rf->naf) {
        snprintf(sig, sizeof sig, "%s_%s", unit, tx ? "TX" : "RX");
        at = esk_stm32f4_af_pin(s, sig);
        if (at < 0)
            return -1;
        *port = at / 32;
        *bit = at % 32;
        return 0;
    }
    return esk_stm32f1_usart_pin(s, unit, tx, port, bit);
}

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
    u.cr3 = esk_rf_reg(p, "CR3");
    u.brr = esk_rf_reg(p, "BRR");
    if (!u.sr || !u.dr || !u.cr1 || !u.cr2 || !u.brr || !esk_rf_mask(u.sr, "TXE") || !esk_rf_mask(u.cr1, "UE"))
        return 0; /* not this layout (ISR/TDR): registers alone */
    u.irq = esk_rf_irq(p, 0);
    u.apb = p->gate_name && strstr(p->gate_name, "APB2") ? 2 : 1;
    esk_rf_set_reset(u.sr, TXE | TC);
    esk_rf_set_mwv(u.sr, (u64)(CTS | LBD | TC | RXNE), ESK_MWV_0CLR);
    c = (struct usart *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = u;
    usart_reset(c);
    c->cts = 1;
    snprintf(c->sig_tx, sizeof c->sig_tx, "%s_TX", p->name);
    snprintf(c->sig_rx, sizeof c->sig_rx, "%s_RX", p->name);
    s->uart_pin = esk_stm32_usart_pin;
    esk_mach_rx_port(s, p->name, usart_room, usart_put, usart_bit_cycles, c);
    esk_mach_rx_frames(s, usart_format, usart_put_frame, usart_started);
    return c;
}
