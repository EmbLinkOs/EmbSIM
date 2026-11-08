/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* timg.c -- the ESP32's timer groups TIMG0 and TIMG1 (TRM v5 chapters 18
 * and 19): in each, the 64-bit timers T0 and T1, the LAC timer (LACT), the
 * main system watchdog (MWDT) and the RTC_SLOW_CLK calibration. The
 * ESP32-C3's model (mcu/esp32c3/timg.c) on the ESP32's registers.
 *
 *   - T0, T1 and LACT count APB_CLK over DIVIDER (0 is 65536, 1 is 2), up
 *     or down (INCREASE) over 64 bits while EN is set. A write to UPDATE
 *     latches the count in LO/HI; one to LOAD loads LOADLO/HI. With
 *     ALARM_EN a timer alarms when it reaches ALARMLO/HI, or at once when
 *     the alarm is up to 2^63 behind it in its direction; the alarm clears
 *     ALARM_EN, sets its INT_RAW bit and reloads with AUTORELOAD.
 *   - The MWDT counts APB_CLK over WDT_CLK_PRESCALE through four stages
 *     (WDTCONFIG2-5's holds), each's action off, an interrupt (WDT's
 *     INT_RAW bit), a CPU reset (TGWDT_CPU_RESET, 11) or a system reset
 *     (TG0WDT_SYS_RESET 7, TG1WDT_SYS_RESET 8); WDT_EN runs it. With
 *     WDT_FLASHBOOT_MOD_EN, set out of reset in TIMG0, it runs with stage
 *     0 a system reset. WDTFEED returns it to stage 0. While WDTWPROTECT
 *     is not 0x50D83AA1 the WDT registers ignore writes.
 *   - RTCCALICFG's START counts MAX cycles of the clock CLK_SEL names
 *     (RTC_SLOW_CLK as RTC_CNTL selects it, 8MD256's 31.25 kHz, the
 *     board's 32.768 kHz crystal) against the 40 MHz crystal once; when
 *     done RDY is set and RTCCALICFG1's VALUE holds the crystal's count;
 *     a MAX of 0 sets RDY at the clock's next cycle (taken one period
 *     away). START_CYCLING counts again and again. A clock that does not run (no
 *     32 kHz crystal) never makes RDY.
 *   - INT_ST is INT_RAW and INT_ENA; each bit drives its level source
 *     (TIMG0's T0 14, T1 15, WDT 16, LACT 17; TIMG1's 18-21), and an alarm
 *     whose EDGE_INT_EN is set pulses its edge source (TIMG0 51-54, TIMG1
 *     55-58).
 * Inside, the RC oscillators run at their nominal frequencies.
 * Not yet: LACT's RTC step mode (LACTRTC, RTC_ONLY, CPST_EN: it counts
 * APB_CLK), the reset signals' lengths. */
#include <string.h>

#include "mcu/esp32/soc/esp32.h"

#define HALF63 (1ull << 63)
#define WKEY 0x50D83AA1u

#define T_ALARM_EN (1u << 10)
#define T_LEVEL_INT_EN (1u << 11)
#define T_EDGE_INT_EN (1u << 12)
#define T_AUTORELOAD (1u << 29)
#define T_INCREASE (1u << 30)
#define T_EN (1u << 31)

#define W0_FLASHBOOT (1u << 14)
#define W0_EN (1u << 31)

#define CALI_CYCLING (1u << 12)
#define CALI_RDY (1u << 15)
#define CALI_START (1u << 31)

#define WDTCONFIG0 0x48u
#define WDTFEED 0x60u
#define WPROTECT 0x64u
#define RTCCALICFG 0x68u
#define RTCCALICFG1 0x6Cu
#define INT_ENA 0x98u
#define INT_RAW 0x9Cu
#define INT_ST 0xA0u
#define INT_CLR 0xA4u

/* each timer's registers: config, lo, hi, update, alarm lo/hi, load lo/hi, load */
static const u32 tbase[3] = { 0x00, 0x24, 0x70 };

struct e32_timg {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    int group, level[4], clocked;
    u64 value[3], frac[3];
    int stage;
    u64 wcount, w_frac;
    int cali_on;
    u64 cali_x, cali_need, x_frac;
    u64 cali_wait; /* MAX 0: the crystal's cycles to the next slow cycle */
};

static u32 rd(struct e32_timg *k, u32 off) { return esk_rf_read(k->p, off, 4); }

