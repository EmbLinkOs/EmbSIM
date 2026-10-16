/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* aon.c -- the FE310's always-on domain (FE310-G002 manual, chapters 11
 * to 15), over the register file: its watchdog and its real-time clock,
 * both counting the low-frequency clock, and what survives a watchdog
 * reset.
 *
 *   - the low-frequency clock is the HiFive1's 32.768 kHz, counted as the
 *     CLINT counts it: each core cycle adds 32768 to an accumulator that
 *     carries the remainder;
 *   - the RTC: rtccount (48 bits) counts while rtcenalways is set; rtcs is
 *     rtccount shifted right by rtcscale; rtcip0 is high while rtcs >=
 *     rtccmp0, and is PLIC source 2 (the RTC's);
 *   - the watchdog: wdogcount (31 bits) counts while wdogenalways or
 *     wdogencoreawake is set (the core is always awake here); wdogs is its
 *     16 bits from wdogscale; the comparator fires as wdogs reaches
 *     wdogcmp0, setting the sticky wdogip0 (PLIC source 1) and, with
 *     wdogrsten, resetting the chip; with wdogzerocmp the count goes back
 *     to 0 one clock after it fires. A write to wdogcfg, wdogcount,
 *     wdogfeed or wdogcmp0 is taken only just after 0x51F15E was written
 *     to wdogkey, and locks it again; 0xD09F00D written to wdogfeed zeroes
 *     the count;
 *   - a watchdog reset resets the rest of the chip: the domain keeps its
 *     registers and counts, and pmucause's resetcause reads 2;
 *   - the backup registers, the low-frequency oscillator's configuration
 *     and the PMU keep what is written: the PMU's sleep is not modelled. */
#include <string.h>

#include "mcu/riscv/riscv.h"

#define LF_HZ 32768u
#define WD_KEY 0x51F15Eu
#define WD_FOOD 0xD09F00Du

#define CFG_SCALE 0xfu
#define WD_RSTEN 0x100u
#define WD_ZEROCMP 0x200u
#define CFG_ENALWAYS 0x1000u
#define WD_ENCOREAWAKE 0x2000u
#define CFG_IP0 0x10000000u
#define WD_CFG_BITS (CFG_SCALE | WD_RSTEN | WD_ZEROCMP | CFG_ENALWAYS | WD_ENCOREAWAKE | CFG_IP0)

#define RTC_MASK 0xffffffffffffull /* rtccount's 48 bits */
#define WD_MASK 0x7fffffffu        /* wdogcount's 31 bits */

struct fe_aon {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *wcfg, *wcount, *ws, *wfeed, *wkey, *wcmp;
    struct esk_rf_reg *rcfg, *rlo, *rhi, *rs, *rcmp, *cause;
    int wd_irq, rtc_irq;
    u64 rtc;
    u32 wd;
    u64 acc; /* LF_HZ per core cycle, an edge each core_hz */
    int unlock;
    int running;
    /* a watchdog reset: what the domain keeps through it */
    int wd_reset;
    u64 kept[128];
    u64 kept_rtc, kept_acc;
    u32 kept_wd;
};

static u32 cfg(const struct esk_rf_reg *r) { return (u32)r->value; }
static int wd_on(const struct fe_aon *k) { return (cfg(k->wcfg) & (CFG_ENALWAYS | WD_ENCOREAWAKE)) != 0; }
static int rtc_on(const struct fe_aon *k) { return (cfg(k->rcfg) & CFG_ENALWAYS) != 0; }
static u32 wd_scale(const struct fe_aon *k) { return cfg(k->wcfg) & CFG_SCALE; }
static u32 rtc_scale(const struct fe_aon *k) { return cfg(k->rcfg) & CFG_SCALE; }
static u32 wd_s(const struct fe_aon *k, u32 count) { return count >> wd_scale(k) & 0xffffu; }
static u32 rtc_s(const struct fe_aon *k) { return (u32)(k->rtc >> rtc_scale(k)); }

static int rtc_ip(const struct fe_aon *k) { return rtc_s(k) >= (u32)k->rcmp->value; }

/* the registers that read the counts, and the interrupt lines */
static void mirror(struct fe_aon *k)
{
    u32 rc = cfg(k->rcfg) & ~CFG_IP0;
    esk_rf_hw(k->wcount, k->wd);
    esk_rf_hw(k->ws, wd_s(k, k->wd));
    esk_rf_hw(k->wkey, (u32)k->unlock);
    esk_rf_hw(k->wfeed, 0);
    esk_rf_hw(k->rlo, (u32)k->rtc);
    esk_rf_hw(k->rhi, (u32)(k->rtc >> 32));
    esk_rf_hw(k->rs, rtc_s(k));
    esk_rf_hw(k->rcfg, rtc_ip(k) ? rc | CFG_IP0 : rc);
    esk_fe310_plic_source(k->s, k->wd_irq, (cfg(k->wcfg) & CFG_IP0) != 0);
    esk_fe310_plic_source(k->s, k->rtc_irq, rtc_ip(k));
}

/* the count at which the watchdog's comparator next fires, from `count` */
static u64 wd_target(const struct fe_aon *k, u32 count)
{
    u32 sh = wd_scale(k) + 16u;
    u64 window = (u64)1 << sh, t = ((u64)count >> sh << sh) + ((u64)((u32)k->wcmp->value & 0xffffu) << wd_scale(k));
    if (t < count)
        t += window;
    return t;
}

static void wd_fire(struct fe_aon *k)
{
    struct esk_mach *s = k->s;
    int i;
    esk_rf_hw(k->wcfg, cfg(k->wcfg) | CFG_IP0);
    if (!(cfg(k->wcfg) & WD_RSTEN) || s->reset_req)
        return;
    esk_rf_diag(s, ES_SEV_INFO, "mcu.watchdog_reset", "%s",
                "the always-on watchdog's comparator fired with wdogrsten set: it resets the chip");
    for (i = 0; i < k->p->nreg && i < 128; i++)
        k->kept[i] = k->p->reg[i]->value;
    k->kept_rtc = k->rtc;
    k->kept_wd = k->wd;
    k->kept_acc = k->acc;
    k->wd_reset = 1;
    s->reset_req = 1;
    s->yield = 1;
}

/* n edges of the low-frequency clock */
static void edges(struct fe_aon *k, u64 n)
{
    if (rtc_on(k))
        k->rtc = (k->rtc + n) & RTC_MASK;
    if (wd_on(k)) {
        u64 t = wd_target(k, k->wd);
        u64 end = (u64)k->wd + n;
        if (cfg(k->wcfg) & WD_ZEROCMP) {
            /* 0 one clock after the count reaches the target: the period
             * is target + 1 */
            if (end >= t)
                wd_fire(k);
            k->wd = (u32)(end % (t + 1)) & WD_MASK;
        } else {
            k->wd = (u32)end & WD_MASK;
            if (end >= t || wd_s(k, k->wd) >= ((u32)k->wcmp->value & 0xffffu))
                wd_fire(k);
        }
    }
}

static void aon_tick(void *ctx, u32 cycles)
{
    struct fe_aon *k = (struct fe_aon *)ctx;
    u32 hz = esk_mach_core_hz(k->s);
    if (!hz)
        return;
    k->acc += (u64)cycles * LF_HZ;
    if (k->acc >= hz) {
        u64 n = k->acc / hz;
        k->acc %= hz;
        if (rtc_on(k) || wd_on(k)) {
            edges(k, n);
            mirror(k);
        }
    }
}

static int aon_next(void *ctx, u32 *cycles)
{
    struct fe_aon *k = (struct fe_aon *)ctx;
    u32 hz = esk_mach_core_hz(k->s);
    u64 need = 0, left;
    int any = 0;
    if (!hz)
        return 0;
    if (rtc_on(k) && !rtc_ip(k)) {
        u64 at = (u64)(u32)k->rcmp->value << rtc_scale(k);
        if (at <= RTC_MASK && at > k->rtc) {
            need = at - k->rtc;
            any = 1;
        }
    }
    if (wd_on(k) && (!(cfg(k->wcfg) & CFG_IP0) || (cfg(k->wcfg) & WD_RSTEN))) {
        u64 n = wd_target(k, k->wd) - k->wd;
        if (!n)
            n = 1;
        if (!any || n < need)
            need = n;
        any = 1;
    }
    if (!any)
        return 0;
    if (need > ~(u64)0 / hz) {
        *cycles = 0xffffffffu;
        return 1;
    }
    left = (need * hz - k->acc + LF_HZ - 1) / LF_HZ;
    *cycles = left > 0xffffffffu ? 0xffffffffu : (u32)left;
    return 1;
}

static u32 aon_read(void *ctx, u32 off, int n)
{
    return esk_rf_read(((struct fe_aon *)ctx)->p, off, n);
}

/* a write of n bytes at off merged into the register's word */
static u32 merge(u32 old, u32 off, int n, u32 v)
{
    u32 sh = (off & 3u) * 8u, m = n >= 4 ? 0xffffffffu : ((1u << (8u * (u32)n)) - 1u) << sh;
    return (old & ~m) | ((v << sh) & m);
}

static void aon_write(void *ctx, u32 off, int n, u32 v)
{
    struct fe_aon *k = (struct fe_aon *)ctx;
    u32 w = off & ~3u;
    if (w == esk_rf_off(k->wkey)) {
        k->unlock = merge(0, off, n, v) == WD_KEY;
    } else if (w == esk_rf_off(k->wcfg) || w == esk_rf_off(k->wcount) || w == esk_rf_off(k->wfeed) ||
               w == esk_rf_off(k->wcmp)) {
        if (k->unlock) {
            if (w == esk_rf_off(k->wcfg))
                esk_rf_hw(k->wcfg, merge(cfg(k->wcfg), off, n, v) & WD_CFG_BITS);
            else if (w == esk_rf_off(k->wcount))
                k->wd = merge(k->wd, off, n, v) & WD_MASK;
            else if (w == esk_rf_off(k->wcmp))
                esk_rf_hw(k->wcmp, merge((u32)k->wcmp->value, off, n, v) & 0xffffu);
            else if (merge(0, off, n, v) == WD_FOOD)
                k->wd = 0;
        }
        k->unlock = 0;
    } else if (w == esk_rf_off(k->rcfg)) {
        esk_rf_hw(k->rcfg, merge(cfg(k->rcfg), off, n, v) & (CFG_SCALE | CFG_ENALWAYS));
    } else if (w == esk_rf_off(k->rlo)) {
        k->rtc = (k->rtc & ~0xffffffffull) | merge((u32)k->rtc, off, n, v);
    } else if (w == esk_rf_off(k->rhi)) {
        k->rtc = ((u64)(merge((u32)(k->rtc >> 32), off, n, v) & 0xffffu) << 32) | (u32)k->rtc;
    } else if (w == esk_rf_off(k->rcmp)) {
        esk_rf_hw(k->rcmp, merge((u32)k->rcmp->value, off, n, v));
    } else if (w == esk_rf_off(k->ws) || w == esk_rf_off(k->rs) || w == esk_rf_off(k->cause)) {
        /* read-only */
    } else {
        esk_rf_write(k->p, off, n, v);
    }
    mirror(k);
}

static void aon_reset(void *ctx)
{
    struct fe_aon *k = (struct fe_aon *)ctx;
    int i;
    k->unlock = 0;
    k->running = 0;
    if (k->wd_reset) {
        for (i = 0; i < k->p->nreg && i < 128; i++)
            esk_rf_hw(k->p->reg[i], k->kept[i]);
        k->rtc = k->kept_rtc;
        k->wd = k->kept_wd;
        k->acc = k->kept_acc;
        esk_rf_hw(k->cause, 2u << 8); /* resetcause: the watchdog */
        k->wd_reset = 0;
    } else {
        k->rtc = 0;
        k->wd = 0;
        k->acc = 0;
    }
    esk_rf_clock(k->p, &k->running, 1);
    mirror(k);
}

const struct esk_dev_ops esk_fe310_aon_ops = {
    "fe310-aon", aon_read, aon_write, aon_reset, aon_tick, aon_next,
};

void *esk_fe310_aon_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct fe_aon k, *c;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    k.wcfg = esk_rf_reg(p, "WDOGCFG");
    k.wcount = esk_rf_reg(p, "WDOGCOUNT");
    k.ws = esk_rf_reg(p, "WDOGS");
    k.wfeed = esk_rf_reg(p, "WDOGFEED");
    k.wkey = esk_rf_reg(p, "WDOGKEY");
    k.wcmp = esk_rf_reg(p, "WDOGCMP0");
    k.rcfg = esk_rf_reg(p, "RTCCFG");
    k.rlo = esk_rf_reg(p, "RTCCOUNTLO");
    k.rhi = esk_rf_reg(p, "RTCCOUNTHI");
    k.rs = esk_rf_reg(p, "RTCS");
    k.rcmp = esk_rf_reg(p, "RTCCMP0");
    k.cause = esk_rf_reg(p, "PMUCAUSE");
    if (!k.wcfg || !k.wcount || !k.ws || !k.wfeed || !k.wkey || !k.wcmp || !k.rcfg || !k.rlo || !k.rhi ||
        !k.rs || !k.rcmp || !k.cause)
        return 0;
    k.wd_irq = esk_rf_irq(p, "WATCHDOG");
    k.rtc_irq = esk_rf_irq(p, "RTC");
    c = (struct fe_aon *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    return c;
}
