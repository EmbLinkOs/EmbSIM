/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rtc_f4.c -- the STM32F2/F4's RTC (RM0390 22): a BCD calendar in the
 * backup domain, over the register file.
 *
 *   - RTCCLK is RCC's BDCR choice once RTCEN is set: LSE (the board's
 *     crystal, once LSERDY), LSI (32 kHz) or HSE / CFGR's RTCPRE; it runs
 *     the asynchronous prescaler (PREDIV_A + 1) into ck_apre, which counts
 *     the subsecond downcounter SSR from PREDIV_S to 0, each wrap a second
 *     of the calendar: seconds, minutes, hours (24 h, or 12 h with PM by
 *     CR.FMT), weekday, date, month and year 00-99 with the leap years;
 *   - its registers and its time live in the backup domain: they keep going
 *     through every reset but a power-on one (and BDCR's BDRST); writes
 *     need PWR's DBP, and every register but ISR's flags, TAFCR and the
 *     backup registers WPR's keys (0xCA, 0x53; another write locks again);
 *   - ISR.INIT stops the calendar (INITF two RTCCLK clocks later), resets
 *     the prescalers and opens TR, DR, PRER and CR's FMT, DCE and REFCKON
 *     to writes; leaving it starts the calendar again four RTCCLK clocks
 *     later; INITS is set while the year is not 0;
 *   - TR, DR and SSR are read from shadow registers copied from the
 *     calendar every two RTCCLK clocks (RSF set then; cleared by writing 0,
 *     in INIT and with BYPSHAD); reading SSR or TR locks the higher ones
 *     until DR is read; BYPSHAD reads the calendar itself;
 *   - alarms A and B (ALRMxR, ALRMxSSR): each field matched unless masked,
 *     the date or weekday (WDSEL), and MASKSS's low subsecond bits; ALRxF
 *     sets on the match; ALRMxR is written only while ALRxE is clear
 *     (ALRxWF); ALRxIE raises EXTI line 17;
 *   - the wakeup timer counts WUTR + 1 clocks of RTCCLK / 16, 8, 4 or 2, or
 *     of ck_spre (plus 2^16 with WUCKSEL 11x), setting WUTF each time;
 *     WUTR and WUCKSEL are written while WUTE is clear (WUTWF); WUTIE
 *     raises EXTI line 22;
 *   - RTC_AF1 (PC13), whatever its GPIO mode: the timestamp input (TSE,
 *     TSEDGE: TSTR, TSDR, TSSSR captured, TSF, TSOVF if TSF was still set),
 *     the tamper input (TAMP1E: an edge, or with TAMPFLT a level held for
 *     2, 4 or 8 samples at TAMPFREQ; TAMP1F, the backup registers erased,
 *     with TAMPTS a timestamp too); TSIE and TAMPIE raise EXTI line 21. As
 *     an output (RTC_OUT): alarm A, alarm B or the wakeup flag with POL
 *     (OSEL), else the calibration output (COE: 512 Hz, or 1 Hz with
 *     COSEL), push-pull or open-drain (ALARMOUTTYPE);
 *   - calibration: the smooth calibration (CALR: CALP adds 512 pulses and
 *     CALM masks up to 511 in each 2^20 RTCCLK clocks) and the coarse one
 *     (CALIBR with DCE: +4 ppm or -2 ppm a step) set ck_apre's rate;
 *     SHIFTR delays the subseconds by SUBFS and adds a second with ADD1S;
 *     CR's ADD1H and SUB1H move the hour (BKP keeps a note);
 *   - the twenty backup registers keep their values like the rest.
 *
 * Not modelled: the reference clock detection (REFCKON, RTC_REFIN on
 * PB15), RTC_AF2 (PI8, not bonded in the LQFP64), the tamper input's
 * precharge and pull-up (TAMPPRCH, TAMPPUDIS: the pin's level is the
 * circuit's), the calibration windows' 8 and 16 s variants (CALW8, CALW16:
 * the rate is applied evenly), RECALPF's wait, the low-power modes it
 * wakes from (the interrupts reach EXTI as on the part). */
#include <string.h>

#include "mcu/stm32/stm32.h"

#define CR_WUCKSEL 7u
#define CR_BYPSHAD (1u << 5)
#define CR_REFCKON (1u << 4)
#define CR_FMT (1u << 6)
#define CR_DCE (1u << 7)
#define CR_ALRAE (1u << 8)
#define CR_ALRBE (1u << 9)
#define CR_WUTE (1u << 10)
#define CR_TSE (1u << 11)
#define CR_ALRAIE (1u << 12)
#define CR_ALRBIE (1u << 13)
#define CR_WUTIE (1u << 14)
#define CR_TSIE (1u << 15)
#define CR_ADD1H (1u << 16)
#define CR_SUB1H (1u << 17)
#define CR_BKP (1u << 18)
#define CR_COSEL (1u << 19)
#define CR_POL (1u << 20)
#define CR_COE (1u << 23)
#define CR_TSEDGE (1u << 3)
#define ISR_ALRAWF (1u << 0)
#define ISR_ALRBWF (1u << 1)
#define ISR_WUTWF (1u << 2)
#define ISR_SHPF (1u << 3)
#define ISR_INITS (1u << 4)
#define ISR_RSF (1u << 5)
#define ISR_INITF (1u << 6)
#define ISR_INIT (1u << 7)
#define ISR_ALRAF (1u << 8)
#define ISR_ALRBF (1u << 9)
#define ISR_WUTF (1u << 10)
#define ISR_TSF (1u << 11)
#define ISR_TSOVF (1u << 12)
#define ISR_TAMP1F (1u << 13)
#define ISR_TAMP2F (1u << 14)
#define ISR_RECALPF (1u << 16)
#define ISR_FLAGS 0x7f00u               /* the rc_w0 flags */
#define TAFCR_TAMP1E (1u << 0)
#define TAFCR_TAMP1TRG (1u << 1)
#define TAFCR_TAMPIE (1u << 2)
#define TAFCR_TAMPTS (1u << 7)
#define TAFCR_ALARMOUTTYPE (1u << 18)
#define LSI_HZ 32000u

struct cal {
    int year, month, date, wday, hour, min, sec; /* 24 h inside */
};

struct rtc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *tr, *dr, *cr, *isr, *prer, *wutr, *calibr, *alrm[2], *wpr, *ssr, *shiftr;
    struct esk_rf_reg *tstr, *tsdr, *tsssr, *calr, *tafcr, *alrmss[2], *bkp[20];
    int running;
    struct cal c;           /* the calendar */
    u32 ss;                 /* the subsecond downcounter */
    u64 apre;               /* RTCCLK clocks toward the next ck_apre */
    double frac;            /* RTCCLK clocks short of a whole one */
    u64 seen;               /* the machine's cycles last counted */
    u32 rate;               /* the core clock they were counted at */
    u32 hz_last;            /* RTCCLK as last seen (at a reset, BDCR is RCC's to restore) */
    u32 restart;            /* RTCCLK clocks before the calendar counts again after INIT */
    int unlock;             /* WPR's keys: 2 unlocked */
    int lock_dr, lock_tr;   /* TR or SSR read: the higher shadows held until DR is read */
    int pend_flags;         /* ISR flags due after two RTCCLK clocks */
    u32 pend_bits;
    u32 pend_ticks;
    u32 wut;                /* the wakeup counter */
    u64 wut_frac;           /* RTCCLK clocks toward its next count */
    int af1;                /* PC13's level, -1 unknown */
    u64 tamp_ticks;         /* RTCCLK clocks the tamper level has held */
    int tamp_armed;
    int out;                /* RTC_OUT's level */
    u64 cal_ticks;          /* the calibration output's RTCCLK clocks */
};

static u32 reg(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }
static u32 bcd(int v) { return (u32)(v / 10 << 4 | v % 10); }
static int unbcd(u32 v) { return (int)((v >> 4 & 15u) * 10u + (v & 15u)); }

static int leap(int y) { return y % 4 == 0; } /* 2000 to 2099 */

static int days_in(int y, int m)
{
    static const int d[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && leap(y) ? 29 : d[m < 1 || m > 12 ? 1 : m];
}

/* ---- the calendar ------------------------------------------------------------------ */

static void next_day(struct cal *c)
{
    c->wday = c->wday % 7 + 1;
    if (++c->date > days_in(c->year, c->month)) {
        c->date = 1;
        if (++c->month > 12) {
            c->month = 1;
            c->year = (c->year + 1) % 100;
        }
    }
}

static void prev_day(struct cal *c)
{
    c->wday = c->wday <= 1 ? 7 : c->wday - 1;
    if (--c->date < 1) {
        if (--c->month < 1) {
            c->month = 12;
            c->year = (c->year + 99) % 100;
        }
        c->date = days_in(c->year, c->month);
    }
}

static void add_seconds(struct cal *c, u64 n)
{
    u64 t = (u64)c->hour * 3600u + (u64)c->min * 60u + (u64)c->sec + n;
    while (t >= 86400u) {
        t -= 86400u;
        next_day(c);
    }
    c->hour = (int)(t / 3600u);
    c->min = (int)(t / 60u % 60u);
    c->sec = (int)(t % 60u);
}

static void add_hours(struct cal *c, int h)
{
    c->hour += h;
    if (c->hour >= 24) {
        c->hour -= 24;
        next_day(c);
    } else if (c->hour < 0) {
        c->hour += 24;
        prev_day(c);
    }
}

static u32 tr_of(const struct rtc *k, const struct cal *c)
{
    int h = c->hour;
    u32 pm = 0;
    if (reg(k->cr) & CR_FMT) {
        pm = h >= 12;
        h = h % 12 == 0 ? 12 : h % 12;
    }
    return pm << 22 | bcd(h) << 16 | bcd(c->min) << 8 | bcd(c->sec);
}

static u32 dr_of(const struct cal *c)
{
    return bcd(c->year) << 16 | (u32)c->wday << 13 | bcd(c->month) << 8 | bcd(c->date);
}

static void set_tr(struct rtc *k, u32 v)
{
    int h = unbcd(v >> 16 & 0x3fu);
    if (reg(k->cr) & CR_FMT)
        h = (h % 12) + ((v >> 22 & 1u) ? 12 : 0);
    k->c.hour = h % 24;
    k->c.min = unbcd(v >> 8 & 0x7fu) % 60;
    k->c.sec = unbcd(v & 0x7fu) % 60;
}

static void set_dr(struct rtc *k, u32 v)
{
    k->c.year = unbcd(v >> 16 & 0xffu) % 100;
    k->c.wday = (int)(v >> 13 & 7u);
    if (!k->c.wday)
        k->c.wday = 1; /* 000 is forbidden */
    k->c.month = unbcd(v >> 8 & 0x1fu);
    if (k->c.month < 1 || k->c.month > 12)
        k->c.month = 1;
    k->c.date = unbcd(v & 0x3fu);
    if (k->c.date < 1)
        k->c.date = 1;
}

/* ---- clocks ------------------------------------------------------------------------- */

/* RTCCLK in Hz, 0 while it does not run */
static u32 rtcclk(const struct rtc *k)
{
    struct esk_mach *s = k->s;
    struct esk_rf_periph *rcc = s->rf ? esk_rf_periph(s->rf, "RCC") : 0;
    struct esk_rf_reg *bdcr = rcc ? esk_rf_reg(rcc, "BDCR") : 0, *cfgr = rcc ? esk_rf_reg(rcc, "CFGR") : 0;
    struct esk_rf_reg *csr = rcc ? esk_rf_reg(rcc, "CSR") : 0;
    u32 b = reg(bdcr), sel = b >> 8 & 3u, pre;
    if (!bdcr || !(b & (1u << 15)))
        return 0;
    if (sel == 1)
        return (b & 2u) ? (s->lse_hz ? s->lse_hz : 32768u) : 0; /* LSERDY */
    if (sel == 2)
        return (reg(csr) & 2u) ? LSI_HZ : 0;                     /* LSIRDY */
    if (sel == 3) {
        pre = reg(cfgr) >> 16 & 31u;
        return pre >= 2 && s->hse_hz ? s->hse_hz / pre : 0;
    }
    return 0;
}

/* the calibrations' rate: 1 + parts per one */
static double rate_adj(const struct rtc *k)
{
    u32 calr = reg(k->calr), cal = reg(k->calibr);
    double p = (double)((calr >> 15 & 1u) * 512u), m = (double)(calr & 0x1ffu), adj;
    adj = (p - m) / (1048576.0 + m - p);
    if (reg(k->cr) & CR_DCE)
        adj += (cal >> 7 & 1u) ? -2e-6 * (double)(cal & 31u) : 4e-6 * (double)(cal & 31u);
    return 1.0 + adj;
}

static u32 prediv_a(const struct rtc *k) { return (reg(k->prer) >> 16 & 0x7fu) + 1u; }
static u32 prediv_s(const struct rtc *k) { return reg(k->prer) & 0x7fffu; }

/* ---- flags and events -------------------------------------------------------------- */

static void exti_pulse(struct rtc *k, int line)
{
    esk_stm32_exti_input(k->s, line, 1);
    esk_stm32_exti_input(k->s, line, 0);
}

static void isr_set(struct rtc *k, u32 bits) { esk_rf_hw(k->isr, k->isr->value | bits); }
static void isr_clr(struct rtc *k, u32 bits) { esk_rf_hw(k->isr, k->isr->value & ~(u64)bits); }

/* flags that set two RTCCLK clocks after their cause */
static void pend(struct rtc *k, u32 bits)
{
    k->pend_bits |= bits;
    k->pend_flags = 1;
    k->pend_ticks = 2;
}

static void shadows(struct rtc *k)
{
    if (reg(k->isr) & ISR_INITF)
        return;
    if (!k->lock_tr)
        esk_rf_hw(k->tr, tr_of(k, &k->c));
    if (!k->lock_dr)
        esk_rf_hw(k->dr, dr_of(&k->c));
    esk_rf_hw(k->ssr, k->ss);
}

static void out_changed(struct rtc *k);
static void sync(struct rtc *k);

/* an alarm's match on the calendar and the subseconds now */
static int alarm_match(const struct rtc *k, int x)
{
    u32 a = reg(k->alrm[x]), m = reg(k->alrmss[x]), maskss = m >> 24 & 15u;
    const struct cal *c = &k->c;
    int h = c->hour;
    if (maskss) {
        u32 keep = maskss >= 15 ? 0x7fffu : (1u << maskss) - 1u;
        if ((k->ss & keep) != (m & keep))
            return 0;
    }
    if (!(a & (1u << 7)) && unbcd(a & 0x7fu) != c->sec)
        return 0;
    if (!(a & (1u << 15)) && unbcd(a >> 8 & 0x7fu) != c->min)
        return 0;
    if (!(a & (1u << 23))) {
        int ah = unbcd(a >> 16 & 0x3fu);
        if (reg(k->cr) & CR_FMT)
            ah = (ah % 12) + ((a >> 22 & 1u) ? 12 : 0);
        if (ah != h)
            return 0;
    }
    if (!(a & (1u << 31))) {
        if (a & (1u << 30)) {
            if ((int)(a >> 24 & 7u) != c->wday)
                return 0;
        } else if (unbcd(a >> 24 & 0x3fu) != c->date) {
            return 0;
        }
    }
    return 1;
}

static void alarms(struct rtc *k, int at_second)
{
    int x;
    for (x = 0; x < 2; x++) {
        u32 en = x ? CR_ALRBE : CR_ALRAE, ie = x ? CR_ALRBIE : CR_ALRAIE, f = x ? ISR_ALRBF : ISR_ALRAF;
        if (!(reg(k->cr) & en))
            continue;
        if (!(reg(k->alrmss[x]) >> 24 & 15u) && !at_second)
            continue; /* MASKSS 0: matched as the second changes */
        if (alarm_match(k, x)) {
            isr_set(k, f);
            if (reg(k->cr) & ie)
                exti_pulse(k, 17);
            out_changed(k);
        }
    }
}

static void wakeup_clocks(struct rtc *k, u64 rtc_ticks, u64 seconds)
{
    u32 sel = reg(k->cr) & CR_WUCKSEL;
    u64 n;
    if (!(reg(k->cr) & CR_WUTE))
        return;
    if (sel < 4) {
        u64 div = 16u >> sel;
        k->wut_frac += rtc_ticks;
        n = k->wut_frac / div;
        k->wut_frac %= div;
    } else {
        n = seconds;
    }
    while (n) {
        u64 step = n <= k->wut ? n : (u64)k->wut + 1u;
        n -= step;
        if (step <= k->wut) {
            k->wut -= (u32)step;
            continue;
        }
        /* it reached 0: WUTF, and it starts again */
        k->wut = (reg(k->wutr) & 0xffffu) + (sel >= 6 ? 0x10000u : 0u);
        isr_set(k, ISR_WUTF);
        if (reg(k->cr) & CR_WUTIE)
            exti_pulse(k, 22);
        out_changed(k);
    }
}

/* `n` ck_apre clocks of the subsecond counter: the seconds they make */
static void apre_clocks(struct rtc *k, u64 n)
{
    u32 s = prediv_s(k);
    while (n) {
        if (n <= k->ss && !(reg(k->cr) & (CR_ALRAE | CR_ALRBE))) {
            k->ss -= (u32)n;
            return;
        }
        if (k->ss) {
            k->ss--;
            n--;
            if (reg(k->alrmss[0]) >> 24 || reg(k->alrmss[1]) >> 24)
                alarms(k, 0);
            continue;
        }
        k->ss = s;
        n--;
        add_seconds(&k->c, 1);
        wakeup_clocks(k, 0, 1);
        alarms(k, 1);
    }
}

/* time passes up to now: RTCCLK's clocks (at `hz`) into the counters */
static void sync_at(struct rtc *k, u32 hz)
{
    struct esk_mach *s = k->s;
    u64 cyc = s->cycles >= k->seen ? s->cycles - k->seen : s->cycles;
    double t;
    u64 ticks;
    k->seen = s->cycles;
    if (!k->rate)
        k->rate = esk_mach_core_hz(s);
    if (!hz) {
        k->rate = esk_mach_core_hz(s);
        shadows(k);
        return;
    }
    t = (double)cyc * (double)hz * rate_adj(k) / (double)k->rate + k->frac;
    ticks = (u64)t;
    k->frac = t - (double)ticks;
    k->rate = esk_mach_core_hz(s);
    if (!ticks) {
        shadows(k);
        return;
    }
    if (k->pend_flags) {
        if (ticks >= k->pend_ticks) {
            if (k->pend_bits & ISR_INITF) {
                k->ss = prediv_s(k); /* stopped: the prescalers start again */
                k->apre = 0;
            }
            isr_set(k, k->pend_bits);
            k->pend_flags = 0;
            k->pend_bits = 0;
            shadows(k);
        } else {
            k->pend_ticks -= (u32)ticks;
        }
    }
    if (k->tamp_armed) {
        u32 tafcr = reg(k->tafcr), freq = tafcr >> 8 & 7u, nflt = tafcr >> 11 & 3u;
        u64 need = (u64)(32768u >> freq) * (1u << nflt); /* samples at RTCCLK / 32768 ... / 256 */
        k->tamp_ticks += ticks;
        if (k->tamp_ticks >= need) {
            k->tamp_armed = 0;
            esk_stm32_rtc_af1(s, -2); /* the level held: the tamper event */
        }
    }
    if ((reg(k->cr) & CR_COE) && !(reg(k->cr) >> 21 & 3u))
        k->cal_ticks += ticks;
    if (reg(k->isr) & ISR_INITF)
        return; /* the calendar stopped */
    if (k->restart) {
        u64 r = ticks < k->restart ? ticks : k->restart;
        k->restart -= (u32)r;
        ticks -= r;
        if (!ticks)
            return;
    }
    wakeup_clocks(k, ticks, 0);
    {
        u64 total = k->apre + ticks, n = total / prediv_a(k);
        k->apre = total % prediv_a(k);
        apre_clocks(k, n);
    }
    shadows(k);
    if ((reg(k->cr) & CR_COE) && !(reg(k->cr) >> 21 & 3u))
        out_changed(k);
}

/* its clock runs (a tick after each instruction) only while an event is due:
 * a pending flag, a tamper level, the calibration output, an alarm, the
 * wakeup timer; else time is counted at each access */
static void clock_update(struct rtc *k)
{
    u32 cr = reg(k->cr);
    int want = rtcclk(k) && (k->pend_flags || k->tamp_armed || (cr & (CR_COE | CR_ALRAE | CR_ALRBE | CR_WUTE)) ||
                             (cr >> 21 & 3u));
    esk_rf_clock(k->p, &k->running, want);
}

static void sync(struct rtc *k)
{
    k->hz_last = rtcclk(k);
    sync_at(k, k->hz_last);
}

/* ---- RTC_OUT -------------------------------------------------------------------------- */

static int out_level(const struct rtc *k)
{
    u32 cr = reg(k->cr), osel = cr >> 21 & 3u;
    if (osel) {
        u32 f = osel == 1 ? ISR_ALRAF : osel == 2 ? ISR_ALRBF : ISR_WUTF;
        int on = (reg(k->isr) & f) != 0;
        return (cr & CR_POL) ? !on : on;
    }
    if (cr & CR_COE) {
        /* 512 Hz from ck_apre's /64 (the default prescalers), or 1 Hz */
        u32 hz = rtcclk(k);
        u64 half = (cr & CR_COSEL) ? (u64)hz / 2u : (u64)hz / 1024u;
        return half ? (int)((k->cal_ticks / half) & 1u) : 0;
    }
    return -1;
}

static void out_changed(struct rtc *k)
{
    int v = out_level(k);
    if (v != k->out) {
        k->out = v;
        esk_stm32f4_port_changed(k->s, 2);
    }
}

int esk_stm32_rtc_out(struct esk_mach *s, int *open_drain)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "RTC") : 0;
    const struct rtc *k = p && p->ops == &esk_stm32f4_rtc_ops ? (const struct rtc *)p->ctx : 0;
    if (!k || k->out < 0)
        return -1;
    *open_drain = !((reg(k->cr) >> 21 & 3u) && (reg(k->tafcr) & TAFCR_ALARMOUTTYPE));
    return k->out;
}

