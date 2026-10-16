/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* evic.c -- a PIC32MX's interrupt controller (the EVIC, Family Reference
 * Manual section 8), over its registers (INT), driving the M4K in EIC mode.
 *
 *   - An interrupt request is pending while its flag (IFSx) and its enable
 *     (IECx) are set and its vector's priority (IPCx's IP, 1 to 7) is not
 *     0. The vectors are shared among requests, as the device pack says (a
 *     UART's error, receive and transmit requests are its one vector). Of
 *     the pending, the highest priority is presented to the core, then the
 *     highest subpriority (IS), then the lowest vector (the natural order).
 *   - The core takes it when the priority is above Status.IPL: in
 *     multi-vector mode (INTCON.MVEC) at EBASE + 0x200 + the vector times
 *     IntCtl.VS's spacing, in single-vector mode at EBASE + 0x200. Its
 *     register set is the shadow set when its priority is the one
 *     DEVCFG3.FSRSSEL names (multi-vector mode), or with INTCON.SS0
 *     (single-vector mode). INTSTAT then says which vector (VEC) and at
 *     what level (SRIPL).
 *   - The core's own interrupts are requests 0 to 2: its timer (Cause.TI)
 *     and the two software interrupts set CTIF, CS0IF and CS1IF as they
 *     rise; the program clears them, as the others.
 *   - A model sets a flag with esk_pic32_irq, and the program clears it
 *     (IFSxCLR); writing one sets it too, as on the part (a software-
 *     triggered interrupt). A persistent source (a UART's transmitter with
 *     room) sets its flag again as soon as it is cleared, while its
 *     condition holds.
 * Not modelled: INTCON's external interrupt edges (INTxEP), the proximity
 * timer (TPC, IPTMR). */
#include <stdio.h>
#include <string.h>

#include "mcu/mips/mips.h"
#include "mcu/pic32/pic32.h"

#define NLEVEL 16

struct level {
    int irq;
    int (*cond)(void *);
    void *ctx;
};

struct evic {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *intcon, *intstat;
    struct esk_rf_reg *ifs[8], *iec[8], *ipc[64];
    int nw;      /* IFS/IEC words */
    u8 vec[256]; /* each request's vector */
    struct mips_eic eic;
    int presented; /* the request presented, or -1 */
    u32 lines;     /* the core's lines, as last told */
    struct level lv[NLEVEL];
    int nlv;
    int busy; /* evaluating */
};

static struct evic *evic_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "INT") : 0;
    return p && p->ops == &esk_pic32_evic_ops ? (struct evic *)p->ctx : 0;
}

u32 esk_pic32_devcfg(struct esk_mach *m, int n)
{
    u32 w = 0;
    if (n < 0 || n > 3 || esk_bus_debug_read(&m->bus, PIC32MX_DEVCFG3 + 4u * (u32)(3 - n), 4, &w))
        return 0xffffffffu;
    return w;
}

/* request irq's vector's priority, and its subpriority */
static int prio(const struct evic *e, int irq, int *sub)
{
    int v = irq >= 0 && irq < 256 ? e->vec[irq] : 0;
    const struct esk_rf_reg *r = v / 4 < 64 ? e->ipc[v / 4] : 0;
    u32 b = r ? (u32)(r->value >> (8 * (v % 4))) : 0;
    *sub = (int)(b & 3u);
    return (int)((b >> 2) & 7u);
}

static void set_flag(struct evic *e, int irq)
{
    struct esk_rf_reg *r = irq >= 0 && irq / 32 < e->nw ? e->ifs[irq / 32] : 0;
    u64 bit = (u64)1 << (irq % 32);
    if (r && !(r->value & bit))
        esk_rf_hw(r, r->value | bit);
}

/* what the controller presents to the core; first, the persistent sources'
 * flags, which a write cannot clear while their condition holds */
static void evaluate(struct evic *e)
{
    int i, w, best = -1, bp = 0, bs = -1, mvec;
    if (e->busy)
        return;
    e->busy = 1;
    for (i = 0; i < e->nlv; i++)
        if (e->lv[i].cond(e->lv[i].ctx))
            set_flag(e, e->lv[i].irq);
    for (w = 0; w < e->nw; w++) {
        u32 pend = (u32)(e->ifs[w]->value & e->iec[w]->value);
        while (pend) {
            int bit = 0, irq, sub = 0, pr;
            while (!(pend >> bit & 1u))
                bit++;
            pend &= pend - 1u;
            irq = 32 * w + bit;
            pr = prio(e, irq, &sub);
            if (pr && (pr > bp || (pr == bp && (sub > bs || (sub == bs && e->vec[irq] < e->vec[best]))))) {
                best = irq;
                bp = pr;
                bs = sub;
            }
        }
    }
    mvec = (int)(e->intcon->value >> 12 & 1u);
    e->presented = best;
    e->eic.ripl = bp;
    e->eic.offset = 0x200;
    e->eic.vector = best >= 0 && mvec ? e->vec[best] : 0;
    if (mvec)
        e->eic.srs = bp && bp == (int)(esk_pic32_devcfg(e->s, 3) >> 16 & 7u);
    else
        e->eic.srs = bp ? (int)(e->intcon->value >> 16 & 1u) : 0;
    e->busy = 0;
    esk_mips_eic_changed(e->s);
}

