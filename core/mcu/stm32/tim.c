/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* tim.c -- an STM32 timer's time base (RM0008 14.3.1-2, 15.3.1-2): what
 * every timer from the basic TIM6 to the advanced TIM1 has, over the
 * register file.
 *
 *   - CNT counts up while CR1.CEN is set, one count per PSC+1 clocks of
 *     the timer's clock; after ARR it goes back to 0 and that is an
 *     update event: SR.UIF sets and, with DIER.UIE, the timer's interrupt
 *     is made pending (the SVD's: the one named ..._UP_... if it has
 *     several, as TIM1 has). ARR of 0 holds the counter;
 *   - PSC is buffered: it takes effect at the next update event, as on
 *     the part; ARR too with CR1.ARPE, else at once;
 *   - EGR.UG reinitializes the counter and the prescaler and makes an
 *     update event, which sets UIF unless CR1.URS is set; CR1.UDIS stops
 *     update events (UG then only reinitializes); CR1.OPM clears CEN at
 *     the update event;
 *   - SR's flags are rc_w0: written 0, cleared; 1 leaves them;
 *   - the timer's clock is its APB's, from RCC's CFGR: the core's clock
 *     when the APB prescaler (PPRE1 or PPRE2) is 1, else twice the APB's
 *     clock (RM0090 6.2: the core's clock divided by the prescaler over
 *     2). Without RCC's registers, the core's clock;
 *   - it does not count while its clock is gated off (RCC's enable bit).
 * The counter width is CNT's fields' (TIM2 and TIM5 are 32-bit).
 *
 * Output compare (RM0008 14.3.8-10, 15.3.8-11), channels 1 to 4 with
 * CCxS 00 in CCMRx:
 *   - a match is the counter becoming CCRx: CCxIF sets and, with CCxIE,
 *     the timer's capture/compare interrupt is made pending (TIM1_CC on
 *     TIM1, the one interrupt on the others); OCxM 001, 010 and 011 make
 *     OCxREF active, inactive or toggle on it; 100 and 101 force it; PWM
 *     mode 1 (110) has it active while CNT < CCRx, mode 2 (111) while not;
 *     000 leaves it;
 *   - CCRx is buffered with OCxPE, taking its value at the update event;
 *   - with CCxE (and, on TIM1, BDTR.MOE) the channel's pin, configured as
 *     an alternate-function output, is OCxREF, inverted by CCxP; the pins
 *     are AFIO's: TIM1 PA8-PA11 (PE9-PE14 fully remapped), TIM2 PA0-PA3
 *     (partly or fully to PA15, PB3, PB10, PB11), TIM3 PA6, PA7, PB0,
 *     PB1 (PB4, PB5; or PC6-PC9), TIM4 PB6-PB9 (PD12-PD15);
 *   - a pin's level changes at the instruction that brings the counter to
 *     its edge, and a sleeping core's devices wake at it.
 *
 * Not modelled: counting down and center-aligned (DIR and CMS), the
 * repetition counter, input capture, the complementary outputs and dead
 * time, the break input, slave modes, triggers, DMA requests.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/stm32-tim.c. */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

struct tim {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr1, *dier, *sr, *egr, *cnt, *psc, *arr;
    u64 cen, udis, urs, opm, arpe, uie, uif, ug;
    u64 cntmask;                    /* the counter's bits */
    u32 psc_now, arr_now;           /* the shadow registers in use */
    u32 pc;                         /* the prescaler's counter */
    struct esk_rf_reg *cfgr;            /* RCC's, for the APB prescaler */
    u64 ppre;
    int ppre_lsb;
    u32 frac;                       /* core cycles toward the next clock */
    int irq, cc_irq, running;
    /* output compare */
    struct esk_rf_reg *ccmr[2], *ccer, *ccr[4], *bdtr;
    u64 ccif[4], ccie[4];
    u32 ccr_now[4];                 /* CCRx as the comparator has it */
    u8 ref[4];                      /* OCxREF */
    int out[4];                     /* the pin's level, or -1: not driven */
    int unit;                       /* TIMn's n, for the pins; 0 if not 1-4 */
};

