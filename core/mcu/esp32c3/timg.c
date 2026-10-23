/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* timg.c -- the ESP32-C3's timer groups TIMG0 and TIMG1 (TRM v1.4 chapters
 * 11 and 12): in each, the timer T0, the main system watchdog (MWDT) and,
 * in either, the RTC_SLOW_CLK frequency calculation.
 *
 *   - T0 counts TB_CLK, APB_CLK or XTAL_CLK (USE_XTAL) over DIVIDER (0 is
 *     65536, 1 is 2), up or down (INCREASE) over 54 bits while EN is set.
 *     A write to T0UPDATE latches it in T0LO/T0HI (UPDATE reads 0 again at
 *     once); one to T0LOAD loads T0LOADLO/HI. With ALARM_EN it alarms when
 *     it reaches T0ALARMLO/HI, or at once when the alarm is up to 2^53
 *     behind it in its direction (tables 11.2-1 and 11.2-2); the alarm
 *     clears ALARM_EN, sets T0_INT_RAW and reloads with AUTORELOAD.
 *   - The MWDT counts APB_CLK or XTAL_CLK (WDT_USE_XTAL) over
 *     WDT_CLK_PRESCALE through four stages (WDTCONFIG2-5's holds), each's
 *     action off, an interrupt (WDT_INT_RAW), a CPU reset or a core reset
 *     (TRM table 6.1-1: TIMG0's 0x0B and 0x07, TIMG1's 0x11 and 0x08);
 *     WDT_EN runs it at once, while the stages, holds, prescaler, clock
 *     and flash-boot bit take effect when WDT_CONF_UPDATE_EN is written (as
 *     ESP-IDF's mwdt_ll writes it after each). With WDT_FLASHBOOT_MOD_EN,
 *     set out of reset, TIMG0's runs with stage 0 a core reset (12.2.2.4).
 *     WDTFEED returns it to stage 0. While WDTWPROTECT is not 0x50D83AA1
 *     the WDT registers ignore writes.
 *   - RTCCALICFG's RTC_CALI_START counts RTC_CALI_MAX cycles of the clock
 *     RTC_CALI_CLK_SEL names (RC_SLOW_CLK 136 kHz, RC_FAST_DIV_CLK 17.5 MHz
 *     / 256, the board's XTAL32K_CLK) against XTAL_CLK once; when done
 *     RTC_CALI_RDY is set and RTC_CALI_VALUE holds the XTAL_CLK count.
 *     RTC_CALI_START_CYCLING alone counts again and again, each count
 *     setting RTC_CALI_CYCLING_DATA_VLD; it is set out of reset (RC_FAST_DIV
 *     _CLK, one cycle), so a chip calibrates from its start (11.3.4). A
 *     count not done within RTC_CALI_TIMEOUT_THRES XTAL_CLK cycles (a clock
 *     that does not run, a threshold below the count, as ESP-IDF sets to
 *     stop the periodic one) sets RTC_CALI_TIMEOUT and stops.
 *   - INT_ST_TIMERS is INT_RAW and INT_ENA; T0's and the WDT's bits raise
 *     the matrix sources (TIMG0 32 and 33, TIMG1 34 and 35).
 * Inside, the RC oscillators run at their nominal frequencies.
 * Not yet: the reset signals' lengths, REGCLK's clock gates, the
 * DIVCNT_RST prescaler resets (a new divider applies at once). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define XTAL_HZ 40000000u
#define RC_SLOW_HZ 136000u
#define RC_FAST_DIV_HZ (17500000u / 256u)
#define MASK54 ((1ull << 54) - 1u)
#define HALF53 (1ull << 53)
#define WKEY 0x50D83AA1u

#define T0_USE_XTAL (1u << 9)
#define T0_ALARM_EN (1u << 10)
#define T0_AUTORELOAD (1u << 29)
#define T0_INCREASE (1u << 30)
#define T0_EN (1u << 31)

#define W0_FLASHBOOT (1u << 14)
#define W0_USE_XTAL (1u << 21)
#define W0_UPDATE (1u << 22)
#define W0_EN (1u << 31)

#define CALI_CYCLING (1u << 12)
#define CALI_RDY (1u << 15)
#define CALI_START (1u << 31)

#define OFF_T0UPDATE 0x0Cu
#define OFF_T0LOAD 0x20u
#define OFF_WDTFEED 0x60u
#define OFF_WPROTECT 0x64u
#define OFF_INT_CLR 0x7Cu

struct c3_timg {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *t0conf, *lo, *hi, *upd, *alo, *ahi, *llo, *lhi;
    struct esk_rf_reg *wconf[6], *wprot, *cali, *cali1, *cali2, *ena, *raw, *st;
    int group, source, level[2], clocked;
    /* T0 */
    u64 value, t0_frac;
    /* the MWDT's configuration in effect, its stage and count */
    u32 w0, prescale, hold[4];
    int stage;
    u64 wcount, w_frac;
    /* the calibration (1 one-shot, 2 periodic): XTAL_CLK cycles since its
     * start, of `need` */
    int cali_on;
    u64 cali_x, cali_need, x_frac;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void update(struct c3_timg *k)
{
    u32 st = reg(k->raw) & reg(k->ena) & 3u;
    int i;
    esk_rf_hw(k->st, st);
    for (i = 0; i < 2; i++) {
        int l = (int)(st >> i & 1u);
        if (l != k->level[i]) {
            k->level[i] = l;
            esk_esp32c3_source(k->s, k->source + i, l);
        }
    }
}

/* the source clock's ticks over `cycles` of the core, as a fraction kept in
 * `frac` (units of core_hz * div) */
static u64 ticks(struct c3_timg *k, u32 cycles, u32 src_hz, u32 div, u64 *frac)
{
    u64 hz = (u64)esk_mach_core_hz(k->s) * div, n;
    if (!hz)
        return 0;
    *frac += (u64)cycles * src_hz;
    n = *frac / hz;
    *frac %= hz;
    return n;
}

static u32 t0_src(const struct c3_timg *k)
{
    return reg(k->t0conf) & T0_USE_XTAL ? XTAL_HZ : esk_esp32c3_apb_hz(k->s);
}

static u32 t0_div(const struct c3_timg *k)
{
    u32 d = (reg(k->t0conf) >> 13) & 0xffffu;
    return d == 0 ? 65536u : d == 1 ? 2u : d;
}

static u64 alarm_value(const struct c3_timg *k)
{
    return ((u64)(reg(k->ahi) & 0x3fffffu) << 32 | reg(k->alo)) & MASK54;
}

static void t0_alarm(struct c3_timg *k)
{
    esk_rf_hw(k->t0conf, reg(k->t0conf) & ~T0_ALARM_EN);
    esk_rf_hw(k->raw, reg(k->raw) | 1u);
    if (reg(k->t0conf) & T0_AUTORELOAD)
        k->value = ((u64)(reg(k->lhi) & 0x3fffffu) << 32 | reg(k->llo)) & MASK54;
}

/* the alarm's distance ahead in the counting direction (0: due now) */
static u64 t0_ahead(const struct c3_timg *k)
{
    u64 a = alarm_value(k);
    u64 d = reg(k->t0conf) & T0_INCREASE ? (a - k->value) & MASK54 : (k->value - a) & MASK54;
    return d > HALF53 ? 0 : d; /* scenarios 1 and 5: behind, at once */
}

static void t0_run(struct c3_timg *k, u32 cycles)
{
    u64 n;
    if (!(reg(k->t0conf) & T0_EN))
        return;
    n = ticks(k, cycles, t0_src(k), t0_div(k), &k->t0_frac);
    while (n) {
        u64 step = n;
        if (reg(k->t0conf) & T0_ALARM_EN) {
            u64 d = t0_ahead(k);
            if (d == 0) {
                t0_alarm(k);
                continue;
            }
            if (d <= n)
                step = d;
        }
        k->value = (reg(k->t0conf) & T0_INCREASE ? k->value + step : k->value - step) & MASK54;
        n -= step;
        if ((reg(k->t0conf) & T0_ALARM_EN) && t0_ahead(k) == 0)
            t0_alarm(k);
    }
}

/* ---- the MWDT ---------------------------------------------------------------- */

static int flashboot(const struct c3_timg *k) { return k->group == 0 && (k->w0 & W0_FLASHBOOT); }

static int wdt_on(const struct c3_timg *k) { return (reg(k->wconf[0]) & W0_EN) || flashboot(k); }

static u32 stage_action(const struct c3_timg *k, int s)
{
    if (s == 0 && flashboot(k))
        return 3; /* the flash boot protection: a core reset */
    return (k->w0 >> (29 - 2 * s)) & 3u;
}

static void wdt_expire(struct c3_timg *k)
{
    u32 act = stage_action(k, k->stage);
    k->wcount = 0;
    k->stage = (k->stage + 1) & 3;
    if (act == 1) {
        esk_rf_hw(k->raw, reg(k->raw) | 2u);
    } else if (act == 2) {
        esk_esp32c3_reset(k->s, k->group ? 0x11 : 0x0B);
    } else if (act == 3) {
        esk_esp32c3_reset(k->s, k->group ? 0x08 : 0x07);
    }
}

static void wdt_run(struct c3_timg *k, u32 cycles)
{
    u64 n;
    int guard = 8;
    if (!wdt_on(k))
        return;
    n = ticks(k, cycles, k->w0 & W0_USE_XTAL ? XTAL_HZ : esk_esp32c3_apb_hz(k->s),
              k->prescale ? k->prescale : 65536u, &k->w_frac);
    while (n && guard--) {
        u64 left = k->hold[k->stage] > k->wcount ? k->hold[k->stage] - k->wcount : 0;
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

static void wdt_config(struct c3_timg *k)
{
    int i;
    k->w0 = reg(k->wconf[0]);
    k->prescale = (reg(k->wconf[1]) >> 16) & 0xffffu;
    for (i = 0; i < 4; i++)
        k->hold[i] = reg(k->wconf[2 + i]);
}

/* ---- the calibration --------------------------------------------------------- */

static u32 cali_hz(const struct c3_timg *k)
{
    u32 sel = (reg(k->cali) >> 13) & 3u;
    return sel == 0 ? RC_SLOW_HZ : sel == 1 ? RC_FAST_DIV_HZ : sel == 2 ? k->s->lse_hz : 0;
}

/* a frequency calculation from now: one-shot (RTC_CALI_START) or periodic
 * (RTC_CALI_START_CYCLING) */
static void cali_start(struct c3_timg *k, int periodic)
{
    u32 hz = cali_hz(k), max = (reg(k->cali) >> 16) & 0x7fffu;
    k->cali_on = periodic ? 2 : 1;
    k->cali_x = 0;
    k->cali_need = hz ? ((u64)max * XTAL_HZ + hz / 2) / hz : 0;
    if (!periodic)
        esk_rf_hw(k->cali, reg(k->cali) & ~CALI_RDY);
    esk_rf_hw(k->cali2, reg(k->cali2) & ~1u);
}

static void cali_run(struct c3_timg *k, u32 cycles)
{
    u64 thres = reg(k->cali2) >> 7; /* RTC_CALI_TIMEOUT_THRES */
    if (!k->cali_on)
        return;
    k->cali_x += ticks(k, cycles, XTAL_HZ, 1, &k->x_frac);
    /* a count not done within the threshold (a clock that does not run, or
     * a threshold below the count) times out */
    if (!k->cali_need || thres < k->cali_need) {
        if (k->cali_x >= thres) {
            k->cali_on = 0;
            esk_rf_hw(k->cali2, reg(k->cali2) | 1u);
        }
        return;
    }
    if (k->cali_x < k->cali_need)
        return;
    if (k->cali_on == 2) { /* periodic: CYCLING_DATA_VLD, and again */
        esk_rf_hw(k->cali1, (u32)(k->cali_need & 0x1ffffffu) << 7 | 1u);
        k->cali_x %= k->cali_need;
    } else {
        esk_rf_hw(k->cali1, (u32)(k->cali_need & 0x1ffffffu) << 7);
        esk_rf_hw(k->cali, reg(k->cali) | CALI_RDY);
        k->cali_on = 0;
    }
}

/* ---- the device ------------------------------------------------------------------ */

static void clocking(struct c3_timg *k)
{
    esk_rf_clock(k->p, &k->clocked, (reg(k->t0conf) & T0_EN) || wdt_on(k) || k->cali_on);
}

static void tg_tick(void *ctx, u32 cycles)
{
    struct c3_timg *k = (struct c3_timg *)ctx;
    t0_run(k, cycles);
    wdt_run(k, cycles);
    cali_run(k, cycles);
    update(k);
    clocking(k);
}

static u64 to_cycles(const struct c3_timg *k, u64 n, u32 src_hz, u32 div, u64 frac)
{
    u64 hz = (u64)esk_mach_core_hz(k->s) * div;
    if (!src_hz || !hz || n > 0xffffffffffffffffull / hz)
        return 0xffffffffu; /* far: asked again by then */
    return (n * hz - frac + src_hz - 1u) / src_hz;
}

static int tg_next(void *ctx, u32 *cycles)
{
    struct c3_timg *k = (struct c3_timg *)ctx;
    u64 best = 0, c;
    int any = 0;
    if ((reg(k->t0conf) & T0_EN) && (reg(k->t0conf) & T0_ALARM_EN)) {
        u64 d = t0_ahead(k);
        best = d ? to_cycles(k, d, t0_src(k), t0_div(k), k->t0_frac) : 1;
        any = 1;
    }
    if (wdt_on(k)) {
        u64 left = k->hold[k->stage] > k->wcount ? k->hold[k->stage] - k->wcount : 1;
        c = to_cycles(k, left, k->w0 & W0_USE_XTAL ? XTAL_HZ : esk_esp32c3_apb_hz(k->s),
                      k->prescale ? k->prescale : 65536u, k->w_frac);
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
    struct c3_timg *k = (struct c3_timg *)ctx;
    return esk_rf_read(k->p, off, n);
}

static int wdt_reg(const struct c3_timg *k, u32 off)
{
    return (off >= esk_rf_off(k->wconf[0]) && off <= esk_rf_off(k->wconf[5])) || off == OFF_WDTFEED;
}

static void tg_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_timg *k = (struct c3_timg *)ctx;
    u32 conf0 = reg(k->t0conf), cali0 = reg(k->cali);
    if (wdt_reg(k, off) && reg(k->wprot) != WKEY)
        return; /* write-protected */
    esk_rf_write(k->p, off, n, v);
    if (off == OFF_T0UPDATE) {
        esk_rf_hw(k->lo, (u32)k->value);
        esk_rf_hw(k->hi, (u32)(k->value >> 32) & 0x3fffffu);
        esk_rf_hw(k->upd, 0); /* the latch is done */
    } else if (off == OFF_T0LOAD) {
        k->value = ((u64)(reg(k->lhi) & 0x3fffffu) << 32 | reg(k->llo)) & MASK54;
    } else if (off == esk_rf_off(k->t0conf)) {
        if ((reg(k->t0conf) & T0_ALARM_EN) && !(conf0 & T0_ALARM_EN) && t0_ahead(k) == 0)
            t0_alarm(k);
    } else if (off == OFF_WDTFEED) {
        k->stage = 0;
        k->wcount = 0;
    } else if (off == esk_rf_off(k->wconf[0]) && (v & W0_UPDATE)) {
        wdt_config(k);
    } else if (off == esk_rf_off(k->cali)) {
        u32 now = reg(k->cali);
        if ((now & CALI_START) && !(cali0 & CALI_START))
            cali_start(k, 0);
        else if ((now & CALI_CYCLING) && !(cali0 & CALI_CYCLING) && !(now & CALI_START))
            cali_start(k, 1);
        else if (!(now & (CALI_START | CALI_CYCLING)) || (k->cali_on == 2 && !(now & CALI_CYCLING)))
            k->cali_on = 0;
    } else if (off == OFF_INT_CLR) {
        esk_rf_hw(k->raw, reg(k->raw) & ~(v & 3u));
    }
    update(k);
    clocking(k);
}

static void tg_reset(void *ctx)
{
    struct c3_timg *k = (struct c3_timg *)ctx;
    k->value = k->t0_frac = 0;
    k->stage = 0;
    k->wcount = k->w_frac = 0;
    k->cali_on = 0;
    k->cali_x = k->x_frac = 0;
    k->clocked = 0; /* the machine's reset stopped every clock */
    if (reg(k->cali) & CALI_CYCLING) /* out of reset, periodic */
        cali_start(k, 1);
    wdt_config(k);
    update(k);
    clocking(k);
}

const struct esk_dev_ops esk_esp32c3_timg_ops = {
    "esp32c3-timg", tg_read, tg_write, tg_reset, tg_tick, tg_next,
};

void *esk_esp32c3_timg_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_timg *k = (struct c3_timg *)esk_mach_alloc(s, sizeof *k);
    static const char *const wnames[6] = { "WDTCONFIG0", "WDTCONFIG1", "WDTCONFIG2",
                                           "WDTCONFIG3", "WDTCONFIG4", "WDTCONFIG5" };
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->group = strcmp(p->name, "TIMG1") == 0;
    k->source = k->group ? 34 : 32;
    k->t0conf = esk_rf_reg(p, "T0CONFIG");
    k->lo = esk_rf_reg(p, "T0LO");
    k->hi = esk_rf_reg(p, "T0HI");
    k->upd = esk_rf_reg(p, "T0UPDATE");
    k->alo = esk_rf_reg(p, "T0ALARMLO");
    k->ahi = esk_rf_reg(p, "T0ALARMHI");
    k->llo = esk_rf_reg(p, "T0LOADLO");
    k->lhi = esk_rf_reg(p, "T0LOADHI");
    k->wprot = esk_rf_reg(p, "WDTWPROTECT");
    k->cali = esk_rf_reg(p, "RTCCALICFG");
    k->cali1 = esk_rf_reg(p, "RTCCALICFG1");
    k->cali2 = esk_rf_reg(p, "RTCCALICFG2");
    k->ena = esk_rf_reg(p, "INT_ENA_TIMERS");
    k->raw = esk_rf_reg(p, "INT_RAW_TIMERS");
    k->st = esk_rf_reg(p, "INT_ST_TIMERS");
    for (i = 0; i < 6; i++)
        if (!(k->wconf[i] = esk_rf_reg(p, wnames[i])))
            return 0;
    if (!k->t0conf || !k->lo || !k->hi || !k->upd || !k->alo || !k->ahi || !k->llo || !k->lhi ||
        !k->wprot || !k->cali || !k->cali1 || !k->cali2 || !k->ena || !k->raw || !k->st)
        return 0;
    return k;
}
