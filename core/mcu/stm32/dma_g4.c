/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* dma_g4.c -- the STM32G4's DMA controllers (RM0440 12) and its DMA request
 * multiplexer (RM0440 13), over the register file.
 *
 * DMA1 and DMA2 (stm32g4-dma), eight channels each:
 *   - a channel (CCRx.EN) moves one item a request, peripheral to memory
 *     (DIR 0) or memory to peripheral (DIR 1), or, with MEM2MEM, the
 *     whole block without requests, one item every two cycles, as the bus
 *     lets it; the source is read at its size and written at the
 *     destination's, zero-extended or truncated as table 89 gives, both
 *     addresses stepping by their own sizes (PINC, MINC);
 *   - CNDTRx counts the items left: half of them done sets HTIF, all of
 *     them TCIF (and GIF with either); circular mode (CIRC) reloads CNDTR
 *     and the addresses; otherwise the channel stays enabled and serves no
 *     more requests until it is disabled and programmed again; a channel
 *     enabled with CNDTR 0 serves nothing;
 *   - an access the bus refuses sets TEIF and clears EN, which cannot be
 *     set again while TEIF is;
 *   - while EN is set, MEM2MEM, PL, MSIZE, PSIZE, MINC, PINC and DIR, and
 *     CNDTR, keep their values; CPAR and CMAR take a write the transfer in
 *     progress does not use (it keeps its internal addresses);
 *   - IFCR's CGIFx clears the channel's four flags, CTEIFx, CHTIFx and
 *     CTCIFx their own flag (and GIF once none of the three is left); a
 *     channel's interrupt is pending while TCIF with TCIE, HTIF with HTIE
 *     or TEIF with TEIE;
 *   - the last two items of a round tell the peripheral (an SPI's CRC
 *     follows the last, an I2C NACKs the byte after the next-to-last).
 *
 * DMAMUX (stm32g4-dmamux): sixteen request-line channels, 0 to 7 to DMA1's
 * channels 1 to 8 and 8 to 15 to DMA2's, and four request generators:
 *   - CxCR's DMAREQ_ID selects the request line (the description's
 *     dma_requests, the HAL's numbering, 1 to 4 the generators);
 *   - SE: the line reaches the DMA only after an edge (SPOL) on the
 *     synchronisation input SYNC_ID, if a request is pending then (an edge
 *     with none is discarded), for NBREQ + 1 served requests; an edge
 *     while the counter is below NBREQ sets SOFx;
 *   - EGE: an event each NBREQ + 1 served requests (each one with NBREQ 0),
 *     channels 0 to 3's on trigger and synchronisation inputs 16 to 19;
 *   - a generator (RGxCR.GE) produces GNBREQ + 1 requests from an edge
 *     (GPOL) of its trigger input SIG_ID; an edge before they are all
 *     served sets OFx;
 *   - the trigger and synchronisation inputs: EXTI lines 0 to 15 (0 to 15),
 *     the channels' events (16 to 19), LPTIM1_OUT (20);
 *   - CFR and RGCFR clear SOF and OF; DMAMUX_OVR is pending while a flag
 *     is set with its channel's SOIE or generator's OIE.
 *
 * Not modelled: the arbitration's delays (a request is served at once, the
 * channel priority deciding only between channels taking the same
 * request), the bus matrix's timing, the edge filter's two AHB cycles and
 * the three cycles a CxCR or RGxCR write masks the inputs for, the byte and
 * half-word lanes duplicated for an APB peripheral that ignores the access
 * size (the register file takes each size as it is), LPTIM1_OUT (no LPTIM
 * model yet). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

/* CCR */
#define EN (1u << 0)
#define TCIE (1u << 1)
#define HTIE (1u << 2)
#define TEIE (1u << 3)
#define DIR (1u << 4)
#define CIRC (1u << 5)
#define PINC (1u << 6)
#define MINC (1u << 7)
#define MEM2MEM (1u << 14)
/* read-only while EN: MEM2MEM, PL, MSIZE, PSIZE, MINC, PINC, DIR */
#define CCR_LOCKED 0x7FD0u
/* ISR, a channel's nibble */
#define F_GIF 0
#define F_TC 1
#define F_HT 2
#define F_TE 3