/* ---- RTC_AF1: timestamp and tamper ---------------------------------------------------- */

static void timestamp(struct rtc *k)
{
    if (reg(k->isr) & ISR_TSF) {
        isr_set(k, ISR_TSOVF);
        return;
    }
    esk_rf_hw(k->tstr, tr_of(k, &k->c));
    esk_rf_hw(k->tsdr, dr_of(&k->c) & 0xffffu); /* no year */
    esk_rf_hw(k->tsssr, k->ss);
    isr_set(k, ISR_TSF);
}

static void tamper(struct rtc *k)
{
    int i;
    isr_set(k, ISR_TAMP1F);
    for (i = 0; i < 20; i++)
        esk_rf_hw(k->bkp[i], 0); /* the backup registers are erased */
    if (reg(k->tafcr) & TAFCR_TAMPTS)
        timestamp(k);
    if (reg(k->tafcr) & TAFCR_TAMPIE)
        exti_pulse(k, 21);
}

static void af1_edge(struct rtc *k, int level, u32 cr, u32 tafcr);
static void clock_update(struct rtc *k);

void esk_stm32_rtc_af1(struct esk_mach *s, int level)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "RTC") : 0;
    struct rtc *k = p && p->ops == &esk_stm32f4_rtc_ops ? (struct rtc *)p->ctx : 0;
    u32 cr, tafcr;
    if (!k)
        return;
    cr = reg(k->cr);
    tafcr = reg(k->tafcr);
    if (level == -2) { /* sync's: the tamper level held long enough */
        tamper(k);
        return;
    }
    sync(k);
    af1_edge(k, level, cr, tafcr);
    clock_update(k);
}

