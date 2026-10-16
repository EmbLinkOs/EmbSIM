/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* pwm.c -- the RP2040's PWM (its datasheet, 4.5), over the register file:
 * eight slices of two channels on the user pins.
 *
 *   - a slice counts while CSR.EN (or EN's bit for it) is set, once per
 *     DIV clocks of clk_sys: INT + FRAC / 16, INT 0 meaning 256, the
 *     fraction spread over the counts as the divider does;
 *   - CTR counts up to TOP and wraps to 0; with PH_CORRECT it counts up to
 *     TOP and back down to 0. A wrap (at TOP, or at 0 going down) sets the
 *     slice's INTR bit, and INTS ((INTR | INTF) & INTE) raises
 *     PWM_IRQ_WRAP;
 *   - channel A's output is high while CTR < CC.A, B's while CTR < CC.B,
 *     inverted by A_INV and B_INV; CC and TOP are double-buffered while the
 *     slice runs, taking a write at the next wrap, and at once while it is
 *     stopped;
 *   - a pin whose FUNCSEL is PWM (4) is its slice's (pin / 2 % 8) channel
 *     (A on the even pins, B on the odd), through io.c's overrides;
 *   - CTR may be written; an output changes at the instruction that brings
 *     the counter to its edge, and a sleeping core's devices wake at it.
 *
 * Not modelled: DIVMODE's gated and edge-counting modes (B as an input),
 * PH_ADV and PH_RET, DMA requests. */
#include <stdio.h>
#include <string.h>

#include "mcu/rp2040/rp2040.h"

#define NSLICE 8
#define CSR_EN 0x1u
#define CSR_PH_CORRECT 0x2u
#define CSR_A_INV 0x4u
#define CSR_B_INV 0x8u

struct slice {
    struct esk_rf_reg *csr, *div, *ctr, *cc, *top;
    u32 cc_now, top_now; /* the buffered values in use */
    int down;            /* phase-correct, counting down */
    u32 acc;             /* sixteenths of a clock toward the next count */
    int out[2];          /* each channel's output */
};

struct pwm {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct slice sl[NSLICE];
    struct esk_rf_reg *en, *intr, *inte, *intf, *ints;
    int irq, running;
};

static struct pwm *pwm_of(struct esk_mach *s)
{
    return (struct pwm *)esk_rp_model(s, "PWM", &esk_rp2040_pwm_ops);
}

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static int on(const struct slice *c) { return (reg(c->csr) & CSR_EN) != 0; }

/* a count's length in sixteenths of a clk_sys cycle */
static u32 div16(const struct slice *c)
{
    u32 d = reg(c->div) & 0xfffu, i = d >> 4;
    return (i ? i : 256u) * 16u + (d & 15u);
}

static void irq(struct pwm *w)
{
    u32 v = (reg(w->intr) | reg(w->intf)) & reg(w->inte) & 0xffu;
    int was = reg(w->ints) != 0;
    esk_rf_hw(w->ints, v);
    if (v && !was)
        esk_rf_interrupt(w->p, w->irq);
}

static void latch(struct slice *c)
{
    c->cc_now = reg(c->cc);
    c->top_now = reg(c->top) & 0xffffu;
}

static int levels(struct slice *c)
{
    u32 ctr = reg(c->ctr) & 0xffffu, csr = reg(c->csr);
    int a = ctr < (c->cc_now & 0xffffu), b = ctr < (c->cc_now >> 16);
    int changed;
    if (csr & CSR_A_INV)
        a = !a;
    if (csr & CSR_B_INV)
        b = !b;
    changed = a != c->out[0] || b != c->out[1];
    c->out[0] = a;
    c->out[1] = b;
    return changed;
}

/* n counts of slice i */
static void count(struct pwm *w, int i, u32 n)
{
    struct slice *c = &w->sl[i];
    u32 ctr = reg(c->ctr) & 0xffffu;
    int pc = (reg(c->csr) & CSR_PH_CORRECT) != 0;
    while (n) {
        u32 room;
        if (!pc || !c->down) {
            room = ctr < c->top_now ? c->top_now - ctr : 0;
            if (n <= room) {
                ctr += n;
                break;
            }
            n -= room + 1;
            if (pc) {
                ctr = c->top_now ? c->top_now - 1 : 0;
                c->down = 1;
                if (!c->top_now) {
                    ctr = 0;
                    c->down = 0;
                    esk_rf_hw(w->intr, reg(w->intr) | 1u << i);
                    latch(c);
                }
            } else {
                ctr = 0; /* the wrap */
                esk_rf_hw(w->intr, reg(w->intr) | 1u << i);
                latch(c);
            }
        } else {
            room = ctr;
            if (n < room) {
                ctr -= n;
                break;
            }
            n -= room;
            ctr = 0; /* the wrap, at the bottom */
            c->down = 0;
            esk_rf_hw(w->intr, reg(w->intr) | 1u << i);
            latch(c);
            if (n) {
                n--;
                ctr = c->top_now ? 1 : 0;
                if (!c->top_now)
                    continue;
            }
        }
    }
    esk_rf_hw(c->ctr, ctr);
}

/* counts until slice i's next edge or wrap */
static u32 counts_to_next(const struct slice *c)
{
    u32 ctr = reg(c->ctr) & 0xffffu, best = 0, k;
    int pc = (reg(c->csr) & CSR_PH_CORRECT) != 0;
    u32 marks[2] = { c->cc_now & 0xffffu, c->cc_now >> 16 };
    if (!pc || !c->down) {
        best = (c->top_now >= ctr ? c->top_now - ctr : 0) + (pc ? 0 : 1); /* to the wrap */
        if (!best)
            best = 1;
        for (k = 0; k < 2; k++)
            if (marks[k] > ctr && marks[k] - ctr < best)
                best = marks[k] - ctr;
    } else {
        best = ctr ? ctr : 1;
        for (k = 0; k < 2; k++)
            if (marks[k] < ctr && ctr - marks[k] + 1 < best)
                best = ctr - marks[k] + 1;
    }
    return best;
}

static void run_clock(struct pwm *w)
{
    int i, any = 0;
    for (i = 0; i < NSLICE; i++)
        any |= on(&w->sl[i]);
    esk_rf_clock(w->p, &w->running, any);
}

static void pwm_tick(void *ctx, u32 cycles)
{
    struct pwm *w = (struct pwm *)ctx;
    int i, changed = 0;
    for (i = 0; i < NSLICE; i++) {
        struct slice *c = &w->sl[i];
        u64 all;
        u32 d, n;
        if (!on(c))
            continue;
        d = div16(c);
        all = (u64)c->acc + (u64)cycles * 16u;
        n = (u32)(all / d);
        c->acc = (u32)(all % d);
        if (n) {
            count(w, i, n);
            changed |= levels(c);
        }
    }
    if (reg(w->intr))
        irq(w);
    if (changed)
        esk_rp2040_pins_changed(w->s);
}

static int pwm_next(void *ctx, u32 *cycles)
{
    struct pwm *w = (struct pwm *)ctx;
    u64 best = 0;
    int i;
    for (i = 0; i < NSLICE; i++) {
        const struct slice *c = &w->sl[i];
        u64 need;
        if (!on(c))
            continue;
        need = ((u64)counts_to_next(c) * div16(c) - c->acc + 15u) / 16u;
        if (!need)
            need = 1;
        if (!best || need < best)
            best = need;
    }
    if (!best)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static u32 pwm_read(void *ctx, u32 off, int n)
{
    return esk_rf_read(((struct pwm *)ctx)->p, off, n);
}

static void pwm_write(void *ctx, u32 off, int n, u32 v)
{
    struct pwm *w = (struct pwm *)ctx;
    int i, changed = 0;
    if (off == esk_rf_off(w->en)) {
        /* EN is each CSR's EN, together */
        esk_rf_write(w->p, off, n, v);
        for (i = 0; i < NSLICE; i++)
            esk_rf_hw(w->sl[i].csr, (reg(w->sl[i].csr) & ~CSR_EN) | (v >> i & 1u));
    } else {
        esk_rf_write(w->p, off, n, v);
    }
    for (i = 0; i < NSLICE; i++) {
        struct slice *c = &w->sl[i];
        if (!on(c) || off == esk_rf_off(c->csr))
            latch(c); /* stopped, or just started: CC and TOP at once */
        if (off == esk_rf_off(c->ctr))
            c->acc = 0;
        changed |= levels(c);
    }
    {
        u32 en = 0;
        for (i = 0; i < NSLICE; i++)
            en |= (u32)on(&w->sl[i]) << i;
        esk_rf_hw(w->en, en);
    }
    irq(w);
    run_clock(w);
    if (changed)
        esk_rp2040_pins_changed(w->s);
}

static void pwm_reset(void *ctx)
{
    struct pwm *w = (struct pwm *)ctx;
    int i;
    w->running = 0;
    for (i = 0; i < NSLICE; i++) {
        struct slice *c = &w->sl[i];
        latch(c);
        c->down = 0;
        c->acc = 0;
        c->out[0] = c->out[1] = 0;
        levels(c);
    }
}

int esk_rp2040_pwm_out(struct esk_mach *s, int pin)
{
    struct pwm *w = pwm_of(s);
    if (!w || pin < 0 || pin > 29)
        return 0;
    return w->sl[(pin >> 1) & 7].out[pin & 1];
}

const struct esk_dev_ops esk_rp2040_pwm_ops = {
    "rp2040-pwm", pwm_read, pwm_write, pwm_reset, pwm_tick, pwm_next,
};

void *esk_rp2040_pwm_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct pwm *w = (struct pwm *)esk_mach_alloc(s, sizeof *w);
    int i;
    if (!w)
        return 0;
    memset(w, 0, sizeof *w);
    w->s = s;
    w->p = p;
    for (i = 0; i < NSLICE; i++) {
        char n[16];
        struct slice *c = &w->sl[i];
#define REG(f, name)                                                                                       \
        snprintf(n, sizeof n, "CH%d_" name, i);                                                            \
        c->f = esk_rf_reg(p, n);                                                                          \
        if (!c->f)                                                                                         \
            return 0;
        REG(csr, "CSR")
        REG(div, "DIV")
        REG(ctr, "CTR")
        REG(cc, "CC")
        REG(top, "TOP")
#undef REG
    }
    w->en = esk_rf_reg(p, "EN");
    w->intr = esk_rf_reg(p, "INTR");
    w->inte = esk_rf_reg(p, "INTE");
    w->intf = esk_rf_reg(p, "INTF");
    w->ints = esk_rf_reg(p, "INTS");
    if (!w->en || !w->intr || !w->inte || !w->intf || !w->ints)
        return 0;
    w->irq = esk_rf_irq(p, "PWM");
    return w;
}