/* DMAMUX CxCR */
#define C_SOIE (1u << 8)
#define C_EGE (1u << 9)
#define C_SE (1u << 16)
/* RGxCR */
#define G_OIE (1u << 8)
#define G_GE (1u << 16)

#define NCH 8
#define NMUX 16
#define NGEN 4
#define NIN 21
#define NPEND 32
#define NID 128

struct chan {
    struct esk_rf_reg *ccr, *cndtr, *cpar, *cmar;
    u32 pa, ma;  /* the addresses in use */
    u32 total;   /* CNDTR as the channel was enabled, for HT and reloads */
    int ht;      /* HTIF set this round */
    int irq;
};

struct g4dma {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *isr, *ifcr;
    struct chan ch[NCH];
    int mux0; /* the DMAMUX channel of channel 1 (0 for DMA1, 8 for DMA2) */
    int running;
    u32 frac; /* memory to memory: cycles toward the next item */
};

struct dmamux {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr[NMUX], *rg[NGEN], *csr, *cfr, *rgsr, *rgcfr;
    int nch, irq;
    const char *id_sig[NID]; /* request line ID -> its request */
    const char *pend[NPEND]; /* requests waiting for a channel */
    int npend;
    int conn[NMUX];          /* SE: the line connected since a sync edge */
    u32 cnt[NMUX];           /* served requests left before the counter reloads */
    u32 gen_left[NGEN];      /* a generator's requests still to serve */
    int level[NIN];          /* the trigger and sync inputs' levels */
    int busy, depth;
};

static const char *const gen_sig[NGEN] = { "DMAMUX_REQ_GEN0", "DMAMUX_REQ_GEN1", "DMAMUX_REQ_GEN2",
                                           "DMAMUX_REQ_GEN3" };

static u32 v32(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }

/* ---- the DMA controllers ---------------------------------------------------- */

static u32 psize(const struct chan *c) { return 1u << (v32(c->ccr) >> 8 & 3); }
static u32 msize(const struct chan *c) { return 1u << (v32(c->ccr) >> 10 & 3); }

static void irq_check(struct g4dma *d, int k)
{
    struct chan *c = &d->ch[k];
    u32 f = v32(d->isr) >> (4 * k), cr = v32(c->ccr);
    if (((f >> F_TC & 1) && (cr & TCIE)) || ((f >> F_HT & 1) && (cr & HTIE)) || ((f >> F_TE & 1) && (cr & TEIE)))
        esk_rf_interrupt(d->p, c->irq);
}

static void set_flag(struct g4dma *d, int k, int f)
{
    esk_rf_hw(d->isr, d->isr->value | ((u64)1u << (4 * k + f)) | ((u64)1u << (4 * k + F_GIF)));
    irq_check(d, k);
}

/* the bus refused an access: TEIF, the channel off */
static void transfer_error(struct g4dma *d, int k)
{
    esk_rf_hw(d->ch[k].ccr, d->ch[k].ccr->value & ~(u64)EN);
    set_flag(d, k, F_TE);
}

static int active(const struct chan *c) { return (v32(c->ccr) & EN) && v32(c->cndtr); }

/* one item from the source to the destination at their sizes (table 89):
 * 0, or -1 on a bus error */
