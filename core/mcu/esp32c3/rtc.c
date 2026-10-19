/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rtc.c -- the ESP32-C3's RTC_CNTL (ESP32-C3 TRM v1.4, chapters 6, 9 and
 * 12): the reset causes, the RTC timer, the RTC watchdog (RWDT), the
 * super watchdog's controls, the software resets and the RTC interrupts.
 *
 *   - RESET_STATE's RESET_CAUSE_PROCPU reports the last reset by TRM table
 *     6.1-1's code: 0x01 for a chip reset (power-on, CHIP_EN), 0x03
 *     OPTIONS0.SW_SYS_RST, 0x0C SW_PROCPU_RST, 0x09/0x0D/0x10 the RWDT's
 *     core, CPU and system resets, and the other devices' codes
 *     (esk_esp32c3_reset); ALL_RESET_FLAG_PROCPU is set by each reset
 *     until ALL_RESET_FLAG_CLR_PROCPU;
 *   - the RTC domain keeps its registers through a CPU reset and a core
 *     reset, and loses them in a system or chip reset (TRM 6.1.2); a CPU
 *     reset restarts the core alone;
 *   - the RTC timer counts RTC_SLOW_CLK (ANA_CLK_RTC_SEL: RC_SLOW_CLK at
 *     136 kHz, XTAL32K_CLK at the board's 32.768 kHz, RC_FAST_DIV_CLK at
 *     17.5 MHz / 256) from the chip's reset; a write of TIME_UPDATE latches
 *     it in TIME_LOW0 and TIME_HIGH0; SLP_TIMER0/1 with MAIN_TIMER_ALARM_EN
 *     raise MAIN_TIMER when it reaches them;
 *   - the RWDT counts RTC_SLOW_CLK through four stages (WDT_STGn_HOLD,
 *     stage 0's shifted left by EFUSE_WDT_DELAY_SEL + 1); each stage's
 *     action is off, an interrupt (WDT), a CPU, core or system reset;
 *     WDT_EN runs it, and so does WDT_FLASHBOOT_MOD_EN (set at reset) with
 *     stage 0 a system reset; WDTFEED returns it to stage 0; WDTCONFIG0-4
 *     and WDTFEED take writes only while WDT_WKEY holds 0x50D83AA1;
 *   - the super watchdog's SWD_CONF and its key 0x8F1D312A;
 *   - INT_RAW_RTC, INT_ST_RTC = INT_RAW_RTC & INT_ENA_RTC (with its W1TS and
 *     W1TC), INT_CLR_RTC, and the matrix source RTC_CORE (27).
 * Not modelled: sleep and wake-up (light and deep sleep, the power
 * domains, the sleep timer's wake-up), the super watchdog's own timeout
 * (its period is "slightly less than one second" in the TRM, not a value),
 * brown-out, the clock glitch detector, the slow clock's calibration
 * error (RC_SLOW_CLK is exactly 136 kHz), the reset pulse widths
 * (WDT_*_RESET_LENGTH), STAT_VECTOR_SEL_PROCPU (the core always starts
 * at the ROM). */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define RC_SLOW_HZ 136000.0
#define RC_FAST_HZ 17500000.0
#define WDT_KEY 0x50D83AA1u
#define SWD_KEY 0x8F1D312Au
#define CFG0_FLASHBOOT (1u << 12)
#define CFG0_EN (1u << 31)
#define INT_WDT (1u << 3)
#define INT_MAIN_TIMER (1u << 10)
#define INT_MASK 0x0019860Bu /* the bits INT_RAW_RTC has */
#define OPT0_SW_PROCPU_RST (1u << 5)
#define OPT0_SW_SYS_RST (1u << 31)
#define RST_ALL_FLAG (1u << 14)
#define RST_ALL_FLAG_CLR (1u << 16)

enum { ACT_OFF, ACT_INT, ACT_CPU, ACT_CORE, ACT_SYSTEM };

struct c3_rtc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *opt0, *slp0, *slp1, *upd, *low0, *high0, *reset_state, *ena, *raw, *st, *clr,
        *w1ts, *w1tc, *clk, *cfg[5], *feed, *wkey, *swd, *swd_key;
    int code;      /* the cause the next reset reports */
    int level;     /* the RTC_CORE source */
    int clocked;
    /* the slow clock's count at `base_cycles` of the core, and the part of
     * a slow cycle beyond it */
    u64 slow, base_cycles;
    double frac;
    /* the RWDT: its stage and its count of slow cycles in it, at `slow` */
    int stage;
    u64 wdt_from; /* the slow count at which the stage began */
};

static struct c3_rtc *rtc_of(struct esk_mach *m);

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static double slow_hz(const struct c3_rtc *k)
{
    switch ((reg(k->clk) >> 30) & 3u) {
    case 1:
        return k->s->lse_hz ? (double)k->s->lse_hz : 32768.0;
    case 2:
        return RC_FAST_HZ / 256.0;
    default:
        return RC_SLOW_HZ;
    }
}

/* the slow count brought up to the core's cycles */
static void sync(struct c3_rtc *k)
{
    u64 now = k->s->cycles;
    double t;
    if (now <= k->base_cycles) {
        k->base_cycles = now;
        return;
    }
    t = (double)(now - k->base_cycles) * slow_hz(k) / (double)esk_mach_core_hz(k->s) + k->frac;
    k->slow += (u64)t;
    k->frac = t - (double)(u64)t;
    k->base_cycles = now;
}

/* core cycles to `n` more slow cycles, rounded up */
static u64 cycles_to(const struct c3_rtc *k, u64 n)
{
    double c = ((double)n - k->frac) * (double)esk_mach_core_hz(k->s) / slow_hz(k);
    return c <= 1.0 ? 1u : (u64)c + 1u;
}

static int wdt_on(const struct c3_rtc *k)
{
    return (reg(k->cfg[0]) & (CFG0_EN | CFG0_FLASHBOOT)) != 0;
}

static int stage_action(const struct c3_rtc *k, int stage)
{
    u32 c = reg(k->cfg[0]);
    if (stage == 0 && (c & CFG0_FLASHBOOT))
        return ACT_SYSTEM; /* flash boot protection: a system reset */
    return (int)((c >> (28 - 3 * stage)) & 7u);
}

static u64 stage_hold(const struct c3_rtc *k, int stage)
{
    u64 h = reg(k->cfg[1 + stage]);
    if (stage == 0)
        h <<= esk_esp32c3_efuse_wdt_delay_sel(k->s) + 1;
    return h ? h : 1;
}

static void update(struct c3_rtc *k)
{
    u32 st = reg(k->raw) & reg(k->ena);
    esk_rf_hw(k->st, st);
    if ((st != 0) != k->level) {
        k->level = st != 0;
        esk_esp32c3_source(k->s, 27, k->level);
    }
}

static void clocking(struct c3_rtc *k)
{
    int on = wdt_on(k) || (reg(k->slp1) & (1u << 16));
    esk_rf_clock(k->p, &k->clocked, on);
}

/* the RWDT's and the alarm's expiries, at the current slow count */
static void expire(struct c3_rtc *k)
{
    int guard = 4;
    if ((reg(k->slp1) & (1u << 16))) {
        u64 alarm = (u64)reg(k->slp0) | (u64)(reg(k->slp1) & 0xffffu) << 32;
        if ((k->slow & 0xffffffffffffull) >= alarm) {
            esk_rf_hw(k->raw, reg(k->raw) | INT_MAIN_TIMER);
            esk_rf_hw(k->slp1, reg(k->slp1) & ~(1u << 16)); /* the alarm is spent */
        }
    }
    while (wdt_on(k) && k->slow - k->wdt_from >= stage_hold(k, k->stage) && guard--) {
        int act = stage_action(k, k->stage);
        k->wdt_from += stage_hold(k, k->stage);
        k->stage = (k->stage + 1) & 3;
        if (act == ACT_INT) {
            esk_rf_hw(k->raw, reg(k->raw) | INT_WDT);
        } else if (act == ACT_CPU) {
            esk_esp32c3_reset(k->s, 0x0D);
            break;
        } else if (act == ACT_CORE) {
            esk_esp32c3_reset(k->s, 0x09);
            break;
        } else if (act == ACT_SYSTEM) {
            esk_esp32c3_reset(k->s, 0x10);
            break;
        }
    }
    update(k);
    clocking(k);
}

static u32 rt_read(void *ctx, u32 off, int n)
{
    struct c3_rtc *k = (struct c3_rtc *)ctx;
    sync(k);
    return esk_rf_read(k->p, off, n);
}

static void rt_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_rtc *k = (struct c3_rtc *)ctx;
    int keyed = reg(k->wkey) == WDT_KEY, i;
    sync(k);
    if (off == esk_rf_off(k->feed)) {
        if (keyed && (v & (1u << 31))) {
            k->stage = 0;
            k->wdt_from = k->slow;
        }
        return;
    }
    for (i = 0; i < 5; i++)
        if (off == esk_rf_off(k->cfg[i]) && !keyed)
            return; /* write-protected */
    if (off == esk_rf_off(k->swd) && reg(k->swd_key) != SWD_KEY)
        return;
    if (off == esk_rf_off(k->clr)) {
        esk_rf_hw(k->raw, reg(k->raw) & ~v);
        update(k);
        return;
    }
    if (off == esk_rf_off(k->w1ts)) {
        esk_rf_hw(k->ena, reg(k->ena) | (v & INT_MASK));
        update(k);
        return;
    }
    if (off == esk_rf_off(k->w1tc)) {
        esk_rf_hw(k->ena, reg(k->ena) & ~v);
        update(k);
        return;
    }
    if (off == esk_rf_off(k->raw) || off == esk_rf_off(k->st))
        return;
    if (off == esk_rf_off(k->cfg[0])) {
        u32 was = reg(k->cfg[0]);
        esk_rf_write(k->p, off, n, v);
        if (!(was & (CFG0_EN | CFG0_FLASHBOOT)) && wdt_on(k)) {
            k->stage = 0; /* started: from stage 0 */
            k->wdt_from = k->slow;
        }
    } else if (off == esk_rf_off(k->reset_state)) {
        esk_rf_write(k->p, off, n, v);
        if (v & RST_ALL_FLAG_CLR)
            esk_rf_hw(k->reset_state, reg(k->reset_state) & ~(RST_ALL_FLAG | RST_ALL_FLAG_CLR));
    } else {
        esk_rf_write(k->p, off, n, v);
    }
    if (off == esk_rf_off(k->upd) && (v & (1u << 31))) {
        u64 t = k->slow & 0xffffffffffffull;
        esk_rf_hw(k->low0, (u32)t);
        esk_rf_hw(k->high0, (u32)(t >> 32));
    }
    if (off == esk_rf_off(k->opt0)) {
        if (v & OPT0_SW_SYS_RST)
            esk_esp32c3_reset(k->s, 0x03);
        else if (v & OPT0_SW_PROCPU_RST)
            esk_esp32c3_reset(k->s, 0x0C);
        esk_rf_hw(k->opt0, reg(k->opt0) & ~(OPT0_SW_SYS_RST | OPT0_SW_PROCPU_RST));
    }
    if (off == esk_rf_off(k->swd))
        esk_rf_hw(k->swd, reg(k->swd) & ~((1u << 28) | (1u << 29))); /* FEED, RST_FLAG_CLR: pulses */
    expire(k);
}

