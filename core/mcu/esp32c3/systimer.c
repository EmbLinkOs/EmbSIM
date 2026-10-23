/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* systimer.c -- the ESP32-C3's system timer (TRM v1.4 chapter 10): two
 * 52-bit counters, UNIT0 and UNIT1, counting CNT_CLK, XTAL_CLK / 2.5 =
 * 16 MHz, while CONF's TIMER_UNITn_WORK_EN is set (UNIT0's is, out of
 * reset); three comparators raising SYSTIMER_TARGETx_INT (matrix sources
 * 37-39).
 *
 *   - UNITn_OP's UPDATE latches the count in VALUE_HI/LO and sets
 *     VALUE_VALID; UNITn_LOAD loads LOAD_HI/LO into the counter;
 *   - COMPx_LOAD takes TARGETx_HI/LO and TARGETx_CONF's PERIOD into the
 *     comparator, which compares, while TARGETx_WORK_EN is set, the counter
 *     TARGETx_TIMER_UNIT_SEL names: in target mode one alarm when the count
 *     reaches the target, at once when the target is up to 2^51 - 1 behind
 *     it (table 10.4-2); in period mode (PERIOD_MODE) an alarm every PERIOD
 *     counts from the count when the mode, the comparator or its load
 *     starts it;
 *   - an alarm sets INT_RAW's bit, which INT_CLR clears; INT_ST is INT_RAW
 *     and INT_ENA, each bit its source's level.
 * Inside, a counter advances at CNT_CLK's average, 16 MHz, rather than its
 * alternate thirds and halves of XTAL_CLK; the two agree every 5 XTAL_CLK
 * cycles.
 * Not yet: the counters' stall while the CPU is halted by a debugger
 * (TIMER_UNITn_COREm_STALL_EN). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define CNT_HZ 16000000u
#define MASK52 ((1ull << 52) - 1u)
#define HALF51 (1ull << 51)

#define CONF_UNIT0_WORK (1u << 30)
#define CONF_UNIT1_WORK (1u << 29)
#define CONF_TARGET_WORK(x) (1u << (24 - (x)))
#define TCONF_PERIOD_MODE (1u << 30)
#define TCONF_UNIT_SEL (1u << 31)

struct comp {
    u64 target;  /* the loaded alarm value (target mode) */
    u32 period;  /* the loaded period */
    u64 next;    /* the next alarm in period mode */
    int armed;   /* target mode: the alarm is still to come */
};