static int item(struct g4dma *d, int k)
{
    struct chan *c = &d->ch[k];
    u32 cr = v32(c->ccr), v = 0;
    int to_p = (cr & DIR) != 0;
    u32 *src = to_p ? &c->ma : &c->pa, *dst = to_p ? &c->pa : &c->ma;
    u32 ss = to_p ? msize(c) : psize(c), ds = to_p ? psize(c) : msize(c);
    int sinc = (cr & (to_p ? MINC : PINC)) != 0, dinc = (cr & (to_p ? PINC : MINC)) != 0;
    if (esk_bus_read(&d->s->bus, *src, (int)ss, &v))
        return -1;
    if (ss < 4)
        v &= (1u << (8 * ss)) - 1u;
    if (ds < 4)
        v &= (1u << (8 * ds)) - 1u;
    if (esk_bus_write(&d->s->bus, *dst, (int)ds, v))
        return -1;
    if (sinc)
        *src += ss;
    if (dinc)
        *dst += ds;
    return 0;
}

/* one item counted: HT, TC, the circular reload */
static void counted(struct g4dma *d, int k)
{
    struct chan *c = &d->ch[k];
    u32 n = v32(c->cndtr);
    if (n)
        esk_rf_hw(c->cndtr, --n);
    if (!c->ht && c->total > 1 && n <= c->total / 2) {
        c->ht = 1;
        set_flag(d, k, F_HT);
    }
    if (n)
        return;
    set_flag(d, k, F_TC);
    if (v32(c->ccr) & CIRC) {
        esk_rf_hw(c->cndtr, c->total);
        c->pa = v32(c->cpar);
        c->ma = v32(c->cmar);
        c->ht = 0;
    }
}

/* a request served by channel k: one item; 1 when served */
static int serve(struct g4dma *d, int k, const char *sig)
{
    struct chan *c = &d->ch[k];
    u32 before;
    if (!active(c) || esk_rf_gated(d->p))
        return 0;
    if (item(d, k)) {
        transfer_error(d, k);
        return 1; /* the request was taken, and the channel is off */
    }
    before = v32(c->cndtr);
    counted(d, k);
    if (before <= 2 && sig)
        esk_stm32_dma_items_left(d->s, sig, before - 1);
    return 1;
}

static int m2m_any(const struct g4dma *d)
{
    int k;
    for (k = 0; k < NCH; k++)
        if (active(&d->ch[k]) && (v32(d->ch[k].ccr) & MEM2MEM))
            return 1;
    return 0;
}

static void run_clock(struct g4dma *d)
{
    esk_rf_clock(d->p, &d->running, m2m_any(d));
}

static void dma_tick(void *ctx, u32 cycles)
{
    struct g4dma *d = (struct g4dma *)ctx;
    u32 items = (d->frac + cycles) / 2;
    int pl, k;
    d->frac = (d->frac + cycles) % 2;
    /* the most urgent memory-to-memory channel first */
    for (pl = 3; pl >= 0 && items; pl--)
        for (k = 0; k < NCH && items; k++) {
            struct chan *c = &d->ch[k];
            if ((int)(v32(c->ccr) >> 12 & 3) != pl || !(v32(c->ccr) & MEM2MEM))
                continue;
            while (items && active(c)) {
                if (item(d, k)) {
                    transfer_error(d, k);
                    break;
                }
                counted(d, k);
                items--;
            }
        }
    run_clock(d);
}

static int dma_next_event(void *ctx, u32 *cycles)
{
    struct g4dma *d = (struct g4dma *)ctx;
    if (!m2m_any(d))
        return 0;
    *cycles = 2;
    return 1;
}

static struct dmamux *mux_of(struct esk_mach *s);
static void mux_drain(struct dmamux *x);

static void enable(struct g4dma *d, int k)
{
    struct chan *c = &d->ch[k];
    c->total = v32(c->cndtr);
    c->pa = v32(c->cpar);
    c->ma = v32(c->cmar);
    c->ht = 0;
}

static u32 dma_read(void *ctx, u32 off, int n)
{
    struct g4dma *d = (struct g4dma *)ctx;
    return esk_rf_read(d->p, off, n);
}

