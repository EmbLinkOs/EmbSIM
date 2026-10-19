/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* dma_f4.c -- the STM32F2/F4/F7 DMA controller (RM0390 9): eight streams,
 * each taking one of eight channels' requests (CHSEL), over the register
 * file.
 *
 *   - a request is a peripheral's (esk_stm32_dma_request: USART2_TX,
 *     TIM1_UP, ...); the description's dma_requests says which stream and
 *     channel carry it. It is a level: while no enabled stream takes it, it
 *     waits, and a stream enabled on its channel serves it at once;
 *   - a stream (EN) moves data peripheral-to-memory, memory-to-peripheral
 *     or memory-to-memory (DMA2 only: no requests, as fast as the bus
 *     lets it, one item every two cycles) through the bus as the core's
 *     accesses go, so a peripheral sees the DMA read its data register or
 *     write it (PSIZE, MSIZE, PINC, MINC, PINCOS's 4-byte step);
 *   - direct mode (DMDIS 0) moves one item a request, of PSIZE; the FIFO
 *     (DMDIS 1, forced on for memory-to-memory) packs PSIZE items into
 *     MSIZE ones: peripheral data is written to memory once FTH's threshold
 *     fills (and at the end), memory data is read ahead to fill it; a
 *     request moves a peripheral burst (PBURST: 1, 4, 8 or 16 items); FS
 *     reports the FIFO's level; a memory burst that FTH's threshold cannot
 *     hold is a FIFO error at enable (FEIF), the stream not enabled;
 *   - NDTR counts the items left: half of them done sets HTIF, all of them
 *     TCIF, the last two telling the peripheral (the end of transfer an
 *     SPI sends its CRC after, the EOT-1 an I2C NACKs the next byte on); circular mode (CIRC) or double buffer
 *     (DBM, CT toggling between M0AR and M1AR) starts again, else EN
 *     clears; an access the bus refuses sets TEIF and clears EN;
 *   - LISR and HISR's flags clear by writing 1 to LIFCR and HIFCR; a
 *     stream's interrupt is pending while TCIF with TCIE, HTIF with HTIE,
 *     TEIF with TEIE, DMEIF with DMEIE or FEIF with FCR.FEIE is set;
 *   - while EN is set, the stream's registers keep their values (but EN,
 *     and in double buffer the memory address not in use).
 *
 * Not modelled: the arbitration's delays between streams (a request is
 * served at once, the stream priority PL deciding only between streams
 * that take the same request), the direct mode and FIFO overrun/underrun
 * errors (they need a request served late, which does not happen here:
 * DMEIF and FEIF set only for the FIFO configuration error), the
 * peripheral flow controller (PFCTRL: SDIO's), the bus matrix's timing. */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

/* CR */
#define EN (1u << 0)
#define DMEIE (1u << 1)
#define TEIE (1u << 2)
#define HTIE (1u << 3)
#define TCIE (1u << 4)
#define CIRC (1u << 8)
#define PINC (1u << 9)
#define MINC (1u << 10)
#define PINCOS (1u << 15)
#define DBM (1u << 18)
#define CT (1u << 19)
/* FCR */
#define DMDIS (1u << 2)
#define FEIE (1u << 7)
/* the flags, from a stream's base bit */
#define F_FE 0
#define F_DME 2
#define F_TE 3
#define F_HT 4
#define F_TC 5

#define NPEND 32

struct stream {
    struct esk_rf_reg *cr, *ndtr, *par, *m0ar, *m1ar, *fcr;
    u32 pa, ma;        /* the addresses in use */
    u32 total;         /* NDTR as the stream was enabled, for HT and reloads */
    int ht;            /* HTIF set this round */
    u8 fifo[16];
    int fifo_n;        /* bytes in the FIFO */
    u32 left_mem;      /* memory items still to read ahead (memory to peripheral) */
    int irq;
};

struct dma {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *isr[2], *ifcr[2];
    struct stream st[8];
    int busy, running;
    const char *pend[NPEND]; /* requests waiting for a stream */
    int npend;
    u32 frac;                /* memory-to-memory: cycles toward the next item */
};

static u32 v32(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }
static u32 dir(const struct stream *x) { return v32(x->cr) >> 6 & 3; }
static u32 psize(const struct stream *x) { return 1u << (v32(x->cr) >> 11 & 3); }
static u32 msize(const struct stream *x) { return 1u << (v32(x->cr) >> 13 & 3); }
static int fifo_mode(const struct stream *x) { return (v32(x->fcr) & DMDIS) != 0; }

static int flag_bit(int s, int f)
{
    static const int base[4] = { 0, 6, 16, 22 };
    return base[s & 3] + f;
}

static void irq_check(struct dma *d, int s)
{
    struct stream *x = &d->st[s];
    u32 isr = v32(d->isr[s / 4]), cr = v32(x->cr);
    if (((isr >> flag_bit(s, F_TC) & 1) && (cr & TCIE)) || ((isr >> flag_bit(s, F_HT) & 1) && (cr & HTIE)) ||
        ((isr >> flag_bit(s, F_TE) & 1) && (cr & TEIE)) || ((isr >> flag_bit(s, F_DME) & 1) && (cr & DMEIE)) ||
        ((isr >> flag_bit(s, F_FE) & 1) && (v32(x->fcr) & FEIE)))
        esk_rf_interrupt(d->p, x->irq);
}

static void set_flag(struct dma *d, int s, int f)
{
    esk_rf_hw(d->isr[s / 4], d->isr[s / 4]->value | (u64)1u << flag_bit(s, f));
    irq_check(d, s);
}

static void disable(struct stream *x)
{
    esk_rf_hw(x->cr, x->cr->value & ~(u64)EN);
}

/* FS: the FIFO's level, in quarters (100 when empty) */
static void fifo_status(struct stream *x)
{
    u32 fs = !fifo_mode(x) ? 4 : x->fifo_n == 0 ? 4 : x->fifo_n >= 16 ? 5 : (u32)x->fifo_n / 4;
    esk_rf_hw(x->fcr, (x->fcr->value & ~(u64)0x38) | (u64)fs << 3);
}

static int bus_rd(struct dma *d, u32 a, u32 n, u32 *v)
{
    return esk_bus_read(&d->s->bus, a, (int)n, v);
}

static int bus_wr(struct dma *d, u32 a, u32 n, u32 v)
{
    return esk_bus_write(&d->s->bus, a, (int)n, v);
}

/* the bus refused an access: TEIF, the stream off */
static void transfer_error(struct dma *d, int s)
{
    disable(&d->st[s]);
    set_flag(d, s, F_TE);
}

static u32 p_step(const struct stream *x)
{
    if (!(v32(x->cr) & PINC))
        return 0;
    return (v32(x->cr) & PINCOS) ? 4 : psize(x);
}

/* the round is done: circular, double buffer, or off */
static void round_end(struct dma *d, int s)
{
    struct stream *x = &d->st[s];
    u32 cr = v32(x->cr);
    set_flag(d, s, F_TC);
    if (cr & (CIRC | DBM)) {
        esk_rf_hw(x->ndtr, x->total);
        x->pa = v32(x->par);
        if (cr & DBM) {
            esk_rf_hw(x->cr, x->cr->value ^ CT);
            x->ma = (v32(x->cr) & CT) ? v32(x->m1ar) : v32(x->m0ar);
        } else {
            x->ma = v32(x->m0ar);
        }
        x->ht = 0;
        if (dir(x) == 1 && fifo_mode(x))
            x->left_mem = x->total * psize(x) / msize(x);
    } else {
        disable(x);
    }
}

/* one item counted: HT, TC */
static void counted(struct dma *d, int s)
{
    struct stream *x = &d->st[s];
    u32 n = v32(x->ndtr);
    if (n)
        esk_rf_hw(x->ndtr, --n);
    if (!x->ht && x->total > 1 && n <= x->total / 2) {
        x->ht = 1;
        set_flag(d, s, F_HT);
    }
    if (!n)
        round_end(d, s);
}

/* the FIFO's bytes to memory, in MSIZE items, while at least `keep` stay
 * out; 0, or -1 on a bus error */
static int fifo_to_memory(struct dma *d, int s, int all)
{
    struct stream *x = &d->st[s];
    u32 m = msize(x), thr = ((v32(x->fcr) & 3) + 1) * 4;
    if (!all && (u32)x->fifo_n < thr)
        return 0;
    while ((u32)x->fifo_n >= m || (all && x->fifo_n > 0)) {
        u32 n = (u32)x->fifo_n < m ? (u32)x->fifo_n : m, v = 0, k;
        for (k = 0; k < n; k++)
            v |= (u32)x->fifo[k] << (8 * k);
        if (bus_wr(d, x->ma, n, v))
            return -1;
        memmove(x->fifo, x->fifo + n, (size_t)(x->fifo_n - (int)n));
        x->fifo_n -= (int)n;
        if (v32(x->cr) & MINC)
            x->ma += n;
    }
    return 0;
}

/* memory read ahead into the FIFO, MSIZE at a time */
static int memory_to_fifo(struct dma *d, int s)
{
    struct stream *x = &d->st[s];
    u32 m = msize(x), v, k;
    while (x->left_mem && x->fifo_n + (int)m <= 16) {
        if (bus_rd(d, x->ma, m, &v))
            return -1;
        for (k = 0; k < m; k++)
            x->fifo[x->fifo_n++] = (u8)(v >> (8 * k));
        if (v32(x->cr) & MINC)
            x->ma += m;
        x->left_mem--;
    }
    return 0;
}

/* one item, peripheral to memory or memory to peripheral (or memory to
 * memory, the peripheral address the source); 0, or -1 on a bus error */
static int item(struct dma *d, int s)
{
    struct stream *x = &d->st[s];
    u32 ps = psize(x), v = 0, k;
    if (dir(x) == 1) { /* memory to peripheral */
        if (fifo_mode(x)) {
            if (memory_to_fifo(d, s))
                return -1;
            for (k = 0; k < ps && (int)k < x->fifo_n; k++)
                v |= (u32)x->fifo[k] << (8 * k);
            memmove(x->fifo, x->fifo + ps, (size_t)(x->fifo_n - (int)ps > 0 ? x->fifo_n - (int)ps : 0));
            x->fifo_n = x->fifo_n > (int)ps ? x->fifo_n - (int)ps : 0;
        } else {
            if (bus_rd(d, x->ma, ps, &v))
                return -1;
            if (v32(x->cr) & MINC)
                x->ma += ps;
        }
        if (bus_wr(d, x->pa, ps, v))
            return -1;
        x->pa += p_step(x);
        return 0;
    }
    /* peripheral (or the memory-to-memory source) to memory */
    if (bus_rd(d, x->pa, ps, &v))
        return -1;
    x->pa += p_step(x);
    if (!fifo_mode(x)) {
        if (bus_wr(d, x->ma, ps, v))
            return -1;
        if (v32(x->cr) & MINC)
            x->ma += ps;
        return 0;
    }
    for (k = 0; k < ps && x->fifo_n < 16; k++)
        x->fifo[x->fifo_n++] = (u8)(v >> (8 * k));
    return fifo_to_memory(d, s, v32(x->ndtr) <= 1);
}

static void items_left(struct esk_mach *s, const char *sig, u32 left)
{
    if (!left) {
        esk_stm32_spi_dma_end(s, sig);
        esk_stm32_dma_ended_adc(s, sig);
        esk_stm32_dac_dma_end(s, sig);
    }
    esk_stm32_i2c_dma_left(s, sig, left);
}

/* a request served by stream s: a peripheral burst of items; the last two
 * of a round tell the peripheral (an SPI's CRC follows the last, an I2C
 * NACKs the byte after the next-to-last) */
static void serve(struct dma *d, int s, const char *sig)
{
    struct stream *x = &d->st[s];
    static const u32 beats[4] = { 1, 4, 8, 16 };
    u32 n = fifo_mode(x) ? beats[v32(x->cr) >> 21 & 3] : 1, k;
    for (k = 0; k < n && (v32(x->cr) & EN) && v32(x->ndtr); k++) {
        if (item(d, s)) {
            transfer_error(d, s);
            break;
        }
        u32 before = v32(x->ndtr);
        counted(d, s);
        /* the peripheral hears of the last items (EOT, I2C's EOT-1) */
        if (before <= 2)
            items_left(d->s, sig, before - 1);
    }
    fifo_status(x);
}

/* the streams that take `sig` now, by priority (PL, then the lower stream) */
static int takes(struct dma *d, int s, const char *sig)
{
    struct stream *x = &d->st[s];
    u32 cr = v32(x->cr);
    return (cr & EN) && dir(x) != 2 && esk_rf_dma_maps(d->s->rf, d->p->name, s, (int)(cr >> 25 & 7), sig);
}

static int try_serve(struct dma *d, const char *sig)
{
    int s, served = 0, pl;
    /* every stream that takes it moves its data; the most urgent first */
    for (pl = 3; pl >= 0; pl--)
        for (s = 0; s < 8; s++)
            if (takes(d, s, sig) && (int)(v32(d->st[s].cr) >> 16 & 3) == pl) {
                serve(d, s, sig);
                served = 1;
            }
    return served;
}

static void pend_add(struct dma *d, const char *sig)
{
    int i;
    for (i = 0; i < d->npend; i++)
        if (!strcmp(d->pend[i], sig))
            return;
    if (d->npend < NPEND)
        d->pend[d->npend++] = sig;
}

static void pend_remove(struct dma *d, const char *sig)
{
    int i;
    for (i = 0; i < d->npend; i++)
        if (!strcmp(d->pend[i], sig)) {
            d->pend[i] = d->pend[--d->npend];
            return;
        }
}

/* the waiting requests a stream now takes */
static void drain(struct dma *d)
{
    int i, again = 1;
    if (d->busy)
        return;
    d->busy = 1;
    while (again) {
        again = 0;
        for (i = 0; i < d->npend; i++) {
            const char *sig = d->pend[i];
            int k, mapped = 0;
            for (k = 0; k < 8; k++)
                mapped |= takes(d, k, sig);
            if (!mapped)
                continue;
            pend_remove(d, sig);
            (void)try_serve(d, sig);
            again = 1;
            break;
        }
    }
    d->busy = 0;
}

/* a DMA controller; a request waits on it even while its clock is off */
static struct dma *controller(struct esk_mach *s, int i)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph_at(s->rf, i) : 0;
    return p && p->ops == &esk_stm32f4_dma_ops ? (struct dma *)p->ctx : 0;
}

