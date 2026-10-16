/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* timer.c -- the RP2040's system timer (its datasheet, 4.6), over the
 * register file.
 *
 *   - a 64-bit count of the watchdog's microsecond ticks (clocks.c makes
 *     them from clk_ref), while the tick runs, PAUSE is clear and the
 *     timer is out of reset;
 *   - TIMERAWH and TIMERAWL read it as it is; a read of TIMELR latches the
 *     high half, which TIMEHR then reads; TIMELW then TIMEHW write it;
 *   - ALARM0-3: a write arms the alarm (its ARMED bit); when the count's
 *     low half reaches the value written, INTR's bit sets and ARMED's
 *     clears. Writing ARMED's bit disarms it;
 *   - INTS is (INTR | INTF) & INTE, and each of its bits is TIMER_IRQ_n,
 *     made pending in the NVIC when it sets (and again when a write leaves
 *     it set). INTR's bits are cleared by writing 1.
 * DBGPAUSE has no effect: a debugger's halt stops all of the machine's
 * time. */
#include <stdio.h>
#include <string.h>

#include "mcu/rp2040/rp2040.h"

struct timer {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *timehw, *timelw, *timehr, *timelr, *alarm[4], *armed;
    struct esk_rf_reg *rawh, *rawl, *pause, *intr, *inte, *intf, *ints;
    int irq[4];
    u64 time;
    u32 lw;      /* TIMELW, until TIMEHW */
    u32 latched; /* the high half TIMELR latched */
    int latch;
};

static struct timer *timer_of(struct esk_mach *s)
{
    return (struct timer *)esk_rp_model(s, "TIMER", &esk_rp2040_timer_ops);
}

static void show(struct timer *t)
{
    esk_rf_hw(t->rawh, t->time >> 32);
    esk_rf_hw(t->rawl, (u32)t->time);
}

/* INTS again: each bit that sets raises its TIMER_IRQ_n, and with `again`
 * (a write) each that stays set */
static void irqs(struct timer *t, u32 was, int again)
{
    u32 v = (u32)((t->intr->value | t->intf->value) & t->inte->value) & 15u;
    int n;
    if (v != t->ints->value)
        esk_rf_hw(t->ints, v);
    for (n = 0; n < 4; n++)
        if ((v >> n & 1) && (!(was >> n & 1) || again))
            esk_rf_interrupt(t->p, t->irq[n]);
}

void esk_rp2040_timer_ticks(struct esk_mach *s, u32 n)
{
    struct timer *t = timer_of(s);
    u32 lo, was, fired = 0;
    int k;
    if (!t || esk_rf_gated(t->p) || (t->pause->value & 1))
        return;
    lo = (u32)t->time;
    was = (u32)t->ints->value;
    t->time += n;
    show(t);
    for (k = 0; k < 4; k++)
        if (t->armed->value >> k & 1) {
            u32 d = (u32)t->alarm[k]->value - lo; /* ticks to the match */
            if (d && d <= n)
                fired |= 1u << k;
        }
    if (fired) {
        esk_rf_hw(t->armed, t->armed->value & ~(u64)fired);
        esk_rf_hw(t->intr, t->intr->value | fired);
        irqs(t, was, 0);
    }
}

int esk_rp2040_timer_next(struct esk_mach *s, u32 *ticks)
{
    struct timer *t = timer_of(s);
    int any = 0, k;
    if (!t || esk_rf_gated(t->p) || (t->pause->value & 1))
        return 0;
    for (k = 0; k < 4; k++)
        if ((t->armed->value & t->inte->value) >> k & 1) {
            u32 d = (u32)t->alarm[k]->value - (u32)t->time;
            if (!d)
                continue; /* passed: the count must wrap first */
            if (!any || d < *ticks)
                *ticks = d;
            any = 1;
        }
    return any;
}

static u32 timer_read(void *ctx, u32 off, int n)
{
    struct timer *t = (struct timer *)ctx;
    show(t);
    if (off == esk_rf_off(t->timelr) && !t->s->bus.debug) {
        esk_rf_hw(t->timelr, (u32)t->time);
        t->latched = (u32)(t->time >> 32);
        t->latch = 1;
    }
    if (off == esk_rf_off(t->timehr)) {
        esk_rf_hw(t->timehr, t->latch ? t->latched : (u32)(t->time >> 32));
        if (!t->s->bus.debug)
            t->latch = 0;
    }
    return esk_rf_read(t->p, off, n);
}

static void timer_write(void *ctx, u32 off, int n, u32 v)
{
    struct timer *t = (struct timer *)ctx;
    u32 was = (u32)t->ints->value;
    int k;
    esk_rf_write(t->p, off, n, v);
    if (off == esk_rf_off(t->timelw)) {
        t->lw = v;
    } else if (off == esk_rf_off(t->timehw)) {
        t->time = (u64)v << 32 | t->lw;
        show(t);
    }
    for (k = 0; k < 4; k++)
        if (off == esk_rf_off(t->alarm[k]))
            esk_rf_hw(t->armed, t->armed->value | (u64)1 << k);
    irqs(t, was, 1);
}

static void timer_reset(void *ctx)
{
    struct timer *t = (struct timer *)ctx;
    t->time = 0;
    t->lw = 0;
    t->latch = 0;
    show(t);
}

const struct esk_dev_ops esk_rp2040_timer_ops = {
    "rp2040-timer", timer_read, timer_write, timer_reset, 0, 0,
};

void *esk_rp2040_timer_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    static const char *const an[4] = { "ALARM0", "ALARM1", "ALARM2", "ALARM3" };
    struct timer *t = (struct timer *)esk_mach_alloc(s, sizeof *t);
    int k;
    if (!t)
        return 0;
    memset(t, 0, sizeof *t);
    t->s = s;
    t->p = p;
    if (!(t->timehw = esk_rf_reg(p, "TIMEHW")) || !(t->timelw = esk_rf_reg(p, "TIMELW")) ||
        !(t->timehr = esk_rf_reg(p, "TIMEHR")) || !(t->timelr = esk_rf_reg(p, "TIMELR")) ||
        !(t->armed = esk_rf_reg(p, "ARMED")) || !(t->rawh = esk_rf_reg(p, "TIMERAWH")) ||
        !(t->rawl = esk_rf_reg(p, "TIMERAWL")) || !(t->pause = esk_rf_reg(p, "PAUSE")) ||
        !(t->intr = esk_rf_reg(p, "INTR")) || !(t->inte = esk_rf_reg(p, "INTE")) ||
        !(t->intf = esk_rf_reg(p, "INTF")) || !(t->ints = esk_rf_reg(p, "INTS")))
        return 0;
    for (k = 0; k < 4; k++) {
        char nm[16];
        if (!(t->alarm[k] = esk_rf_reg(p, an[k])))
            return 0;
        (void)snprintf(nm, sizeof nm, "TIMER_IRQ_%d", k);
        t->irq[k] = esk_rf_irq(p, nm);
    }
    return t;
}