static void hw(struct e32_timg *k, u32 off, u32 v)
{
    struct esk_rf_reg *r = esk_rf_reg_at(k->p, off);
    if (r)
        esk_rf_hw(r, v);
}

/* a timer's register: 0 config, 1 lo, 2 hi, 3 update, 4/5 alarm, 6/7 load, 8 load trigger */
static u32 toff(int t, int r)
{
    return tbase[t] + (t == 2 && r > 0 ? 8u : 0u) + 4u * (u32)r - (t == 2 && r > 0 ? 4u : 0u);
}

static void update(struct e32_timg *k)
{
    u32 st = rd(k, INT_RAW) & rd(k, INT_ENA) & 15u;
    int i;
    hw(k, INT_ST, st);
    for (i = 0; i < 4; i++) {
        int l = (int)(st >> i & 1u);
        if (l != k->level[i]) {
            k->level[i] = l;
            esk_esp32_source(k->s, (k->group ? 18 : 14) + i, l);
        }
    }
}

static void edge(struct e32_timg *k, int i)
{
    int src = (k->group ? 55 : 51) + i;
    esk_esp32_source(k->s, src, 1);
    esk_esp32_source(k->s, src, 0);
}

static u64 ticks(struct e32_timg *k, u32 cycles, u32 src_hz, u32 div, u64 *frac)
{
    u64 hz = (u64)esk_mach_core_hz(k->s) * div, n;
    if (!hz)
        return 0;
    *frac += (u64)cycles * src_hz;
    n = *frac / hz;
    *frac %= hz;
    return n;
}

static u32 t_div(struct e32_timg *k, int t)
{
    u32 d = (rd(k, toff(t, 0)) >> 13) & 0xffffu;
    return d == 0 ? 65536u : d == 1 ? 2u : d;
}

static u64 pair(struct e32_timg *k, u32 lo)
{
    return (u64)rd(k, lo + 4u) << 32 | rd(k, lo);
}

static void t_alarm(struct e32_timg *k, int t)
{
    u32 c = rd(k, toff(t, 0));
    hw(k, toff(t, 0), c & ~T_ALARM_EN);
    hw(k, INT_RAW, rd(k, INT_RAW) | (t == 2 ? 8u : 1u << t));
    if (c & T_AUTORELOAD)
        k->value[t] = pair(k, toff(t, 6));
    if (c & T_EDGE_INT_EN)
        edge(k, t == 2 ? 3 : t);
}

static u64 t_ahead(struct e32_timg *k, int t)
{
    u64 a = pair(k, toff(t, 4)), d;
    d = rd(k, toff(t, 0)) & T_INCREASE ? a - k->value[t] : k->value[t] - a;
    return d > HALF63 ? 0 : d;
}

static void t_run(struct e32_timg *k, int t, u32 cycles)
{
    u64 n;
    if (!(rd(k, toff(t, 0)) & T_EN))
        return;
    n = ticks(k, cycles, esk_esp32_apb_hz(k->s), t_div(k, t), &k->frac[t]);
    while (n) {
        u64 step = n;
        u32 c = rd(k, toff(t, 0));
        if (c & T_ALARM_EN) {
            u64 d = t_ahead(k, t);
            if (d == 0) {
                t_alarm(k, t);
                continue;
            }
            if (d <= n)
                step = d;
        }
        k->value[t] = c & T_INCREASE ? k->value[t] + step : k->value[t] - step;
        n -= step;
        if ((rd(k, toff(t, 0)) & T_ALARM_EN) && t_ahead(k, t) == 0)
            t_alarm(k, t);
    }
}

/* ---- the MWDT ------------------------------------------------------------ */

static u32 w0(struct e32_timg *k) { return rd(k, WDTCONFIG0); }
static int flashboot(struct e32_timg *k) { return k->group == 0 && (w0(k) & W0_FLASHBOOT); }
static int wdt_on(struct e32_timg *k) { return (w0(k) & W0_EN) || flashboot(k); }
static u32 prescale(struct e32_timg *k)
{
    u32 p = (rd(k, WDTCONFIG0 + 4u) >> 16) & 0xffffu;
    return p ? p : 65536u;
}
static u64 hold(struct e32_timg *k, int s) { return rd(k, WDTCONFIG0 + 8u + 4u * (u32)s); }

