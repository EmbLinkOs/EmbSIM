/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* pwm.c -- the FE310's PWM peripherals (FE310-G002 manual, chapter 14),
 * over the register file: PWM0 with 8-bit comparators, PWM1 and PWM2 with
 * 16-bit ones, four each.
 *
 *   - pwmcount counts the bus clock (the core's) while pwmenalways is set,
 *     or until it next resets while pwmenoneshot is (which then clears);
 *     pwms is pwmcount shifted right by pwmscale, as wide as a comparator;
 *   - comparator n's pwmcmpNip is set while pwms >= pwmcmpN (pwmcmpNcenter
 *     compares the count folded at its top bit instead); with pwmdeglitch
 *     a set bit holds until the count resets, with pwmsticky until the
 *     program clears it; a bit set raises the PLIC source of its channel
 *     (40 + 4 * unit + n), level-triggered;
 *   - pwmzerocmp resets the count when pwms has spent its tick at
 *     pwmcmp0: a period of (pwmcmp0 + 1) << pwmscale cycles, as
 *     freedom-metal's driver computes it; without it the count wraps at the
 *     comparators' width;
 *   - a channel's output is its pwmcmpNip (with pwmcmpNgang, ANDed with
 *     the next channel's inverse), driven on its pin while the GPIO gives
 *     the pin to IOF1: PWM0 0-3 on GPIO 0-3, PWM1 0-3 on GPIO 20, 19, 21
 *     and 22 (the HiFive1's RGB LED), PWM2 0-3 on GPIO 10-13;
 *   - a change of the outputs is a device event, at most one in an
 *     instruction.
 *
 * Not modelled: the comparators' shadow registers (a write takes effect at
 * once). */
#include <string.h>

#include "mcu/riscv/riscv.h"

#define CFG_SCALE 0xfu
#define CFG_STICKY (1u << 8)
#define CFG_ZEROCMP (1u << 9)
#define CFG_DEGLITCH (1u << 10)
#define CFG_ENALWAYS (1u << 12)
#define CFG_ENONESHOT (1u << 13)
#define CFG_CENTER(n) (1u << (16 + (n)))
#define CFG_GANG(n) (1u << (24 + (n)))
#define CFG_IP(n) (1u << (28 + (n)))
#define CFG_IPS 0xf0000000u

struct fe_pwm {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cfg, *count, *pwms, *cmp[4];
    int unit, width, running;
    u64 cnt;          /* the raw count, 15 + width bits */
    u64 left, since;  /* core cycles to the next change */
    u32 out;          /* the channels' outputs driven */
    u32 src;          /* the PLIC sources raised */
};

static u32 v32(const struct esk_rf_reg *r) { return (u32)r->value; }

static const int pins[3][4] = { { 0, 1, 2, 3 }, { 20, 19, 21, 22 }, { 10, 11, 12, 13 } };

static int counting(const struct fe_pwm *k) { return (v32(k->cfg) & (CFG_ENALWAYS | CFG_ENONESHOT)) != 0; }

static u64 cnt_mask(const struct fe_pwm *k) { return ((u64)1 << (15 + k->width)) - 1; }

static u32 s_of(const struct fe_pwm *k, u64 cnt)
{
    return (u32)(cnt >> (v32(k->cfg) & CFG_SCALE)) & ((1u << k->width) - 1u);
}

/* comparator n against pwms (or its folded value in center mode) */
static int hit(const struct fe_pwm *k, int n, u32 s)
{
    u32 msb = 1u << (k->width - 1), cmp = v32(k->cmp[n]) & ((1u << k->width) - 1u);
    if (v32(k->cfg) & CFG_CENTER(n))
        s = (s & msb) ? (~s & (msb - 1u)) : s;
    return s >= cmp;
}

/* the count at which the period ends (0: no reset but the wrap) */
static u64 period(const struct fe_pwm *k)
{
    u32 cfg = v32(k->cfg);
    if (cfg & CFG_ZEROCMP)
        return ((u64)(v32(k->cmp[0]) & ((1u << k->width) - 1u)) + 1u) << (cfg & CFG_SCALE);
    return (u64)1 << ((cfg & CFG_SCALE) + (u32)k->width);
}

static void outputs(struct fe_pwm *k)
{
    struct esk_mach *s = k->s;
    u32 cfg = v32(k->cfg), out = 0, src;
    int n;
    for (n = 0; n < 4; n++) {
        int ip = (cfg & CFG_IP(n)) != 0;
        if ((cfg & CFG_GANG(n)) && (cfg & CFG_IP((n + 1) & 3)))
            ip = 0;
        out |= (u32)ip << n;
    }
    if (out != k->out) {
        u32 ch = out ^ k->out;
        k->out = out;
        for (n = 0; n < 4; n++)
            if (ch >> n & 1)
                esk_fe310_gpio_iof_level(s, 1, pins[k->unit][n], (int)(out >> n & 1));
    }
    src = cfg >> 28 & 0xfu;
    if (src != k->src) {
        u32 ch = src ^ k->src;
        k->src = src;
        for (n = 0; n < 4; n++)
            if (ch >> n & 1)
                esk_fe310_plic_source(s, 40 + 4 * k->unit + n, (int)(src >> n & 1));
    }
}

/* the ip bits for the count as it stands; `reset` is the count's return to
 * zero, which lets deglitched bits go */
