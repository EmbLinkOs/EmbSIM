/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* tmr.c -- a PIC32MX's 16-bit timers (Family Reference Manual section 14),
 * over the register file: Timer1 (type A: TCKPS 1, 8, 64, 256) and Timer2
 * to Timer5 (type B: TCKPS 1 to 256 in eight steps; T32 pairs Timer2 with
 * Timer3 and Timer4 with Timer5 into a 32-bit timer).
 *
 *   - TMRx counts while TxCON.ON is set, one count per prescale clocks of
 *     PBCLK (SYSCLK / OSCCON.PBDIV);
 *   - when it has reached PRx it goes back to 0 on the next count and the
 *     timer's interrupt flag is set (TIMER_x); a TMRx above PRx counts on to
 *     0xFFFF and wraps, without the flag;
 *   - in 32-bit mode (T32 in the even timer's TxCON) the even timer's TMR
 *     and PR are the low half, the odd one's the high half, and the flag is
 *     the odd timer's; the odd timer's own TxCON is not used;
 *   - a write to TMRx or TxCON clears the prescaler's count; PRx is 0xFFFF
 *     out of reset.
 * TMRx is kept current as the core runs, so a program that polls it reads
 * the count. Not modelled: the external clock (TCS: the timer stops, as
 * with no clock on its pin), gated accumulation, Timer1's asynchronous
 * mode and its write-in-progress bit. */
#include <stdio.h>
#include <string.h>

#include "mcu/pic32/pic32.h"

struct tmr {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    int n; /* 1 to 5 */
    struct esk_rf_reg *con, *tmr, *pr;
    struct tmr *odd;  /* an even timer's partner, for T32 */
    struct tmr *even; /* an odd timer's */
    int irq, irq32;   /* TIMER_n, and the pair's (the odd one's) */
    u64 acc;          /* core cycles toward the next count */
    int running;
    u64 since; /* the cycle its clock started */
};

static const u32 psa[4] = { 1, 8, 64, 256 };
static const u32 psb[8] = { 1, 2, 4, 8, 16, 32, 64, 256 };

static int t32(const struct tmr *t) { return t->odd && (t->con->value >> 3 & 1u); }

/* an odd timer whose even partner has it in 32-bit mode */
static int slave(const struct tmr *t) { return t->even && t32(t->even); }

/* core cycles per count */
static u64 divisor(const struct tmr *t)
{
    u32 ck = (u32)(t->con->value >> 4);
    u32 ps = t->n == 1 ? psa[ck & 3u] : psb[ck & 7u];
    return (u64)ps * esk_pic32_pb_div(t->s);
}

static int on(const struct tmr *t)
{
    return (t->con->value >> 15 & 1u) && !(t->con->value >> 1 & 1u) && !slave(t);
}

static u64 count_of(const struct tmr *t)
{
    if (t32(t))
        return (t->tmr->value & 0xffffu) | (t->odd->tmr->value & 0xffffu) << 16;
    return t->tmr->value & 0xffffu;
}

static u64 period_of(const struct tmr *t)
{
    if (t32(t))
        return (t->pr->value & 0xffffu) | (t->odd->pr->value & 0xffffu) << 16;
    return t->pr->value & 0xffffu;
}

static void set_count(struct tmr *t, u64 c)
{
    t->tmr->value = (t->tmr->value & ~(u64)0xffff) | (c & 0xffffu);
    if (t32(t))
        t->odd->tmr->value = (t->odd->tmr->value & ~(u64)0xffff) | (c >> 16 & 0xffffu);
}

static void run_clock(struct tmr *t)
{
    int was = t->running;
    esk_rf_clock(t->p, &t->running, on(t));
    if (t->running && !was)
        t->since = t->s->cycles;
}

static void advance(struct tmr *t, u64 ticks)
{
    u64 c = count_of(t), pr = period_of(t), top = t32(t) ? 0xffffffffu : 0xffffu;
    int flag = 0;
    if (c > pr) {
        /* above PR: on to the top, and round to 0 */
        u64 room = top - c + 1;
        if (ticks < room) {
            set_count(t, c + ticks);
            return;
        }
        ticks -= room;
        c = 0;
    }
    /* from c (at most PR): every PR + 1 counts, a match and back to 0 */
    if (ticks >= pr - c + 1) {
        flag = 1;
        ticks -= pr - c + 1;
        c = ticks % (pr + 1);
    } else {
        c += ticks;
    }
    set_count(t, c);
    if (flag)
        esk_pic32_irq(t->s, t32(t) ? t->irq32 : t->irq);
}

static void tmr_tick(void *ctx, u32 cycles)
{
    struct tmr *t = (struct tmr *)ctx;
    struct esk_mach *s = t->s;
    u64 d = divisor(t), a;
    cycles = esk_mach_after(s, cycles, t->since); /* the instruction that started it */
    a = t->acc + cycles;
    if (a < d) {
        t->acc = a;
        return;
    }
    t->acc = a % d;
    advance(t, a / d);
}

/* when the flag next sets, while its interrupt is enabled */
static int tmr_next(void *ctx, u32 *cycles)
{
    struct tmr *t = (struct tmr *)ctx;
    u64 c, pr, top, ticks, cyc;
    if (!on(t) || !esk_pic32_irq_enabled(t->s, t32(t) ? t->irq32 : t->irq))
        return 0;
    c = count_of(t);
    pr = period_of(t);
    top = t32(t) ? 0xffffffffu : 0xffffu;
    ticks = c <= pr ? pr - c + 1 : top - c + 1 + pr + 1;
    cyc = ticks * divisor(t) - t->acc;
    *cycles = cyc > 0xffffffffu ? 0xffffffffu : (u32)cyc;
    return 1;
}

static u32 tmr_read(void *ctx, u32 off, int n)
{
    struct tmr *t = (struct tmr *)ctx;
    return esk_rf_read(t->p, off, n);
}

static void tmr_write(void *ctx, u32 off, int n, u32 v)
{
    struct tmr *t = (struct tmr *)ctx;
    esk_rf_write(t->p, off, n, v);
    if (off == esk_rf_off(t->tmr) || off == esk_rf_off(t->con))
        t->acc = 0;
    run_clock(t);
    if (t->odd)
        run_clock(t->odd);
}

static void tmr_reset(void *ctx)
{
    struct tmr *t = (struct tmr *)ctx;
    t->acc = 0;
    t->running = 0;
}

const struct esk_dev_ops esk_pic32_tmr_ops = {
    "pic32-tmr", tmr_read, tmr_write, tmr_reset, tmr_tick, tmr_next,
};

static struct tmr *timer_of(struct esk_mach *s, int n)
{
    char nm[8];
    struct esk_rf_periph *p;
    snprintf(nm, sizeof nm, "TMR%d", n);
    p = esk_rf_periph(s->rf, nm);
    return p && p->ops == &esk_pic32_tmr_ops ? (struct tmr *)p->ctx : 0;
}

void *esk_pic32_tmr_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    char nm[16];
    int n = 0;
    struct tmr *t, *o;
    if (sscanf(p->name, "TMR%d", &n) != 1 || n < 1 || n > 9)
        return 0;
    t = (struct tmr *)esk_mach_alloc(s, sizeof *t);
    if (!t)
        return 0;
    memset(t, 0, sizeof *t);
    t->s = s;
    t->p = p;
    t->n = n;
    snprintf(nm, sizeof nm, "T%dCON", n);
    t->con = esk_rf_reg(p, nm);
    snprintf(nm, sizeof nm, "TMR%d", n);
    t->tmr = esk_rf_reg(p, nm);
    snprintf(nm, sizeof nm, "PR%d", n);
    t->pr = esk_rf_reg(p, nm);
    if (!t->con || !t->tmr || !t->pr)
        return 0;
    snprintf(nm, sizeof nm, "TIMER_%d", n);
    t->irq = esk_rf_irq_named(s->rf, nm);
    esk_rf_set_reset(t->pr, 0xffff);
    /* pairs: the even timer and the odd one after it, whichever is made
     * second finding the first */
    o = n >= 2 ? timer_of(s, n & 1 ? n - 1 : n + 1) : 0;
    if (o) {
        struct tmr *ev = n & 1 ? o : t, *od = n & 1 ? t : o;
        ev->odd = od;
        od->even = ev;
        ev->irq32 = od->irq;
    }
    return t;
}
