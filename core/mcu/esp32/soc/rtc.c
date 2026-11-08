/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rtc.c -- the ESP32's RTC_CNTL (TRM v5 chapters 31 and 32): the reset
 * causes, the RTC timer, the RTC watchdog (RWDT), the software resets and
 * stalls, the clock selection the CPU's clock follows, the store
 * registers and the RTC interrupts.
 *
 *   - RESET_STATE's RESET_CAUSE_PROCPU and RESET_CAUSE_APPCPU report each
 *     CPU's last reset by the TRM's code: 1 POWERON for a chip reset
 *     (power-on, CHIP_PU), 3 SW_RESET (OPTIONS0.SW_SYS_RST), 12 SW_CPU
 *     (SW_PROCPU_RST, SW_APPCPU_RST), 9, 13 and 16 the RWDT's system, CPU
 *     and RTC resets, 7 and 8 the timer groups' watchdogs
 *     (esk_esp32_reset); the APP_CPU reports 14 (EXT_CPU) while it was
 *     held by the PRO_CPU (DPORT);
 *   - the RTC domain keeps its registers through CPU and system resets
 *     and loses them in a chip or RTC reset; a CPU reset restarts that CPU
 *     alone (esk_lx6_core_restart);
 *   - the RTC timer counts RTC_SLOW_CLK (CLK_CONF's ANA_CLK_RTC_SEL: the
 *     150 kHz RC oscillator, the 32.768 kHz crystal, 8MD256) from the chip's
 *     reset; a write of TIME_UPDATE's bit 31 latches it in TIME0 and TIME1
 *     and sets TIME_VALID; SLP_TIMER0/1 with MAIN_TIMER_ALARM_EN raise
 *     MAIN_TIMER when it reaches them;
 *   - the RWDT counts RTC_SLOW_CLK through four stages (WDT_STGn_HOLD);
 *     each stage's action is off, an interrupt (WDT), a CPU, system or RTC
 *     reset; WDT_EN runs it, and so does WDT_FLASHBOOT_MOD_EN (set at a
 *     chip reset) with stage 0 an RTC reset; WDTFEED returns it to stage
 *     0; WDTCONFIG0-4 and WDTFEED take writes only while WDT_WKEY holds
 *     0x50D83AA1;
 *   - a CPU stalls while its SW_STALL_*CPU_C1 (SW_CPU_STALL) is 0x21 and
 *     its C0 (OPTIONS0) 2;
 *   - INT_RAW, INT_ST = INT_RAW & INT_ENA, INT_CLR, and the matrix source
 *     RTC_CORE (46).
 * Not modelled: sleep and wake-up (light and deep sleep, the power
 * domains, the ULP coprocessor), brown-out, the slow clocks' error (the
 * RC oscillator is exactly 150 kHz), the reset pulse widths
 * (WDT_*_RESET_LENGTH), STAT_VECTOR_SEL (the cores always start at the
 * ROM). */
#include <string.h>

#include "mcu/esp32/soc/esp32.h"
#include "mcu/xtensa/lx6.h"

#define RC_SLOW_HZ 150000.0
#define RC_FAST_HZ 8000000.0
#define WDT_KEY 0x50D83AA1u
#define CFG0_FLASHBOOT (1u << 10)
#define CFG0_EN (1u << 31)
#define INT_WDT (1u << 3)
#define INT_MAIN_TIMER (1u << 8)
#define OPT0_SW_APPCPU_RST (1u << 4)
#define OPT0_SW_PROCPU_RST (1u << 5)
#define OPT0_SW_SYS_RST (1u << 31)

#define OPTIONS0 0x00u
#define SLP_TIMER0 0x04u
#define SLP_TIMER1 0x08u
#define TIME_UPDATE 0x0Cu
#define TIME0 0x10u
#define TIME1 0x14u
#define RESET_STATE 0x34u
#define INT_ENA 0x3Cu
#define INT_RAW 0x40u
#define INT_ST 0x44u
#define INT_CLR 0x48u
#define CLK_CONF 0x70u
#define WDTCONFIG0 0x8Cu
#define WDTFEED 0xA0u
#define WDTWPROTECT 0xA4u
#define SW_CPU_STALL 0xACu

