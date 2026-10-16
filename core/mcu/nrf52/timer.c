/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* timer.c -- the nRF52840's TIMER0-4 (Product Specification 6.30), over
 * the register file.
 *
 *   - START, STOP, CLEAR, CAPTURE[n], SHUTDOWN, COUNT; in Timer mode the
 *     counter counts at 16 MHz / 2^PRESCALER (PRESCALER above 9 is 9), in
 *     the Counter modes at each COUNT task; BITMODE's width (16, 8, 24, 32
 *     bits) wraps it;
 *   - COMPARE[n] when the counter becomes CC[n]; SHORTS' COMPAREn_CLEAR
 *     and COMPAREn_STOP. TIMER0-2 have four CC registers, TIMER3-4 six (the
 *     PS's instance table; the SVD gives each six: the two extra keep what
 *     is written). */
#include <string.h>

#include "mcu/nrf52/nrf52.h"

struct ntimer {
    struct nrfp x;
    int ncc, on, running;
    u32 count;
    u32 frac; /* core cycles toward the next tick */
    u64 since;
};

static u32 tim_mask(const struct ntimer *t)
{
    static const u32 m[4] = { 0xffffu, 0xffu, 0xffffffu, 0xffffffffu };
    return m[esk_nrfp_rd(&t->x, 0x508) & 3u];
}

static u32 tim_period(const struct ntimer *t) /* core cycles a tick */
{
    u32 p = esk_nrfp_rd(&t->x, 0x510) & 15u;
    return 4u << (p > 9 ? 9 : p);
}

static int tim_timer_mode(const struct ntimer *t) { return (esk_nrfp_rd(&t->x, 0x504) & 3u) == 0; }

static void tim_clock(struct ntimer *t)
{
    int was = t->running;
    esk_rf_clock(t->x.p, &t->running, t->on && tim_timer_mode(t));
    if (t->running && !was)
        t->since = t->x.s->cycles;
}


/* the counter is now `count`: the compares it meets, and their shorts */
static void tim_set(struct ntimer *t, u32 count)
{
    int n;
    t->count = count & tim_mask(t);
    for (n = 0; n < t->ncc; n++)
        if (t->count == (esk_nrfp_rd(&t->x, 0x540u + 4u * (u32)n) & tim_mask(t))) {
            u32 sh = esk_nrfp_rd(&t->x, 0x200);
            esk_nrfp_event(&t->x, 0x140u + 4u * (u32)n);
            if (sh >> n & 1u)
                t->count = 0;
            if (sh >> (8 + n) & 1u) {
                t->on = 0;
                tim_clock(t);
            }
        }

}

static void tim_tick(void *ctx, u32 cycles)
{
    struct ntimer *t = (struct ntimer *)ctx;
    struct esk_mach *s = t->x.s;
    u32 per = tim_period(t);
    cycles = esk_mach_after(s, cycles, t->since);
    t->frac += cycles;
    while (t->frac >= per && t->on) {
        t->frac -= per;
        tim_set(t, t->count + 1u);
    }
}

static int tim_next(void *ctx, u32 *cycles)
{
    struct ntimer *t = (struct ntimer *)ctx;
    u32 best = 0, per = tim_period(t), m = tim_mask(t);
    u64 c;
    int any = 0, n;
    if (!t->on || !tim_timer_mode(t))
        return 0;
    for (n = 0; n < t->ncc; n++) {
        u32 d;
        if (!(t->x.inten >> (16 + n) & 1u) && !(esk_nrfp_rd(&t->x, 0x200) >> n & 1u))
            continue;
        d = (esk_nrfp_rd(&t->x, 0x540u + 4u * (u32)n) - t->count) & m;
        if (!d)
            d = m == 0xffffffffu ? 0xffffffffu : m + 1u;
        if (!any || d < best)
            best = d;
        any = 1;
    }
    if (!any)
        return 0;
    c = (u64)best * per - t->frac;
    *cycles = c > 0xffffffffu ? 0xffffffffu : (u32)c;
    return 1;
}

static u32 tim_read(void *ctx, u32 off, int n)
{
    struct ntimer *t = (struct ntimer *)ctx;
    esk_nrfp_inten_read(&t->x, off);
    return esk_rf_read(t->x.p, off, n);
}

static void tim_write(void *ctx, u32 off, int n, u32 v)
{
    struct ntimer *t = (struct ntimer *)ctx;
    int was = esk_nrfp_line(&t->x);
    esk_rf_write(t->x.p, off, n, v);
    esk_nrfp_inten_write(&t->x, off, v);
    if (off < 0x100 && (v & 1)) {
        switch (off) {
        case 0x000: t->on = 1; break; /* START */
        case 0x004: t->on = 0; break; /* STOP */
        case 0x008:                   /* COUNT */
            if (!tim_timer_mode(t) && t->on)
                tim_set(t, t->count + 1u);
            break;
        case 0x00c: t->count = 0; break;               /* CLEAR */
        case 0x010: t->on = 0; t->count = 0; break;    /* SHUTDOWN */
        default:
            if (off >= 0x040 && off < 0x040u + 4u * (u32)t->ncc) /* CAPTURE */
                esk_nrfp_hw(&t->x, 0x540u + (off - 0x040u), t->count);
            break;
        }
    }
    tim_clock(t);
    esk_nrfp_irq(&t->x, was, 1);
}

static void tim_reset(void *ctx)
{
    struct ntimer *t = (struct ntimer *)ctx;
    t->on = t->running = 0;
    t->count = t->frac = 0;
    t->x.inten = 0;
}

const struct esk_dev_ops esk_nrf_timer_ops = {
    "nrf-timer", tim_read, tim_write, tim_reset, tim_tick, tim_next,
};

void *esk_nrf_timer_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct ntimer *t;
    if (strncmp(p->name, "TIMER", 5))
        return 0;
    t = (struct ntimer *)esk_mach_alloc(s, sizeof *t);
    if (!t)
        return 0;
    memset(t, 0, sizeof *t);
    if (esk_nrfp_init(&t->x, s, p))
        return 0;
    t->ncc = !strcmp(p->name, "TIMER3") || !strcmp(p->name, "TIMER4") ? 6 : 4;
    return t;
}
