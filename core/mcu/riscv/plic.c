/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* plic.c -- the FE310's platform-level interrupt controller (FE310-G002
 * manual, chapter 10; the RISC-V PLIC specification), over the register
 * file: 52 sources, 7 priority levels, one hart's machine-mode context.
 *
 *   - each source is a level from its device (a UART's enabled watermark,
 *     a GPIO pin's enabled pending bits); its gateway makes it pending
 *     while the level is high and it is not being served;
 *   - MEIP is raised while some source is pending, enabled and of a
 *     priority above the threshold;
 *   - a read of the claim register returns the highest-priority such
 *     source (the lowest number among equals), clears its pending bit and
 *     marks it served; writing its number back completes it, and a source
 *     whose level is still high is pending again;
 *   - a source of priority 0 never interrupts. */
#include <stdio.h>
#include <string.h>

#include "mcu/riscv/riscv.h"

#define NSRC 53 /* sources 1 to 52; 0 is none */

struct fe_plic {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *prio[NSRC], *pending[2], *enable[2], *threshold, *claim;
    u8 level[NSRC];
    u8 served[NSRC];
};

static struct fe_plic *plic_of(struct esk_mach *m)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_fe310_plic_ops)
            return (struct fe_plic *)p->ctx;
    }
    return NULL;
}

static int bit_of(const struct fe_plic *k, struct esk_rf_reg *const *r, int src)
{
    ES_UNUSED(k);
    return (int)(r[src / 32]->value >> (src % 32) & 1);
}

static void set_pending(struct fe_plic *k, int src, int on)
{
    struct esk_rf_reg *r = k->pending[src / 32];
    u64 m = (u64)1 << (src % 32);
    esk_rf_hw(r, on ? r->value | m : r->value & ~m);
}

/* the best claimable source, or 0 */
static int best(const struct fe_plic *k)
{
    int s, b = 0;
    u32 bp = 0, th = (u32)k->threshold->value & 7;
    for (s = 1; s < NSRC; s++) {
        u32 pr = k->prio[s] ? (u32)k->prio[s]->value & 7 : 0;
        if (pr > th && pr > bp && bit_of(k, k->pending, s) && bit_of(k, k->enable, s)) {
            b = s;
            bp = pr;
        }
    }
    return b;
}

static void update(struct fe_plic *k)
{
    int s;
    for (s = 1; s < NSRC; s++)
        if (k->level[s] && !k->served[s])
            set_pending(k, s, 1);
    esk_riscv_line(k->s, MIP_MEIP, best(k) != 0);
}

void esk_fe310_plic_source(struct esk_mach *m, int source, int level)
{
    struct fe_plic *k = plic_of(m);
    if (!k || source <= 0 || source >= NSRC || k->level[source] == (level != 0))
        return;
    k->level[source] = (u8)(level != 0);
    update(k);
}

static u32 pl_read(void *ctx, u32 off, int n)
{
    struct fe_plic *k = (struct fe_plic *)ctx;
    if (off == esk_rf_off(k->claim)) {
        int b = best(k);
        if (!k->s->bus.debug && b) {
            set_pending(k, b, 0);
            k->served[b] = 1;
            update(k);
        }
        return (u32)b;
    }
    return esk_rf_read(k->p, off, n);
}

static void pl_write(void *ctx, u32 off, int n, u32 v)
{
    struct fe_plic *k = (struct fe_plic *)ctx;
    if (off == esk_rf_off(k->claim)) {
        if (v > 0 && v < NSRC)
            k->served[v] = 0; /* complete */
    } else if (off == esk_rf_off(k->pending[0]) || off == esk_rf_off(k->pending[1])) {
        /* the pending bits are the gateways': writes are ignored */
    } else {
        esk_rf_write(k->p, off, n, v);
    }
    update(k);
}

static void pl_reset(void *ctx)
{
    struct fe_plic *k = (struct fe_plic *)ctx;
    memset(k->served, 0, sizeof k->served);
    update(k);
}

const struct esk_dev_ops esk_fe310_plic_ops = {
    "fe310-plic", pl_read, pl_write, pl_reset, 0, 0,
};

void *esk_fe310_plic_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct fe_plic *c, k;
    char name[16];
    int i;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    for (i = 1; i < NSRC; i++) {
        (void)snprintf(name, sizeof name, "PRIORITY%d", i);
        k.prio[i] = esk_rf_reg(p, name);
    }
    k.pending[0] = esk_rf_reg(p, "PENDING0");
    k.pending[1] = esk_rf_reg(p, "PENDING1");
    k.enable[0] = esk_rf_reg(p, "ENABLE0");
    k.enable[1] = esk_rf_reg(p, "ENABLE1");
    k.threshold = esk_rf_reg(p, "THRESHOLD");
    k.claim = esk_rf_reg(p, "CLAIM");
    if (!k.pending[0] || !k.pending[1] || !k.enable[0] || !k.enable[1] || !k.threshold || !k.claim ||
        !k.prio[1])
        return 0;
    c = (struct fe_plic *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = k;
    return c;
}