void esk_stm32_dma_request(struct esk_mach *s, const char *sig, int level)
{
    int i;
    struct esk_rf_periph *p;
    for (i = 0; s->rf && (p = esk_rf_periph_at(s->rf, i)) != 0; i++) {
        struct dma *d = controller(s, i);
        if (!d)
            continue;
        if (!level) {
            pend_remove(d, sig);
            continue;
        }
        pend_add(d, sig); /* served now if a stream takes it, else it waits */
        if (!esk_rf_gated(d->p))
            drain(d);
    }
}

/* ---- memory to memory ------------------------------------------------------ */

static int m2m_any(const struct dma *d)
{
    int s;
    for (s = 0; s < 8; s++)
        if ((v32(d->st[s].cr) & EN) && dir(&d->st[s]) == 2)
            return 1;
    return 0;
}

static void run_clock(struct dma *d)
{
    esk_rf_clock(d->p, &d->running, m2m_any(d));
}

static void dma_tick(void *ctx, u32 cycles)
{
    struct dma *d = (struct dma *)ctx;
    u32 items = (d->frac + cycles) / 2;
    int s;
    d->frac = (d->frac + cycles) % 2;
    for (s = 0; s < 8 && items; s++) {
        struct stream *x = &d->st[s];
        while (items && (v32(x->cr) & EN) && dir(x) == 2 && v32(x->ndtr)) {
            if (item(d, s)) {
                transfer_error(d, s);
                break;
            }
            counted(d, s);
            items--;
        }
        fifo_status(x);
    }
    run_clock(d);
}

