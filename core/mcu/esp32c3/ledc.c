/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ledc.c -- the ESP32-C3's LED PWM controller (LEDC, TRM v1.4 chapter 32):
 * four timers and six PWM generators, ledc_ls_sig_out0-5 (signals 45-50 of
 * the GPIO matrix).
 *
 *   - The timers count LEDC_CLKx (CONF's APB_CLK_SEL: 1 APB_CLK, 2
 *     RC_FAST_CLK, 3 XTAL_CLK) divided by CLK_DIV / 256 (10 integer and 8
 *     fractional bits), from 0 to 2^DUTY_RES - 1, then overflow
 *     (LSTIMERx_OVF); RST holds a timer at 0, PAUSE stops it. New CLK_DIV and
 *     DUTY_RES take effect at the overflow after PARA_UP (it clears itself).
 *     TIMERx_VALUE is the count.
 *   - A generator (TIMER_SEL's timer) goes high at its HPOINT count and low
 *     at HPOINT + DUTY[18:4] (never, past the counter's top: 100 %; at once,
 *     when DUTY is 0: 0 %); DUTY[3:0] of every 16 periods last one count
 *     longer. With SIG_OUT_EN 0 it outputs IDLE_LV. TIMER_SEL, HPOINT,
 *     DUTY and SIG_OUT_EN take effect at the timer's overflow after the
 *     channel's PARA_UP.
 *   - Fading: PARA_UP with DUTY_START set starts from DUTY at once; every
 *     DUTY_CYCLE overflows the duty steps DUTY_SCALE up (DUTY_INC) or down,
 *     DUTY_NUM times, then DUTY_CHNG_END_CHn. DUTY_R is the duty in use.
 *     ESP-IDF's ledc_set_duty is such a fade of one step of 0.
 *   - OVF_CNT_EN counts the timer's overflows; the (OVF_NUM + 1)th raises
 *     OVF_CNT_CHn (OVF_CNT_RESET restarts it).
 *   - INT_ST is INT_RAW and INT_ENA, raising matrix source 23; INT_CLR
 *     clears.
 * Where it differs inside: a fractional divider's counts are all of the
 * average length (the part alternates A and A + 1 LEDC_CLKx cycles); the
 * dithered periods are spread by an accumulator (the TRM says only that
 * they are spread). Not yet: light-sleep, the clock gate's effect on a
 * running timer, a CLK_DIV below 1 (the timer then stops). */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define SOURCE 23
#define NCH 6
#define NTM 4
#define SIG_OUT0 45
#define RC_FAST_HZ 17500000u
#define XTAL_HZ 40000000u
#define FS_PER_S 1000000000000000ull

#define T_PAUSE (1u << 22)
#define T_RST (1u << 23)
#define T_PARA_UP (1u << 25)
#define C_SIG_OUT_EN (1u << 2)
#define C_IDLE_LV (1u << 3)
#define C_PARA_UP (1u << 4)
#define C_OVF_CNT_EN (1u << 15)
#define C_OVF_CNT_RESET (1u << 16)
#define F_INC (1u << 30)
#define F_START (1u << 31)

struct tmr {
    struct esk_rf_reg *conf, *value;
    u32 res, div;   /* in use: DUTY_RES, CLK_DIV */
    int update;     /* PARA_UP: the new ones at the next overflow */
    u32 cnt;        /* the count */
    u64 pos;        /* fs into the period */
};

struct chan {
    struct esk_rf_reg *conf0, *hpoint, *duty, *conf1, *duty_r;
    int update;      /* PARA_UP: the new settings at the next overflow */
    u32 sel, hp, en, idle; /* in use */
    u32 duty_cur;    /* the duty (19 bits, 4 of them fractions) */
    u32 lp;          /* this period's low point */
    u32 dither;      /* the fractions' accumulator */
    int fading;
    u32 cyc, steps, ovf;
    int out;
};

struct c3_ledc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *conf, *raw, *st, *ena;
    struct tmr t[NTM];
    struct chan c[NCH];
    int clocked, level;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void update(struct c3_ledc *k)
{
    u32 st = reg(k->raw) & reg(k->ena) & 0xffffu;
    int level = st != 0;
    esk_rf_hw(k->st, st);
    if (level != k->level) {
        k->level = level;
        esk_esp32c3_source(k->s, SOURCE, level);
    }
}

static void raise(struct c3_ledc *k, u32 bits)
{
    esk_rf_hw(k->raw, reg(k->raw) | bits);
    update(k);
}

/* a count's length in fs, or 0 while the timer does not count */
static double tick_fs(const struct c3_ledc *k, const struct tmr *t)
{
    u32 sel = reg(k->conf) & 3u, hz, c = reg(t->conf);
    if ((c & (T_RST | T_PAUSE)) || t->div < 256u)
        return 0;
    hz = sel == 1 ? esk_esp32c3_apb_hz(k->s) : sel == 2 ? RC_FAST_HZ : sel == 3 ? XTAL_HZ : 0;
    if (!hz)
        return 0;
    return (double)t->div / 256.0 * (double)FS_PER_S / (double)hz;
}

static u32 top(const struct tmr *t) { return (1u << (t->res > 14u ? 14u : t->res)) - 1u; }

static u64 fs_per_cycle(const struct c3_ledc *k)
{
    u32 hz = esk_mach_core_hz(k->s);
    return hz ? FS_PER_S / hz : 0;
}

static void drive(struct c3_ledc *k, int n, int out)
{
    struct chan *c = &k->c[n];
    if (out != c->out) {
        c->out = out;
        esk_esp32c3_signal(k->s, SIG_OUT0 + n, out, 1);
    }
}

/* the channel's settings as written, in use from now */
static void chan_apply(struct c3_ledc *k, int n)
{
    struct chan *c = &k->c[n];
    u32 c0 = reg(c->conf0);
    c->sel = c0 & 3u;
    c->hp = reg(c->hpoint) & 0x3fffu;
    c->en = (c0 & C_SIG_OUT_EN) != 0;
    c->idle = (c0 & C_IDLE_LV) != 0;
    c->update = 0;
    if (!c->en)
        drive(k, n, (int)c->idle);
}

/* the low point of the period starting: HPOINT + the duty, a count longer
 * in DUTY[3:0] periods of 16 */
static void period_start(struct c3_ledc *k, int n)
{
    struct chan *c = &k->c[n];
    u32 frac = c->duty_cur & 0xfu, d = c->duty_cur >> 4;
    c->dither += frac;
    if (c->dither >= 16u) {
        c->dither -= 16u;
        d++;
    }
    c->lp = c->hp + d;
    esk_rf_hw(c->duty_r, c->duty_cur);
}

/* the timer overflowed: what waits for it */
static void overflow(struct c3_ledc *k, int x)
{
    struct tmr *t = &k->t[x];
    int n;
    if (t->update) {
        t->res = reg(t->conf) & 0xfu;
        t->div = (reg(t->conf) >> 4) & 0x3ffffu;
        t->update = 0;
    }
    raise(k, 1u << x);
    for (n = 0; n < NCH; n++) {
        struct chan *c = &k->c[n];
        u32 c1;
        if (c->sel != (u32)x)
            continue;
        if (c->update)
            chan_apply(k, n);
        c1 = reg(c->conf1);
        if (c->fading) {
            u32 every = (c1 >> 10) & 0x3ffu;
            if (++c->cyc >= (every ? every : 1u)) {
                u32 num = (c1 >> 20) & 0x3ffu, scale = (c1 & 0x3ffu) << 4;
                c->cyc = 0;
                if (c->steps < num) {
                    if (c1 & F_INC)
                        c->duty_cur = c->duty_cur + scale > 0x7ffffu ? 0x7ffffu : c->duty_cur + scale;
                    else
                        c->duty_cur = c->duty_cur > scale ? c->duty_cur - scale : 0;
                    c->steps++;
                }
                if (c->steps >= num) {
                    c->fading = 0;
                    raise(k, 1u << (4 + n));
                }
            }
        }
        if (reg(c->conf0) & C_OVF_CNT_EN) {
            if (++c->ovf >= ((reg(c->conf0) >> 5) & 0x3ffu) + 1u) {
                c->ovf = 0;
                raise(k, 1u << (10 + n));
            }
        }
        if (c->sel == (u32)x)
            period_start(k, n);
    }
}

/* the timer entered count `cnt`: the outputs that switch there */
static void at_count(struct c3_ledc *k, int x)
{
    struct tmr *t = &k->t[x];
    int n;
    for (n = 0; n < NCH; n++) {
        struct chan *c = &k->c[n];
        if (c->sel != (u32)x || !c->en)
            continue;
        if (t->cnt == c->hp && c->lp != c->hp)
            drive(k, n, 1);
        if (t->cnt == c->lp)
            drive(k, n, 0);
    }
}

/* the next count after the present one where something happens: an output
 * switching or the overflow (top + 1) */
static u32 next_count(const struct c3_ledc *k, int x)
{
    const struct tmr *t = &k->t[x];
    u32 best = top(t) + 1u;
    int n;
    for (n = 0; n < NCH; n++) {
        const struct chan *c = &k->c[n];
        if (c->sel != (u32)x || !c->en)
            continue;
        if (c->hp > t->cnt && c->hp < best)
            best = c->hp;
        if (c->lp > t->cnt && c->lp < best)
            best = c->lp;
    }
    return best;
}

/* fs from the present to the timer's next event, or 0 when it stands */
static u64 due(const struct c3_ledc *k, int x)
{
    const struct tmr *t = &k->t[x];
    double tf = tick_fs(k, t);
    u64 at;
    if (tf <= 0)
        return 0;
    at = (u64)((double)next_count(k, x) * tf + 0.5);
    return at > t->pos ? at - t->pos : 1u;
}

static void clocking(struct c3_ledc *k)
{
    int x, on = 0;
    for (x = 0; x < NTM; x++)
        if (tick_fs(k, &k->t[x]) > 0)
            on = 1;
    esk_rf_clock(k->p, &k->clocked, on);
}

static void advance(struct c3_ledc *k, u64 budget)
{
    struct esk_mach *s = k->s;
    u64 fpc = fs_per_cycle(k);
    int x;
    if (!fpc)
        return;
    for (;;) {
        u64 d = 0;
        int which = -1;
        for (x = 0; x < NTM; x++) {
            u64 dx = due(k, x);
            if (dx && (which < 0 || dx < d)) {
                d = dx;
                which = x;
            }
        }
        if (which < 0 || d > budget) {
            for (x = 0; x < NTM; x++) {
                struct tmr *t = &k->t[x];
                double tf = tick_fs(k, t);
                if (tf <= 0)
                    continue;
                t->pos += budget;
                t->cnt = (u32)((double)t->pos / tf);
                if (t->cnt > top(t))
                    t->cnt = top(t);
            }
            break;
        }
        budget -= d;
        s->output_lag = (u32)(budget / fpc);
        for (x = 0; x < NTM; x++) {
            struct tmr *t = &k->t[x];
            double tf = tick_fs(k, t);
            if (tf <= 0)
                continue;
            if (x == which) {
                u32 c = next_count(k, x);
                t->pos += d;
                if (c > top(t)) {
                    t->pos = 0;
                    t->cnt = 0;
                    overflow(k, x);
                } else {
                    t->cnt = c;
                }
                at_count(k, x);
            } else {
                t->pos += d;
                t->cnt = (u32)((double)t->pos / tf);
            }
        }
        s->output_lag = 0;
    }
    for (x = 0; x < NTM; x++)
        esk_rf_hw(k->t[x].value, k->t[x].cnt & 0x3fffu);
}

static void ledc_tick(void *ctx, u32 cycles)
{
    struct c3_ledc *k = (struct c3_ledc *)ctx;
    advance(k, (u64)cycles * fs_per_cycle(k));
}

static int ledc_next(void *ctx, u32 *cycles)
{
    struct c3_ledc *k = (struct c3_ledc *)ctx;
    u64 fpc = fs_per_cycle(k), best = 0;
    int x, any = 0;
    if (!fpc)
        return 0;
    for (x = 0; x < NTM; x++) {
        u64 d = due(k, x), c;
        if (!d)
            continue;
        c = (d + fpc - 1u) / fpc;
        if (!any || c < best)
            best = c;
        any = 1;
    }
    if (!any)
        return 0;
    *cycles = best == 0 ? 1u : best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static u32 ledc_read(void *ctx, u32 off, int n)
{
    struct c3_ledc *k = (struct c3_ledc *)ctx;
    return esk_rf_read(k->p, off, n);
}

/* the timer starts counting (out of RST): its generators' first period */
static void timer_starts(struct c3_ledc *k, int x)
{
    int n;
    for (n = 0; n < NCH; n++)
        if (k->c[n].sel == (u32)x)
            period_start(k, n);
    at_count(k, x);
}

static void ledc_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_ledc *k = (struct c3_ledc *)ctx;
    u32 w = off & ~3u, held[NTM];
    int i;
    for (i = 0; i < NTM; i++)
        held[i] = reg(k->t[i].conf) & T_RST;
    if (w == 0xCCu) { /* INT_CLR */
        esk_rf_hw(k->raw, reg(k->raw) & ~v);
        update(k);
        return;
    }
    if (w == 0xC4u)
        return; /* INT_ST */
    esk_rf_write(k->p, off, n, v);
    if (w == 0xC0u || w == 0xC8u)
        update(k);
    for (i = 0; i < NTM; i++) {
        struct tmr *t = &k->t[i];
        if (w != esk_rf_off(t->conf))
            continue;
        if (reg(t->conf) & T_RST) {
            t->cnt = 0;
            t->pos = 0;
        }
        if (v & T_PARA_UP)
            t->update = 1;
        if (!t->div && !t->res) /* never set: the first settings at once */
            t->update = 1;
        if (t->update && (reg(t->conf) & T_RST)) { /* held: nothing to wait for */
            t->res = reg(t->conf) & 0xfu;
            t->div = (reg(t->conf) >> 4) & 0x3ffffu;
            t->update = 0;
        }
        if (held[i] && !(reg(t->conf) & T_RST))
            timer_starts(k, i);
    }
    for (i = 0; i < NCH; i++) {
        struct chan *c = &k->c[i];
        if (w != esk_rf_off(c->conf0))
            continue;
        if (v & C_OVF_CNT_RESET)
            c->ovf = 0;
        if (v & C_PARA_UP) {
            u32 c1 = reg(c->conf1);
            c->update = 1;
            c->duty_cur = reg(c->duty) & 0x7ffffu; /* the fade's start, at once */
            c->fading = (c1 & F_START) != 0;
            c->cyc = 0;
            c->steps = 0;
            esk_rf_hw(c->duty_r, c->duty_cur);
            if (tick_fs(k, &k->t[c->sel]) <= 0) { /* no overflow coming: now */
                chan_apply(k, i);
                period_start(k, i);
            }
        }
    }
    clocking(k);
}

static void ledc_reset(void *ctx)
{
    struct c3_ledc *k = (struct c3_ledc *)ctx;
    int i;
    for (i = 0; i < NTM; i++) {
        struct tmr *t = &k->t[i];
        t->res = reg(t->conf) & 0xfu;
        t->div = (reg(t->conf) >> 4) & 0x3ffffu;
        t->update = 0;
        t->cnt = 0;
        t->pos = 0;
    }
    for (i = 0; i < NCH; i++) {
        struct chan *c = &k->c[i];
        c->update = 0;
        c->duty_cur = 0;
        c->lp = 0;
        c->dither = 0;
        c->fading = 0;
        c->cyc = c->steps = c->ovf = 0;
        c->out = -1;
        chan_apply(k, i);
        drive(k, i, (int)c->idle);
    }
    k->level = 0;
    esk_esp32c3_source(k->s, SOURCE, 0);
    k->clocked = 0;
    update(k);
    clocking(k);
}

const struct esk_dev_ops esk_esp32c3_ledc_ops = {
    "esp32c3-ledc", ledc_read, ledc_write, ledc_reset, ledc_tick, ledc_next,
};

void *esk_esp32c3_ledc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_ledc *k = (struct c3_ledc *)esk_mach_alloc(s, sizeof *k);
    char name[24];
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->conf = esk_rf_reg(p, "CONF");
    k->raw = esk_rf_reg(p, "INT_RAW");
    k->st = esk_rf_reg(p, "INT_ST");
    k->ena = esk_rf_reg(p, "INT_ENA");
    if (!k->conf || !k->raw || !k->st || !k->ena)
        return 0;
    for (i = 0; i < NTM; i++) {
        snprintf(name, sizeof name, "TIMER%d_CONF", i);
        k->t[i].conf = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "TIMER%d_VALUE", i);
        k->t[i].value = esk_rf_reg(p, name);
        if (!k->t[i].conf || !k->t[i].value)
            return 0;
    }
    for (i = 0; i < NCH; i++) {
        struct chan *c = &k->c[i];
        snprintf(name, sizeof name, "CH%d_CONF0", i);
        c->conf0 = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "CH%d_HPOINT", i);
        c->hpoint = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "CH%d_DUTY", i);
        c->duty = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "CH%d_CONF1", i);
        c->conf1 = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "CH%d_DUTY_R", i);
        c->duty_r = esk_rf_reg(p, name);
        if (!c->conf0 || !c->hpoint || !c->duty || !c->conf1 || !c->duty_r)
            return 0;
    }
    return k;
}