enum { ACT_OFF, ACT_INT, ACT_CPU, ACT_SYSTEM, ACT_RTC };

struct e32_rtc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    int code[2];   /* the cause each CPU's next reset reports */
    int level;     /* the RTC_CORE source */
    int clocked;
    u64 slow, base_cycles;
    double frac;
    int stage;
    u64 wdt_from;
};

static struct e32_rtc *rtc_of(struct esk_mach *m)
{
    return m->rf ? (struct e32_rtc *)esk_rf_ctx_of(m->rf, &esk_esp32_rtc_ops) : 0;
}

static u32 reg(struct e32_rtc *k, u32 off)
{
    struct esk_rf_reg *r = esk_rf_reg_at(k->p, off);
    return r ? (u32)r->value : 0;
}

static void hw(struct e32_rtc *k, u32 off, u32 v)
{
    struct esk_rf_reg *r = esk_rf_reg_at(k->p, off);
    if (r)
        esk_rf_hw(r, v);
}

static double slow_hz(struct e32_rtc *k)
{
    switch ((reg(k, CLK_CONF) >> 30) & 3u) {
    case 1:
        return k->s->lse_hz ? (double)k->s->lse_hz : 32768.0;
    case 2:
        return RC_FAST_HZ / 256.0;
    default:
        return RC_SLOW_HZ;
    }
}

u32 esk_esp32_rtc_soc_clk_sel(struct esk_mach *m)
{
    struct e32_rtc *k = rtc_of(m);
    return k ? (reg(k, CLK_CONF) >> 27) & 3u : 0;
}

double esk_esp32_rtc_slow_hz(struct esk_mach *m)
{
    struct e32_rtc *k = rtc_of(m);
    return k ? slow_hz(k) : RC_SLOW_HZ;
}

static void sync(struct e32_rtc *k)
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

static u64 cycles_to(struct e32_rtc *k, u64 n)
{
    double c = ((double)n - k->frac) * (double)esk_mach_core_hz(k->s) / slow_hz(k);
    return c <= 1.0 ? 1u : (u64)c + 1u;
}

static int wdt_on(struct e32_rtc *k)
{
    return (reg(k, WDTCONFIG0) & (CFG0_EN | CFG0_FLASHBOOT)) != 0;
}

static int stage_action(struct e32_rtc *k, int stage)
{
    u32 c = reg(k, WDTCONFIG0);
    if (stage == 0 && (c & CFG0_FLASHBOOT))
        return ACT_RTC; /* flash boot protection */
    return (int)((c >> (28 - 3 * stage)) & 7u);
}

static u64 stage_hold(struct e32_rtc *k, int stage)
{
    u64 h = reg(k, WDTCONFIG0 + 4u * (u32)(stage + 1));
    return h ? h : 1;
}

static void update(struct e32_rtc *k)
{
    u32 st = reg(k, INT_RAW) & reg(k, INT_ENA);
    hw(k, INT_ST, st);
    if ((st != 0) != k->level) {
        k->level = st != 0;
        esk_esp32_source(k->s, esk_rf_irq(k->p, "RTC_CORE"), k->level);
    }
}

static void clocking(struct e32_rtc *k)
{
    int on = wdt_on(k) || (reg(k, SLP_TIMER1) & (1u << 16));
    esk_rf_clock(k->p, &k->clocked, on);
}