static void compare(struct fe_pwm *k, int reset)
{
    u32 cfg = v32(k->cfg), ips = 0, s = s_of(k, k->cnt);
    int n;
    for (n = 0; n < 4; n++) {
        int on = hit(k, n, s);
        int held = (cfg & CFG_IP(n)) && ((cfg & CFG_STICKY) || ((cfg & CFG_DEGLITCH) && !reset));
        if (on || held)
            ips |= CFG_IP(n);
    }
    esk_rf_hw(k->cfg, (cfg & ~(u64)CFG_IPS) | ips);
    esk_rf_hw(k->count, k->cnt);
    esk_rf_hw(k->pwms, s);
    outputs(k);
}

/* cycles from the count to the next one where a comparison or the period
 * changes something */
static u64 to_next(const struct fe_pwm *k)
{
    u32 cfg = v32(k->cfg), scale = cfg & CFG_SCALE;
    u64 end = period(k), best = end > k->cnt ? end - k->cnt : 1;
    u32 s = s_of(k, k->cnt);
    int n;
    for (n = 0; n < 4; n++) {
        u32 cmp = v32(k->cmp[n]) & ((1u << k->width) - 1u);
        u64 at;
        if (cfg & CFG_CENTER(n)) {
            /* folded: step one tick at a time */
            at = ((k->cnt >> scale) + 1u) << scale;
        } else if (s < cmp) {
            at = (u64)cmp << scale;
        } else {
            continue; /* stays set until the period ends */
        }
        if (at > k->cnt && at - k->cnt < best)
            best = at - k->cnt;
    }
    return best ? best : 1;
}

static void schedule(struct fe_pwm *k)
{
    if (!counting(k)) {
        esk_rf_clock(k->p, &k->running, 0);
        return;
    }
    k->left = to_next(k);
    k->since = k->s->cycles;
    esk_rf_clock(k->p, &k->running, 1);
}

/* the count moved on by c cycles, up to and including one change */
static void advance(struct fe_pwm *k, u64 c)
{
    u64 end = period(k);
    k->cnt += c;
    if (k->cnt >= end) {
        k->cnt = 0;
        if (v32(k->cfg) & CFG_ENONESHOT)
            esk_rf_hw(k->cfg, v32(k->cfg) & ~(u64)CFG_ENONESHOT);
        compare(k, 1);
    } else {
        k->cnt &= cnt_mask(k);
        compare(k, 0);
    }
}

static void pwm_tick(void *ctx, u32 cycles)
{
    struct fe_pwm *k = (struct fe_pwm *)ctx;
    if (!k->running)
        return;
    cycles = esk_mach_after(k->s, cycles, k->since);
    k->since = 0;
    if (k->left > cycles) {
        k->left -= cycles;
        k->cnt += cycles;
        return;
    }
    {
        /* one change in an instruction; what the instruction ran past it
         * counts toward the next, up to a whole step */
        u64 over = cycles - k->left;
        advance(k, k->left);
        k->left = 0;
        schedule(k);
        if (k->running && k->left) {
            u64 cut = over < k->left ? over : k->left - 1;
            k->left -= cut;
            k->cnt += cut;
            k->since = 0;
        }
    }
}

static int pwm_next(void *ctx, u32 *cycles)
{
    struct fe_pwm *k = (struct fe_pwm *)ctx;
    if (!k->running)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)(k->left ? k->left : 1);
    return 1;
}

static u32 pwm_read(void *ctx, u32 off, int n)
{
    struct fe_pwm *k = (struct fe_pwm *)ctx;
    if (!k->s->bus.debug && k->running && (off == esk_rf_off(k->count) || off == esk_rf_off(k->pwms))) {
        esk_rf_hw(k->count, k->cnt);
        esk_rf_hw(k->pwms, s_of(k, k->cnt));
    }
    return esk_rf_read(k->p, off, n);
}

static void pwm_write(void *ctx, u32 off, int n, u32 v)
{
    struct fe_pwm *k = (struct fe_pwm *)ctx;
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->count))
        k->cnt = v32(k->count) & cnt_mask(k);
    compare(k, 0);
    schedule(k);
}

static void pwm_reset(void *ctx)
{
    struct fe_pwm *k = (struct fe_pwm *)ctx;
    int n;
    k->running = 0;
    k->cnt = 0;
    k->left = 0;
    for (n = 0; n < 4; n++)
        esk_fe310_gpio_iof_level(k->s, 1, pins[k->unit][n], 0); /* IOF1 drives the outputs, low */
    k->out = 0;
    k->src = 0;
}

const struct esk_dev_ops esk_fe310_pwm_ops = {
    "fe310-pwm", pwm_read, pwm_write, pwm_reset, pwm_tick, pwm_next,
};

void *esk_fe310_pwm_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct fe_pwm k, *c;
    char name[8] = "PWMCMPx";
    int n;
    memset(&k, 0, sizeof k);
    if (strncmp(p->name, "PWM", 3) || p->name[3] < '0' || p->name[3] > '2' || p->name[4])
        return 0;
    k.s = s;
    k.p = p;
    k.unit = p->name[3] - '0';
    k.width = k.unit == 0 ? 8 : 16;
    k.cfg = esk_rf_reg(p, "PWMCFG");
    k.count = esk_rf_reg(p, "PWMCOUNT");
    k.pwms = esk_rf_reg(p, "PWMS");
    for (n = 0; n < 4; n++) {
        name[6] = (char)('0' + n);
        k.cmp[n] = esk_rf_reg(p, name);
        if (!k.cmp[n])
            return 0;
    }
    if (!k.cfg || !k.count || !k.pwms)
        return 0;
    c = (struct fe_pwm *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = k;
    return c;
}
