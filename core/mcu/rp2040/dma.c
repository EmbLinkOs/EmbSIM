/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* dma.c -- the RP2040's DMA controller (its datasheet, 2.5).
 *
 *   - twelve channels, each a READ_ADDR, WRITE_ADDR, TRANS_COUNT and CTRL
 *     seen through four aliases (CTRL_TRIG; AL1 CTRL, READ, WRITE,
 *     TRANS_COUNT_TRIG; AL2 CTRL, TRANS_COUNT, READ, WRITE_ADDR_TRIG; AL3
 *     CTRL, WRITE, TRANS_COUNT, READ_ADDR_TRIG): writing an alias's last
 *     register triggers the channel when CTRL.EN is set; a write of 0 there
 *     is a null trigger, an interrupt when IRQ_QUIET is set. TRANS_COUNT
 *     written is the count the next trigger takes, read the transfers left;
 *     READ_ADDR and WRITE_ADDR read where the channel is;
 *   - each transfer reads DATA_SIZE bytes (1, 2 or 4) and writes them
 *     through the bus as the core would, so a FIFO it reads or writes moves
 *     as it does for the core; INCR_READ and INCR_WRITE step the addresses,
 *     RING_SIZE wraps the one RING_SEL names on a 2^n byte boundary, BSWAP
 *     reverses the bytes;
 *   - one transfer a clk_sys cycle among the channels ready to transfer, the
 *     HIGH_PRIORITY ones first, round robin within each;
 *   - a channel is ready while its TREQ_SEL's request is: a peripheral's
 *     DREQ (the PIOs' state machine FIFOs, SPI, UART, PWM wraps, I2C, the
 *     ADC), one of the four pacing timers (TIMERn's X/Y of clk_sys), or
 *     permanent (0x3f);
 *   - when its count reaches 0 the channel stops (BUSY clears), sets its
 *     INTR bit unless IRQ_QUIET, and triggers CHAIN_TO when that is another
 *     channel; INTS0 and INTS1 ((INTR | INTFn) & INTEn) raise DMA_IRQ_0 and
 *     DMA_IRQ_1; writing INTR, INTS0 or INTS1 clears the bits written;
 *   - MULTI_CHAN_TRIGGER triggers the channels it names, CHAN_ABORT stops
 *     them; a bus error stops the channel with READ_ERROR or WRITE_ERROR and
 *     AHB_ERROR;
 *   - the sniffer (SNIFF_CTRL): the data of channel DMACH, when that
 *     channel's SNIFF_EN is set, into SNIFF_DATA as a CRC-32, a CRC-32 of
 *     bit-reversed data, a CRC-16-CCITT, the same of bit-reversed data, an
 *     XOR or a sum, the data byte-swapped first with BSWAP; SNIFF_DATA read
 *     bit-reversed with OUT_REV and inverted with OUT_INV.
 * Omissions: the bus's arbitration with the cores (a transfer takes no
 * cycles from them), the XIP streaming DREQs, the debug registers
 * (CHn_DBG_CTDREQ, CHn_DBG_TCR: plain), the DREQ counters' exact behaviour
 * against a peripheral whose request drops between two transfers. */
#include <string.h>

#include "mcu/rp2040/rp2040.h"

#define NCH 12
#define CTRL_EN 0x1u
#define CTRL_HIGH 0x2u
#define CTRL_INCR_READ 0x10u
#define CTRL_INCR_WRITE 0x20u
#define CTRL_RING_SEL 0x400u
#define CTRL_IRQ_QUIET 0x200000u
#define CTRL_BSWAP 0x400000u
#define CTRL_SNIFF 0x800000u
#define CTRL_BUSY 0x1000000u
#define CTRL_WERR 0x20000000u
#define CTRL_RERR 0x40000000u
#define CTRL_AHB 0x80000000u

struct chan {
    u32 read, write, count, reload, ctrl;
};