static u32 stage_action(struct e32_timg *k, int s)
{
    if (s == 0 && flashboot(k))
        return 3;
    return (w0(k) >> (29 - 2 * s)) & 3u;
}

static void wdt_expire(struct e32_timg *k)
{
    u32 act = stage_action(k, k->stage);
    k->wcount = 0;
    k->stage = (k->stage + 1) & 3;
    if (act == 1) {
        hw(k, INT_RAW, rd(k, INT_RAW) | 4u);
        if (w0(k) & (1u << 22))
            edge(k, 2);
    } else if (act == 2) {
        esk_esp32_cpu_reset(k->s, 0, 11);
        esk_esp32_cpu_reset(k->s, 1, 11);
    } else if (act == 3) {
        esk_esp32_reset(k->s, k->group ? 8 : 7);
    }
}

static void wdt_run(struct e32_timg *k, u32 cycles)
{
    u64 n;
    int guard = 8;
    if (!wdt_on(k))
        return;
    n = ticks(k, cycles, esk_esp32_apb_hz(k->s), prescale(k), &k->w_frac);
    while (n && guard--) {
        u64 h = hold(k, k->stage), left = h > k->wcount ? h - k->wcount : 0;
        if (n < left) {
            k->wcount += n;
            return;
        }
        n -= left;
        wdt_expire(k);
        if (k->s->reset_req)
            return;
    }
}

/* ---- the calibration ----------------------------------------------------- */

static double cali_hz(struct e32_timg *k)
{
    u32 sel = (rd(k, RTCCALICFG) >> 13) & 3u;
    return sel == 0 ? esk_esp32_rtc_slow_hz(k->s) : sel == 1 ? 8000000.0 / 256.0 : (double)k->s->lse_hz;
}

static void cali_start(struct e32_timg *k, int periodic)
{
    double hz = cali_hz(k);
    u32 max = (rd(k, RTCCALICFG) >> 16) & 0x7fffu;
    k->cali_on = periodic ? 2 : 1;
    k->cali_x = 0;
    k->cali_need = hz > 0 ? (u64)((double)max * ESP32_XTAL_HZ / hz + 0.5) : 0;
    k->cali_wait = 0;
    if (hz > 0 && max == 0) {
        /* none counted: RDY at the clock's next cycle (ESP-IDF's
         * rtc_clk_wait_for_slow_cycle), taken as one period away */
        k->cali_wait = (u64)((double)ESP32_XTAL_HZ / hz + 0.5);
    }
    if (!periodic)
        hw(k, RTCCALICFG, rd(k, RTCCALICFG) & ~CALI_RDY);
}

static u64 cali_end(struct e32_timg *k)
{
    return k->cali_need ? k->cali_need : k->cali_wait;
}

static void cali_run(struct e32_timg *k, u32 cycles)
{
    if (!k->cali_on || !cali_end(k))
        return;
    k->cali_x += ticks(k, cycles, ESP32_XTAL_HZ, 1, &k->x_frac);
    if (k->cali_x < cali_end(k))
        return;
    hw(k, RTCCALICFG1, (u32)(k->cali_need & 0x1ffffffu) << 7);
    hw(k, RTCCALICFG, rd(k, RTCCALICFG) | CALI_RDY);
    if (k->cali_on == 2)
        k->cali_x %= cali_end(k);
    else
        k->cali_on = 0;
}

/* ---- the device ---------------------------------------------------------- */

static int any_on(struct e32_timg *k)
{
    int t;
    for (t = 0; t < 3; t++)
        if (rd(k, toff(t, 0)) & T_EN)
            return 1;
    return wdt_on(k) || (k->cali_on && cali_end(k));
}

static void clocking(struct e32_timg *k)
{
    esk_rf_clock(k->p, &k->clocked, any_on(k));
}

static void tg_tick(void *ctx, u32 cycles)
{
    struct e32_timg *k = (struct e32_timg *)ctx;
    int t;
    for (t = 0; t < 3; t++)
        t_run(k, t, cycles);
    wdt_run(k, cycles);
    cali_run(k, cycles);
    update(k);
    clocking(k);
}

static u64 to_cycles(struct e32_timg *k, u64 n, double src_hz, u64 div, u64 frac)
{
    double c = ((double)n * (double)esk_mach_core_hz(k->s) * (double)div - (double)frac) / src_hz;
    if (src_hz <= 0 || c > 4.0e9)
        return 0xffffffffu;
    return c < 1.0 ? 1u : (u64)c + 1u;
}