struct c3_systimer {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *conf, *op[2], *load_hi[2], *load_lo[2], *val_hi[2], *val_lo[2];
    struct esk_rf_reg *t_hi[3], *t_lo[3], *t_conf[3], *ena, *raw, *st;
    u64 count[2];
    u64 frac; /* CNT_CLK fractions owed: cycles * 16 MHz, in core_hz units */
    struct comp c[3];
    int clocked, level[3];
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static int unit_works(const struct c3_systimer *k, int u)
{
    return (reg(k->conf) & (u ? CONF_UNIT1_WORK : CONF_UNIT0_WORK)) != 0;
}

static int comp_on(const struct c3_systimer *k, int x)
{
    return (reg(k->conf) & CONF_TARGET_WORK(x)) != 0;
}

static int comp_unit(const struct c3_systimer *k, int x) { return (reg(k->t_conf[x]) & TCONF_UNIT_SEL) != 0; }

static int period_mode(const struct c3_systimer *k, int x)
{
    return (reg(k->t_conf[x]) & TCONF_PERIOD_MODE) != 0;
}

static void update(struct c3_systimer *k)
{
    u32 st = reg(k->raw) & reg(k->ena) & 7u;
    int x;
    esk_rf_hw(k->st, st);
    for (x = 0; x < 3; x++) {
        int l = (int)(st >> x & 1u);
        if (l != k->level[x]) {
            k->level[x] = l;
            esk_esp32c3_source(k->s, 37 + x, l);
        }
    }
}

static void alarm(struct c3_systimer *k, int x)
{
    esk_rf_hw(k->raw, reg(k->raw) | 1u << x);
}

/* the comparator's checks after its counter went from `from` to `to`
 * (both within 52 bits; `to` may have wrapped) */
static void compare(struct c3_systimer *k, int x, u64 from, u64 to)
{
    struct comp *c = &k->c[x];
    u64 span = (to - from) & MASK52;
    if (!comp_on(k, x))
        return;
    if (period_mode(k, x)) {
        int guard = 1000;
        if (!c->period)
            return;
        while (((c->next - from) & MASK52) <= span && ((c->next - from) & MASK52) != 0 && guard--) {
            alarm(k, x);
            c->next = (c->next + c->period) & MASK52;
        }
    } else if (c->armed && ((c->target - from) & MASK52) <= span && ((c->target - from) & MASK52) != 0) {
        c->armed = 0;
        alarm(k, x);
    }
}

/* a target loaded or a comparator started: an alarm at once when the
 * target is up to 2^51 - 1 behind the count (TRM table 10.4-2) */
static void start(struct c3_systimer *k, int x)
{
    struct comp *c = &k->c[x];
    u64 now = k->count[comp_unit(k, x)];
    if (period_mode(k, x)) {
        c->next = (now + c->period) & MASK52;
        return;
    }
    c->armed = 1;
    if (comp_on(k, x) && ((now - c->target) & MASK52) < HALF51) {
        c->armed = 0;
        alarm(k, x);
    }
}

static void advance(struct c3_systimer *k, u32 cycles)
{
    u32 hz = esk_mach_core_hz(k->s);
    u64 n;
    int u, x;
    if (!hz)
        return;
    k->frac += (u64)cycles * CNT_HZ;
    n = k->frac / hz;
    k->frac %= hz;
    if (!n)
        return;
    for (u = 0; u < 2; u++) {
        u64 from = k->count[u];
        if (!unit_works(k, u))
            continue;
        k->count[u] = (from + n) & MASK52;
        for (x = 0; x < 3; x++)
            if (comp_unit(k, x) == u)
                compare(k, x, from, k->count[u]);
    }
    update(k);
}

static void clocking(struct c3_systimer *k)
{
    esk_rf_clock(k->p, &k->clocked, unit_works(k, 0) || unit_works(k, 1));
}

static u32 tm_read(void *ctx, u32 off, int n)
{
    struct c3_systimer *k = (struct c3_systimer *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void tm_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_systimer *k = (struct c3_systimer *)ctx;
    u32 conf0 = reg(k->conf), tconf0[3];
    int u, x;
    for (x = 0; x < 3; x++)
        tconf0[x] = reg(k->t_conf[x]);
    esk_rf_write(k->p, off, n, v);
    for (u = 0; u < 2; u++) {
        if (off == esk_rf_off(k->op[u]) && (v & (1u << 30))) {
            esk_rf_hw(k->val_hi[u], (u32)(k->count[u] >> 32) & 0xfffffu);
            esk_rf_hw(k->val_lo[u], (u32)k->count[u]);
            esk_rf_hw(k->op[u], reg(k->op[u]) | 1u << 29);
        }
        if (off == 0x5Cu + 4u * (u32)u && (v & 1u))
            k->count[u] = ((u64)(reg(k->load_hi[u]) & 0xfffffu) << 32 | reg(k->load_lo[u])) & MASK52;
    }
    for (x = 0; x < 3; x++) {
        int started = 0;
        if (off == 0x50u + 4u * (u32)x && (v & 1u)) {
            k->c[x].target = ((u64)(reg(k->t_hi[x]) & 0xfffffu) << 32 | reg(k->t_lo[x])) & MASK52;
            k->c[x].period = reg(k->t_conf[x]) & 0x3ffffffu;
            started = 1;
        }
        if (off == esk_rf_off(k->conf) && !(conf0 & CONF_TARGET_WORK(x)) && comp_on(k, x))
            started = 1;
        if (off == esk_rf_off(k->t_conf[x]) && !(tconf0[x] & TCONF_PERIOD_MODE) && period_mode(k, x))
            started = 1;
        if (started)
            start(k, x);
    }
    if (off == 0x6Cu) /* INT_CLR */
        esk_rf_hw(k->raw, reg(k->raw) & ~(v & 7u));
    update(k);
    clocking(k);
}

static void tm_tick(void *ctx, u32 cycles)
{
    advance((struct c3_systimer *)ctx, cycles);
}

/* cycles until the next alarm of an enabled comparator */
static int tm_next(void *ctx, u32 *cycles)
{
    struct c3_systimer *k = (struct c3_systimer *)ctx;
    u32 hz = esk_mach_core_hz(k->s);
    u64 best = 0;
    int x, any = 0;
    if (!hz)
        return 0;
    for (x = 0; x < 3; x++) {
        int u = comp_unit(k, x);
        u64 d, cyc;
        if (!comp_on(k, x) || !unit_works(k, u))
            continue;
        if (period_mode(k, x)) {
            if (!k->c[x].period)
                continue;
            d = (k->c[x].next - k->count[u]) & MASK52;
        } else {
            if (!k->c[x].armed)
                continue;
            d = (k->c[x].target - k->count[u]) & MASK52;
        }
        /* d counts are d * hz / CNT_HZ cycles, less what is owed */
        cyc = d > (1ull << 32) ? 0xffffffffu : (d * hz - k->frac + CNT_HZ - 1u) / CNT_HZ;
        if (!any || cyc < best)
            best = cyc;
        any = 1;
    }
    if (!any)
        return 0;
    *cycles = best == 0 ? 1u : best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static void tm_reset(void *ctx)
{
    struct c3_systimer *k = (struct c3_systimer *)ctx;
    memset(k->count, 0, sizeof k->count);
    memset(k->c, 0, sizeof k->c);
    k->frac = 0;
    k->clocked = 0; /* the machine's reset stopped every clock */
    update(k);
    clocking(k);
}

const struct esk_dev_ops esk_esp32c3_systimer_ops = {
    "esp32c3-systimer", tm_read, tm_write, tm_reset, tm_tick, tm_next,
};

void *esk_esp32c3_systimer_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_systimer *k = (struct c3_systimer *)esk_mach_alloc(s, sizeof *k);
    static const char *const unit_regs[2][5] = {
        { "UNIT0_OP", "UNIT0_LOAD_HI", "UNIT0_LOAD_LO", "UNIT0_VALUE_HI", "UNIT0_VALUE_LO" },
        { "UNIT1_OP", "UNIT1_LOAD_HI", "UNIT1_LOAD_LO", "UNIT1_VALUE_HI", "UNIT1_VALUE_LO" },
    };
    static const char *const target_regs[3][3] = {
        { "TARGET0_HI", "TARGET0_LO", "TARGET0_CONF" },
        { "TARGET1_HI", "TARGET1_LO", "TARGET1_CONF" },
        { "TARGET2_HI", "TARGET2_LO", "TARGET2_CONF" },
    };
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->conf = esk_rf_reg(p, "CONF");
    k->ena = esk_rf_reg(p, "INT_ENA");
    k->raw = esk_rf_reg(p, "INT_RAW");
    k->st = esk_rf_reg(p, "INT_ST");
    if (!k->conf || !k->ena || !k->raw || !k->st)
        return 0;
    for (i = 0; i < 2; i++) {
        k->op[i] = esk_rf_reg(p, unit_regs[i][0]);
        k->load_hi[i] = esk_rf_reg(p, unit_regs[i][1]);
        k->load_lo[i] = esk_rf_reg(p, unit_regs[i][2]);
        k->val_hi[i] = esk_rf_reg(p, unit_regs[i][3]);
        k->val_lo[i] = esk_rf_reg(p, unit_regs[i][4]);
        if (!k->op[i] || !k->load_hi[i] || !k->load_lo[i] || !k->val_hi[i] || !k->val_lo[i])
            return 0;
    }
    for (i = 0; i < 3; i++) {
        k->t_hi[i] = esk_rf_reg(p, target_regs[i][0]);
        k->t_lo[i] = esk_rf_reg(p, target_regs[i][1]);
        k->t_conf[i] = esk_rf_reg(p, target_regs[i][2]);
        if (!k->t_hi[i] || !k->t_lo[i] || !k->t_conf[i])
            return 0;
    }
    return k;
}