static int gated(const struct tim *t)
{
    return t->p->gate && !(t->p->gate->value & t->p->gate_mask);
}

/* core cycles per timer clock: 1, or with an APB prescaler of 4, 8 or 16,
 * 2, 4 or 8 */
static u32 divisor(const struct tim *t)
{
    if (!t->ppre)
        return 1;
    u32 v = (u32)((t->cfgr->value & t->ppre) >> t->ppre_lsb);
    return v < 5 ? 1 : 1u << (v - 4);
}

static void irq_check(struct tim *t)
{
    int ch;
    if ((t->sr->value & t->uif) && (t->dier->value & t->uie))
        esk_rf_interrupt(t->p, t->irq);
    for (ch = 0; ch < 4; ch++)
        if (t->ccif[ch] && (t->sr->value & t->ccif[ch]) && (t->dier->value & t->ccie[ch]))
            esk_rf_interrupt(t->p, t->cc_irq);
}

/* ---- output compare ---------------------------------------------------------- */

/* channel ch's CCMR byte: CCxS in 1:0, OCxPE 3, OCxM 6:4 */
static u32 ccm(const struct tim *t, int ch)
{
    if (!t->ccmr[ch / 2])
        return 0;
    return (u32)(t->ccmr[ch / 2]->value >> (8 * (ch & 1))) & 0xffu;
}

static int is_output(const struct tim *t, int ch) { return t->ccr[ch] && (ccm(t, ch) & 3u) == 0; }
static u32 ocm(const struct tim *t, int ch) { return ccm(t, ch) >> 4 & 7u; }

/* some channel compares: its flag, its interrupt or its pin to keep */
static int oc_any(const struct tim *t)
{
    int ch;
    if (!t->ccer)
        return 0;
    for (ch = 0; ch < 4; ch++)
        if (is_output(t, ch) && (ocm(t, ch) || (t->ccer->value >> (4 * ch) & 1) || (t->dier->value & t->ccie[ch])))
            return 1;
    return 0;
}

/* the counter became CCRx on channel ch */
static void match(struct tim *t, int ch)
{
    u32 m = ocm(t, ch);
    if (t->ccif[ch]) {
        esk_rf_hw(t->sr, t->sr->value | t->ccif[ch]);
        if (t->dier->value & t->ccie[ch])
            esk_rf_interrupt(t->p, t->cc_irq);
    }
    if (m == 1)
        t->ref[ch] = 1;
    else if (m == 2)
        t->ref[ch] = 0;
    else if (m == 3)
        t->ref[ch] ^= 1;
}

/* the counter's values (a, b] passed: the channels whose CCRx is there */
static void matches(struct tim *t, u64 a, u64 b)
{
    int ch;
    for (ch = 0; ch < 4; ch++)
        if (is_output(t, ch) && a < t->ccr_now[ch] && t->ccr_now[ch] <= b)
            match(t, ch);
}

static void matches_at_zero(struct tim *t)
{
    int ch;
    for (ch = 0; ch < 4; ch++)
        if (is_output(t, ch) && t->ccr_now[ch] == 0)
            match(t, ch);
}

/* OCxREF for the levels it has from the counter, and the pins */
static void outputs(struct tim *t)
{
    u64 cnt = t->cnt->value & t->cntmask;
    int ch, changed = 0;
    int moe = !t->bdtr || (t->bdtr->value >> 15 & 1);
    for (ch = 0; ch < 4; ch++) {
        u32 m = ocm(t, ch);
        int lvl = -1;
        if (!is_output(t, ch))
            m = 0;
        if (m == 4)
            t->ref[ch] = 0;
        else if (m == 5)
            t->ref[ch] = 1;
        else if (m == 6)
            t->ref[ch] = cnt < t->ccr_now[ch];
        else if (m == 7)
            t->ref[ch] = !(cnt < t->ccr_now[ch]);
        if (t->ccer && is_output(t, ch) && (t->ccer->value >> (4 * ch) & 1) && moe)
            lvl = t->ref[ch] ^ (int)(t->ccer->value >> (4 * ch + 1) & 1);
        if (lvl != t->out[ch]) {
            t->out[ch] = lvl;
            changed = 1;
        }
    }
    if (changed)
        esk_stm32f1_pins_changed(t->s);
}