static int tg_next(void *ctx, u32 *cycles)
{
    struct e32_timg *k = (struct e32_timg *)ctx;
    u64 best = 0, c;
    int any = 0, t;
    for (t = 0; t < 3; t++) {
        u32 conf = rd(k, toff(t, 0));
        if ((conf & T_EN) && (conf & T_ALARM_EN)) {
            u64 d = t_ahead(k, t);
            c = d ? to_cycles(k, d, (double)esk_esp32_apb_hz(k->s), t_div(k, t), k->frac[t]) : 1;
            if (!any || c < best)
                best = c;
            any = 1;
        }
    }
    if (wdt_on(k)) {
        u64 h = hold(k, k->stage), left = h > k->wcount ? h - k->wcount : 1;
        c = to_cycles(k, left, (double)esk_esp32_apb_hz(k->s), prescale(k), k->w_frac);
        if (!any || c < best)
            best = c;
        any = 1;
    }
    if (k->cali_on && cali_end(k)) {
        u64 left = cali_end(k) > k->cali_x ? cali_end(k) - k->cali_x : 1;
        c = to_cycles(k, left, (double)ESP32_XTAL_HZ, 1, k->x_frac);
        if (!any || c < best)
            best = c;
        any = 1;
    }
    if (!any)
        return 0;
    *cycles = best == 0 ? 1u : best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static u32 tg_read(void *ctx, u32 off, int n)
{
    struct e32_timg *k = (struct e32_timg *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void tg_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_timg *k = (struct e32_timg *)ctx;
    u32 cali0 = rd(k, RTCCALICFG), conf[3];
    int t;
    if (((off >= WDTCONFIG0 && off <= WDTFEED)) && rd(k, WPROTECT) != WKEY)
        return; /* write-protected */
    for (t = 0; t < 3; t++)
        conf[t] = rd(k, toff(t, 0));
    esk_rf_write(k->p, off, n, v);
    for (t = 0; t < 3; t++) {
        if (off == toff(t, 3)) {
            hw(k, toff(t, 1), (u32)k->value[t]);
            hw(k, toff(t, 2), (u32)(k->value[t] >> 32));
        } else if (off == toff(t, 8)) {
            k->value[t] = pair(k, toff(t, 6));
        } else if (off == toff(t, 0)) {
            if ((rd(k, off) & T_ALARM_EN) && !(conf[t] & T_ALARM_EN) && t_ahead(k, t) == 0)
                t_alarm(k, t);
        }
    }
    if (off == WDTFEED) {
        k->stage = 0;
        k->wcount = 0;
    } else if (off == WDTCONFIG0 && !(v & W0_EN) && !(v & W0_FLASHBOOT)) {
        k->stage = 0;
        k->wcount = 0;
    } else if (off == RTCCALICFG) {
        u32 now = rd(k, RTCCALICFG);
        if ((now & CALI_START) && !(cali0 & CALI_START))
            cali_start(k, 0);
        else if ((now & CALI_CYCLING) && !(cali0 & CALI_CYCLING) && !(now & CALI_START))
            cali_start(k, 1);
        else if (!(now & (CALI_START | CALI_CYCLING)))
            k->cali_on = 0;
    } else if (off == INT_CLR) {
        hw(k, INT_RAW, rd(k, INT_RAW) & ~(v & 15u));
    }
    update(k);
    clocking(k);
}

static void tg_reset(void *ctx)
{
    struct e32_timg *k = (struct e32_timg *)ctx;
    int t;
    for (t = 0; t < 3; t++)
        k->value[t] = k->frac[t] = 0;
    k->stage = 0;
    k->wcount = k->w_frac = 0;
    k->cali_on = 0;
    k->cali_x = k->x_frac = 0;
    k->clocked = 0;
    memset(k->level, 0, sizeof k->level);
    update(k);
    clocking(k);
}

const struct esk_dev_ops esk_esp32_timg_ops = {
    "esp32-timg", tg_read, tg_write, tg_reset, tg_tick, tg_next,
};

void *esk_esp32_timg_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_timg *k;
    if (strcmp(p->name, "TIMG0") && strcmp(p->name, "TIMG1"))
        return 0;
    k = (struct e32_timg *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    k->s = s;
    k->p = p;
    k->group = p->name[4] == '1';
    return k;
}
