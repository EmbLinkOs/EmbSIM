/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* usart_v2.c -- an STM32 USART, UART or LPUART of the ISR/ICR/TDR/RDR
 * layout (the G4's: RM0440 37 and 38; the same IP on the L4, G0, H7),
 * over the register file, at its baud rate.
 *
 * The frame:
 *   - a start bit, 7, 8 or 9 bits (CR1's M1:M0: 10, 00, 01), the last of
 *     them the parity bit with PCE (PS: even or odd), and the stop bits
 *     (CR2.STOP: 1, 0.5, 2 or 1.5; an LPUART has no 0.5 or 1.5);
 *   - its data least significant bit first, or most with CR2.MSBFIRST,
 *     inverted with DATAINV (the bits on the line, and so the character a
 *     terminal decodes from them);
 *   - the bit time from the kernel clock (RCC's CCIPR selection,
 *     esk_stm32g4_kernel_hz) over PRESC's prescaler (1 to 256): a USART's
 *     USARTDIV clocks (BRR with 16 times oversampling; with OVER8,
 *     BRR[15:4] << 1 | BRR[2:0]... as RM0440 37.5.7 gives it, 8 times), an
 *     LPUART's 256 x clock / BRR.
 * Transmitting:
 *   - TE and RE, with UE, set TEACK and REACK (ISR), which the HAL waits
 *     for; TE set sends an idle frame first;
 *   - a write to TDR goes to the transmit FIFO with CR1.FIFOEN (8 frames,
 *     TXFNF in ISR's bit 7 while it is not full, TXFE when empty, TXFT at
 *     CR3.TXFTCFG's threshold), else to the transmit data register (TXE,
 *     bit 7, while it is empty); a frame starts as the shift register
 *     empties, TC at the end of the last one;
 *   - RQR's SBKRQ sends a break after the current frame (SBKF meanwhile):
 *     the frame's length of 0s (13 with LINEN) and a stop bit; TXFRQ
 *     empties the transmit FIFO;
 *   - with CR3.CTSE a frame starts only while nCTS is low; its changes set
 *     CTSIF (CTS the line's level); with CR3.DEM, DE (on the RTS pin,
 *     DEP's polarity) is asserted from a frame's start to its end, then
 *     deasserted;
 *   - each frame goes to the MCU runtime (esk_mach_host.uart_frame), with
 *     its format and bit time: the serial stream, and the TX pin's waveform
 *     (TX idles high while UE and TE are set; with CR2.SWAP TX and RX
 *     exchange pins).
 * Receiving (RE):
 *   - a frame comes from the RX pin's waveform (the runtime's decoder at
 *     this format) or from the session's serial input; it goes to the
 *     receive FIFO (FIFOEN, 8 frames: RXFNE in ISR's bit 5 while not
 *     empty, RXFF when full, RXFT at CR3.RXFTCFG's threshold) or to RDR
 *     (RXNE); one that finds no room is an overrun (ORE, the frame lost;
 *     with CR3.OVRDIS it overwrites RDR instead and ORE stays clear);
 *     a parity mismatch sets PE, a low stop bit FE (a break: FE with 0, and
 *     LBDF with LINEN); a line idle for a frame after a reception sets
 *     IDLE; a received character equal to CR2's ADD sets CMF; with RTOEN,
 *     RTOR's count of bit times after the last stop bit sets RTOF;
 *     the error flags are cleared through ICR; reading RDR takes a frame;
 *   - mute mode (MME, entered by RQR.MMRQ: RWU): frames are ignored until
 *     an idle line (WAKE 0) or an address frame (WAKE 1: the MSB set and
 *     ADD's 4 low bits, or its 7 with ADDM7) wakes it; an address frame
 *     for another node mutes it again;
 *   - with CR3.RTSE nRTS is low while the receiver has room;
 *   - half-duplex (HDSEL): the receiver listens on the TX pin.
 *   - in Stop, a receiver whose kernel clock runs there (CR1.UESM with
 *     HSI16 or LSE selected) still receives, setting WUF on the event
 *     CR3.WUS selects (00 address match, 10 a start bit, 11 RXNE) and so
 *     waking the core through its interrupt (WUFIE) and EXTI's direct
 *     line; otherwise a frame arriving in Stop is lost.
 * Synchronous mode (CR2.CLKEN, master): the CK pin clocks each data bit
 *   (the last with LBCL), idling at CPOL, the data valid at the edge CPHA
 *   picks; RX is sampled at that edge.
 * DMA (CR3): DMAT requests the transmitter's DMA while TXE/TXFNF is set,
 *   DMAR the receiver's while RXNE/RXFNE is set (USARTn_TX, USARTn_RX,
 *   LPUART1_TX, ...); DDRE: a reception error stops DMAR's requests until
 *   the error is cleared.
 * The interrupt is pending while a flag is set with its enable: TXE/TXFNF
 *   with TXEIE, TC with TCIE, RXNE/RXFNE or ORE with RXNEIE, IDLE with
 *   IDLEIE, PE with PEIE, LBDF with LBDIE, CTSIF with CTSIE, CMF with
 *   CMIE, RTOF with RTOIE, WUF with WUFIE, TXFE with TXFEIE, RXFF with
 *   RXFFIE, TXFT with TXFTIE, RXFT with RXFTIE, or FE, NE or ORE with EIE.
 *
 * Not modelled: noise detection (one sample a bit: NE never sets, ONEBIT
 * changes nothing), auto baud rate detection (ABREN: ABRF never sets and
 * the rate stays BRR's), TXINV and RXINV (the line's levels are the
 * standard ones: the runtime's waveform knows no inverted line), the DE
 * assertion and deassertion times (DEAT, DEDT: DE spans the frame), the
 * synchronous slave (SLVEN), smartcard (SCEN, NACK, GTPR, SCARCNT, EOBF)
 * and IrDA (IREN, IRLP) modes, which no catalog part speaks, the
 * TCBGT flag. */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

/* ISR */
#define PE (1u << 0)
#define FE (1u << 1)
#define NE (1u << 2)
#define ORE (1u << 3)
#define IDLE (1u << 4)
#define RXNE (1u << 5)
#define TC (1u << 6)
#define TXE (1u << 7)
#define LBDF (1u << 8)
#define CTSIF (1u << 9)
#define CTS (1u << 10)
#define RTOF (1u << 11)
#define BUSY (1u << 16)
#define CMF (1u << 17)
#define SBKF (1u << 18)
#define RWU (1u << 19)
#define WUF (1u << 20)
#define TEACK (1u << 21)
#define REACK (1u << 22)
#define TXFE (1u << 23)
#define RXFF (1u << 24)
#define RXFT (1u << 26)
#define TXFT (1u << 27)
/* CR1 */
#define UE (1u << 0)
#define UESM (1u << 1)
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
#define M0 (1u << 12)
#define MME (1u << 13)
#define CMIE (1u << 14)
#define OVER8 (1u << 15)
#define RTOIE (1u << 26)
#define M1 (1u << 28)
#define FIFOEN (1u << 29)
#define TXFEIE (1u << 30)
#define RXFFIE (1u << 31)
/* CR2 */
#define ADDM7 (1u << 4)
#define LBDIE (1u << 6)
#define LBCL (1u << 8)
#define CPHA (1u << 9)
#define CPOL (1u << 10)
#define CLKEN (1u << 11)
#define LINEN (1u << 14)
#define SWAP (1u << 15)
#define DATAINV (1u << 18)
#define MSBFIRST (1u << 19)
#define RTOEN (1u << 23)
/* CR3 */
#define EIE (1u << 0)
#define HDSEL (1u << 3)
#define DMAR (1u << 6)
#define DMAT (1u << 7)
#define RTSE (1u << 8)
#define CTSE (1u << 9)
#define CTSIE (1u << 10)
#define OVRDIS (1u << 12)
#define DDRE (1u << 13)
#define DEM (1u << 14)
#define DEP (1u << 15)
#define WUFIE (1u << 22)
#define TXFTIE (1u << 23)
#define RXFTIE (1u << 28)
/* RQR */
#define SBKRQ (1u << 1)
#define MMRQ (1u << 2)
#define RXFRQ (1u << 3)
#define TXFRQ (1u << 4)

#define FIFO 8
#define CK_MAX 20

struct uv2 {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr1, *cr2, *cr3, *brr, *rtor, *rqr, *isr, *icr, *rdr, *tdr, *presc;
    int irq, lp, running;
    /* the transmitter: the frames waiting (TDR, or the FIFO), the shift register */
    u16 txq[FIFO];
    int ntx;
    int busy, brk, idle_frame;
    u64 left;
    /* the receiver */
    u16 rxq[FIFO];
    u8 rxerr[FIFO];      /* each frame's PE and FE */
    int nrx;
    u64 idle_left, rto_left;
    int cts, rts, ck, de;
    u64 ck_at[CK_MAX];
    int nck, ck_next, ck_sample[CK_MAX];
    u16 sync_rx;
    int sync_bits;
    char sig_tx[16], sig_rx[16];
};

static u32 v32(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }

static int fifo(const struct uv2 *u) { return (v32(u->cr1) & FIFOEN) != 0; }

static int depth(const struct uv2 *u) { return fifo(u) ? FIFO : 1; }

static void isr_set(struct uv2 *u, u32 set, u32 clear)
{
    u64 v = (u->isr->value & ~(u64)clear) | set;
    if (v != u->isr->value)
        esk_rf_hw(u->isr, v);
}

/* the FIFO thresholds (TXFTCFG, RXFTCFG): 1/8, 1/4, 1/2, 3/4, 7/8, full */
static int threshold(u32 cfg)
{
    static const int n[8] = { 1, 2, 4, 6, 7, 8, 8, 8 };
    return n[cfg & 7];
}

/* ISR's flags as the queues have them */
static void flags(struct uv2 *u)
{
    u32 set = 0, clear = 0, c3 = v32(u->cr3);
    int room = depth(u) - u->ntx;
    if (room > 0)
        set |= TXE;
    else
        clear |= TXE;
    if (u->ntx == 0)
        set |= TXFE;
    else
        clear |= TXFE;
    if (u->nrx > 0)
        set |= RXNE;
    else
        clear |= RXNE;
    if (fifo(u)) {
        if (u->nrx >= FIFO)
            set |= RXFF;
        else
            clear |= RXFF;
        /* TXFT: the FIFO holds no more than its threshold's free slots' worth */
        if (FIFO - u->ntx >= threshold(c3 >> 29))
            set |= TXFT;
        else
            clear |= TXFT;
        if (u->nrx >= threshold(c3 >> 25))
            set |= RXFT;
        else
            clear |= RXFT;
    } else {
        clear |= RXFF | RXFT | TXFT;
    }
    if (u->busy)
        set |= BUSY;
    else
        clear |= BUSY;
    isr_set(u, set, clear);
}

static void dma_check(struct uv2 *u)
{
    u32 isr = v32(u->isr), c3 = v32(u->cr3), c1 = v32(u->cr1);
    int err = (c3 & DDRE) && (isr & (PE | FE | NE | ORE));
    esk_stm32_dma_request(u->s, u->sig_tx, (c3 & DMAT) && (isr & TXE) && (c1 & UE) && (c1 & TE));
    esk_stm32_dma_request(u->s, u->sig_rx, (c3 & DMAR) && (isr & RXNE) && (c1 & UE) && !err);
}

static void update_irq(struct uv2 *u)
{
    u32 isr = v32(u->isr), c1 = v32(u->cr1), c2 = v32(u->cr2), c3 = v32(u->cr3);
    if (((isr & TXE) && (c1 & TXEIE)) || ((isr & TC) && (c1 & TCIE)) || ((isr & (RXNE | ORE)) && (c1 & RXNEIE)) ||
        ((isr & IDLE) && (c1 & IDLEIE)) || ((isr & PE) && (c1 & PEIE)) || ((isr & LBDF) && (c2 & LBDIE)) ||
        ((isr & CTSIF) && (c3 & CTSIE)) || ((isr & CMF) && (c1 & CMIE)) || ((isr & RTOF) && (c1 & RTOIE)) ||
        ((isr & WUF) && (c3 & WUFIE)) || ((isr & TXFE) && (c1 & TXFEIE) && fifo(u)) ||
        ((isr & RXFF) && (c1 & RXFFIE)) || ((isr & TXFT) && (c3 & TXFTIE)) || ((isr & RXFT) && (c3 & RXFTIE)) ||
        ((isr & (FE | NE | ORE)) && (c3 & EIE)))
        esk_rf_interrupt(u->p, u->irq);
}

/* ---- the frame's format and time --------------------------------------------- */

static int data_bits(const struct uv2 *u)
{
    u32 c1 = v32(u->cr1);
    return (c1 & M1) ? 7 : (c1 & M0) ? 9 : 8;
}

static int stop_halves(const struct uv2 *u)
{
    static const int halves[4] = { 2, 1, 4, 3 };
    int h = halves[v32(u->cr2) >> 12 & 3];
    if (u->lp && (h == 1 || h == 3))
        h = 2; /* an LPUART's: 1 or 2 stop bits */
    return h;
}

static u32 presc_div(const struct uv2 *u)
{
    static const u32 d[12] = { 1, 2, 4, 6, 8, 10, 12, 16, 32, 64, 128, 256 };
    u32 k = v32(u->presc) & 15u;
    return k < 12 ? d[k] : 256;
}

static u32 kernel_hz(const struct uv2 *u)
{
    u32 hz = esk_stm32g4_kernel_hz(u->s, u->p->name);
    if (!hz && !esk_stm32g4_rcc(u->s))
        hz = esk_stm32_pclk(u->s, u->p->gate_name && strstr(u->p->gate_name, "APB2") ? 2 : 1);
    return hz / presc_div(u);
}

/* core cycles per bit */
static u32 bit_cycles(const struct uv2 *u)
{
    u32 brr = v32(u->brr), ker = kernel_hz(u), core = esk_mach_core_hz(u->s);
    u64 num, den, c;
    if (!ker || !brr || !core)
        return 1;
    if (u->lp) {
        num = (u64)brr * core;   /* bit = BRR / (256 x ker) */
        den = (u64)256u * ker;
    } else {
        u32 div = brr & 0xffffu;
        if (v32(u->cr1) & OVER8)
            div = (div & 0xfff0u) | (div & 7u) << 1; /* BRR[3] must be 0 */
        num = (u64)div * core;
        den = ker;
        if (v32(u->cr1) & OVER8)
            den *= 2u;
    }
    c = (num + den / 2) / den;
    return c ? (u32)(c > 0xffffffu ? 0xffffffu : c) : 1;
}

static u64 frame_cycles(const struct uv2 *u)
{
    return (u64)bit_cycles(u) * (u64)(2 * (1 + data_bits(u)) + stop_halves(u)) / 2u;
}

static u32 parity_of(u32 v, int n)
{
    u32 ones = 0;
    int k;
    for (k = 0; k < n; k++)
        ones += v >> k & 1u;
    return ones & 1u;
}

/* the bits on the line (LSB first) of data `v`: MSBFIRST and DATAINV
 * applied to the data, then the parity bit */
static u32 line_bits(const struct uv2 *u, u32 v)
{
    int n = data_bits(u), d = (v32(u->cr1) & PCE) ? n - 1 : n, k;
    u32 data = v & ((1u << d) - 1), out = 0;
    if (v32(u->cr2) & DATAINV)
        data = ~data & ((1u << d) - 1);
    if (v32(u->cr2) & MSBFIRST) {
        for (k = 0; k < d; k++)
            out |= (data >> k & 1u) << (d - 1 - k);
        data = out;
    }
    if (v32(u->cr1) & PCE)
        data |= (parity_of(data, d) ^ ((v32(u->cr1) & PS) ? 1u : 0u)) << d;
    return data;
}

/* the data a frame's line bits carry, and whether its parity is right */
static u32 data_of(const struct uv2 *u, u32 bits, int *parity_ok)
{
    int n = data_bits(u), d = (v32(u->cr1) & PCE) ? n - 1 : n, k;
    u32 data = bits & ((1u << d) - 1), out = 0;
    *parity_ok = 1;
    if (v32(u->cr1) & PCE)
        *parity_ok = (parity_of(bits, n) ^ ((v32(u->cr1) & PS) ? 1u : 0u)) == 0;
    if (v32(u->cr2) & MSBFIRST) {
        for (k = 0; k < d; k++)
            out |= (data >> k & 1u) << (d - 1 - k);
        data = out;
    }
    if (v32(u->cr2) & DATAINV)
        data = ~data & ((1u << d) - 1);
    /* RDR holds the parity bit in its place, as the frame had it */
    if (v32(u->cr1) & PCE)
        data |= (bits >> d & 1u) << d;
    return data;
}

static void run_clock(struct uv2 *u)
{
    esk_rf_clock(u->p, &u->running, u->busy || u->idle_left || u->rto_left || u->ck_next < u->nck);
}

static void pins(struct uv2 *u)
{
    if (u->s->pins_changed)
        u->s->pins_changed(u->s);
}

static void rts_update(struct uv2 *u)
{
    int r = (v32(u->cr3) & RTSE) ? u->nrx >= depth(u) : 1;
    if (r != u->rts) {
        u->rts = r;
        pins(u);
    }
}

static void de_set(struct uv2 *u, int on)
{
    if ((v32(u->cr3) & DEM) && on != u->de) {
        u->de = on;
        pins(u);
    }
}

/* ---- the receiver -------------------------------------------------------- */

static int receiving(const struct uv2 *u)
{
    u32 c1 = v32(u->cr1);
    if (!(c1 & UE) || !(c1 & RE))
        return 0;
    if (u->s->clocks_stopped) {
        /* Stop: only a kernel clock that runs there (HSI16, LSE) with UESM */
        u32 sel = 0;
        struct esk_rf_periph *rcc = esk_rf_periph(u->s->rf, "RCC");
        struct esk_rf_reg *ccipr = rcc ? esk_rf_reg(rcc, "CCIPR") : 0;
        int k = !strcmp(u->p->name, "LPUART1") ? 5 : u->p->name[strlen(u->p->name) - 1] - '1';
        if (ccipr && k >= 0 && k <= 5)
            sel = (u32)(ccipr->value >> (2 * k) & 3);
        return (c1 & UESM) && (sel == 2 || sel == 3);
    }
    return 1;
}

static void wake_event(struct uv2 *u, u32 wus)
{
    u32 c3 = v32(u->cr3);
    if (!u->s->clocks_stopped || ((c3 >> 20) & 3) != wus)
        return;
    isr_set(u, WUF, 0);
    if (c3 & WUFIE)
        esk_rf_interrupt(u->p, u->irq);
}

static void receive(struct uv2 *u, u32 bits, int framing_error)
{
    u32 c1 = v32(u->cr1), c2 = v32(u->cr2), n = (u32)data_bits(u), raw = bits & ((1u << n) - 1), v;
    int brk = framing_error && raw == 0, pok;
    if (!receiving(u))
        return;
    v = data_of(u, raw, &pok);
    u->idle_left = frame_cycles(u);
    u->rto_left = (c2 & RTOEN) ? (u64)bit_cycles(u) * (v32(u->rtor) & 0xffffffu) : 0;
    run_clock(u);
    if (brk && (c2 & LINEN))
        isr_set(u, LBDF, 0);
    {
        /* address mark (WAKE 1): the data's MSB (below the parity bit) set,
         * ADD's 4 (or 7, ADDM7) low bits matching */
        u32 msb = (c1 & PCE) ? n - 2 : n - 1, amask = (c2 & ADDM7) ? 0x7fu : 0x0fu;
        int address = (c1 & WAKE) && (v >> msb & 1);
        int ours = address && (v & amask) == ((c2 >> 24) & amask);
        if (ours)
            wake_event(u, 0);
        if (v32(u->isr) & RWU) {
            if (!ours) {
                update_irq(u);
                return;
            }
            isr_set(u, 0, RWU);
        } else if (address && !ours && (c1 & MME)) {
            isr_set(u, RWU, 0);
            update_irq(u);
            return;
        }
    }
    if ((v & 0xffu) == (c2 >> 24))
        isr_set(u, CMF, 0);
    if (u->nrx >= depth(u)) {
        if (v32(u->cr3) & OVRDIS) {
            u->rxq[u->nrx - 1] = (u16)v; /* the last frame overwritten */
        } else {
            isr_set(u, ORE, 0);
        }
    } else {
        u->rxq[u->nrx] = (u16)v;
        u->rxerr[u->nrx] = (u8)((pok ? 0 : PE) | (framing_error ? FE : 0));
        u->nrx++;
        if (u->nrx == 1) {
            esk_rf_hw(u->rdr, u->rxq[0]);
            isr_set(u, u->rxerr[0], 0);
        }
    }
    flags(u);
    wake_event(u, 3);
    rts_update(u);
    update_irq(u);
    dma_check(u);
}

static u32 uv2_format(void *ctx)
{
    const struct uv2 *u = (const struct uv2 *)ctx;
    return (u32)data_bits(u) | (u32)stop_halves(u) << 8;
}

static void uv2_put_frame(void *ctx, u32 bits, int framing_error)
{
    receive((struct uv2 *)ctx, bits, framing_error);
}

static void uv2_started(void *ctx)
{
    struct uv2 *u = (struct uv2 *)ctx;
    u->idle_left = 0;
    u->rto_left = 0;
    if (receiving(u))
        wake_event(u, 2);
}

/* the session's serial input: a byte as a terminal sends it (LSB first,
 * the parity it is set up for); the receiver reads it as its own format
 * says, MSBFIRST and DATAINV included */
static void uv2_put(void *ctx, int c)
{
    struct uv2 *u = (struct uv2 *)ctx;
    int n = data_bits(u), d = (v32(u->cr1) & PCE) ? n - 1 : n;
    u32 bits = (u32)c & ((1u << d) - 1);
    if (v32(u->cr1) & PCE)
        bits |= (parity_of(bits, d) ^ ((v32(u->cr1) & PS) ? 1u : 0u)) << d;
    receive(u, bits, 0);
}

static int uv2_room(void *ctx)
{
    struct uv2 *u = (struct uv2 *)ctx;
    return (v32(u->cr1) & UE) && (v32(u->cr1) & RE) && !(v32(u->cr2) & CLKEN);
}

static u32 uv2_bit_cycles(void *ctx)
{
    return bit_cycles((const struct uv2 *)ctx);
}

/* ---- the transmitter ----------------------------------------------------- */

static void sync_clock(struct uv2 *u, u64 b)
{
    u32 c2 = v32(u->cr2);
    int n = data_bits(u), k, pulses = (c2 & LBCL) ? n : n - 1;
    u->nck = 0;
    u->ck_next = 0;
    u->sync_rx = 0;
    u->sync_bits = 0;
    for (k = 0; k < pulses && u->nck + 2 <= CK_MAX; k++) {
        u64 bit0 = b + (u64)k * b;
        u64 e1 = (c2 & CPHA) ? bit0 : bit0 + b / 2, e2 = e1 + b / 2;
        u->ck_at[u->nck] = e1;
        u->ck_sample[u->nck++] = !(c2 & CPHA);
        u->ck_at[u->nck] = e2;
        u->ck_sample[u->nck++] = (c2 & CPHA) != 0;
    }
}

static void start_frame(struct uv2 *u, u32 v, int brk, int idle)
{
    struct esk_mach *s = u->s;
    int n = data_bits(u);
    u64 b = bit_cycles(u);
    u->busy = 1;
    u->brk = brk;
    u->idle_frame = idle;
    u->left = frame_cycles(u);
    isr_set(u, BUSY, TC);
    if (brk) {
        int zeros = (v32(u->cr2) & LINEN) ? 12 : n + 1;
        u->left = b * (u64)(1 + zeros) + b;
        isr_set(u, SBKF, 0);
        if (s->host.uart_frame)
            s->host.uart_frame(s->host.ctx, u->p->name, -1, 0, zeros, 2, (u32)b);
    } else if (!idle) {
        u32 bits = line_bits(u, v);
        int d = (v32(u->cr1) & PCE) ? n - 1 : n;
        int c = (int)(bits & ((1u << (d > 8 ? 8 : d)) - 1));
        de_set(u, 1);
        if (s->host.uart_frame)
            s->host.uart_frame(s->host.ctx, u->p->name, c, bits, n, stop_halves(u), (u32)b);
        else if (s->host.uart_tx)
            s->host.uart_tx(s->host.ctx, u->p->name, c, (u32)b);
        if (v32(u->cr2) & CLKEN)
            sync_clock(u, b);
    }
    run_clock(u);
}

static void next_frame(struct uv2 *u)
{
    if (u->busy)
        return;
    if (!(v32(u->cr1) & UE) || !(v32(u->cr1) & TE))
        return;
    if ((v32(u->cr3) & CTSE) && u->cts)
        return;
    if (v32(u->isr) & SBKF) {
        start_frame(u, 0, 1, 0);
        return;
    }
    if (u->ntx) {
        u16 v = u->txq[0];
        memmove(u->txq, u->txq + 1, (size_t)(u->ntx - 1) * sizeof u->txq[0]);
        u->ntx--;
        flags(u);
        start_frame(u, v, 0, 0);
    }
}

static void frame_end(struct uv2 *u)
{
    u->busy = 0;
    u->left = 0;
    if (u->brk)
        isr_set(u, 0, SBKF);
    u->brk = u->idle_frame = 0;
    next_frame(u);
    if (!u->busy) {
        isr_set(u, TC, BUSY);
        de_set(u, 0);
    }
    flags(u);
}

static int rx_level(struct uv2 *u)
{
    int port, bit, lvl = 1;
    if (esk_stm32_usartv2_pin(u->s, u->p->name, 0, &port, &bit) == 0 && u->s->host.pin_in &&
        !u->s->host.pin_in(u->s->host.ctx, port, bit, &lvl))
        lvl = 1;
    return lvl != 0;
}

static void sync_edges(struct uv2 *u, u64 elapsed)
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

/* ---- the device -------------------------------------------------------------- */

static void uv2_tick(void *ctx, u32 cycles)
{
    struct uv2 *u = (struct uv2 *)ctx;
    if (u->idle_left) {
        if (u->idle_left > cycles) {
            u->idle_left -= cycles;
        } else {
            u->idle_left = 0;
            if ((v32(u->cr1) & UE) && (v32(u->cr1) & RE)) {
                if ((v32(u->isr) & RWU) && !(v32(u->cr1) & WAKE))
                    isr_set(u, 0, RWU); /* an idle line wakes it */
                else if (!(v32(u->isr) & RWU))
                    isr_set(u, IDLE, 0);
                update_irq(u);
            }
        }
    }
    if (u->rto_left) {
        if (u->rto_left > cycles) {
            u->rto_left -= cycles;
        } else {
            u->rto_left = 0;
            isr_set(u, RTOF, 0);
            update_irq(u);
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

static int uv2_next(void *ctx, u32 *cycles)
{
    struct uv2 *u = (struct uv2 *)ctx;
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
    if (u->rto_left && (!best || u->rto_left < best))
        best = u->rto_left;
    if (!best)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static void take_rx(struct uv2 *u)
{
    if (!u->nrx)
        return;
    memmove(u->rxq, u->rxq + 1, (size_t)(u->nrx - 1) * sizeof u->rxq[0]);
    memmove(u->rxerr, u->rxerr + 1, (size_t)(u->nrx - 1));
    u->nrx--;
    if (u->nrx) {
        esk_rf_hw(u->rdr, u->rxq[0]);
        isr_set(u, u->rxerr[0], 0); /* the next frame's errors show */
    }
}

static u32 uv2_read(void *ctx, u32 off, int n)
{
    struct uv2 *u = (struct uv2 *)ctx;
    u32 v;
    if (off == esk_rf_off(u->rdr)) {
        v = u->nrx ? u->rxq[0] : (u32)u->rdr->value;
        if (u->s->bus.debug)
            return v;
        take_rx(u);
        flags(u);
        rts_update(u);
        update_irq(u);
        dma_check(u);
        return v;
    }
    return esk_rf_read(u->p, off, n);
}

static void enables(struct uv2 *u, u32 was)
{
    u32 c1 = v32(u->cr1);
    u32 set = 0, clear = 0;
    if ((c1 & UE) && (c1 & TE))
        set |= TEACK;
    else
        clear |= TEACK;
    if ((c1 & UE) && (c1 & RE))
        set |= REACK;
    else
        clear |= REACK;
    isr_set(u, set, clear);
    if ((c1 & UE) && (c1 & TE) && !((was & UE) && (was & TE)) && !u->busy)
        start_frame(u, 0x1ff, 0, 1); /* TE: an idle frame */
    if (!(c1 & UE)) {
        /* disabled: the transmitter and receiver stop, their queues empty */
        u->ntx = u->nrx = 0;
        u->busy = 0;
        u->left = u->idle_left = u->rto_left = 0;
        u->nck = u->ck_next = 0;
        esk_rf_hw(u->isr, TXE | TC | TXFE); /* UE clear: the flags as at reset */
        de_set(u, 0);
    }
    if ((c1 & UE) != (was & UE) || (c1 & TE) != (was & TE))
        pins(u);
}

static void uv2_write(void *ctx, u32 off, int n, u32 v)
{
    struct uv2 *u = (struct uv2 *)ctx;
    u32 c1 = v32(u->cr1);
    if (off == esk_rf_off(u->tdr)) {
        if (!(c1 & UE))
            return;
        if (u->ntx < depth(u)) {
            u->txq[u->ntx++] = (u16)(v & 0x1ffu);
            isr_set(u, 0, TC);
        }
        flags(u);
        next_frame(u);
        update_irq(u);
        dma_check(u);
        return;
    }
    if (off == esk_rf_off(u->icr)) {
        u32 clr = v & (PE | FE | NE | ORE | IDLE | TC | LBDF | CTSIF | RTOF | (1u << 12) | CMF | WUF);
        if (v & (1u << 7))
            clr |= 1u << 25; /* TCBGTCF */
        isr_set(u, 0, clr);
        update_irq(u);
        dma_check(u);
        return;
    }
    if (off == esk_rf_off(u->rqr)) {
        if (v & SBKRQ) {
            isr_set(u, SBKF, 0);
            next_frame(u);
        }
        if (v & MMRQ && (c1 & MME))
            isr_set(u, RWU, 0);
        if (v & RXFRQ) {
            u->nrx = 0;
            isr_set(u, 0, RXNE);
        }
        if (v & TXFRQ)
            u->ntx = 0;
        flags(u);
        rts_update(u);
        update_irq(u);
        dma_check(u);
        return;
    }
    esk_rf_write(u->p, off, n, v);
    if (off == esk_rf_off(u->cr1))
        enables(u, c1);
    if (off == esk_rf_off(u->cr2) || off == esk_rf_off(u->cr1)) {
        int ck = (v32(u->cr2) & CLKEN) && (v32(u->cr1) & UE) ? (v32(u->cr2) & CPOL) != 0 : -1;
        if (ck != u->ck && u->ck_next >= u->nck) {
            u->ck = ck;
            pins(u);
        }
    }
    flags(u);
    rts_update(u);
    run_clock(u);
    update_irq(u);
    dma_check(u);
}

static void uv2_reset(void *ctx)
{
    struct uv2 *u = (struct uv2 *)ctx;
    u->ntx = u->nrx = 0;
    u->busy = u->brk = u->idle_frame = 0;
    u->left = u->idle_left = u->rto_left = 0;
    u->running = 0;
    u->rts = 1;
    u->de = 0;
    u->ck = -1;
    u->nck = u->ck_next = 0;
}

const struct esk_dev_ops esk_stm32_usartv2_ops = {
    "stm32-usart-v2", uv2_read, uv2_write, uv2_reset, uv2_tick, uv2_next,
};

/* ---- the pins -------------------------------------------------------------- */

static struct uv2 *uv2_named(struct esk_mach *s, const char *name)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_stm32_usartv2_ops && !esk_rf_gated(p) ? (struct uv2 *)p->ctx : 0;
}

static struct uv2 *signal_of(struct esk_mach *s, const char *sig, const char **fn)
{
    char name[16];
    const char *us = strchr(sig, '_');
    size_t l = us ? (size_t)(us - sig) : 0;
    if (!us || l >= sizeof name || (strncmp(sig, "USART", 5) && strncmp(sig, "UART", 4) && strncmp(sig, "LPUART", 6)))
        return 0;
    memcpy(name, sig, l);
    name[l] = 0;
    *fn = us + 1;
    return uv2_named(s, name);
}

int esk_stm32_usartv2_signal(struct esk_mach *s, const char *sig)
{
    const char *fn;
    struct uv2 *u = signal_of(s, sig, &fn);
    u32 c2;
    if (!u || !(v32(u->cr1) & UE))
        return -1;
    c2 = v32(u->cr2);
    if (!strcmp(fn, (c2 & SWAP) ? "RX" : "TX"))
        return (v32(u->cr1) & TE) || (v32(u->cr3) & HDSEL) ? 1 : -1;
    if (!strcmp(fn, "DE") && (v32(u->cr3) & DEM))
        return u->de ? !(v32(u->cr3) & DEP) : (v32(u->cr3) & DEP) != 0;
    if (!strcmp(fn, "RTS"))
        return (v32(u->cr3) & RTSE) ? u->rts : -1;
    if (!strcmp(fn, "CK"))
        return u->ck;
    return -1;
}

void esk_stm32_usartv2_signal_input(struct esk_mach *s, const char *sig, int level)
{
    const char *fn;
    struct uv2 *u = signal_of(s, sig, &fn);
    if (!u || strcmp(fn, "CTS"))
        return;
    level = level != 0;
    if (level == u->cts)
        return;
    u->cts = level;
    isr_set(u, level ? 0 : CTS, level ? CTS : 0); /* CTS: the nCTS line's state, inverted */
    if (v32(u->cr3) & CTSE) {
        isr_set(u, CTSIF, 0);
        next_frame(u);
        update_irq(u);
    }
}

int esk_stm32_usartv2_pin(struct esk_mach *s, const char *unit, int tx, int *port, int *bit)
{
    struct uv2 *u = uv2_named(s, unit);
    char sig[24];
    int at;
    if (u && !tx && (v32(u->cr3) & HDSEL))
        tx = 1;
    if (u && (v32(u->cr2) & SWAP))
        tx = !tx;
    snprintf(sig, sizeof sig, "%s_%s", unit, tx ? "TX" : "RX");
    at = esk_stm32f4_af_pin(s, sig);
    if (at < 0)
        return -1;
    *port = at / 32;
    *bit = at % 32;
    return 0;
}

void *esk_stm32_usartv2_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct uv2 *u;
    if (!esk_rf_reg(p, "ISR") || !esk_rf_reg(p, "ICR") || !esk_rf_reg(p, "TDR") || !esk_rf_reg(p, "RDR") ||
        !esk_rf_reg(p, "CR1") || !esk_rf_reg(p, "BRR"))
        return 0;
    u = (struct uv2 *)esk_mach_alloc(s, sizeof *u);
    if (!u)
        return 0;
    u->s = s;
    u->p = p;
    u->cr1 = esk_rf_reg(p, "CR1");
    u->cr2 = esk_rf_reg(p, "CR2");
    u->cr3 = esk_rf_reg(p, "CR3");
    u->brr = esk_rf_reg(p, "BRR");
    u->rtor = esk_rf_reg(p, "RTOR");
    u->rqr = esk_rf_reg(p, "RQR");
    u->isr = esk_rf_reg(p, "ISR");
    u->icr = esk_rf_reg(p, "ICR");
    u->rdr = esk_rf_reg(p, "RDR");
    u->tdr = esk_rf_reg(p, "TDR");
    u->presc = esk_rf_reg(p, "PRESC");
    if (!u->cr2 || !u->cr3)
        return 0;
    u->lp = !strncmp(p->name, "LPUART", 6);
    u->irq = esk_rf_irq(p, 0);
    /* ISR is the hardware's: its flags are cleared through ICR, RQR */
    esk_rf_set_acc(u->isr, ~(u64)0, ESK_ACC_RO);
    esk_rf_set_acc(u->icr, ~(u64)0, ESK_ACC_WO);
    esk_rf_set_acc(u->rdr, ~(u64)0, ESK_ACC_RO);
    esk_rf_set_reset(u->isr, TXE | TC | TXFE);
    uv2_reset(u);
    u->cts = 1;
    snprintf(u->sig_tx, sizeof u->sig_tx, "%s_TX", p->name);
    snprintf(u->sig_rx, sizeof u->sig_rx, "%s_RX", p->name);
    s->uart_pin = esk_stm32_usartv2_pin;
    esk_mach_rx_port(s, p->name, uv2_room, uv2_put, uv2_bit_cycles, u);
    esk_mach_rx_frames(s, uv2_format, uv2_put_frame, uv2_started);
    return u;
}