static const struct tim *tim_unit(struct esk_mach *s, int unit)
{
    char name[8] = "TIMx";
    struct esk_rf_periph *p;
    name[3] = (char)('0' + unit);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_stm32_tim_ops ? (const struct tim *)p->ctx : 0;
}

/* channel ch's pin of TIMunit, as AFIO's MAPR places it: port * 16 + bit */
static int tim_pin_of(u32 mapr, int unit, int ch)
{
    static const int t1[2][4] = { { 8, 9, 10, 11 }, { 4 * 16 + 9, 4 * 16 + 11, 4 * 16 + 13, 4 * 16 + 14 } };
    static const int t2a[4] = { 0, 1, 2, 3 }, t2b[4] = { 15, 16 + 3, 16 + 10, 16 + 11 };
    static const int t3[3][4] = { { 6, 7, 16, 17 }, { 16 + 4, 16 + 5, 16, 17 }, { 32 + 6, 32 + 7, 32 + 8, 32 + 9 } };
    u32 r;
    switch (unit) {
    case 1:
        return t1[(mapr >> 6 & 3u) == 3 ? 1 : 0][ch];
    case 2:
        r = mapr >> 8 & 3u; /* bit 0: CH1-2 remapped, bit 1: CH3-4 */
        return ch < 2 ? ((r & 1) ? t2b[ch] : t2a[ch]) : ((r & 2) ? t2b[ch] : t2a[ch]);
    case 3:
        r = mapr >> 10 & 3u;
        return t3[r == 2 ? 1 : r == 3 ? 2 : 0][ch];
    case 4:
        return (mapr >> 12 & 1) ? 48 + 12 + ch : 16 + 6 + ch;
    default:
        return -1;
    }
}

int esk_stm32f1_tim_pin(struct esk_mach *s, int port, int bit)
{
    u32 mapr = esk_stm32f1_mapr(s);
    int unit, ch;
    for (unit = 1; unit <= 4; unit++) {
        const struct tim *t = tim_unit(s, unit);
        if (!t)
            continue;
        for (ch = 0; ch < 4; ch++)
            if (t->out[ch] >= 0 && tim_pin_of(mapr, unit, ch) == port * 16 + bit)
                return t->out[ch];
    }
    return -1;
}

static void run_clock(struct tim *t)
{
    esk_rf_clock(t->p, &t->running, (t->cr1->value & t->cen) != 0);
}

/* an update event; `ug`: made by EGR.UG */
static void update(struct tim *t, int ug)
{
    if (t->cr1->value & t->udis)
        return;
    t->psc_now = (u32)t->psc->value & 0xffff;
    t->arr_now = (u32)(t->arr->value & t->cntmask);
    for (int ch = 0; ch < 4; ch++)
        if (t->ccr[ch])
            t->ccr_now[ch] = (u32)(t->ccr[ch]->value & t->cntmask);
    if (!(ug && (t->cr1->value & t->urs))) {
        esk_rf_hw(t->sr, t->sr->value | t->uif);
        irq_check(t);
    }
    if (!ug && (t->cr1->value & t->opm)) {
        esk_rf_hw(t->cr1, t->cr1->value & ~t->cen);
        run_clock(t);
    }
}

/* `n` counts of the counter; with `oc`, the compare channels' matches */
static void count(struct tim *t, u64 n, int oc)
{
    u64 cnt = t->cnt->value & t->cntmask;
    if (cnt + n <= t->arr_now) {    /* no update on the way */
        if (oc)
            matches(t, cnt, cnt + n);
        t->cnt->value += n;
        return;
    }
    while (n && (t->cr1->value & t->cen)) {
        u64 to;                     /* counts to the next wrap */
        if (cnt > t->arr_now)
            to = t->cntmask - cnt + 1;          /* past ARR: to 0, no event */
        else
            to = t->arr_now - cnt + 1;
        if (n < to) {
            if (oc)
                matches(t, cnt, cnt + n);
            cnt += n;
            break;
        }
        if (oc)
            matches(t, cnt, cnt + to - 1);
        n -= to;
        int event = cnt <= t->arr_now;
        cnt = 0;
        t->cnt->value = (t->cnt->value & ~t->cntmask) | cnt;
        if (event)
            update(t, 0);
        if (oc)
            matches_at_zero(t);
    }
    t->cnt->value = (t->cnt->value & ~t->cntmask) | cnt;
}