static void expire(struct e32_rtc *k)
{
    int guard = 4;
    if (reg(k, SLP_TIMER1) & (1u << 16)) {
        u64 alarm = (u64)reg(k, SLP_TIMER0) | (u64)(reg(k, SLP_TIMER1) & 0xffffu) << 32;
        if ((k->slow & 0xffffffffffffull) >= alarm) {
            hw(k, INT_RAW, reg(k, INT_RAW) | INT_MAIN_TIMER);
            hw(k, SLP_TIMER1, reg(k, SLP_TIMER1) & ~(1u << 16));
        }
    }
    while (wdt_on(k) && k->slow - k->wdt_from >= stage_hold(k, k->stage) && guard--) {
        int act = stage_action(k, k->stage);
        k->wdt_from += stage_hold(k, k->stage);
        k->stage = (k->stage + 1) & 3;
        if (act == ACT_INT) {
            hw(k, INT_RAW, reg(k, INT_RAW) | INT_WDT);
        } else if (act == ACT_CPU) {
            esk_esp32_cpu_reset(k->s, 0, 13);
            esk_esp32_cpu_reset(k->s, 1, 13);
            break;
        } else if (act == ACT_SYSTEM) {
            esk_esp32_reset(k->s, 9);
            break;
        } else if (act == ACT_RTC) {
            esk_esp32_reset(k->s, 16);
            break;
        }
    }
    update(k);
    clocking(k);
}

static void stalls(struct e32_rtc *k)
{
    u32 o = reg(k, OPTIONS0), c1 = reg(k, SW_CPU_STALL);
    esk_lx6_core_stall(k->s, 0, 2, ((o >> 2) & 3u) == 2u && ((c1 >> 26) & 0x3fu) == 0x21u);
    esk_lx6_core_stall(k->s, 1, 2, (o & 3u) == 2u && ((c1 >> 20) & 0x3fu) == 0x21u);
}

static u32 rt_read(void *ctx, u32 off, int n)
{
    struct e32_rtc *k = (struct e32_rtc *)ctx;
    sync(k);
    return esk_rf_read(k->p, off, n);
}

static void rt_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_rtc *k = (struct e32_rtc *)ctx;
    int keyed = reg(k, WDTWPROTECT) == WDT_KEY;
    sync(k);
    if (off == WDTFEED) {
        if (keyed && (v & (1u << 31))) {
            k->stage = 0;
            k->wdt_from = k->slow;
        }
        return;
    }
    if (off >= WDTCONFIG0 && off <= WDTCONFIG0 + 16u && !keyed)
        return;
    if (off == INT_CLR) {
        hw(k, INT_RAW, reg(k, INT_RAW) & ~v);
        update(k);
        return;
    }
    if (off == INT_RAW || off == INT_ST)
        return;
    if (off == WDTCONFIG0) {
        u32 was = reg(k, WDTCONFIG0);
        esk_rf_write(k->p, off, n, v);
        if (!(was & (CFG0_EN | CFG0_FLASHBOOT)) && wdt_on(k)) {
            k->stage = 0;
            k->wdt_from = k->slow;
        }
    } else {
        esk_rf_write(k->p, off, n, v);
    }
    if (off == TIME_UPDATE && (v & (1u << 31))) {
        u64 t = k->slow & 0xffffffffffffull;
        hw(k, TIME0, (u32)t);
        hw(k, TIME1, (u32)(t >> 32));
        hw(k, TIME_UPDATE, (reg(k, TIME_UPDATE) & ~(1u << 31)) | (1u << 30));
    }
    if (off == CLK_CONF)
        k->s->yield = 1; /* SOC_CLK_SEL: the CPU's clock (apbctrl.c) */
    if (off == OPTIONS0 || off == SW_CPU_STALL)
        stalls(k);
    if (off == OPTIONS0) {
        u32 o = reg(k, OPTIONS0);
        hw(k, OPTIONS0, o & ~(OPT0_SW_SYS_RST | OPT0_SW_PROCPU_RST | OPT0_SW_APPCPU_RST));
        if (v & OPT0_SW_SYS_RST) {
            esk_esp32_reset(k->s, 3);
            return;
        }
        if (v & OPT0_SW_APPCPU_RST)
            esk_esp32_cpu_reset(k->s, 1, 12);
        if (v & OPT0_SW_PROCPU_RST)
            esk_esp32_cpu_reset(k->s, 0, 12);
    }
    expire(k);
}

