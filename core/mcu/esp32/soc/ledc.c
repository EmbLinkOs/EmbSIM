/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ledc.c -- the ESP32's LED PWM controller (LEDC, TRM v5 chapter 14):
 * eight high-speed and eight low-speed channels on four timers each,
 * ledc_hs_sig_out0-7 and ledc_ls_sig_out0-7 (signals 71-86 of the GPIO
 * matrix). The ESP32-C3's model (mcu/esp32c3/ledc.c) on the ESP32's
 * registers.
 *
 *   - A timer counts its clock -- REF_TICK (1 MHz) with TICK_SEL 0; with
 *     TICK_SEL 1 APB_CLK for the high-speed timers, SLOW_CLK (APB_CLK with
 *     CONF's APB_CLK_SEL, else the 8 MHz RC_FAST_CLK) for the low-speed ones
 *     -- divided by DIV_NUM / 256 (10 integer and 8 fractional bits), from
 *     0 to 2^DUTY_RES - 1 (up to 20 bits), then overflows; RST holds it at
 *     0, PAUSE stops it. A high-speed timer's new DIV_NUM and DUTY_RES
 *     take effect at its next overflow, a low-speed one's at the overflow
 *     after PARA_UP.
 *   - A channel (TIMER_SEL's timer of its group) goes high at its HPOINT
 *     count and low at HPOINT + DUTY[24:4]; DUTY[3:0] of every 16 periods
 *     last one count longer; with SIG_OUT_EN 0 it outputs IDLE_LV. Its
 *     settings take effect at the timer's overflow after DUTY_START (a
 *     high-speed channel) or the PARA_UP that follows it (a low-speed one).
 *   - Fading: from DUTY, every DUTY_CYCLE overflows the duty steps
 *     DUTY_SCALE up (DUTY_INC) or down, DUTY_NUM times, then
 *     DUTY_CHNG_END. DUTY_R is the duty in use.
 *   - INT_ST is INT_RAW and INT_ENA (the timers' overflows, bits 0-7; the
 *     channels' fade ends, 8-23), raising matrix source 43; INT_CLR clears.
 * Where it differs inside: a fractional divider's counts are all of the
 * average length; the dithered periods are spread by an accumulator.
 * Not yet: light sleep, the clock gate's effect on a running timer. */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32/soc/esp32.h"

#define NCH 16 /* HSCH0-7, LSCH0-7 */
#define NTM 8  /* HSTIMER0-3, LSTIMER0-3 */
#define SIG_OUT0 71 /* ledc_hs_sig_out0; ledc_ls_sig_out0 is 79 */
#define RC_FAST_HZ 8000000u
#define REF_TICK_HZ 1000000u
#define FS_PER_S 1000000000000000ull

#define T_PAUSE (1u << 23)
#define T_RST (1u << 24)
#define T_TICK_SEL (1u << 25)
#define T_PARA_UP (1u << 26)
#define C_SIG_OUT_EN (1u << 2)
#define C_IDLE_LV (1u << 3)
#define C_PARA_UP (1u << 4)
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

struct e32_ledc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *conf, *raw, *st, *ena;
    struct tmr t[NTM];
    struct chan c[NCH];
    int clocked, level, source;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void update(struct e32_ledc *k)
{
    u32 st = reg(k->raw) & reg(k->ena) & 0xffffffu;
    int level = st != 0;
    esk_rf_hw(k->st, st);
    if (level != k->level) {
        k->level = level;
        esk_esp32_source(k->s, k->source, level);
    }
}

static void raise(struct e32_ledc *k, u32 bits)
{
    esk_rf_hw(k->raw, reg(k->raw) | bits);
    update(k);
}

/* a count's length in fs, or 0 while the timer does not count */
static double tick_fs(const struct e32_ledc *k, const struct tmr *t)
{
    u32 hz, c = reg(t->conf), x = (u32)(t - k->t);
    if ((c & (T_RST | T_PAUSE)) || t->div < 256u)
        return 0;
    if (!(c & T_TICK_SEL))
        hz = REF_TICK_HZ;
    else if (x < 4u || (reg(k->conf) & 1u))
        hz = esk_esp32_apb_hz(k->s); /* high speed: APB_CLK; low speed: SLOW_CLK */
    else
        hz = RC_FAST_HZ;
    if (!hz)
        return 0;
    return (double)t->div / 256.0 * (double)FS_PER_S / (double)hz;
}

static u32 top(const struct tmr *t) { return (1u << (t->res > 20u ? 20u : t->res)) - 1u; }

static u64 fs_per_cycle(const struct e32_ledc *k)
{
    u32 hz = esk_mach_core_hz(k->s);
    return hz ? FS_PER_S / hz : 0;
}

static void drive(struct e32_ledc *k, int n, int out)
{
    struct chan *c = &k->c[n];
    if (out != c->out) {
        c->out = out;
        esk_esp32_signal(k->s, SIG_OUT0 + n, out, 1);
    }
}

/* the channel's settings as written, in use from now */
static void chan_apply(struct e32_ledc *k, int n)
{
    struct chan *c = &k->c[n];
    u32 c0 = reg(c->conf0);
    c->sel = (c0 & 3u) + (n >= 8 ? 4u : 0u); /* its group's timer */
    c->hp = reg(c->hpoint) & 0xfffffu;
    c->en = (c0 & C_SIG_OUT_EN) != 0;
    c->idle = (c0 & C_IDLE_LV) != 0;
    c->update = 0;
    if (!c->en)
        drive(k, n, (int)c->idle);
}

/* the low point of the period starting: HPOINT + the duty, a count longer
 * in DUTY[3:0] periods of 16 */
static void period_start(struct e32_ledc *k, int n)
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
static void overflow(struct e32_ledc *k, int x)
{
    struct tmr *t = &k->t[x];
    int n;
    if (t->update) {
        t->res = reg(t->conf) & 0x1fu;
        t->div = (reg(t->conf) >> 5) & 0x3ffffu;
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
                        c->duty_cur = c->duty_cur + scale > 0x1ffffffu ? 0x1ffffffu : c->duty_cur + scale;
                    else
                        c->duty_cur = c->duty_cur > scale ? c->duty_cur - scale : 0;
                    c->steps++;
                }
                if (c->steps >= num) {
                    c->fading = 0;
                    raise(k, 1u << (8 + n));
                }
            }
        }
        if (c->sel == (u32)x)
            period_start(k, n);
    }
}