static void af1_edge(struct rtc *k, int level, u32 cr, u32 tafcr)
{
    int was;
    was = k->af1;
    k->af1 = level;
    if (was < 0 || was == level)
        return;
    if ((cr & CR_TSE) && level == !(cr & CR_TSEDGE)) {
        timestamp(k);
        if (cr & CR_TSIE)
            exti_pulse(k, 21);
    }
    if (tafcr & TAFCR_TAMP1E) {
        int active = (tafcr & TAFCR_TAMP1TRG) ? 1 : 0;
        if (!(tafcr >> 11 & 3u)) { /* TAMPFLT 0: an edge, rising (or falling with TAMP1TRG) */
            if (level == !active)
                tamper(k);
        } else if (level == active) {
            k->tamp_armed = 1; /* a level: it must hold */
            k->tamp_ticks = 0;
        } else {
            k->tamp_armed = 0;
        }
    }
}

/* ---- the registers ------------------------------------------------------------------ */

static int protected_ok(const struct rtc *k) { return k->unlock == 2; }

static int domain_ok(const struct rtc *k)
{
    struct esk_rf_periph *pwr = k->s->rf ? esk_rf_periph(k->s->rf, "PWR") : 0;
    struct esk_rf_reg *pcr = pwr ? esk_rf_reg(pwr, "CR") : 0;
    return !pcr || (pcr->value & (1u << 8)); /* DBP */
}