struct dma {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct chan ch[NCH];
    u32 intr, sniff;
    u32 credit[64];       /* pulses not yet used, for the requests that are pulses */
    u64 tacc[4];          /* the pacing timers' X/Y accumulators */
    int last_hi, last_lo; /* round robin */
    int irq0, irq1, was0, was1, running;
};

static struct esk_rf_reg *at(struct dma *d, u32 off)
{
    int i;
    for (i = 0; i < d->p->nreg; i++)
        if (d->p->reg[i]->addr == d->p->base + off)
            return d->p->reg[i];
    return 0;
}

static u32 rv(struct dma *d, u32 off)
{
    struct esk_rf_reg *r = at(d, off);
    return r ? (u32)r->value : 0;
}

static void hw(struct dma *d, u32 off, u32 v)
{
    struct esk_rf_reg *r = at(d, off);
    if (r)
        esk_rf_hw(r, v);
}

/* the channel's registers as the program reads them, in every alias */
static void sync(struct dma *d, int n)
{
    struct chan *c = &d->ch[n];
    u32 b = (u32)n * 0x40u;
    hw(d, b + 0x00, c->read);
    hw(d, b + 0x04, c->write);
    hw(d, b + 0x08, c->count);
    hw(d, b + 0x0c, c->ctrl);
    hw(d, b + 0x10, c->ctrl);
    hw(d, b + 0x14, c->read);
    hw(d, b + 0x18, c->write);
    hw(d, b + 0x1c, c->count);
    hw(d, b + 0x20, c->ctrl);
    hw(d, b + 0x24, c->count);
    hw(d, b + 0x28, c->read);
    hw(d, b + 0x2c, c->write);
    hw(d, b + 0x30, c->ctrl);
    hw(d, b + 0x34, c->write);
    hw(d, b + 0x38, c->count);
    hw(d, b + 0x3c, c->read);
}

static void irqs(struct dma *d)
{
    u32 s0 = (d->intr | rv(d, 0x408)) & rv(d, 0x404) & 0xfffu;
    u32 s1 = (d->intr | rv(d, 0x418)) & rv(d, 0x414) & 0xfffu;
    hw(d, 0x400, d->intr);
    hw(d, 0x410, d->intr);
    hw(d, 0x40c, s0);
    hw(d, 0x41c, s1);
    esk_rf_irq_level(d->p, d->irq0, s0 != 0, &d->was0);
    esk_rf_irq_level(d->p, d->irq1, s1 != 0, &d->was1);
}

static int any_busy(const struct dma *d)
{
    int i;
    for (i = 0; i < NCH; i++)
        if (d->ch[i].ctrl & CTRL_BUSY)
            return 1;
    return 0;
}

static void clock_it(struct dma *d) { esk_rf_clock(d->p, &d->running, any_busy(d)); }

static void trigger(struct dma *d, int n)
{
    struct chan *c = &d->ch[n];
    if (!(c->ctrl & CTRL_EN))
        return;
    if (!(c->ctrl & CTRL_BUSY)) {
        c->count = c->reload;
        c->ctrl |= CTRL_BUSY;
        if (!c->count) { /* nothing to transfer: done at once */
            c->ctrl &= ~CTRL_BUSY;
            if (!(c->ctrl & CTRL_IRQ_QUIET))
                d->intr |= 1u << n;
        }
    }
    sync(d, n);
}

/* ---- the requests ------------------------------------------------------- */

void esk_rp2040_dreq_pulse(struct esk_mach *s, void **dma, int dreq)
{
    struct dma *d = (struct dma *)*dma;
    if (!d)
        *dma = d = (struct dma *)esk_rp_model(s, "DMA", &esk_rp2040_dma_ops);
    if (d && dreq >= 0 && dreq < 64 && d->credit[dreq] < 0xffffu)
        d->credit[dreq]++;
}