/* called after each instruction while CEN is set: kept short */
static void tim_tick(void *ctx, u32 cycles)
{
    struct tim *t = (struct tim *)ctx;
    if (!t->arr_now || gated(t))
        return;
    u64 clocks = cycles;
    u32 d = divisor(t);
    if (d > 1) {
        u32 all = t->frac + cycles;
        clocks = all / d;
        t->frac = all % d;
    }
    u64 all = t->pc + clocks;
    if (all <= t->psc_now) {        /* the prescaler has not wrapped */
        t->pc = (u32)all;
        return;
    }
    int oc = oc_any(t);
    if (!t->psc_now) {
        t->pc = 0;
        count(t, all, oc);
    } else {
        u64 per = (u64)t->psc_now + 1;
        t->pc = (u32)(all % per);
        count(t, all / per, oc);
    }
    if (oc)
        outputs(t);
}

/* counts from the counter's value until it is v */
static u64 counts_to(const struct tim *t, u64 cnt, u64 v)
{
    u64 wrap = cnt > t->arr_now ? t->cntmask - cnt + 1 : t->arr_now - cnt + 1;
    if (v > cnt && (v <= t->arr_now || cnt > t->arr_now))
        return v - cnt;
    return v <= t->arr_now ? wrap + v : 0;
}

static int tim_next_event(void *ctx, u32 *cycles)
{
    struct tim *t = (struct tim *)ctx;
    u64 cnt, counts = 0;
    int ch;
    if (!(t->cr1->value & t->cen) || !t->arr_now || gated(t))
        return 0;
    cnt = t->cnt->value & t->cntmask;
    if ((t->dier->value & t->uie) && !(t->cr1->value & t->udis))
        counts = counts_to(t, cnt, 0);
    if (oc_any(t)) {
        u64 c = counts_to(t, cnt, 0); /* PWM's edge at the update */
        if (c && (!counts || c < counts))
            counts = c;
        for (ch = 0; ch < 4; ch++) {
            if (!is_output(t, ch))
                continue;
            c = counts_to(t, cnt, t->ccr_now[ch]);
            if (c && (!counts || c < counts))
                counts = c;
        }
    }
    if (!counts)
        return 0;
    u64 per = (u64)t->psc_now + 1;
    u64 clocks = (per - t->pc) + (counts - 1) * per;
    u64 c = clocks * divisor(t) - t->frac;
    *cycles = c > 0xffffffffULL ? 0xffffffffu : (u32)c;
    return 1;
}

static u32 tim_read(void *ctx, u32 off, int n)
{
    struct tim *t = (struct tim *)ctx;
    return esk_rf_read(t->p, off, n);
}

static void tim_write(void *ctx, u32 off, int n, u32 v)
{
    struct tim *t = (struct tim *)ctx;
    esk_rf_write(t->p, off, n, v);
    if (off == esk_rf_off(t->arr) && !(t->cr1->value & t->arpe))
        t->arr_now = (u32)(t->arr->value & t->cntmask);
    if (off == esk_rf_off(t->egr) && (v & t->ug)) {
        /* UG: the counter and the prescaler start again */
        t->cnt->value &= ~t->cntmask;
        t->pc = 0;
        update(t, 1);
    }
    for (int ch = 0; ch < 4; ch++) {
        if (t->ccr[ch] && off == esk_rf_off(t->ccr[ch]) && !(ccm(t, ch) & 8u))
            t->ccr_now[ch] = (u32)(t->ccr[ch]->value & t->cntmask);
        if (off == esk_rf_off(t->egr) && (v >> (ch + 1) & 1))
            match(t, ch); /* CCxG */
    }
    run_clock(t);
    irq_check(t);
    if (t->ccer)
        outputs(t);
}