static u32 rtc_read(void *ctx, u32 off, int n)
{
    struct rtc *k = (struct rtc *)ctx;
    int bypass = (reg(k->cr) & CR_BYPSHAD) != 0;
    if (k->s->bus.debug)
        return esk_rf_read(k->p, off, n);
    sync(k);
    if (off == esk_rf_off(k->tr)) {
        if (bypass)
            esk_rf_hw(k->tr, tr_of(k, &k->c));
        else
            k->lock_dr = 1;
    } else if (off == esk_rf_off(k->ssr)) {
        if (bypass)
            esk_rf_hw(k->ssr, k->ss);
        else
            k->lock_dr = k->lock_tr = 1;
    } else if (off == esk_rf_off(k->dr)) {
        u32 v;
        if (bypass)
            esk_rf_hw(k->dr, dr_of(&k->c));
        v = esk_rf_read(k->p, off, n);
        k->lock_dr = k->lock_tr = 0; /* the higher registers go on */
        return v;
    }
    return esk_rf_read(k->p, off, n);
}

static void write_reg(struct rtc *k, u32 off, int n, u32 v)
{
    int init;
    u32 was;
    if (!domain_ok(k))
        return; /* the backup domain's write protection */
    sync(k);
    init = (reg(k->isr) & ISR_INITF) != 0;
    if (off == esk_rf_off(k->wpr)) {
        u32 key = v & 0xffu;
        k->unlock = key == 0xcau ? 1 : (key == 0x53u && k->unlock == 1) ? 2 : 0;
        return;
    }
    if (off >= esk_rf_off(k->bkp[0]) && off <= esk_rf_off(k->bkp[19])) {
        esk_rf_write(k->p, off, n, v);
        return;
    }
    if (off == esk_rf_off(k->tafcr)) {
        esk_rf_write(k->p, off, n, v);
        out_changed(k);
        return;
    }
    if (off == esk_rf_off(k->isr)) {
        /* the flags clear by writing 0, unprotected; INIT needs the keys */
        u32 clr = ~v & ISR_FLAGS;
        if (!(v & ISR_RSF))
            clr |= ISR_RSF;
        isr_clr(k, clr);
        if ((clr & ISR_RSF) && !(reg(k->cr) & CR_BYPSHAD) && !init)
            pend(k, ISR_RSF);
        if (protected_ok(k)) {
            if ((v & ISR_INIT) && !(reg(k->isr) & ISR_INIT)) {
                isr_set(k, ISR_INIT);
                isr_clr(k, ISR_RSF);
                pend(k, ISR_INITF);
                k->ss = prediv_s(k); /* the prescalers start again */
                k->apre = 0;
            } else if (!(v & ISR_INIT) && (reg(k->isr) & ISR_INIT)) {
                isr_clr(k, ISR_INIT | ISR_INITF);
                k->pend_bits &= ~ISR_INITF;
                k->restart = 4; /* counting again four RTCCLK clocks later */
                if (k->c.year)
                    isr_set(k, ISR_INITS);
                else
                    isr_clr(k, ISR_INITS);
                pend(k, ISR_RSF);
            }
        }
        out_changed(k);
        return;
    }
    if (!protected_ok(k))
        return; /* WPR's write protection */
    if (off == esk_rf_off(k->tr) || off == esk_rf_off(k->dr) || off == esk_rf_off(k->prer)) {
        if (!init)
            return;
        esk_rf_write(k->p, off, n, v);
        if (off == esk_rf_off(k->tr))
            set_tr(k, reg(k->tr));
        else if (off == esk_rf_off(k->dr))
            set_dr(k, reg(k->dr));
        else
            k->ss = prediv_s(k);
        esk_rf_hw(k->tr, tr_of(k, &k->c));
        esk_rf_hw(k->dr, dr_of(&k->c));
        return;
    }
    if (off == esk_rf_off(k->cr)) {
        was = reg(k->cr);
        if (!init) /* FMT, DCE and REFCKON in INIT only */
            v = (v & ~(CR_FMT | CR_DCE | CR_REFCKON)) | (was & (CR_FMT | CR_DCE | CR_REFCKON));
        if ((was & CR_WUTE) || !(reg(k->isr) & ISR_WUTWF))
            v = (v & ~CR_WUCKSEL) | (was & CR_WUCKSEL);
        esk_rf_write(k->p, off, n, v & ~(CR_ADD1H | CR_SUB1H));
        if ((v & CR_ADD1H) && !init)
            add_hours(&k->c, 1);
        if ((v & CR_SUB1H) && !init)
            add_hours(&k->c, -1);
        if ((was & CR_ALRAE) && !(v & CR_ALRAE))
            pend(k, ISR_ALRAWF);
        if (v & CR_ALRAE)
            isr_clr(k, ISR_ALRAWF);
        if ((was & CR_ALRBE) && !(v & CR_ALRBE))
            pend(k, ISR_ALRBWF);
        if (v & CR_ALRBE)
            isr_clr(k, ISR_ALRBWF);
        if ((was & CR_WUTE) && !(v & CR_WUTE))
            pend(k, ISR_WUTWF);
        if ((v & CR_WUTE) && !(was & CR_WUTE)) {
            isr_clr(k, ISR_WUTWF);
            k->wut = (reg(k->wutr) & 0xffffu) + ((v & CR_WUCKSEL) >= 6 ? 0x10000u : 0u);
            k->wut_frac = 0;
        }
        if (v & CR_BYPSHAD)
            isr_clr(k, ISR_RSF);
        shadows(k);
        out_changed(k);
        return;
    }
    if (off == esk_rf_off(k->alrm[0]) || off == esk_rf_off(k->alrmss[0])) {
        if (reg(k->isr) & ISR_ALRAWF || init)
            esk_rf_write(k->p, off, n, v);
        return;
    }
    if (off == esk_rf_off(k->alrm[1]) || off == esk_rf_off(k->alrmss[1])) {
        if (reg(k->isr) & ISR_ALRBWF || init)
            esk_rf_write(k->p, off, n, v);
        return;
    }
    if (off == esk_rf_off(k->wutr)) {
        if (reg(k->isr) & ISR_WUTWF)
            esk_rf_write(k->p, off, n, v);
        return;
    }
    if (off == esk_rf_off(k->shiftr)) {
        if (reg(k->isr) & ISR_SHPF)
            return;
        if (v & (1u << 31))
            add_seconds(&k->c, 1); /* ADD1S */
        k->ss += v & 0x7fffu;     /* SUBFS: the subseconds delayed */
        shadows(k);
        return;
    }
    esk_rf_write(k->p, off, n, v); /* CALIBR, CALR */
}