static void dma_write(void *ctx, u32 off, int n, u32 v)
{
    struct g4dma *d = (struct g4dma *)ctx;
    int k;
    if (off == esk_rf_off(d->isr))
        return;
    if (off == esk_rf_off(d->ifcr)) {
        u32 isr = v32(d->isr);
        for (k = 0; k < NCH; k++) {
            u32 w = v >> (4 * k) & 0xFu, f = isr >> (4 * k) & 0xFu;
            if (w & 1u)
                f = 0;
            f &= ~(w & 0xEu);
            if (!(f & 0xEu))
                f = 0;
            isr = (isr & ~(0xFu << (4 * k))) | f << (4 * k);
        }
        esk_rf_hw(d->isr, isr);
        return;
    }
    for (k = 0; k < NCH; k++) {
        struct chan *c = &d->ch[k];
        u32 cr = v32(c->ccr);
        if (off == esk_rf_off(c->ccr)) {
            u32 nv;
            esk_rf_write(d->p, off, n, v);
            nv = v32(c->ccr);
            if (cr & EN)
                nv = (nv & ~CCR_LOCKED) | (cr & CCR_LOCKED);
            if ((nv & EN) && (v32(d->isr) >> (4 * k + F_TE) & 1))
                nv &= ~EN; /* not again until TEIF is cleared */
            esk_rf_hw(c->ccr, nv);
            if ((nv & EN) && !(cr & EN)) {
                enable(d, k);
                run_clock(d);
                mux_drain(mux_of(d->s));
            }
            return;
        }
        if (off == esk_rf_off(c->cndtr)) {
            if (!(cr & EN))
                esk_rf_write(d->p, off, n, v);
            return;
        }
    }
    esk_rf_write(d->p, off, n, v);
}

static void dma_reset(void *ctx)
{
    struct g4dma *d = (struct g4dma *)ctx;
    int k;
    d->running = 0;
    d->frac = 0;
    for (k = 0; k < NCH; k++) {
        d->ch[k].total = 0;
        d->ch[k].ht = 0;
        d->ch[k].pa = d->ch[k].ma = 0;
    }
}

const struct esk_dev_ops esk_stm32g4_dma_ops = {
    "stm32g4-dma", dma_read, dma_write, dma_reset, dma_tick, dma_next_event,
};

void *esk_stm32g4_dma_create(struct esk_mach *m, struct esk_rf_periph *p)
{
    struct g4dma *d = (struct g4dma *)esk_mach_alloc(m, sizeof *d);
    char n[16];
    int k;
    if (!d)
        return 0;
    d->s = m;
    d->p = p;
    d->isr = esk_rf_reg(p, "ISR");
    d->ifcr = esk_rf_reg(p, "IFCR");
    if (!d->isr || !d->ifcr)
        return 0;
    d->mux0 = !strcmp(p->name, "DMA2") ? 8 : 0;
    for (k = 0; k < NCH; k++) {
        struct chan *c = &d->ch[k];
        snprintf(n, sizeof n, "CCR%d", k + 1);
        c->ccr = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "CNDTR%d", k + 1);
        c->cndtr = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "CPAR%d", k + 1);
        c->cpar = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "CMAR%d", k + 1);
        c->cmar = esk_rf_reg(p, n);
        if (!c->ccr || !c->cndtr || !c->cpar || !c->cmar)
            return 0;
        snprintf(n, sizeof n, "_CH%d", k + 1);
        c->irq = esk_rf_irq(p, n);
    }
    esk_rf_set_mwv(d->ifcr, ~(u64)0, ESK_MWV_MODIFY);
    return d;
}

/* ---- the request multiplexer ------------------------------------------------- */

static struct dmamux *mux_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "DMAMUX") : 0;
    return p && p->ops == &esk_stm32g4_dmamux_ops ? (struct dmamux *)p->ctx : 0;
}

/* the DMA channel a multiplexer channel feeds */
static struct g4dma *dma_for(struct dmamux *x, int c, int *k)
{
    struct esk_rf_periph *p = esk_rf_periph(x->s->rf, c < 8 ? "DMA1" : "DMA2");
    *k = c % 8;
    return p && p->ops == &esk_stm32g4_dma_ops ? (struct g4dma *)p->ctx : 0;
}