/* the timer entered count `cnt`: the outputs that switch there */
static void at_count(struct e32_ledc *k, int x)
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
static u32 next_count(const struct e32_ledc *k, int x)
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
static u64 due(const struct e32_ledc *k, int x)
{
    const struct tmr *t = &k->t[x];
    double tf = tick_fs(k, t);
    u64 at;
    if (tf <= 0)
        return 0;
    at = (u64)((double)next_count(k, x) * tf + 0.5);
    return at > t->pos ? at - t->pos : 1u;
}

static void clocking(struct e32_ledc *k)
{
    int x, on = 0;
    for (x = 0; x < NTM; x++)
        if (tick_fs(k, &k->t[x]) > 0)
            on = 1;
    esk_rf_clock(k->p, &k->clocked, on);
}

static void advance(struct e32_ledc *k, u64 budget)
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
        esk_rf_hw(k->t[x].value, k->t[x].cnt & 0xfffffu);
}

static void ledc_tick(void *ctx, u32 cycles)
{
    struct e32_ledc *k = (struct e32_ledc *)ctx;
    advance(k, (u64)cycles * fs_per_cycle(k));
}

static int ledc_next(void *ctx, u32 *cycles)
{
    struct e32_ledc *k = (struct e32_ledc *)ctx;
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
    struct e32_ledc *k = (struct e32_ledc *)ctx;
    return esk_rf_read(k->p, off, n);
}

/* the timer starts counting (out of RST): its generators' first period */
static void timer_starts(struct e32_ledc *k, int x)
{
    int n;
    for (n = 0; n < NCH; n++)
        if (k->c[n].sel == (u32)x)
            period_start(k, n);
    at_count(k, x);
}