static void rtc_write(void *ctx, u32 off, int n, u32 v)
{
    struct rtc *k = (struct rtc *)ctx;
    write_reg(k, off, n, v);
    clock_update(k);
}

static void rtc_tick(void *ctx, u32 cycles)
{
    struct rtc *k = (struct rtc *)ctx;
    (void)cycles;
    if (k->running)
        sync(k);
    clock_update(k);
}

/* the machine's cycles until the RTC must be looked at: its next RTCCLK
 * pair (pending flags, a tamper level, the calibration output), its next
 * ck_apre clock (subsecond alarms) or second (alarms, the wakeup timer) */
static int rtc_next(void *ctx, u32 *cycles)
{
    struct rtc *k = (struct rtc *)ctx;
    u32 hz = rtcclk(k), core = esk_mach_core_hz(k->s), cr = reg(k->cr);
    u64 ticks = 0;
    if (!k->running || !hz)
        return 0;
    if (k->pend_flags || k->tamp_armed || ((cr & CR_COE) && !(cr >> 21 & 3u)))
        ticks = (cr & CR_COE) ? hz / 2048u : 2u;
    else if ((reg(k->alrmss[0]) >> 24 && (cr & CR_ALRAE)) || (reg(k->alrmss[1]) >> 24 && (cr & CR_ALRBE)))
        ticks = prediv_a(k);
    else if ((cr & (CR_ALRAE | CR_ALRBE)) || (cr & CR_WUTE))
        ticks = (u64)prediv_a(k) * (prediv_s(k) + 1u);
    else
        return 0;
    if ((cr & CR_WUTE) && (cr & CR_WUCKSEL) < 4) {
        u64 w = ((u64)k->wut + 1) * (16u >> (cr & CR_WUCKSEL));
        if (w < ticks)
            ticks = w;
    }
    if (!ticks)
        ticks = 1;
    {
        u64 c = ticks * core / hz + 1;
        *cycles = c > 0xffffffffu ? 0xffffffffu : (u32)c;
    }
    return 1;
}