static void rt_tick(void *ctx, u32 cycles)
{
    struct c3_rtc *k = (struct c3_rtc *)ctx;
    (void)cycles;
    sync(k);
    expire(k);
}

static int rt_next(void *ctx, u32 *cycles)
{
    struct c3_rtc *k = (struct c3_rtc *)ctx;
    u64 best = 0, c;
    sync(k);
    if (wdt_on(k)) {
        u64 hold = stage_hold(k, k->stage), done = k->slow - k->wdt_from;
        best = cycles_to(k, hold > done ? hold - done : 1);
    }
    if (reg(k->slp1) & (1u << 16)) {
        u64 alarm = (u64)reg(k->slp0) | (u64)(reg(k->slp1) & 0xffffu) << 32, now = k->slow & 0xffffffffffffull;
        c = cycles_to(k, alarm > now ? alarm - now : 1);
        if (!best || c < best)
            best = c;
    }
    if (!best)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static void rt_reset(void *ctx)
{
    struct c3_rtc *k = (struct c3_rtc *)ctx;
    int chip = k->s->reset_cause == ESK_RESET_POWER || k->s->reset_cause == ESK_RESET_PIN;
    int code = chip ? 0x01 : k->code;
    k->p->backup = 1; /* kept through the next core reset */
    if (chip || code == 0x0F || code == 0x10 || code == 0x12 || code == 0x13) {
        /* a chip or system reset: the RTC domain starts again */
        k->slow = 0;
        k->frac = 0;
        k->stage = 0;
        k->wdt_from = 0;
    }
    k->base_cycles = 0; /* the core's count starts again */
    k->clocked = 0;
    k->level = 0;
    if (!chip && code != 0x0F && code != 0x10 && code != 0x12 && code != 0x13) {
        /* a core reset: the watchdog and the timer keep counting */
    } else {
        k->stage = 0;
    }
    esk_rf_hw(k->reset_state, (reg(k->reset_state) & ~0x3fu) | (u32)code | RST_ALL_FLAG);
    k->code = 0x01;
    update(k);
    clocking(k);
}

const struct esk_dev_ops esk_esp32c3_rtc_ops = {
    "esp32c3-rtc", rt_read, rt_write, rt_reset, rt_tick, rt_next,
};

static struct c3_rtc *rtc_of(struct esk_mach *m)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_esp32c3_rtc_ops)
            return (struct c3_rtc *)p->ctx;
    }
    return NULL;
}