static void rt_tick(void *ctx, u32 cycles)
{
    struct e32_rtc *k = (struct e32_rtc *)ctx;
    ES_UNUSED(cycles);
    sync(k);
    expire(k);
}

static int rt_next(void *ctx, u32 *cycles)
{
    struct e32_rtc *k = (struct e32_rtc *)ctx;
    u64 best = 0, c;
    sync(k);
    if (wdt_on(k)) {
        u64 hold = stage_hold(k, k->stage), done = k->slow - k->wdt_from;
        best = cycles_to(k, hold > done ? hold - done : 1);
    }
    if (reg(k, SLP_TIMER1) & (1u << 16)) {
        u64 alarm = (u64)reg(k, SLP_TIMER0) | (u64)(reg(k, SLP_TIMER1) & 0xffffu) << 32,
            now = k->slow & 0xffffffffffffull;
        c = cycles_to(k, alarm > now ? alarm - now : 1);
        if (!best || c < best)
            best = c;
    }
    if (!best)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static int rtc_domain_lost(int code)
{
    return code == 1 || code == 16;
}

static void rt_reset(void *ctx)
{
    struct e32_rtc *k = (struct e32_rtc *)ctx;
    int chip = k->s->reset_cause == ESK_RESET_POWER || k->s->reset_cause == ESK_RESET_PIN;
    int code = chip ? 1 : k->code[0];
    if (chip || rtc_domain_lost(code)) {
        int i;
        k->slow = 0;
        k->frac = 0;
        for (i = 0; i < k->p->nreg; i++) { /* a CHIP_PU reset too: the RTC domain starts again */
            k->p->reg[i]->value = k->p->reg[i]->reset;
            k->p->reg[i]->once = 0;
        }
    }
    k->stage = 0;
    k->wdt_from = k->slow;
    k->base_cycles = 0;
    k->clocked = 0;
    k->level = 0;
    hw(k, RESET_STATE, (reg(k, RESET_STATE) & ~0xfffu) | (u32)code | (u32)(chip ? 1 : k->code[1]) << 6);
    k->code[0] = k->code[1] = 1;
    k->p->backup = 1; /* kept through the next system reset */
    update(k);
    clocking(k);
}

const struct esk_dev_ops esk_esp32_rtc_ops = {
    "esp32-rtc", rt_read, rt_write, rt_reset, rt_tick, rt_next,
};

void *esk_esp32_rtc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_rtc *k = (struct e32_rtc *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    k->s = s;
    k->p = p;
    k->code[0] = k->code[1] = 1;
    return k;
}

/* a reset of the whole digital system (and of the RTC domain for 1, 16) */
void esk_esp32_reset(struct esk_mach *m, int code)
{
    struct e32_rtc *k = rtc_of(m);
    if (k) {
        k->code[0] = k->code[1] = code;
        if (rtc_domain_lost(code))
            k->p->backup = 0;
    }
    m->reset_cpu_only = 0;
    m->reset_cause = code == 3 ? ESK_RESET_SOFTWARE : ESK_RESET_WATCHDOG;
    m->reset_req = 1;
    m->yield = 1;
}

/* one CPU alone: it restarts at its reset vector and reports `code` */
void esk_esp32_cpu_reset(struct esk_mach *m, int core, int code)
{
    struct e32_rtc *k = rtc_of(m);
    if (k) {
        u32 rs = reg(k, RESET_STATE);
        if (core == 0)
            rs = (rs & ~0x3fu) | (u32)code;
        else
            rs = (rs & ~0xfc0u) | (u32)code << 6;
        hw(k, RESET_STATE, rs);
    }
    esk_lx6_core_restart(m, core);
}

void esk_esp32_rtc_reset_cause(struct esk_mach *m, int core, int code)
{
    struct e32_rtc *k = rtc_of(m);
    if (k) {
        u32 rs = reg(k, RESET_STATE);
        rs = core == 0 ? (rs & ~0x3fu) | (u32)code : (rs & ~0xfc0u) | (u32)code << 6;
        hw(k, RESET_STATE, rs);
    }
}