static void rtc_reset(void *ctx)
{
    struct rtc *k = (struct rtc *)ctx;
    if (k->s->reset_cause != ESK_RESET_POWER)
        sync_at(k, k->hz_last); /* its time up to the reset */
    k->seen = 0; /* the machine's cycles start again at 0 */
    k->rate = esk_mach_core_hz(k->s);
    k->unlock = 0;
    k->lock_dr = k->lock_tr = 0;
    k->af1 = 0; /* as PC13's IDR starts */
    k->tamp_armed = 0;
    k->running = 0;
    if (k->s->reset_cause == ESK_RESET_POWER) {
        /* the backup domain's power-on: the calendar at its reset value */
        memset(&k->c, 0, sizeof k->c);
        set_tr(k, reg(k->tr));
        set_dr(k, reg(k->dr));
        k->ss = prediv_s(k);
        k->apre = 0;
        k->frac = 0.0;
        k->pend_flags = 0;
        k->pend_bits = 0;
        k->wut = 0;
        k->wut_frac = 0;
        k->cal_ticks = 0;
    }
    k->out = out_level(k);
    clock_update(k); /* its time goes on through the reset, counted at each access */
}

const struct esk_dev_ops esk_stm32f4_rtc_ops = {
    "stm32f4-rtc", rtc_read, rtc_write, rtc_reset, rtc_tick, rtc_next,
};