/* the channel's request now; a pulse request is used up by the transfer */
static int ready(struct dma *d, int sel, int take)
{
    struct esk_mach *s = d->s;
    int r = 0;
    if (sel == 0x3f)
        return 1;
    if (sel < 16)
        r = esk_rp2040_pio_dreq(s, sel / 8, sel % 4, (sel / 4) & 1);
    else if (sel < 20)
        r = esk_rp2040_spi_dreq(s, (sel - 16) / 2, (sel - 16) & 1);
    else if (sel < 24)
        r = esk_rp2040_uart_dreq(s, (sel - 20) / 2, (sel - 20) & 1);
    else if (sel < 32 || (sel >= 0x3b && sel <= 0x3e)) {
        r = d->credit[sel] > 0;
        if (r && take)
            d->credit[sel]--;
    } else if (sel < 36)
        r = esk_rp2040_i2c_dreq(s, (sel - 32) / 2, (sel - 32) & 1);
    else if (sel == 36)
        r = esk_rp2040_adc_dreq(s);
    return r;
}

/* ---- the sniffer -------------------------------------------------------- */

static u32 rev32(u32 v)
{
    u32 r = 0;
    int i;
    for (i = 0; i < 32; i++)
        r |= (v >> i & 1u) << (31 - i);
    return r;
}

static u8 rev8(u8 v) { return (u8)(rev32(v) >> 24); }

static void sniff(struct dma *d, u32 data, int size)
{
    u32 sc = rv(d, 0x434), calc = sc >> 5 & 15u, crc = d->sniff;
    int i, b;
    if (sc & 0x200u) { /* BSWAP: the bytes reversed */
        u32 v = 0;
        for (i = 0; i < size; i++)
            v |= (data >> (8 * i) & 0xffu) << (8 * (size - 1 - i));
        data = v;
    }
    if (calc == 0xe) {
        crc ^= data;
    } else if (calc == 0xf) {
        crc += data;
    } else if (calc <= 3) {
        for (i = 0; i < size; i++) {
            u8 byte = (u8)(data >> (8 * i));
            if (calc & 1u)
                byte = rev8(byte);
            if (calc < 2) { /* CRC-32, 0x04C11DB7, MSB first */
                crc ^= (u32)byte << 24;
                for (b = 0; b < 8; b++)
                    crc = (crc & 0x80000000u) ? crc << 1 ^ 0x04c11db7u : crc << 1;
            } else { /* CRC-16-CCITT, 0x1021, MSB first */
                crc ^= (u32)byte << 8;
                for (b = 0; b < 8; b++)
                    crc = (crc & 0x8000u) ? (crc << 1 ^ 0x1021u) & 0xffffu : (crc << 1) & 0xffffu;
            }
        }
    }
    d->sniff = crc;
    hw(d, 0x438, (sc & 0x400u ? rev32(crc) : crc) ^ (sc & 0x800u ? 0xffffffffu : 0u));
}

/* ---- the transfers ------------------------------------------------------ */

static u32 step_addr(u32 a, u32 size, u32 ring, int incr)
{
    if (!incr)
        return a;
    if (!ring)
        return a + size;
    return (a & ~((1u << ring) - 1u)) | ((a + size) & ((1u << ring) - 1u));
}

