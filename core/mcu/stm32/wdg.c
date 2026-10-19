/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* wdg.c -- the STM32 independent and window watchdogs (RM0008 19-20,
 * RM0390 19-20), over the register file.
 *
 * The IWDG:
 *   - KR's 0xCCCC starts it (and forces LSI on): its 12-bit counter counts
 *     down from 0xFFF at LSI (32 kHz) divided by PR's 4 to 256; 0xAAAA
 *     reloads it with RLR; 0x5555 opens PR and RLR to writes, another key
 *     closes them; a written PR or RLR reaches the counter's domain five
 *     LSI clocks later, PVU or RVU set meanwhile (a write while it is set
 *     is lost). At 0 it resets the part (IWDGRSTF): it runs until a reset
 *     stops it;
 *   - with the option bytes' hardware watchdog (WDG_SW 0 in their user
 *     byte, which the part loads into FLASH's option register at reset) it
 *     starts by itself at every reset.
 *
 * The WWDG:
 *   - its 7-bit counter (CR.T) counts down every 4096 x 2^WDGTB PCLK1
 *     clocks while its clock is on; CFR holds the window W and EWI;
 *   - once WDGA is set (it clears only with a reset) the part resets
 *     (WWDGRSTF) when T6 clears (0x40 to 0x3F), when CR is written with T6
 *     clear, or when CR is written while the counter is above W;
 *   - the counter reaching 0x40 sets EWIF (rc_w0) and, with EWI (set only),
 *     makes the WWDG interrupt pending.
 *
 * Not modelled: LSI's spread (17 to 47 kHz on the part: 32 kHz here), the
 * debug freeze bits (DBGMCU's DBG_IWDG_STOP, DBG_WWDG_STOP: a debugger's
 * halt stops all of the machine's time here); the WWDG's counter while
 * WDGA is clear counts from the last access made while its clock is on. */
#include <string.h>

#include "mcu/stm32/stm32.h"

#define LSI_HZ 32000u
#define KR_START 0xCCCCu
#define KR_RELOAD 0xAAAAu
#define KR_ACCESS 0x5555u
#define SR_PVU (1u << 0)
#define SR_RVU (1u << 1)

/* ---- IWDG ------------------------------------------------------------------------ */

struct iwdg {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *kr, *pr, *rlr, *sr;
    int running, on, access;
    u32 count, pr_now, rlr_now;
    u64 acc;            /* core cycles x LSI_HZ toward the next count */
    u64 since;
    u32 upd;            /* PVU/RVU: bits waiting */
    u64 upd_left;       /* core cycles until they land */
    u32 pr_new, rlr_new;
};

static u32 reg(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }

/* core cycles x LSI_HZ per count */
static u64 iw_unit(const struct iwdg *w)
{
    u32 core = esk_mach_core_hz(w->s);
    u32 div = w->pr_now >= 6 ? 256u : 4u << w->pr_now;
    return (u64)(core ? core : 16000000u) * div;
}

static u64 lsi_cycles(const struct iwdg *w, u32 n)
{
    u32 core = esk_mach_core_hz(w->s);
    return (u64)(core ? core : 16000000u) * n / LSI_HZ + 1;
}

static void iw_clock(struct iwdg *w) { esk_rf_clock(w->p, &w->running, w->on || w->upd); }

static void iw_start(struct iwdg *w)
{
    if (!w->on) {
        w->on = 1;
        w->count = 0xfffu;
        w->acc = 0;
        w->since = w->s->cycles;
        esk_stm32_rcc_lsi(w->s);
    }
    iw_clock(w);
}

static void iw_tick(void *ctx, u32 cycles)
{
    struct iwdg *w = (struct iwdg *)ctx;
    if (!w->running)
        return;
    cycles = esk_mach_after(w->s, cycles, w->since);
    w->since = 0;
    if (w->upd) {
        if (w->upd_left > cycles) {
            w->upd_left -= cycles;
        } else {
            if (w->upd & SR_PVU)
                w->pr_now = w->pr_new;
            if (w->upd & SR_RVU)
                w->rlr_now = w->rlr_new;
            w->upd = 0;
            esk_rf_hw(w->sr, 0);
        }
    }
    if (w->on && !w->s->reset_req) {
        u64 unit = iw_unit(w), n;
        w->acc += (u64)cycles * LSI_HZ;
        n = w->acc / unit;
        w->acc -= n * unit;
        if (n > w->count) { /* the count from 0: RLR+1 counts in all */
            w->count = 0;
            esk_rf_diag(w->s, ES_SEV_INFO, "mcu.watchdog_reset", "%s",
                        "the independent watchdog counted down to 0: it resets the part");
            w->s->reset_cause = ESK_RESET_WATCHDOG;
            w->s->reset_req = 1;
            w->s->yield = 1;
            return;
        }
        w->count -= (u32)n;
    }
    iw_clock(w);
}

static int iw_next(void *ctx, u32 *cycles)
{
    struct iwdg *w = (struct iwdg *)ctx;
    u64 best = ~(u64)0;
    if (!w->running)
        return 0;
    if (w->on) {
        u64 need = ((u64)w->count + 1) * iw_unit(w);
        need = need > w->acc ? (need - w->acc + LSI_HZ - 1) / LSI_HZ : 1;
        best = need;
    }
    if (w->upd && w->upd_left < best)
        best = w->upd_left ? w->upd_left : 1;
    if (best == ~(u64)0)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static u32 iw_read(void *ctx, u32 off, int n)
{
    struct iwdg *w = (struct iwdg *)ctx;
    return esk_rf_read(w->p, off, n);
}

static void iw_write(void *ctx, u32 off, int n, u32 v)
{
    struct iwdg *w = (struct iwdg *)ctx;
    (void)n;
    if (off == esk_rf_off(w->kr)) {
        u32 key = v & 0xffffu;
        w->access = key == KR_ACCESS;
        if (key == KR_START)
            iw_start(w);
        else if (key == KR_RELOAD && w->on) {
            w->count = w->rlr_now & 0xfffu;
            w->acc = 0;
        }
        return;
    }
    if (!w->access)
        return; /* write-protected */
    if (off == esk_rf_off(w->pr) && !(w->upd & SR_PVU)) {
        w->pr_new = v & 7u;
        esk_rf_hw(w->pr, w->pr_new);
        w->upd |= SR_PVU;
    } else if (off == esk_rf_off(w->rlr) && !(w->upd & SR_RVU)) {
        w->rlr_new = v & 0xfffu;
        esk_rf_hw(w->rlr, w->rlr_new);
        w->upd |= SR_RVU;
    } else {
        return;
    }
    esk_rf_hw(w->sr, w->upd);
    w->upd_left = lsi_cycles(w, 5);
    w->since = w->s->cycles;
    iw_clock(w);
}

/* the option bytes' hardware watchdog: their user byte's WDG_SW 0, as the
 * part loads it at reset into FLASH's option register (the F2/F4's OPTCR:
 * the halfword at 0x1FFFC000, RDP and USER; the F1's OBR: USER at
 * 0x1FFFF802) */
static int hardware_watchdog(struct esk_mach *s)
{
    struct esk_rf_periph *f = s->rf ? esk_rf_periph(s->rf, "FLASH") : 0;
    struct esk_rf_reg *optcr = f ? esk_rf_reg(f, "OPTCR") : 0, *obr = f ? esk_rf_reg(f, "OBR") : 0;
    u32 v = 0;
    if (optcr && esk_rf_mask(optcr, "WDG_SW") && !esk_bus_debug_read(&s->bus, 0x1FFFC000u, 2, &v)) {
        esk_rf_hw(optcr, (optcr->value & ~(u64)0xffecu) | (v & 0xffecu)); /* RDP, USER, BOR_LEV */
        return !(v & 0x20u);
    }
    if (obr && esk_rf_mask(obr, "WDG_SW") && !esk_bus_debug_read(&s->bus, 0x1FFFF802u, 1, &v)) {
        esk_rf_hw(obr, (obr->value & ~(u64)0x1cu) | (u64)(v & 7u) << 2); /* WDG_SW, nRST_STOP, nRST_STDBY */
        return !(v & 1u);
    }
    return 0;
}

static void iw_reset(void *ctx)
{
    struct iwdg *w = (struct iwdg *)ctx;
    w->running = 0;
    w->on = w->access = 0;
    w->upd = 0;
    w->pr_now = 0;
    w->rlr_now = 0xfffu;
    w->count = 0xfffu;
    w->acc = 0;
    w->since = 0;
    if (hardware_watchdog(w->s))
        iw_start(w);
}

const struct esk_dev_ops esk_stm32_iwdg_ops = {
    "stm32-iwdg", iw_read, iw_write, iw_reset, iw_tick, iw_next,
};

int esk_stm32_iwdg_running(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "IWDG") : 0;
    return p && p->ops == &esk_stm32_iwdg_ops && ((const struct iwdg *)p->ctx)->on;
}

void *esk_stm32_iwdg_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct iwdg w, *c;
    memset(&w, 0, sizeof w);
    w.s = s;
    w.p = p;
    w.kr = esk_rf_reg(p, "KR");
    w.pr = esk_rf_reg(p, "PR");
    w.rlr = esk_rf_reg(p, "RLR");
    w.sr = esk_rf_reg(p, "SR");
    if (!w.kr || !w.pr || !w.rlr || !w.sr)
        return 0;
    w.rlr_now = 0xfffu;
    w.count = 0xfffu;
    p->always_on = 1; /* on LSI: it counts through Stop and Standby */
    c = (struct iwdg *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = w;
    return c;
}

/* ---- WWDG ------------------------------------------------------------------------ */

#define CR_WDGA (1u << 7)
#define CFR_EWI (1u << 9)
#define SR_EWIF (1u << 0)

struct wwdg {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr, *cfr, *sr;
    int irq, running;
    u32 base;           /* the counter at base_at */
    u64 base_at;        /* the machine's cycles then */
    int ewi_done;       /* EWIF already raised for this pass through 0x40 */
};

/* core cycles per count */
static u64 ww_unit(const struct wwdg *w)
{
    u32 core = esk_mach_core_hz(w->s), pclk = esk_stm32_pclk(w->s, 1);
    u64 ratio = core && pclk && core >= pclk ? core / pclk : 1;
    return ratio * ((u64)4096u << (reg(w->cfr) >> 7 & 3u));
}

/* the counter now (T[6:0]); its clock off: as it was */
static u32 ww_count(struct wwdg *w)
{
    u64 n;
    if (esk_rf_gated(w->p)) {
        w->base_at = w->s->cycles;
        return w->base;
    }
    n = (w->s->cycles - w->base_at) / ww_unit(w);
    return (w->base - (u32)(n & 0x7fu)) & 0x7fu; /* it wraps past 0 */
}

static void ww_rebase(struct wwdg *w, u32 t)
{
    w->base = t & 0x7fu;
    w->base_at = w->s->cycles;
    w->ewi_done = w->base < 0x40u;
    esk_rf_hw(w->cr, (w->cr->value & CR_WDGA) | w->base);
}

static void ww_reset_part(struct wwdg *w)
{
    if (w->s->reset_req)
        return;
    esk_rf_diag(w->s, ES_SEV_INFO, "mcu.watchdog_reset", "%s", "the window watchdog resets the part");
    w->s->reset_cause = ESK_RESET_WINDOW;
    w->s->reset_req = 1;
    w->s->yield = 1;
}

/* the counter's events up to now: EWIF at 0x40, the reset past it */
static void ww_sync(struct wwdg *w)
{
    u32 t = ww_count(w);
    u64 elapsed = esk_rf_gated(w->p) ? 0 : (w->s->cycles - w->base_at) / ww_unit(w);
    int passed_40 = w->base >= 0x40u && (elapsed >= (u64)(w->base - 0x40u));
    if (passed_40 && !w->ewi_done) {
        w->ewi_done = 1;
        esk_rf_hw(w->sr, w->sr->value | SR_EWIF);
        if (reg(w->cfr) & CFR_EWI)
            esk_rf_interrupt(w->p, w->irq);
    }
    if ((reg(w->cr) & CR_WDGA) && w->base >= 0x40u && elapsed > (u64)(w->base - 0x40u)) {
        ww_reset_part(w);
        return;
    }
    esk_rf_hw(w->cr, (w->cr->value & CR_WDGA) | t);
}

static void ww_clock(struct wwdg *w)
{
    esk_rf_clock(w->p, &w->running, (reg(w->cr) & CR_WDGA) || (reg(w->cfr) & CFR_EWI));
}

static void ww_tick(void *ctx, u32 cycles)
{
    struct wwdg *w = (struct wwdg *)ctx;
    (void)cycles;
    if (!w->running)
        return;
    ww_sync(w);
}

static int ww_next(void *ctx, u32 *cycles)
{
    struct wwdg *w = (struct wwdg *)ctx;
    u64 unit, at, now;
    if (!w->running || esk_rf_gated(w->p))
        return 0;
    unit = ww_unit(w);
    now = w->s->cycles - w->base_at;
    at = (now / unit + 1) * unit; /* the next count: EWIF and the reset happen on one */
    *cycles = at - now > 0xffffffffu ? 0xffffffffu : (u32)(at - now);
    return 1;
}

static u32 ww_read(void *ctx, u32 off, int n)
{
    struct wwdg *w = (struct wwdg *)ctx;
    if (off == esk_rf_off(w->cr) && !w->s->bus.debug)
        ww_sync(w);
    return esk_rf_read(w->p, off, n);
}

static void ww_write(void *ctx, u32 off, int n, u32 v)
{
    struct wwdg *w = (struct wwdg *)ctx;
    (void)n;
    ww_sync(w);
    if (off == esk_rf_off(w->cr)) {
        u32 was_on = reg(w->cr) & CR_WDGA, now = ww_count(w), win = reg(w->cfr) & 0x7fu;
        if (was_on && now > win) {
            ww_reset_part(w); /* refreshed outside the window */
            return;
        }
        esk_rf_hw(w->cr, was_on | (v & CR_WDGA)); /* WDGA only sets */
        ww_rebase(w, v & 0x7fu);
        if ((reg(w->cr) & CR_WDGA) && !(v & 0x40u))
            ww_reset_part(w); /* T6 written clear */
    } else if (off == esk_rf_off(w->cfr)) {
        u32 keep = reg(w->cfr) & CFR_EWI; /* EWI only sets */
        u32 t = ww_count(w);
        esk_rf_hw(w->cfr, (v & 0x3ffu) | keep);
        ww_rebase(w, t);
    } else if (off == esk_rf_off(w->sr)) {
        if (!(v & SR_EWIF))
            esk_rf_hw(w->sr, 0); /* rc_w0 */
    }
    ww_clock(w);
}

static void ww_reset(void *ctx)
{
    struct wwdg *w = (struct wwdg *)ctx;
    w->running = 0;
    w->base = 0x7fu;
    w->base_at = 0; /* the machine's cycles start again at 0 */
    w->ewi_done = 0;
}

const struct esk_dev_ops esk_stm32_wwdg_ops = {
    "stm32-wwdg", ww_read, ww_write, ww_reset, ww_tick, ww_next,
};

void *esk_stm32_wwdg_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct wwdg w, *c;
    memset(&w, 0, sizeof w);
    w.s = s;
    w.p = p;
    w.cr = esk_rf_reg(p, "CR");
    w.cfr = esk_rf_reg(p, "CFR");
    w.sr = esk_rf_reg(p, "SR");
    if (!w.cr || !w.cfr || !w.sr)
        return 0;
    w.irq = esk_rf_irq(p, "WWDG");
    w.base = 0x7fu;
    c = (struct wwdg *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = w;
    return c;
}
