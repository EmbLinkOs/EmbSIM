/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* osc.c -- a PIC32MX's oscillator (OSCCON, Family Reference Manual section
 * 6) and its system lock (CFG's SYSKEY), over the register file.
 *
 *   - Out of reset OSCCON has the clock the configuration words select:
 *     COSC and NOSC are DEVCFG1.FNOSC, PLLMULT and PLLODIV DEVCFG2's
 *     FPLLMUL and FPLLODIV, PBDIV DEVCFG1.FPBDIV; FRCDIV is 1 (FRC / 2).
 *   - SYSCLK is the current oscillator: FRC (8 MHz), FRC or the primary
 *     oscillator (the board's crystal, POSC) through the PLL -- divided by
 *     DEVCFG2.FPLLIDIV (1 to 12), multiplied by PLLMULT (15 to 24), divided
 *     by PLLODIV (1 to 256) --, POSC, SOSC (32.768 kHz), LPRC (31.25 kHz),
 *     FRC / 16, or FRC / FRCDIV. It is the core's clock: the MCU runtime
 *     turns the core's cycles into time at it, and sees a change at once.
 *   - Setting OSWEN switches to NOSC at once (CLKLOCK aside) and clears it;
 *     SLOCK says the PLL is locked whenever it is in use.
 *   - PBDIV divides SYSCLK for the peripheral bus (1, 2, 4 or 8): the
 *     timers and UARTs count PBCLK (esk_pic32_pb_div).
 *   - SYSKEY: writing 0xAA996655 then 0x556699AA unlocks OSCCON, and any
 *     other write locks it again; SYSKEY reads 0. A write to OSCCON while
 *     locked is ignored, as on the part, and the first is a warning
 *     (mcu.locked_write).
 * Not modelled: the clock fail-safe monitor, the USB PLL, sleep and idle
 * (SLPEN), the oscillator's start-up time, OSCTUN. */
#include <string.h>

#include "mcu/pic32/pic32.h"

struct cfg {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *syskey;
    int stage; /* 0; 1: the first key written; 2: unlocked */
};

struct osc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *osccon;
    int warned;
};

static struct cfg *cfg_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "CFG") : 0;
    return p && p->ops == &esk_pic32_cfg_ops ? (struct cfg *)p->ctx : 0;
}

static struct osc *osc_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "OSC") : 0;
    return p && p->ops == &esk_pic32_osc_ops ? (struct osc *)p->ctx : 0;
}

int esk_pic32_unlocked(struct esk_mach *m)
{
    struct cfg *c = cfg_of(m);
    return !c || c->stage == 2;
}

/* ---- the clock ------------------------------------------------------------- */

static u32 fld(u64 v, int lsb, int w) { return (u32)(v >> lsb) & ((1u << w) - 1u); }

static u32 pll_hz(const struct osc *o, u32 in)
{
    static const u32 idiv[8] = { 1, 2, 3, 4, 5, 6, 10, 12 };
    static const u32 mul[8] = { 15, 16, 17, 18, 19, 20, 21, 24 };
    static const u32 odiv[8] = { 1, 2, 4, 8, 16, 32, 64, 256 };
    u64 v = o->osccon->value;
    u64 hz = (u64)in / idiv[esk_pic32_devcfg(o->s, 2) & 7u] * mul[fld(v, 16, 3)] / odiv[fld(v, 27, 3)];
    return (u32)hz;
}

static u32 osc_hz(const struct osc *o)
{
    static const u32 frcdiv[8] = { 1, 2, 4, 8, 16, 32, 64, 256 };
    u64 v = o->osccon->value;
    u32 posc = o->s->hse_hz ? o->s->hse_hz : 8000000u;
    switch (fld(v, 12, 3)) {
    case 0: return PIC32MX_FRC_HZ;
    case 1: return pll_hz(o, PIC32MX_FRC_HZ);
    case 2: return posc;
    case 3: return pll_hz(o, posc);
    case 4: return PIC32MX_SOSC_HZ;
    case 5: return PIC32MX_LPRC_HZ;
    case 6: return PIC32MX_FRC_HZ / 16u;
    default: return PIC32MX_FRC_HZ / frcdiv[fld(v, 24, 3)];
    }
}

static u32 core_hz(struct esk_mach *m)
{
    struct osc *o = (struct osc *)m->clock_ctx;
    return o ? osc_hz(o) : 0;
}

u32 esk_pic32_sysclk(struct esk_mach *m)
{
    struct osc *o = osc_of(m);
    return o ? osc_hz(o) : esk_mach_core_hz(m);
}

u32 esk_pic32_pb_div(struct esk_mach *m)
{
    struct osc *o = osc_of(m);
    return o ? 1u << fld(o->osccon->value, 19, 2) : 1u;
}

/* SLOCK: the PLL is locked while it is in use */
static void status(struct osc *o)
{
    u64 v = o->osccon->value, cosc = fld(v, 12, 3);
    u64 w = (cosc == 1 || cosc == 3) ? v | (u64)1 << 5 : v & ~((u64)1 << 5);
    if (w != v)
        esk_rf_hw(o->osccon, w);
}