static int dma_next_event(void *ctx, u32 *cycles)
{
    struct dma *d = (struct dma *)ctx;
    if (!m2m_any(d))
        return 0;
    *cycles = 2;
    return 1;
}

/* ---- the registers ------------------------------------------------------------ */

/* a memory burst the FIFO threshold cannot hold (RM0390 table 30) */
static int fifo_config_error(const struct stream *x)
{
    static const u32 beats[4] = { 1, 4, 8, 16 };
    u32 burst = beats[v32(x->cr) >> 23 & 3] * msize(x), thr = ((v32(x->fcr) & 3) + 1) * 4;
    return fifo_mode(x) && burst > 1 && (burst > thr || thr % burst);
}

static void enable(struct dma *d, int s)
{
    struct stream *x = &d->st[s];
    if (dir(x) == 2)
        esk_rf_hw(x->fcr, x->fcr->value | DMDIS); /* memory to memory: the FIFO */
    if (fifo_config_error(x)) {
        disable(x);
        set_flag(d, s, F_FE);
        return;
    }
    x->total = v32(x->ndtr);
    if (!x->total) {
        disable(x);
        return;
    }
    x->pa = v32(x->par);
    x->ma = (v32(x->cr) & DBM) && (v32(x->cr) & CT) ? v32(x->m1ar) : v32(x->m0ar);
    x->ht = 0;
    x->fifo_n = 0;
    x->left_mem = dir(x) == 1 && fifo_mode(x) ? x->total * psize(x) / msize(x) : 0;
    if (x->left_mem && memory_to_fifo(d, s))
        transfer_error(d, s);
    fifo_status(x);
}