static const char *line_of(const struct dmamux *x, int c)
{
    u32 id = v32(x->cr[c]) & 0x7Fu;
    return id ? x->id_sig[id] : 0;
}

static u32 nbreq(const struct dmamux *x, int c) { return (v32(x->cr[c]) >> 19 & 31u) + 1u; }

static void mux_irq(struct dmamux *x)
{
    u32 sof = v32(x->csr), of = v32(x->rgsr);
    int c;
    for (c = 0; c < x->nch; c++)
        if ((sof >> c & 1) && (v32(x->cr[c]) & C_SOIE)) {
            esk_rf_interrupt(x->p, x->irq);
            return;
        }
    for (c = 0; c < NGEN; c++)
        if ((of >> c & 1) && (v32(x->rg[c]) & G_OIE)) {
            esk_rf_interrupt(x->p, x->irq);
            return;
        }
}

static void mux_input(struct dmamux *x, int in, int level);

/* channel c's event output, a pulse on inputs 16 to 19 */
static void event_out(struct dmamux *x, int c)
{
    if (c > 3 || x->depth > 4)
        return;
    x->depth++;
    mux_input(x, 16 + c, 1);
    mux_input(x, 16 + c, 0);
    x->depth--;
}

/* may channel c pass its line now (SE: connected by a sync edge) */
static int passes(const struct dmamux *x, int c)
{
    return !(v32(x->cr[c]) & C_SE) || x->conn[c];
}

/* a served request on channel c: the counter for SE and EGE */
static void counted_req(struct dmamux *x, int c)
{
    u32 cr = v32(x->cr[c]);
    if (!(cr & (C_SE | C_EGE)))
        return;
    if (x->cnt[c] > 1) {
        x->cnt[c]--;
        return;
    }
    x->cnt[c] = nbreq(x, c);
    if (cr & C_SE)
        x->conn[c] = 0;
    if (cr & C_EGE)
        event_out(x, c);
}

/* a request on line `sig`: every channel selecting it that passes it
 * serves one item; 1 when one did */
static int offer(struct dmamux *x, const char *sig)
{
    int c, served = 0, pl;
    if (esk_rf_gated(x->p))
        return 0;
    for (pl = 3; pl >= 0; pl--)
        for (c = 0; c < x->nch; c++) {
            const char *l = line_of(x, c);
            struct g4dma *d;
            int k;
            if (!l || strcmp(l, sig) || !passes(x, c) || !(d = dma_for(x, c, &k)))
                continue;
            if ((int)(v32(d->ch[k].ccr) >> 12 & 3) != pl || (v32(d->ch[k].ccr) & MEM2MEM))
                continue;
            if (serve(d, k, sig)) {
                served = 1;
                counted_req(x, c);
            }
        }
    return served;
}

/* could a channel take `sig` now */
static int taken(struct dmamux *x, const char *sig)
{
    int c;
    for (c = 0; c < x->nch; c++) {
        const char *l = line_of(x, c);
        struct g4dma *d;
        int k;
        if (l && !strcmp(l, sig) && passes(x, c) && (d = dma_for(x, c, &k)) && active(&d->ch[k]) &&
            !(v32(d->ch[k].ccr) & MEM2MEM) && !esk_rf_gated(d->p))
            return 1;
    }
    return 0;
}

static int pending(const struct dmamux *x, const char *sig)
{
    int i;
    for (i = 0; i < x->npend; i++)
        if (!strcmp(x->pend[i], sig))
            return 1;
    for (i = 0; i < NGEN; i++)
        if (x->gen_left[i] && !strcmp(gen_sig[i], sig))
            return 1;
    return 0;
}

static void pend_add(struct dmamux *x, const char *sig)
{
    int i;
    for (i = 0; i < x->npend; i++)
        if (!strcmp(x->pend[i], sig))
            return;
    if (x->npend < NPEND)
        x->pend[x->npend++] = sig;
}