void esk_stm32_rtc_sync(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "RTC") : 0;
    if (p && p->ops == &esk_stm32f4_rtc_ops)
        sync((struct rtc *)p->ctx);
}

void *esk_stm32f4_rtc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct rtc k, *c;
    int i;
    static const char *const bk[20] = { "BKP0R",  "BKP1R",  "BKP2R",  "BKP3R",  "BKP4R",  "BKP5R",  "BKP6R",
                                        "BKP7R",  "BKP8R",  "BKP9R",  "BKP10R", "BKP11R", "BKP12R", "BKP13R",
                                        "BKP14R", "BKP15R", "BKP16R", "BKP17R", "BKP18R", "BKP19R" };
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    k.tr = esk_rf_reg(p, "TR");
    k.dr = esk_rf_reg(p, "DR");
    k.cr = esk_rf_reg(p, "CR");
    k.isr = esk_rf_reg(p, "ISR");
    k.prer = esk_rf_reg(p, "PRER");
    k.wutr = esk_rf_reg(p, "WUTR");
    k.calibr = esk_rf_reg(p, "CALIBR");
    k.alrm[0] = esk_rf_reg(p, "ALRMAR");
    k.alrm[1] = esk_rf_reg(p, "ALRMBR");
    k.wpr = esk_rf_reg(p, "WPR");
    k.ssr = esk_rf_reg(p, "SSR");
    k.shiftr = esk_rf_reg(p, "SHIFTR");
    k.tstr = esk_rf_reg(p, "TSTR");
    k.tsdr = esk_rf_reg(p, "TSDR");
    k.tsssr = esk_rf_reg(p, "TSSSR");
    k.calr = esk_rf_reg(p, "CALR");
    k.tafcr = esk_rf_reg(p, "TAFCR");
    k.alrmss[0] = esk_rf_reg(p, "ALRMASSR");
    k.alrmss[1] = esk_rf_reg(p, "ALRMBSSR");
    for (i = 0; i < 20; i++)
        if (!(k.bkp[i] = esk_rf_reg(p, bk[i])))
            return 0;
    if (!k.tr || !k.dr || !k.cr || !k.isr || !k.prer || !k.wutr || !k.calibr || !k.alrm[0] || !k.alrm[1] ||
        !k.wpr || !k.ssr || !k.shiftr || !k.tstr || !k.tsdr || !k.tsssr || !k.calr || !k.tafcr || !k.alrmss[0] ||
        !k.alrmss[1])
        return 0; /* not this layout: the registers alone */
    p->backup = 1;
    k.af1 = -1;
    k.out = -1;
    c = (struct rtc *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    return c;
}
