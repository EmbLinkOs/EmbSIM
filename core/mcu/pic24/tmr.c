/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* tmr.c -- a PIC24FJ's Timer1 to Timer5, over its registers (TMRx, PRx,
 * TxCON, TMR3HLD, TMR5HLD: one peripheral, TMR).
 *
 *   - TMRx counts while TxCON.TON is set, one count per prescale (TCKPS:
 *     1, 8, 64, 256) instruction cycles;
 *   - when it has reached PRx it goes back to 0 on the next count and the
 *     timer's interrupt flag is set (Tx); a TMRx above PRx counts on to
 *     0xFFFF and wraps, without the flag;
 *   - T32 in T2CON (T4CON) pairs Timer2 and Timer3 (Timer4 and Timer5)
 *     into a 32-bit timer: TMR2 and PR2 the low half, TMR3 and PR3 the
 *     high, the flag Timer3's (Timer5's), T3CON (T5CON) not used. A read
 *     of TMR2 (TMR4) copies TMR3 (TMR5) to TMR3HLD (TMR5HLD), as the part
 *     latches it;
 *   - a write to TMRx or TxCON clears the prescaler's count; PRx is
 *     0xFFFF out of reset.
 * TMRx is kept current as the core runs. Not modelled: the external
 * clocks (TCS: the timer stops, with no clock on its pin), gated
 * accumulation, Timer1's asynchronous mode.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/pic24-tmr.c. */
#include <stdio.h>
#include <string.h>

#include "mcu/pic24/pic24.h"

#define NT 5

struct t {
    struct esk_rf_reg *con, *tmr, *pr, *hld;
    int irq;
    u32 acc; /* cycles toward the next count */
};

struct tmrs {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct t t[NT + 1]; /* 1 to 5 */
    int running;
};

static const u32 prescale[4] = { 1, 8, 64, 256 };

/* timer n (2 or 4) and n + 1 as one */
static int t32(const struct tmrs *m, int n)
{
    return (n == 2 || n == 4) && m->t[n + 1].con && (m->t[n].con->value >> 3 & 1u);
}

static int slave(const struct tmrs *m, int n)
{
    return (n == 3 || n == 5) && t32(m, n - 1);
}

static int on(const struct tmrs *m, int n)
{
    const struct t *t = &m->t[n];
    return t->con && (t->con->value >> 15 & 1u) && !(t->con->value >> 1 & 1u) && !slave(m, n);
}

static u32 divisor(const struct t *t)
{
    return prescale[t->con->value >> 4 & 3u];
}

static u64 count_of(const struct tmrs *m, int n)
{
    if (t32(m, n))
        return (m->t[n].tmr->value & 0xffffu) | (m->t[n + 1].tmr->value & 0xffffu) << 16;
    return m->t[n].tmr->value & 0xffffu;
}

static u64 period_of(const struct tmrs *m, int n)
{
    if (t32(m, n))
        return (m->t[n].pr->value & 0xffffu) | (m->t[n + 1].pr->value & 0xffffu) << 16;
    return m->t[n].pr->value & 0xffffu;
}

static void set_count(struct tmrs *m, int n, u64 c)
{
    m->t[n].tmr->value = c & 0xffffu;
    if (t32(m, n))
        m->t[n + 1].tmr->value = c >> 16 & 0xffffu;
}

static void advance(struct tmrs *m, int n, u64 ticks)
{
    u64 c = count_of(m, n), pr = period_of(m, n), to_match;
    u64 top = t32(m, n) ? 0xffffffffu : 0xffffu;
    int flag = 0;
    if (c > pr) {
        u64 room = top - c + 1;
        if (ticks < room) {
            set_count(m, n, c + ticks);
            return;
        }
        ticks -= room;
        c = 0;
    }
    to_match = pr - c + 1;
    if (ticks >= to_match) {
        flag = 1;
        ticks -= to_match;
        c = ticks % (pr + 1);
    } else {
        c += ticks;
    }
    set_count(m, n, c);
    if (flag)
        esk_pic24_irq(m->s, t32(m, n) ? m->t[n + 1].irq : m->t[n].irq);
}