/* one transfer of channel n */
static void transfer(struct dma *d, int n)
{
    struct chan *c = &d->ch[n];
    u32 size = 1u << (c->ctrl >> 2 & 3u), v = 0, ring = c->ctrl >> 6 & 15u, i;
    int sel = (int)(c->ctrl >> 15 & 0x3fu);
    if (size > 4)
        size = 4;
    (void)ready(d, sel, 1);
    if (esk_bus_read(&d->s->bus, c->read & ~(size - 1u), (int)size, &v)) {
        c->ctrl = (c->ctrl & ~CTRL_BUSY) | CTRL_RERR | CTRL_AHB;
        sync(d, n);
        return;
    }
    if (c->ctrl & CTRL_BSWAP) {
        u32 w = 0;
        for (i = 0; i < size; i++)
            w |= (v >> (8 * i) & 0xffu) << (8 * (size - 1 - i));
        v = w;
    }
    if ((c->ctrl & CTRL_SNIFF) && (rv(d, 0x434) & 1u) && (int)(rv(d, 0x434) >> 1 & 15u) == n)
        sniff(d, v, (int)size);
    if (esk_bus_write(&d->s->bus, c->write & ~(size - 1u), (int)size, v)) {
        c->ctrl = (c->ctrl & ~CTRL_BUSY) | CTRL_WERR | CTRL_AHB;
        sync(d, n);
        return;
    }
    c->read = step_addr(c->read, size, (c->ctrl & CTRL_RING_SEL) ? 0 : ring, (c->ctrl & CTRL_INCR_READ) != 0);
    c->write = step_addr(c->write, size, (c->ctrl & CTRL_RING_SEL) ? ring : 0, (c->ctrl & CTRL_INCR_WRITE) != 0);
    if (--c->count == 0) {
        int to = (int)(c->ctrl >> 11 & 15u);
        c->ctrl &= ~CTRL_BUSY;
        if (!(c->ctrl & CTRL_IRQ_QUIET))
            d->intr |= 1u << n;
        sync(d, n);
        if (to != n && to < NCH)
            trigger(d, to);
        irqs(d);
        return;
    }
    sync(d, n);
}

/* the next channel to transfer: high priority first, round robin */
static int pick(struct dma *d)
{
    int pass, k;
    for (pass = 0; pass < 2; pass++) {
        int *last = pass ? &d->last_lo : &d->last_hi;
        for (k = 1; k <= NCH; k++) {
            int n = (*last + k) % NCH;
            struct chan *c = &d->ch[n];
            if (!(c->ctrl & CTRL_BUSY) || ((c->ctrl & CTRL_HIGH) != 0) == pass)
                continue;
            if (ready(d, (int)(c->ctrl >> 15 & 0x3fu), 0)) {
                *last = n;
                return n;
            }
        }
    }
    return -1;
}

static void pace(struct dma *d, u32 cycles)
{
    int t;
    for (t = 0; t < 4; t++) {
        u32 x = rv(d, 0x420u + 4u * (u32)t) >> 16, y = rv(d, 0x420u + 4u * (u32)t) & 0xffffu;
        u64 n;
        if (!x || !y)
            continue;
        d->tacc[t] += (u64)cycles * x;
        n = d->tacc[t] / y;
        d->tacc[t] %= y;
        d->credit[0x3b + t] = (u32)(d->credit[0x3b + t] + n > 0xffffu ? 0xffffu : d->credit[0x3b + t] + n);
    }
}

static void dma_tick(void *ctx, u32 cycles)
{
    struct dma *d = (struct dma *)ctx;
    u32 k;
    pace(d, cycles);
    for (k = 0; k < cycles; k++) {
        int n = pick(d);
        if (n < 0)
            break;
        transfer(d, n);
    }
    clock_it(d);
}

static int dma_next(void *ctx, u32 *cycles)
{
    struct dma *d = (struct dma *)ctx;
    int i;
    for (i = 0; i < NCH; i++)
        if ((d->ch[i].ctrl & CTRL_BUSY) && ready(d, (int)(d->ch[i].ctrl >> 15 & 0x3fu), 0)) {
            *cycles = 1;
            return 1;
        }
    /* a pacing timer's next pulse, for a channel it paces */
    for (i = 0; i < NCH; i++) {
        int sel = (int)(d->ch[i].ctrl >> 15 & 0x3fu);
        if ((d->ch[i].ctrl & CTRL_BUSY) && sel >= 0x3b && sel <= 0x3e) {
            u32 t = rv(d, 0x420u + 4u * (u32)(sel - 0x3b)), x = t >> 16, y = t & 0xffffu;
            if (x && y) {
                u64 need = (y - d->tacc[sel - 0x3b] + x - 1) / x;
                *cycles = need ? (u32)need : 1u;
                return 1;
            }
        }
    }
    return 0;
}

