/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* intc.c -- a PIC24F's interrupt controller, over its registers (INTCON1,
 * INTCON2, INTCON4, IFSx, IECx, IPCx, INTTREG: the INT peripheral),
 * driving the core (struct pic24_intc).
 *
 *   - Interrupt n is requested while its flag (IFS<n/16>, bit n%16) and
 *     its enable (IEC) are set and its priority (IPC<n/4>, three bits at
 *     4*(n%4)) is not 0, and INTCON2.GIE is set. The highest priority is
 *     presented to the core, then the lowest number (the natural order);
 *     the core takes it when it is above its own priority.
 *   - Taking it, INTTREG says which: VECNUM (the vector number, n + 8)
 *     and ILR (the new priority).
 *   - The core's traps set INTCON1's status bits (OSCFAIL, ADDRERR,
 *     STKERR, MATHERR) and INTCON4.SGHT for a hard trap; the program
 *     clears them in its handler.
 *   - INTCON1.NSTDIS: an interrupt runs at priority 7, so none nests.
 *   - A model raises its interrupt with esk_pic24_irq; the program clears
 *     the flag. Writing a flag sets it too, as on the part. A persistent
 *     source (a UART's transmitter with room, in its mode) sets its flag
 *     again as soon as it is cleared, while its condition holds.
 * Not modelled: the external interrupt pins (INTx and their edges), the
 * alternate vector table (AIVTEN), the software trap (SWTRAP).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/pic24-intc.c. Changes
 * for EmbSIM: the request is evaluated when a register or a flag changes,
 * not at every instruction; persistent sources; a request that comes
 * wakes a waiting core. */
#include <stdio.h>
#include <string.h>

#include "mcu/pic24/pic24.h"

#define NLEVEL 16

struct level {
    int irq;
    int (*cond)(void *);
    void *ctx;
};

struct intc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *intcon1, *intcon2, *intcon4, *inttreg;
    struct esk_rf_reg *ifs[16], *iec[16], *ipc[64];
    int nw;
    struct pic24_intc ic;
    int best, best_irq; /* the request presented */
    struct level lv[NLEVEL];
    int nlv;
    int busy;
};

static struct intc *intc_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "INT") : 0;
    return p && p->ops == &esk_pic24_intc_ops ? (struct intc *)p->ctx : 0;
}

static int prio(const struct intc *c, int irq)
{
    const struct esk_rf_reg *r = c->ipc[irq / 4];
    return r ? (int)(r->value >> (4 * (irq % 4)) & 7u) : 0;
}

static void set_flag(struct intc *c, int irq)
{
    struct esk_rf_reg *r = irq >= 0 && irq / 16 < c->nw ? c->ifs[irq / 16] : 0;
    u64 bit = (u64)1 << (irq % 16);
    if (r && !(r->value & bit))
        esk_rf_hw(r, r->value | bit);
}

static void evaluate(struct intc *c)
{
    int i, w, best = 0, best_irq = 0;
    if (c->busy)
        return;
    c->busy = 1;
    for (i = 0; i < c->nlv; i++)
        if (c->lv[i].cond(c->lv[i].ctx))
            set_flag(c, c->lv[i].irq);
    if (!c->intcon2 || (c->intcon2->value & 0x8000u)) {
        for (w = 0; w < c->nw; w++) {
            u32 pend = (u32)(c->ifs[w]->value & c->iec[w]->value) & 0xffffu;
            while (pend) {
                int bit = 0, n, pr;
                while (!(pend >> bit & 1u))
                    bit++;
                pend &= pend - 1u;
                n = 16 * w + bit;
                pr = prio(c, n);
                if (pr > best) {
                    best = pr;
                    best_irq = n;
                }
            }
        }
    }
    c->best = best;
    c->best_irq = best_irq;
    c->busy = 0;
    if (best)
        esk_pic24_intc_changed(c->s);
}

static int request(void *ctx, int *irq)
{
    struct intc *c = (struct intc *)ctx;
    *irq = c->best_irq;
    return c->best;
}

static void taken(void *ctx, int irq, int ipl)
{
    struct intc *c = (struct intc *)ctx;
    if (c->inttreg)
        esk_rf_hw(c->inttreg, (u64)((u32)(ipl & 15) << 8 | (u32)((irq + 8) & 0xff)));
}