static u32 dma_read(void *ctx, u32 off, int n)
{
    struct dma *d = (struct dma *)ctx;
    return esk_rf_read(d->p, off, n);
}

static void dma_write(void *ctx, u32 off, int n, u32 v)
{
    struct dma *d = (struct dma *)ctx;
    int s, i;
    for (i = 0; i < 2; i++)
        if (off == esk_rf_off(d->ifcr[i])) {
            esk_rf_hw(d->isr[i], d->isr[i]->value & ~(u64)v);
            esk_rf_hw(d->ifcr[i], 0);
            return;
        }
    for (s = 0; s < 8; s++) {
        struct stream *x = &d->st[s];
        u32 cr = v32(x->cr), base = esk_rf_off(x->cr);
        if (off < base || off >= base + 0x18)
            continue;
        if (cr & EN) {
            /* enabled: only EN, and in double buffer the memory address
             * not in use, take a write */
            if (off == base) {
                if (!(v & EN))
                    disable(x);
            } else if ((cr & DBM) && off == esk_rf_off((cr & CT) ? x->m0ar : x->m1ar)) {
                esk_rf_write(d->p, off, n, v);
            }
            return;
        }
        esk_rf_write(d->p, off, n, v);
        if (off == base && (v & EN)) {
            enable(d, s);
            run_clock(d);
            drain(d);
        }
        return;
    }
    esk_rf_write(d->p, off, n, v);
}