/* the TRM's reset classes (table 6.1-1) */
static int cpu_reset(int code) { return code == 0x0B || code == 0x0C || code == 0x0D || code == 0x11; }
static int system_reset(int code) { return code == 0x0F || code == 0x10 || code == 0x12 || code == 0x13; }

void esk_esp32c3_reset(struct esk_mach *m, int code)
{
    struct c3_rtc *k = rtc_of(m);
    if (k) {
        k->code = code;
        if (system_reset(code))
            k->p->backup = 0; /* the RTC domain's registers start again */
        if (cpu_reset(code)) {
            /* the CPU alone: the devices keep their state; RESET_STATE says so now */
            esk_rf_hw(k->reset_state, (reg(k->reset_state) & ~0x3fu) | (u32)code | RST_ALL_FLAG);
            k->code = 0x01;
        }
    }
    esk_esp32c3_assist_cpu_reset(m);
    m->reset_cpu_only = cpu_reset(code);
    m->reset_cause = code == 0x03 || code == 0x0C ? ESK_RESET_SOFTWARE : ESK_RESET_WATCHDOG;
    m->reset_req = 1;
    m->yield = 1;
}

void *esk_esp32c3_rtc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_rtc *k;
    char name[16];
    int i;
    k = (struct c3_rtc *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->code = 0x01;
    k->opt0 = esk_rf_reg(p, "OPTIONS0");
    k->slp0 = esk_rf_reg(p, "SLP_TIMER0");
    k->slp1 = esk_rf_reg(p, "SLP_TIMER1");
    k->upd = esk_rf_reg(p, "TIME_UPDATE");
    k->low0 = esk_rf_reg(p, "TIME_LOW0");
    k->high0 = esk_rf_reg(p, "TIME_HIGH0");
    k->reset_state = esk_rf_reg(p, "RESET_STATE");
    k->ena = esk_rf_reg(p, "INT_ENA_RTC");
    k->raw = esk_rf_reg(p, "INT_RAW_RTC");
    k->st = esk_rf_reg(p, "INT_ST_RTC");
    k->clr = esk_rf_reg(p, "INT_CLR_RTC");
    k->w1ts = esk_rf_reg(p, "INT_ENA_RTC_W1TS");
    k->w1tc = esk_rf_reg(p, "INT_ENA_RTC_W1TC");
    k->clk = esk_rf_reg(p, "CLK_CONF");
    for (i = 0; i < 5; i++) {
        (void)snprintf(name, sizeof name, "WDTCONFIG%d", i);
        k->cfg[i] = esk_rf_reg(p, name);
        if (!k->cfg[i])
            return 0;
    }
    k->feed = esk_rf_reg(p, "WDTFEED");
    k->wkey = esk_rf_reg(p, "WDTWPROTECT");
    k->swd = esk_rf_reg(p, "SWD_CONF");
    k->swd_key = esk_rf_reg(p, "SWD_WPROTECT");
    if (!k->opt0 || !k->slp0 || !k->slp1 || !k->upd || !k->low0 || !k->high0 || !k->reset_state || !k->ena ||
        !k->raw || !k->st || !k->clr || !k->w1ts || !k->w1tc || !k->clk || !k->feed || !k->wkey || !k->swd ||
        !k->swd_key)
        return 0;
    p->backup = 1;
    return k;
}
