/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* systick.c -- the Cortex-M's SysTick timer, at 0xE000E010.
 *
 * It counts the core's estimated cycles (the processor clock), not the
 * host's time, so a timed run gives the same answer every time; with
 * CSR.CLKSOURCE clear it counts the part's external reference clock
 * instead, the processor clock divided as the part divides it (an
 * STM32's HCLK/8: the description's core.systick.ref_div), CLKSOURCE
 * reading 1 on a part with no reference. From 1 to 0 it sets COUNTFLAG
 * and, with TICKINT, pends the SysTick exception (15); at 0 it reloads RVR
 * (a reload of 0 stops it there). Writing CVR clears it and COUNTFLAG;
 * reading CSR clears COUNTFLAG. CALIB reads the description's
 * core.systick.calib (NOREF set when the part has no reference). Its next
 * event is what lets a WFI skip ahead instead of stepping through the
 * wait.
 *
 * Its registers are word registers inside the system control space: a
 * narrower access reads the word and shifts it, or writes the word with
 * the other bytes zero, as the SCS's do.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/systick.c. Changes: the
 * reference clock, CALIB, counting a long wait at once. */
#include "mcu/cortexm/cortexm.h"

struct systick {
    struct esk_mach *sim;
    u32 csr, rvr, cvr;
    int running;                    /* CSR.ENABLE, as esk_mach_clock knows it */
    void (*pend)(void *ctx, int n); /* a multi-core part's: its own core */
    void *pend_ctx;
    u32 ref_div;                    /* processor cycles per reference tick, 0 none */
    u32 calib;
    u32 frac;                       /* processor cycles toward the next reference tick */
};

/* the processor cycles of one count: 1, or the reference's divider */
static u32 per_count(const struct systick *t)
{
    return (t->csr & 4) || !t->ref_div ? 1 : t->ref_div;
}

static u32 reg_read(struct systick *t, u32 o)
{
    u32 v;
    switch (o) {
    case 0x0: v = t->csr; t->csr &= ~(1u << 16); return v;
    case 0x4: return t->rvr;
    case 0x8: return t->cvr;
    case 0xc: return t->calib;
    }
    return 0;
}

static void reg_write(struct systick *t, u32 o, u32 v)
{
    switch (o) {
    case 0x0:
        t->csr = (t->csr & (1u << 16)) | (v & 7);
        if (!t->ref_div)
            t->csr |= 4; /* no reference: the processor clock */
        esk_mach_clock(t->sim, &t->running, t->csr & 1);
        return;
    case 0x4: t->rvr = v & 0xffffff; return;
    case 0x8: t->cvr = 0; t->csr &= ~(1u << 16); return;
    }
}

static u32 st_read(void *ctx, u32 off, int n)
{
    (void)n;
    return reg_read(ctx, off & ~3u) >> (8 * (off & 3));
}

static void st_write(void *ctx, u32 off, int n, u32 v)
{
    if (n == 4)
        reg_write(ctx, off, v);
    else
        reg_write(ctx, off & ~3u, v << (8 * (off & 3)));
}

static void fire(struct systick *t)
{
    t->csr |= 1u << 16;
    if ((t->csr & 2) && t->pend)
        t->pend(t->pend_ctx, 15);
    else if (t->csr & 2)
        t->sim->cpu->ops->interrupt(t->sim->cpu, 15);
}

static void st_tick(void *ctx, u32 c)
{
    struct systick *t = ctx;
    u32 d;
    u64 n;
    if (!(t->csr & 1))
        return;
    d = per_count(t);
    n = c;
    if (d > 1) {
        u64 all = (u64)t->frac + c;
        n = all / d;
        t->frac = (u32)(all % d);
    }
    while (n) {
        if (t->cvr == 0) {
            if (!t->rvr)
                return; /* a reload of 0: it stays */
            t->cvr = t->rvr;
            n--;
            continue;
        }
        if (n < t->cvr) {
            t->cvr -= (u32)n;
            return;
        }
        n -= t->cvr;
        t->cvr = 0;
        fire(t);
        /* whole periods (reload, then RVR counts to 0) at once */
        if (n > (u64)t->rvr + 1)
            n %= (u64)t->rvr + 1;
    }
}

static int st_next_event(void *ctx, u32 *cycles)
{
    struct systick *t = ctx;
    u64 counts, c;
    if ((t->csr & 3) != 3 || (!t->cvr && !t->rvr))
        return 0;
    counts = t->cvr ? t->cvr : (u64)t->rvr + 1;
    c = counts * per_count(t);
    c = c > t->frac ? c - t->frac : 1;
    *cycles = c > 0xffffffffULL ? 0xffffffffu : (u32)c;
    return 1;
}

static void st_reset(void *ctx)
{
    struct systick *t = ctx;
    t->csr = t->ref_div ? 0 : 4;
    t->rvr = t->cvr = 0;
    t->frac = 0;
    t->running = 0;
}

const struct esk_dev_ops esk_systick_ops = {
    "systick", st_read, st_write, st_reset, st_tick, st_next_event,
};

void *esk_systick_create(struct esk_mach *s)
{
    struct systick *t = (struct systick *)esk_mach_alloc(s, sizeof *t);
    if (t) {
        t->sim = s;
        t->calib = 0x80000000u; /* NOREF: until the part says otherwise */
        t->csr = 4;
    }
    return t;
}

void esk_systick_target(void *systick, void (*pend)(void *ctx, int n), void *ctx)
{
    struct systick *t = (struct systick *)systick;
    t->pend = pend;
    t->pend_ctx = ctx;
}

void esk_systick_config(void *systick, u32 ref_div, u32 calib)
{
    struct systick *t = (struct systick *)systick;
    t->ref_div = ref_div;
    t->calib = ref_div ? calib & ~0x80000000u : calib | 0x80000000u;
    t->csr = ref_div ? t->csr & ~4u : t->csr | 4u;
}
