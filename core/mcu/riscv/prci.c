/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* prci.c -- the FE310's power, reset, clock and interrupt block (FE310-G002
 * manual, chapter 7), over the register file: the core clock's sources.
 *
 *   - hfrosc, the internal oscillator: 72 MHz / (hfroscdiv + 1), nominal
 *     (the trim is not modelled; out of reset hfroscdiv is 4: 14.4 MHz);
 *   - hfxosc, the board's crystal (the HiFive1's 16 MHz);
 *   - the PLL: its reference (pllrefsel: hfxosc, else hfrosc) bypassed, or
 *     ref / (pllr + 1) * 2 (pllf + 1) / 2^pllq; pllsel takes the core
 *     clock from it, else from hfrosc; plloutdiv divides it by
 *     2 (div + 1) unless divby1;
 *   - the ready and lock bits read 1 while their oscillator is on: they
 *     settle at once.
 * A change of the core clock ends the quantum, so the runtime sees it at
 * the instruction that made it. */
#include <string.h>

#include "mcu/riscv/riscv.h"

struct fe_prci {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *hfrosc, *hfxosc, *pll, *outdiv;
};

static u32 core_hz(struct esk_mach *m);

static struct fe_prci *prci_of(struct esk_mach *m)
{
    return m->core_hz == core_hz ? (struct fe_prci *)m->clock_ctx : NULL;
}

static u32 core_hz(struct esk_mach *m)
{
    struct fe_prci *k = prci_of(m);
    u32 pll, rosc, ref, hz;
    if (!k)
        return 0;
    rosc = 72000000u / (((u32)k->hfrosc->value & 63u) + 1u);
    pll = (u32)k->pll->value;
    if (!(pll >> 16 & 1))
        return rosc;
    ref = (pll >> 17 & 1) ? (m->hse_hz ? m->hse_hz : 16000000u) : rosc;
    if (pll >> 18 & 1) {
        hz = ref;
    } else {
        u64 r = (pll & 7u) + 1u, f = 2u * (((pll >> 4) & 63u) + 1u), q = 1u << ((pll >> 10) & 3u);
        hz = (u32)((u64)ref / r * f / q);
    }
    if (!((u32)k->outdiv->value >> 8 & 1))
        hz /= 2u * (((u32)k->outdiv->value & 63u) + 1u);
    return hz;
}

static void ready(struct fe_prci *k)
{
    u64 lock = 1ull << 31;
    esk_rf_hw(k->hfrosc, (k->hfrosc->value >> 30 & 1) ? k->hfrosc->value | lock : k->hfrosc->value & ~lock);
    esk_rf_hw(k->hfxosc, (k->hfxosc->value >> 30 & 1) ? k->hfxosc->value | lock : k->hfxosc->value & ~lock);
    esk_rf_hw(k->pll, k->pll->value | lock);
}

static u32 pr_read(void *ctx, u32 off, int n)
{
    return esk_rf_read(((struct fe_prci *)ctx)->p, off, n);
}

static void pr_write(void *ctx, u32 off, int n, u32 v)
{
    struct fe_prci *k = (struct fe_prci *)ctx;
    u32 before = core_hz(k->s);
    esk_rf_write(k->p, off, n, v);
    ready(k);
    if (core_hz(k->s) != before)
        k->s->yield = 1;
}

static void pr_reset(void *ctx) { ready((struct fe_prci *)ctx); }

const struct esk_dev_ops esk_fe310_prci_ops = {
    "fe310-prci", pr_read, pr_write, pr_reset, 0, 0,
};

void *esk_fe310_prci_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct fe_prci k, *c;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    k.hfrosc = esk_rf_reg(p, "HFROSCCFG");
    k.hfxosc = esk_rf_reg(p, "HFXOSCCFG");
    k.pll = esk_rf_reg(p, "PLLCFG");
    k.outdiv = esk_rf_reg(p, "PLLOUTDIV");
    if (!k.hfrosc || !k.hfxosc || !k.pll || !k.outdiv)
        return 0;
    c = (struct fe_prci *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    s->clock_ctx = c;
    s->core_hz = core_hz;
    return c;
}