static void trap(void *ctx, int n)
{
    static const u16 bit[8] = { 0x0002, 0x0008, 0, 0x0004, 0x0010, 0, 0, 0 };
    struct intc *c = (struct intc *)ctx;
    if (bit[n] && c->intcon1)
        esk_rf_hw(c->intcon1, c->intcon1->value | bit[n]);
    if (n == TRAP_HARD && c->intcon4)
        esk_rf_hw(c->intcon4, c->intcon4->value | 1u);
}

static int math(void *ctx, int why)
{
    static const char *const err[] = { "DIV0ERR", "OVAERR", "OVBERR", "COVAERR", "COVBERR", "SFTACERR" };
    static const char *const en[] = { 0, "OVATE", "OVBTE", "COVTE", "COVTE", 0 };
    struct intc *c = (struct intc *)ctx;
    if (!c->intcon1)
        return !en[why];
    if (en[why] && !(c->intcon1->value & esk_rf_mask(c->intcon1, en[why])))
        return 0;
    esk_rf_hw(c->intcon1, c->intcon1->value | esk_rf_mask(c->intcon1, err[why]) | 0x0010u);
    return 1;
}

static int nstdis(void *ctx)
{
    struct intc *c = (struct intc *)ctx;
    return c->intcon1 && (c->intcon1->value & 0x8000u);
}

static u32 intc_read(void *ctx, u32 off, int n)
{
    struct intc *c = (struct intc *)ctx;
    return esk_rf_read(c->p, off, n);
}

static void intc_write(void *ctx, u32 off, int n, u32 v)
{
    struct intc *c = (struct intc *)ctx;
    esk_rf_write(c->p, off, n, v);
    evaluate(c);
}

static void intc_reset(void *ctx)
{
    struct intc *c = (struct intc *)ctx;
    evaluate(c);
}

const struct esk_dev_ops esk_pic24_intc_ops = {
    "pic24-intc", intc_read, intc_write, intc_reset, 0, 0,
};

void esk_pic24_irq(struct esk_mach *m, int irq)
{
    struct intc *c = intc_of(m);
    if (!c || irq < 0 || irq / 16 >= c->nw)
        return;
    set_flag(c, irq);
    evaluate(c);
}

void esk_pic24_irq_update(struct esk_mach *m)
{
    struct intc *c = intc_of(m);
    if (c)
        evaluate(c);
}

int esk_pic24_irq_enabled(struct esk_mach *m, int irq)
{
    struct intc *c = intc_of(m);
    if (!c || irq < 0 || irq / 16 >= c->nw)
        return 0;
    return (int)(c->iec[irq / 16]->value >> (irq % 16) & 1u) && prio(c, irq) > 0;
}

void esk_pic24_irq_level(struct esk_mach *m, int irq, int (*cond)(void *ctx), void *ctx)
{
    struct intc *c = intc_of(m);
    int i;
    if (!c || irq < 0)
        return;
    for (i = 0; i < c->nlv; i++)
        if (c->lv[i].irq == irq && c->lv[i].ctx == ctx)
            return;
    if (c->nlv == NLEVEL)
        return;
    c->lv[c->nlv].irq = irq;
    c->lv[c->nlv].cond = cond;
    c->lv[c->nlv].ctx = ctx;
    c->nlv++;
}

void *esk_pic24_intc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct intc *c;
    char nm[16];
    int w, k;
    if (!esk_pic24_is(s->cpu))
        return 0;
    c = (struct intc *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    memset(c, 0, sizeof *c);
    c->s = s;
    c->p = p;
    c->intcon1 = esk_rf_reg(p, "INTCON1");
    c->intcon2 = esk_rf_reg(p, "INTCON2");
    c->intcon4 = esk_rf_reg(p, "INTCON4");
    c->inttreg = esk_rf_reg(p, "INTTREG");
    for (w = 0; w < 16; w++) {
        snprintf(nm, sizeof nm, "IFS%d", w);
        c->ifs[w] = esk_rf_reg(p, nm);
        snprintf(nm, sizeof nm, "IEC%d", w);
        c->iec[w] = esk_rf_reg(p, nm);
        if (!c->ifs[w] || !c->iec[w])
            break;
        c->nw = w + 1;
    }
    for (k = 0; k < 64; k++) {
        snprintf(nm, sizeof nm, "IPC%d", k);
        c->ipc[k] = esk_rf_reg(p, nm);
    }
    if (!c->nw)
        return 0;
    c->ic.request = request;
    c->ic.taken = taken;
    c->ic.trap = trap;
    c->ic.math = math;
    c->ic.nstdis = nstdis;
    c->ic.ctx = c;
    esk_pic24_intc_attach(s, &c->ic);
    return c;
}