static void tim_reset(void *ctx)
{
    struct tim *t = (struct tim *)ctx;
    t->psc_now = (u32)t->psc->value & 0xffff;
    t->arr_now = (u32)(t->arr->value & t->cntmask);
    t->pc = t->frac = 0;
    t->running = 0;
    for (int ch = 0; ch < 4; ch++) {
        t->ccr_now[ch] = t->ccr[ch] ? (u32)(t->ccr[ch]->value & t->cntmask) : 0;
        t->ref[ch] = 0;
        t->out[ch] = -1;
    }
}

const struct esk_dev_ops esk_stm32_tim_ops = {
    "stm32-tim", tim_read, tim_write, tim_reset, tim_tick, tim_next_event,
};

void *esk_stm32_tim_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct tim t;
    memset(&t, 0, sizeof t);
    t.s = s;
    t.p = p;
    t.cr1 = esk_rf_reg(p, "CR1");
    t.dier = esk_rf_reg(p, "DIER");
    t.sr = esk_rf_reg(p, "SR");
    t.egr = esk_rf_reg(p, "EGR");
    t.cnt = esk_rf_reg(p, "CNT");
    t.psc = esk_rf_reg(p, "PSC");
    t.arr = esk_rf_reg(p, "ARR");
    if (!t.cr1 || !t.dier || !t.sr || !t.egr || !t.cnt || !t.psc || !t.arr)
        return 0;
    t.cen = esk_rf_mask(t.cr1, "CEN");
    t.udis = esk_rf_mask(t.cr1, "UDIS");
    t.urs = esk_rf_mask(t.cr1, "URS");
    t.opm = esk_rf_mask(t.cr1, "OPM");
    t.arpe = esk_rf_mask(t.cr1, "ARPE");
    t.uie = esk_rf_mask(t.dier, "UIE");
    t.uif = esk_rf_mask(t.sr, "UIF");
    t.ug = esk_rf_mask(t.egr, "UG");
    if (!t.cen || !t.uif || !t.ug)
        return 0;
    for (int i = 0; i < t.cnt->nf; i++) {
        int top = t.cnt->f[i].lsb + t.cnt->f[i].width;
        u64 m = top >= 64 ? ~0ULL : (1ULL << top) - 1;
        if (m > t.cntmask)
            t.cntmask = m;
    }
    if (t.cntmask > 0xffffffffULL || !t.cntmask)
        t.cntmask = 0xffff;
    esk_rf_set_mwv(t.sr, ~0ULL, ESK_MWV_0CLR);
    t.irq = esk_rf_irq(p, "_UP");
    t.cc_irq = esk_rf_irq(p, "_CC");
    t.ccmr[0] = esk_rf_reg(p, "CCMR1");
    t.ccmr[1] = esk_rf_reg(p, "CCMR2");
    t.ccer = esk_rf_reg(p, "CCER");
    t.bdtr = esk_rf_reg(p, "BDTR");
    for (int ch = 0; ch < 4; ch++) {
        char n[8];
        snprintf(n, sizeof n, "CCR%d", ch + 1);
        t.ccr[ch] = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "CC%dIF", ch + 1);
        t.ccif[ch] = esk_rf_mask(t.sr, n);
        snprintf(n, sizeof n, "CC%dIE", ch + 1);
        t.ccie[ch] = esk_rf_mask(t.dier, n);
    }
    if (!strncmp(p->name, "TIM", 3) && p->name[3] >= '1' && p->name[3] <= '4' && !p->name[4])
        t.unit = p->name[3] - '0';
    /* the APB it is on: the one whose enable register has its bit */
    struct esk_rf_periph *rcc = esk_rf_periph(s->rf, "RCC");
    t.cfgr = esk_rf_reg(rcc, "CFGR");
    if (t.cfgr && p->gate_name)
        t.ppre = esk_rf_mask(t.cfgr, strstr(p->gate_name, "APB2") ? "PPRE2" : "PPRE1");
    while (t.ppre && !(t.ppre >> t.ppre_lsb & 1))
        t.ppre_lsb++;
    struct tim *c = (struct tim *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = t;
    tim_reset(c);
    return c;
}