static void pend_remove(struct dmamux *x, const char *sig)
{
    int i;
    for (i = 0; i < x->npend; i++)
        if (!strcmp(x->pend[i], sig)) {
            x->pend[i] = x->pend[--x->npend];
            return;
        }
}

/* the generators' requests, served while a channel takes them */
static int run_generators(struct dmamux *x)
{
    int g, any = 0;
    for (g = 0; g < NGEN; g++)
        while (x->gen_left[g] && taken(x, gen_sig[g]) && offer(x, gen_sig[g])) {
            x->gen_left[g]--;
            any = 1;
        }
    return any;
}

/* the waiting requests a channel now takes */
static void mux_drain(struct dmamux *x)
{
    int i, again = 1;
    if (!x || x->busy)
        return;
    x->busy = 1;
    while (again) {
        again = run_generators(x);
        for (i = 0; i < x->npend; i++) {
            const char *sig = x->pend[i];
            if (!taken(x, sig))
                continue;
            pend_remove(x, sig);
            (void)offer(x, sig);
            again = 1;
            break;
        }
    }
    x->busy = 0;
}

static int edge(u32 pol, int level)
{
    return pol == 3 || (pol == 1 && level) || (pol == 2 && !level);
}

/* a trigger and synchronisation input's level (0 to 20) */
static void mux_input(struct dmamux *x, int in, int level)
{
    int c;
    if (in < 0 || in >= NIN || x->level[in] == !!level)
        return;
    x->level[in] = !!level;
    if (esk_rf_gated(x->p))
        return;
    for (c = 0; c < NGEN; c++) {
        u32 rg = v32(x->rg[c]);
        if (!(rg & G_GE) || (int)(rg & 31u) != in || !edge(rg >> 17 & 3, level))
            continue;
        if (x->gen_left[c]) {
            esk_rf_hw(x->rgsr, x->rgsr->value | (u64)1u << c);
            mux_irq(x);
            continue;
        }
        x->gen_left[c] = (rg >> 19 & 31u) + 1u;
    }
    for (c = 0; c < x->nch; c++) {
        u32 cr = v32(x->cr[c]);
        const char *l = line_of(x, c);
        if (!(cr & C_SE) || (int)(cr >> 24 & 31u) != in || !edge(cr >> 17 & 3, level))
            continue;
        if (x->conn[c] && x->cnt[c] < nbreq(x, c)) {
            esk_rf_hw(x->csr, x->csr->value | (u64)1u << c);
            mux_irq(x);
            continue;
        }
        if (l && pending(x, l)) {
            x->conn[c] = 1;
            x->cnt[c] = nbreq(x, c);
        }
    }
    mux_drain(x);
}

static u32 mux_read(void *ctx, u32 off, int n)
{
    struct dmamux *x = (struct dmamux *)ctx;
    return esk_rf_read(x->p, off, n);
}

static void mux_write(void *ctx, u32 off, int n, u32 v)
{
    struct dmamux *x = (struct dmamux *)ctx;
    int c;
    if ((x->csr && off == esk_rf_off(x->csr)) || (x->rgsr && off == esk_rf_off(x->rgsr)))
        return;
    if (x->cfr && off == esk_rf_off(x->cfr)) {
        esk_rf_hw(x->csr, x->csr->value & ~(u64)(v & 0xFFFFu));
        return;
    }
    if (x->rgcfr && off == esk_rf_off(x->rgcfr)) {
        esk_rf_hw(x->rgsr, x->rgsr->value & ~(u64)(v & 0xFu));
        return;
    }
    esk_rf_write(x->p, off, n, v);
    for (c = 0; c < x->nch; c++)
        if (off >= esk_rf_off(x->cr[c]) && off < esk_rf_off(x->cr[c]) + 4) {
            u32 cr = v32(x->cr[c]);
            if (!(cr & (C_SE | C_EGE)))
                x->cnt[c] = nbreq(x, c);
            if (!(cr & C_SE))
                x->conn[c] = 0;
            else if (!x->cnt[c])
                x->cnt[c] = nbreq(x, c);
        }
    for (c = 0; c < NGEN; c++)
        if (off >= esk_rf_off(x->rg[c]) && off < esk_rf_off(x->rg[c]) + 4 && !(v32(x->rg[c]) & G_GE))
            x->gen_left[c] = 0;
    mux_drain(x);
}

