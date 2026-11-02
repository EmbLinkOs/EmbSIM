/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rtc.c -- the RP2040's real-time clock (its datasheet, 4.8).
 *
 *   - its reference is clk_rtc divided by CLKDIV_M1 + 1: a second each time
 *     that many clk_rtc edges have gone (the SDK sets CLKDIV_M1 to
 *     clk_rtc's frequency less one, for 1 Hz);
 *   - CTRL.RTC_ENABLE runs it and RTC_ACTIVE says it runs (while clk_rtc
 *     does); CTRL.LOAD takes SETUP_0 and SETUP_1 (year, month, day, day of
 *     the week, hour, minute, second) as the time;
 *   - each second the time goes on as a calendar does: 60 seconds, 60
 *     minutes, 24 hours, the month's days (February's 29 in a year
 *     divisible by 4 unless FORCE_NOTLEAPYEAR), 12 months; the day of the
 *     week 0 to 6 with the day;
 *   - RTC_1 and RTC_0 read the time;
 *   - the alarm: with IRQ_SETUP_0.MATCH_ENA, INTR.RTC is set while every
 *     field whose _ENA bit is set equals the time (MATCH_ACTIVE says the
 *     alarm is armed), and RTC_IRQ follows INTS (INTR or INTF, and INTE).
 * Omissions: RTC_ACTIVE follows RTC_ENABLE at once (the part takes a few
 * clk_rtc edges to cross its clock domains); out-of-range values loaded are
 * counted as they are, not checked. */
#include <string.h>

#include "mcu/rp2040/rp2040.h"

struct rtc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *clkdiv, *setup0, *setup1, *ctrl, *irq0, *irq1, *rtc1, *rtc0, *intr, *inte, *intf, *ints;
    int year, month, day, dotw, hour, min, sec;
    u64 acc; /* clk_rtc edges, in units of 1/clk_sys */
    int running, irq, irq_was;
};

static u32 val(const struct esk_rf_reg *r) { return (u32)r->value; }

static int leap(const struct rtc *r)
{
    return r->year % 4 == 0 && !(val(r->ctrl) & 0x100u);
}

static int days_in(const struct rtc *r)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (r->month < 1 || r->month > 12)
        return 31;
    return r->month == 2 && leap(r) ? 29 : d[r->month - 1];
}

/* the time into RTC_1 and RTC_0, the alarm into INTR, MATCH_ACTIVE */
static void show(struct rtc *r)
{
    u32 i0 = val(r->irq0), i1 = val(r->irq1), match = 0, ints;
    esk_rf_hw(r->rtc1, ((u32)r->year & 0xfffu) << 12 | ((u32)r->month & 0xfu) << 8 | ((u32)r->day & 0x1fu));
    esk_rf_hw(r->rtc0, ((u32)r->dotw & 7u) << 24 | ((u32)r->hour & 0x1fu) << 16 | ((u32)r->min & 0x3fu) << 8 |
                           ((u32)r->sec & 0x3fu));
    esk_rf_hw(r->irq0, (i0 & ~(1u << 29)) | ((i0 & (1u << 28)) ? 1u << 29 : 0));
    if (i0 & (1u << 28)) {
        match = 1;
        if ((i0 & (1u << 26)) && (int)(i0 >> 12 & 0xfffu) != r->year)
            match = 0;
        if ((i0 & (1u << 25)) && (int)(i0 >> 8 & 0xfu) != r->month)
            match = 0;
        if ((i0 & (1u << 24)) && (int)(i0 & 0x1fu) != r->day)
            match = 0;
        if ((i1 & (1u << 31)) && (int)(i1 >> 24 & 7u) != r->dotw)
            match = 0;
        if ((i1 & (1u << 30)) && (int)(i1 >> 16 & 0x1fu) != r->hour)
            match = 0;
        if ((i1 & (1u << 29)) && (int)(i1 >> 8 & 0x3fu) != r->min)
            match = 0;
        if ((i1 & (1u << 28)) && (int)(i1 & 0x3fu) != r->sec)
            match = 0;
    }
    esk_rf_hw(r->intr, match);
    ints = (match | (val(r->intf) & 1u)) & val(r->inte) & 1u;
    esk_rf_hw(r->ints, ints);
    esk_rf_irq_level(r->p, r->irq, (int)ints, &r->irq_was);
}

static void second(struct rtc *r)
{
    if (++r->sec < 60)
        return;
    r->sec = 0;
    if (++r->min < 60)
        return;
    r->min = 0;
    if (++r->hour < 24)
        return;
    r->hour = 0;
    r->dotw = (r->dotw + 1) % 7;
    if (++r->day <= days_in(r))
        return;
    r->day = 1;
    if (++r->month <= 12)
        return;
    r->month = 1;
    r->year++;
}