static void run_clock(struct tmrs *m)
{
    int n, any = 0;
    for (n = 1; n <= NT; n++)
        any |= on(m, n);
    esk_rf_clock(m->p, &m->running, any);
}

static void tmr_tick(void *ctx, u32 cycles)
{
    struct tmrs *m = (struct tmrs *)ctx;
    int n;
    for (n = 1; n <= NT; n++) {
        struct t *t = &m->t[n];
        u32 d;
        u64 a;
        if (!on(m, n))
            continue;
        d = divisor(t);
        a = (u64)t->acc + cycles;
        t->acc = (u32)(a % d);
        if (a >= d)
            advance(m, n, a / d);
    }
}

/* when a flag whose interrupt is enabled next sets */
static int tmr_next(void *ctx, u32 *cycles)
{
    struct tmrs *m = (struct tmrs *)ctx;
    u64 best = ~(u64)0;
    int n;
    for (n = 1; n <= NT; n++) {
        int irq;
        u64 c, pr, top, ticks, cyc;
        if (!on(m, n))
            continue;
        irq = t32(m, n) ? m->t[n + 1].irq : m->t[n].irq;
        if (!esk_pic24_irq_enabled(m->s, irq))
            continue;
        c = count_of(m, n);
        pr = period_of(m, n);
        top = t32(m, n) ? 0xffffffffu : 0xffffu;
        ticks = c <= pr ? pr - c + 1 : top - c + 1 + pr + 1;
        cyc = ticks * divisor(&m->t[n]) - m->t[n].acc;
        if (cyc < best)
            best = cyc;
    }
    if (best == ~(u64)0)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static u32 tmr_read(void *ctx, u32 off, int n)
{
    struct tmrs *m = (struct tmrs *)ctx;
    int k;
    for (k = 2; k <= 4; k += 2)
        if (m->t[k].tmr && off == esk_rf_off(m->t[k].tmr) && m->t[k + 1].hld && m->t[k + 1].tmr &&
            !m->s->bus.debug)
            m->t[k + 1].hld->value = m->t[k + 1].tmr->value;
    return esk_rf_read(m->p, off, n);
}

static void tmr_write(void *ctx, u32 off, int n, u32 v)
{
    struct tmrs *m = (struct tmrs *)ctx;
    int k;
    esk_rf_write(m->p, off, n, v);
    for (k = 1; k <= NT; k++) {
        struct t *t = &m->t[k];
        if (t->con && (off == esk_rf_off(t->tmr) || off == esk_rf_off(t->con)))
            t->acc = 0;
    }
    run_clock(m);
}

static void tmr_reset(void *ctx)
{
    struct tmrs *m = (struct tmrs *)ctx;
    int n;
    for (n = 1; n <= NT; n++)
        m->t[n].acc = 0;
    m->running = 0;
}

const struct esk_dev_ops esk_pic24_tmr_ops = {
    "pic24-tmr", tmr_read, tmr_write, tmr_reset, tmr_tick, tmr_next,
};

void *esk_pic24_tmr_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct tmrs *m;
    char nm[16];
    int n, any = 0;
    if (!esk_pic24_is(s->cpu))
        return 0;
    m = (struct tmrs *)esk_mach_alloc(s, sizeof *m);
    if (!m)
        return 0;
    memset(m, 0, sizeof *m);
    m->s = s;
    m->p = p;
    for (n = 1; n <= NT; n++) {
        struct t *t = &m->t[n];
        snprintf(nm, sizeof nm, "T%dCON", n);
        t->con = esk_rf_reg(p, nm);
        snprintf(nm, sizeof nm, "TMR%d", n);
        t->tmr = esk_rf_reg(p, nm);
        snprintf(nm, sizeof nm, "PR%d", n);
        t->pr = esk_rf_reg(p, nm);
        snprintf(nm, sizeof nm, "TMR%dHLD", n);
        t->hld = esk_rf_reg(p, nm);
        if (!t->con || !t->tmr || !t->pr) {
            memset(t, 0, sizeof *t);
            continue;
        }
        any = 1;
        snprintf(nm, sizeof nm, "T%d", n);
        t->irq = esk_rf_irq_named(s->rf, nm);
    }
    return any ? m : 0;
}