/* ---- OSC ------------------------------------------------------------------- */

static u32 osc_read(void *ctx, u32 off, int n)
{
    struct osc *o = (struct osc *)ctx;
    return esk_rf_read(o->p, off, n);
}

static void osc_write(void *ctx, u32 off, int n, u32 v)
{
    struct osc *o = (struct osc *)ctx;
    struct esk_mach *s = o->s;
    u32 before = osc_hz(o), pb = esk_pic32_pb_div(s);
    u64 c;
    if (off == esk_rf_off(o->osccon) && !esk_pic32_unlocked(s)) {
        if (!o->warned && !s->bus.debug) {
            o->warned = 1;
            esk_rf_diag(s, ES_SEV_WARNING, "mcu.locked_write",
                        "a write at pc 0x%08lx to OSCCON is ignored: it is locked (SYSKEY's unlock "
                        "sequence was not written)",
                        (unsigned long)s->cpu->ops->pc(s->cpu));
        }
        return;
    }
    esk_rf_write(o->p, off, n, v);
    c = o->osccon->value;
    if (off == esk_rf_off(o->osccon) && (c & 1u)) /* OSWEN: switch to NOSC */
        esk_rf_hw(o->osccon, (c & ~(u64)0x7001) | (u64)fld(c, 8, 3) << 12);
    status(o);
    if (osc_hz(o) != before || esk_pic32_pb_div(s) != pb)
        s->yield = 1; /* the runtime sees the new clock now */
}

static void osc_reset(void *ctx)
{
    struct osc *o = (struct osc *)ctx;
    u32 c1 = esk_pic32_devcfg(o->s, 1), c2 = esk_pic32_devcfg(o->s, 2);
    u64 v = o->osccon->reset, fnosc = c1 & 7u;
    v = (v & ~(u64)0x7700) | fnosc << 12 | fnosc << 8;
    v = (v & ~((u64)7 << 16)) | (u64)(c2 >> 4 & 7u) << 16;   /* PLLMULT */
    v = (v & ~((u64)7 << 27)) | (u64)(c2 >> 16 & 7u) << 27;  /* PLLODIV */
    v = (v & ~((u64)3 << 19)) | (u64)(c1 >> 12 & 3u) << 19;  /* PBDIV */
    if (c1 >> 5 & 1u)
        v |= (u64)1 << 1 | (u64)1 << 22; /* SOSCEN, SOSCRDY */
    esk_rf_hw(o->osccon, v);
    status(o);
    o->warned = 0;
}

const struct esk_dev_ops esk_pic32_osc_ops = {
    "pic32-osc", osc_read, osc_write, osc_reset, 0, 0,
};

void *esk_pic32_osc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct osc *o;
    struct esk_rf_reg *r = esk_rf_reg(p, "OSCCON");
    if (!r)
        return 0;
    o = (struct osc *)esk_mach_alloc(s, sizeof *o);
    if (!o)
        return 0;
    memset(o, 0, sizeof *o);
    o->s = s;
    o->p = p;
    o->osccon = r;
    esk_rf_set_acc(r, 0x7000u, ESK_ACC_RO);          /* COSC */
    esk_rf_set_acc(r, (u64)1 << 5 | (u64)1 << 22, ESK_ACC_RO); /* SLOCK, SOSCRDY */
    s->clock_ctx = o;
    s->core_hz = core_hz;
    return o;
}

/* ---- CFG: SYSKEY -------------------------------------------------------------- */

static u32 cfg_read(void *ctx, u32 off, int n)
{
    struct cfg *c = (struct cfg *)ctx;
    if (off == esk_rf_off(c->syskey))
        return 0;
    return esk_rf_read(c->p, off, n);
}

static void cfg_write(void *ctx, u32 off, int n, u32 v)
{
    struct cfg *c = (struct cfg *)ctx;
    esk_rf_write(c->p, off, n, v);
    if (off != esk_rf_off(c->syskey) || n != 4)
        return;
    if (v == 0xaa996655u)
        c->stage = 1;
    else if (v == 0x556699aau && c->stage == 1)
        c->stage = 2;
    else
        c->stage = 0;
}

static void cfg_reset(void *ctx) { ((struct cfg *)ctx)->stage = 0; }

const struct esk_dev_ops esk_pic32_cfg_ops = {
    "pic32-cfg", cfg_read, cfg_write, cfg_reset, 0, 0,
};

void *esk_pic32_cfg_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct cfg *c;
    struct esk_rf_reg *k = esk_rf_reg(p, "SYSKEY");
    if (!k)
        return 0;
    c = (struct cfg *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    memset(c, 0, sizeof *c);
    c->s = s;
    c->p = p;
    c->syskey = k;
    esk_rf_set_acc(k, 0xffffffffu, ESK_ACC_WO);
    return c;
}