static void ledc_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_ledc *k = (struct e32_ledc *)ctx;
    u32 w = off & ~3u, held[NTM];
    int i;
    for (i = 0; i < NTM; i++)
        held[i] = reg(k->t[i].conf) & T_RST;
    if (w == 0x18Cu) { /* INT_CLR */
        esk_rf_hw(k->raw, reg(k->raw) & ~v);
        update(k);
        return;
    }
    if (w == 0x184u)
        return; /* INT_ST */
    esk_rf_write(k->p, off, n, v);
    if (w == 0x180u || w == 0x188u)
        update(k);
    for (i = 0; i < NTM; i++) {
        struct tmr *t = &k->t[i];
        if (w != esk_rf_off(t->conf))
            continue;
        if (reg(t->conf) & T_RST) {
            t->cnt = 0;
            t->pos = 0;
        }
        if (i < 4 || (v & T_PARA_UP))
            t->update = 1; /* a high-speed timer's settings: at its next overflow */
        if (!t->div && !t->res) /* never set: the first settings at once */
            t->update = 1;
        if (t->update && (reg(t->conf) & T_RST)) { /* held: nothing to wait for */
            t->res = reg(t->conf) & 0x1fu;
            t->div = (reg(t->conf) >> 5) & 0x3ffffu;
            t->update = 0;
        }
        if (held[i] && !(reg(t->conf) & T_RST))
            timer_starts(k, i);
    }
    for (i = 0; i < NCH; i++) {
        struct chan *c = &k->c[i];
        /* a high-speed channel takes DUTY_START (CONF1), a low-speed one
         * its PARA_UP (CONF0) after it */
        if (!(i < 8 ? w == esk_rf_off(c->conf1) && (v & F_START) : w == esk_rf_off(c->conf0) && (v & C_PARA_UP)))
            continue;
        {
            u32 c1 = reg(c->conf1);
            c->update = 1;
            c->duty_cur = reg(c->duty) & 0x1ffffffu; /* the fade's start, at once */
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
    struct e32_ledc *k = (struct e32_ledc *)ctx;
    int i;
    for (i = 0; i < NTM; i++) {
        struct tmr *t = &k->t[i];
        t->res = reg(t->conf) & 0x1fu;
        t->div = (reg(t->conf) >> 5) & 0x3ffffu;
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
    esk_esp32_source(k->s, k->source, 0);
    k->clocked = 0;
    update(k);
    clocking(k);
}

const struct esk_dev_ops esk_esp32_ledc_ops = {
    "esp32-ledc", ledc_read, ledc_write, ledc_reset, ledc_tick, ledc_next,
};

void *esk_esp32_ledc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_ledc *k = (struct e32_ledc *)esk_mach_alloc(s, sizeof *k);
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
    k->source = esk_rf_irq(p, "LEDC");
    for (i = 0; i < NTM; i++) {
        snprintf(name, sizeof name, "%sTIMER%d_CONF", i < 4 ? "HS" : "LS", i % 4);
        k->t[i].conf = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "%sTIMER%d_VALUE", i < 4 ? "HS" : "LS", i % 4);
        k->t[i].value = esk_rf_reg(p, name);
        if (!k->t[i].conf || !k->t[i].value)
            return 0;
    }
    for (i = 0; i < NCH; i++) {
        struct chan *c = &k->c[i];
        snprintf(name, sizeof name, "%sCH%d_CONF0", i < 8 ? "HS" : "LS", i % 8);
        c->conf0 = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "%sCH%d_HPOINT", i < 8 ? "HS" : "LS", i % 8);
        c->hpoint = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "%sCH%d_DUTY", i < 8 ? "HS" : "LS", i % 8);
        c->duty = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "%sCH%d_CONF1", i < 8 ? "HS" : "LS", i % 8);
        c->conf1 = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "%sCH%d_DUTY_R", i < 8 ? "HS" : "LS", i % 8);
        c->duty_r = esk_rf_reg(p, name);
        if (!c->conf0 || !c->hpoint || !c->duty || !c->conf1 || !c->duty_r)
            return 0;
    }
    return k;
}