static u32 dma_read(void *ctx, u32 off, int n)
{
    struct dma *d = (struct dma *)ctx;
    return esk_rf_read(d->p, off, n);
}

static void dma_write(void *ctx, u32 off, int n, u32 v)
{
    struct dma *d = (struct dma *)ctx;
    u32 o = off & ~3u;
    if (o < NCH * 0x40u) {
        int ch = (int)(o / 0x40u), trig = 0;
        struct chan *c = &d->ch[ch];
        u32 r = o % 0x40u;
        /* which register of the channel this alias names */
        static const char kind[16] = { 'R', 'W', 'T', 'C', 'C', 'R', 'W', 'T', 'C', 'T', 'R', 'W', 'C', 'W', 'T', 'R' };
        char k = kind[r / 4];
        trig = (r & 0xcu) == 0xcu;
        if (k == 'R')
            c->read = v;
        else if (k == 'W')
            c->write = v;
        else if (k == 'T')
            c->reload = v;
        else { /* CTRL: BUSY and AHB_ERROR read-only, the error bits cleared by a 1 */
            u32 keep = c->ctrl & (CTRL_BUSY | CTRL_AHB | CTRL_RERR | CTRL_WERR);
            keep &= ~(v & (CTRL_RERR | CTRL_WERR));
            c->ctrl = (v & ~(CTRL_BUSY | CTRL_AHB | CTRL_RERR | CTRL_WERR)) | keep;
            if (!(c->ctrl & (CTRL_RERR | CTRL_WERR)))
                c->ctrl &= ~CTRL_AHB;
        }
        if (trig) {
            if (v == 0) { /* a null trigger */
                if (c->ctrl & CTRL_IRQ_QUIET)
                    d->intr |= 1u << ch;
            } else {
                trigger(d, ch);
            }
        }
        sync(d, ch);
    } else {
        esk_rf_write(d->p, off, n, v);
        if (o == 0x400u || o == 0x40cu || o == 0x41cu) /* INTR, INTS0, INTS1: write 1 to clear */
            d->intr &= ~v;
        else if (o == 0x430u) { /* MULTI_CHAN_TRIGGER */
            int i;
            for (i = 0; i < NCH; i++)
                if (v >> i & 1u)
                    trigger(d, i);
        } else if (o == 0x444u) { /* CHAN_ABORT: stopped at once */
            int i;
            for (i = 0; i < NCH; i++)
                if (v >> i & 1u) {
                    d->ch[i].ctrl &= ~CTRL_BUSY;
                    sync(d, i);
                }
            hw(d, 0x444, 0);
        } else if (o == 0x438u) {
            d->sniff = v;
        }
    }
    irqs(d);
    clock_it(d);
}

static void dma_reset(void *ctx)
{
    struct dma *d = (struct dma *)ctx;
    int i;
    memset(d->ch, 0, sizeof d->ch);
    for (i = 0; i < NCH; i++) {
        d->ch[i].ctrl = (u32)i << 11; /* CHAIN_TO: itself (none) */
        sync(d, i);
    }
    d->intr = d->sniff = 0;
    memset(d->credit, 0, sizeof d->credit);
    memset(d->tacc, 0, sizeof d->tacc);
    hw(d, 0x448, NCH);
    irqs(d);
    clock_it(d);
}

const struct esk_dev_ops esk_rp2040_dma_ops = { "rp2040-dma", dma_read, dma_write, dma_reset, dma_tick, dma_next };

void *esk_rp2040_dma_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct dma *d = (struct dma *)esk_mach_alloc(s, sizeof *d);
    if (!d)
        return 0;
    d->s = s;
    d->p = p;
    d->irq0 = esk_rf_irq(p, "DMA_IRQ_0");
    d->irq1 = esk_rf_irq(p, "DMA_IRQ_1");
    return d;
}