void esk_pic32_irq(struct esk_mach *m, int irq)
{
    struct evic *e = evic_of(m);
    if (!e || irq < 0)
        return;
    set_flag(e, irq);
    evaluate(e);
}

void esk_pic32_irq_update(struct esk_mach *m)
{
    struct evic *e = evic_of(m);
    if (e)
        evaluate(e);
}

int esk_pic32_irq_flag(struct esk_mach *m, int irq)
{
    struct evic *e = evic_of(m);
    if (!e || irq < 0 || irq / 32 >= e->nw)
        return 0;
    return (int)(e->ifs[irq / 32]->value >> (irq % 32) & 1u);
}

int esk_pic32_irq_enabled(struct esk_mach *m, int irq)
{
    struct evic *e = evic_of(m);
    int sub = 0;
    if (!e || irq < 0 || irq / 32 >= e->nw)
        return 0;
    return (int)(e->iec[irq / 32]->value >> (irq % 32) & 1u) && prio(e, irq, &sub) > 0;
}

void esk_pic32_irq_level(struct esk_mach *m, int irq, int (*cond)(void *ctx), void *ctx)
{
    struct evic *e = evic_of(m);
    if (!e || irq < 0 || e->nlv == NLEVEL)
        return;
    e->lv[e->nlv].irq = irq;
    e->lv[e->nlv].cond = cond;
    e->lv[e->nlv].ctx = ctx;
    e->nlv++;
}

/* ---- the core's side ---------------------------------------------------- */

static void core_irq(void *ctx, int timer, int sw0, int sw1)
{
    struct evic *e = (struct evic *)ctx;
    u32 l = (u32)(timer != 0) | (u32)(sw0 != 0) << 1 | (u32)(sw1 != 0) << 2, rise = l & ~e->lines;
    int i;
    e->lines = l;
    for (i = 0; i < 3; i++)
        if (rise >> i & 1u)
            set_flag(e, i);
    if (rise)
        evaluate(e);
}

static void taken(void *ctx)
{
    struct evic *e = (struct evic *)ctx;
    if (e->presented >= 0)
        esk_rf_hw(e->intstat, (e->intstat->value & ~(u64)0x7ff) | (u64)e->eic.ripl << 8 |
                                  (u64)(e->eic.vector & 0x3f));
}

static int timer_on(void *ctx)
{
    struct evic *e = (struct evic *)ctx;
    int sub = 0;
    return (int)(e->iec[0]->value & 1u) && prio(e, 0, &sub) > 0;
}

/* ---- the registers ------------------------------------------------------- */

static u32 evic_read(void *ctx, u32 off, int n)
{
    struct evic *e = (struct evic *)ctx;
    return esk_rf_read(e->p, off, n);
}

static void evic_write(void *ctx, u32 off, int n, u32 v)
{
    struct evic *e = (struct evic *)ctx;
    esk_rf_write(e->p, off, n, v);
    evaluate(e);
}

static void evic_reset(void *ctx)
{
    struct evic *e = (struct evic *)ctx;
    e->lines = 0;
    e->presented = -1;
    evaluate(e);
}

const struct esk_dev_ops esk_pic32_evic_ops = {
    "pic32-evic", evic_read, evic_write, evic_reset, 0, 0,
};

void *esk_pic32_evic_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct evic *e;
    char nm[16];
    int i;
    if (!esk_mips_is(s->cpu))
        return 0;
    e = (struct evic *)esk_mach_alloc(s, sizeof *e);
    if (!e)
        return 0;
    memset(e, 0, sizeof *e);
    e->s = s;
    e->p = p;
    e->intcon = esk_rf_reg(p, "INTCON");
    e->intstat = esk_rf_reg(p, "INTSTAT");
    for (i = 0; i < 8; i++) {
        snprintf(nm, sizeof nm, "IFS%d", i);
        e->ifs[i] = esk_rf_reg(p, nm);
        snprintf(nm, sizeof nm, "IEC%d", i);
        e->iec[i] = esk_rf_reg(p, nm);
        if (!e->ifs[i] || !e->iec[i])
            break;
        e->nw = i + 1;
    }
    for (i = 0; i < 64; i++) {
        snprintf(nm, sizeof nm, "IPC%d", i);
        e->ipc[i] = esk_rf_reg(p, nm);
    }
    if (!e->intcon || !e->intstat || !e->nw)
        return 0;
    for (i = 0; i < 256; i++) {
        int v = esk_rf_vector(s->rf, i);
        e->vec[i] = (u8)(v >= 0 && v < 64 ? v : i < 64 ? i : 63);
    }
    e->presented = -1;
    e->eic.spaced = 1;
    e->eic.core_irq = core_irq;
    e->eic.taken = taken;
    e->eic.timer_on = timer_on;
    e->eic.ctx = e;
    esk_mips_eic_attach(s, &e->eic);
    return e;
}