static void mux_reset(void *ctx)
{
    struct dmamux *x = (struct dmamux *)ctx;
    int c;
    x->npend = 0;
    x->busy = x->depth = 0;
    for (c = 0; c < NMUX; c++) {
        x->conn[c] = 0;
        x->cnt[c] = 1;
    }
    for (c = 0; c < NGEN; c++)
        x->gen_left[c] = 0;
    for (c = 0; c < NIN; c++)
        x->level[c] = 0;
}

const struct esk_dev_ops esk_stm32g4_dmamux_ops = {
    "stm32g4-dmamux", mux_read, mux_write, mux_reset, 0, 0,
};

void *esk_stm32g4_dmamux_create(struct esk_mach *m, struct esk_rf_periph *p)
{
    struct dmamux *x = (struct dmamux *)esk_mach_alloc(m, sizeof *x);
    const struct esk_rf *rf = m->rf;
    char n[16];
    int c, i;
    if (!x)
        return 0;
    x->s = m;
    x->p = p;
    for (c = 0; c < NMUX; c++) {
        snprintf(n, sizeof n, "C%dCR", c);
        x->cr[c] = esk_rf_reg(p, n);
        if (!x->cr[c])
            break;
    }
    x->nch = c;
    for (c = 0; c < NGEN; c++) {
        snprintf(n, sizeof n, "RG%dCR", c);
        x->rg[c] = esk_rf_reg(p, n);
        if (!x->rg[c])
            return 0;
    }
    x->csr = esk_rf_reg(p, "CSR");
    x->cfr = esk_rf_reg(p, "CFR");
    x->rgsr = esk_rf_reg(p, "RGSR");
    x->rgcfr = esk_rf_reg(p, "RGCFR");
    if (!x->nch || !x->csr || !x->cfr || !x->rgsr || !x->rgcfr)
        return 0;
    x->irq = esk_rf_irq(p, "OVR");
    /* the request lines, as the description numbers them */
    for (i = 0; rf && i < rf->ndma; i++)
        if (!strcmp(rf->dma_ctl[i], "DMAMUX") && rf->dma_chan[i] > 0 && rf->dma_chan[i] < NID)
            x->id_sig[rf->dma_chan[i]] = rf->dma_sig[i];
    esk_rf_set_mwv(x->cfr, ~(u64)0, ESK_MWV_MODIFY);
    esk_rf_set_mwv(x->rgcfr, ~(u64)0, ESK_MWV_MODIFY);
    mux_reset(x);
    return x;
}

/* ---- the peripherals' side ----------------------------------------------------- */

void esk_stm32g4_dma_request(struct esk_mach *s, const char *sig, int level)
{
    struct dmamux *x = mux_of(s);
    if (!x)
        return;
    if (!level) {
        pend_remove(x, sig);
        return;
    }
    pend_add(x, sig); /* served now if a channel takes it, else it waits */
    mux_drain(x);
}

void esk_stm32g4_dmamux_input(struct esk_mach *s, const char *sig, int level)
{
    struct dmamux *x = mux_of(s);
    int in = -1;
    if (!x || !sig)
        return;
    if (!strncmp(sig, "EXTI", 4) && sig[4] >= '0' && sig[4] <= '9')
        in = (int)strtol(sig + 4, NULL, 10);
    else if (!strcmp(sig, "LPTIM1_OUT"))
        in = 20;
    if ((in >= 0 && in < 16) || in == 20)
        mux_input(x, in, level);
}