/* clk_sys cycles to the next second, 0 while stopped */
static u64 per_second(const struct rtc *r, u64 *rtc_hz)
{
    u64 sys = esk_rp2040_hz(r->s, RP_CLK_SYS);
    *rtc_hz = esk_rp2040_hz(r->s, RP_CLK_RTC);
    if (!sys || !*rtc_hz || !(val(r->ctrl) & 1u))
        return 0;
    return sys * ((u64)(val(r->clkdiv) & 0xffffu) + 1u);
}

static void clock_it(struct rtc *r)
{
    u64 hz;
    esk_rf_clock(r->p, &r->running, per_second(r, &hz) != 0);
    /* RTC_ACTIVE: the clock runs */
    esk_rf_hw(r->ctrl, (val(r->ctrl) & ~2u) | (r->running ? 2u : 0u));
}

static void rtc_tick(void *ctx, u32 cycles)
{
    struct rtc *r = (struct rtc *)ctx;
    u64 hz, one = per_second(r, &hz);
    int any = 0;
    if (!one)
        return;
    r->acc += (u64)cycles * hz;
    while (r->acc >= one) {
        r->acc -= one;
        second(r);
        any = 1;
    }
    if (any)
        show(r);
}

static int rtc_next(void *ctx, u32 *cycles)
{
    struct rtc *r = (struct rtc *)ctx;
    u64 hz, one = per_second(r, &hz), left;
    if (!one || !hz || !(val(r->irq0) & (1u << 28)))
        return 0; /* nothing comes out of it without an alarm */
    left = (one - r->acc + hz - 1) / hz;
    *cycles = left > 0xffffffffu ? 0xffffffffu : (u32)(left ? left : 1);
    return 1;
}

static u32 rtc_read(void *ctx, u32 off, int n)
{
    struct rtc *r = (struct rtc *)ctx;
    return esk_rf_read(r->p, off, n);
}

static void rtc_write(void *ctx, u32 off, int n, u32 v)
{
    struct rtc *r = (struct rtc *)ctx;
    esk_rf_write(r->p, off, n, v);
    if ((off & ~3u) == 0x0cu && (v & 0x10u)) { /* LOAD: SETUP_0 and SETUP_1 are the time */
        u32 a = val(r->setup0), b = val(r->setup1);
        r->year = (int)(a >> 12 & 0xfffu);
        r->month = (int)(a >> 8 & 0xfu);
        r->day = (int)(a & 0x1fu);
        r->dotw = (int)(b >> 24 & 7u);
        r->hour = (int)(b >> 16 & 0x1fu);
        r->min = (int)(b >> 8 & 0x3fu);
        r->sec = (int)(b & 0x3fu);
        r->acc = 0;
        esk_rf_hw(r->ctrl, val(r->ctrl) & ~0x10u);
    }
    clock_it(r);
    show(r);
}

static void rtc_reset(void *ctx)
{
    struct rtc *r = (struct rtc *)ctx;
    r->year = r->month = r->day = r->dotw = r->hour = r->min = r->sec = 0;
    r->acc = 0;
    clock_it(r);
    show(r);
}

const struct esk_dev_ops esk_rp2040_rtc_ops = { "rp2040-rtc", rtc_read, rtc_write, rtc_reset, rtc_tick, rtc_next };

void *esk_rp2040_rtc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct rtc *r = (struct rtc *)esk_mach_alloc(s, sizeof *r);
    if (!r)
        return 0;
    r->s = s;
    r->p = p;
    r->irq = esk_rf_irq(p, "RTC");
    if (!(r->clkdiv = esk_rf_reg(p, "CLKDIV_M1")) || !(r->setup0 = esk_rf_reg(p, "SETUP_0")) ||
        !(r->setup1 = esk_rf_reg(p, "SETUP_1")) || !(r->ctrl = esk_rf_reg(p, "CTRL")) ||
        !(r->irq0 = esk_rf_reg(p, "IRQ_SETUP_0")) || !(r->irq1 = esk_rf_reg(p, "IRQ_SETUP_1")) ||
        !(r->rtc1 = esk_rf_reg(p, "RTC_1")) || !(r->rtc0 = esk_rf_reg(p, "RTC_0")) ||
        !(r->intr = esk_rf_reg(p, "INTR")) || !(r->inte = esk_rf_reg(p, "INTE")) ||
        !(r->intf = esk_rf_reg(p, "INTF")) || !(r->ints = esk_rf_reg(p, "INTS")))
        return 0;
    return r;
}