static void dma_reset(void *ctx)
{
    struct dma *d = (struct dma *)ctx;
    int s;
    d->npend = 0;
    d->busy = d->running = 0;
    d->frac = 0;
    for (s = 0; s < 8; s++) {
        d->st[s].fifo_n = 0;
        d->st[s].total = 0;
        d->st[s].left_mem = 0;
    }
}

const struct esk_dev_ops esk_stm32f4_dma_ops = {
    "stm32f4-dma", dma_read, dma_write, dma_reset, dma_tick, dma_next_event,
};

void *esk_stm32f4_dma_create(struct esk_mach *m, struct esk_rf_periph *p)
{
    struct dma *d;
    int s;
    char n[16];
    d = (struct dma *)esk_mach_alloc(m, sizeof *d);
    if (!d)
        return 0;
    d->s = m;
    d->p = p;
    d->isr[0] = esk_rf_reg(p, "LISR");
    d->isr[1] = esk_rf_reg(p, "HISR");
    d->ifcr[0] = esk_rf_reg(p, "LIFCR");
    d->ifcr[1] = esk_rf_reg(p, "HIFCR");
    if (!d->isr[0] || !d->isr[1] || !d->ifcr[0] || !d->ifcr[1])
        return 0;
    for (s = 0; s < 8; s++) {
        struct stream *x = &d->st[s];
        snprintf(n, sizeof n, "S%dCR", s);
        x->cr = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "S%dNDTR", s);
        x->ndtr = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "S%dPAR", s);
        x->par = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "S%dM0AR", s);
        x->m0ar = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "S%dM1AR", s);
        x->m1ar = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "S%dFCR", s);
        x->fcr = esk_rf_reg(p, n);
        if (!x->cr || !x->ndtr || !x->par || !x->m0ar || !x->m1ar || !x->fcr)
            return 0;
        snprintf(n, sizeof n, "Stream%d", s);
        x->irq = esk_rf_irq(p, n);
    }
    esk_rf_set_mwv(d->ifcr[0], ~(u64)0, ESK_MWV_MODIFY);
    return d;
}
