/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* tim.c -- an STM32 timer (RM0008 14-15, RM0390 12-16): the basic TIM6
 * and TIM7, the general-purpose TIM2-TIM5 and TIM9-TIM14 and the advanced
 * TIM1 and TIM8 are one IP with more or fewer registers, and each timer
 * has the parts its registers have, over the register file. The bits are
 * at the reference manuals' positions, the same on every family.
 *
 * Time base:
 *   - the counter's clock is the timer's own (esk_stm32_timclk: its APB's
 *     clock, twice that when the APB prescaler is not 1, four times with
 *     the F4's TIMPRE), one count per PSC+1 of its ticks; CEN starts it;
 *   - CR1's CMS and DIR: up (0 to ARR, then 0 with an overflow), down (ARR
 *     to 0, then ARR with an underflow) or center-aligned (0 up to ARR and
 *     back to 0, an overflow at ARR and an underflow at 0, DIR showing the
 *     direction); ARR of 0 holds the counter;
 *   - the repetition counter (RCR, the advanced timers'): the update event
 *     comes every RCR+1 overflows and underflows;
 *   - an update event loads PSC, ARR (with ARPE; else ARR acts at once),
 *     RCR and the preloaded CCRx, sets SR.UIF (not for EGR.UG with URS)
 *     and with DIER.UIE pends the update interrupt (the one named _UP_ on
 *     the advanced timers; the timer's one on the others); UDIS stops
 *     update events; OPM clears CEN at it; AOE sets MOE again at it;
 *   - EGR.UG reinitializes the prescaler and the counter (to 0, or ARR
 *     counting down) and makes an update event;
 *   - SR's flags are rc_w0;
 *   - it does not count while its clock is gated off (RCC's enable bit).
 *   The counter's width is CNT's fields' (TIM2 and TIM5 are 32-bit).
 *
 * Capture/compare channels (as many as CCRx registers):
 *   - output compare (CCxS 00): a match is the counter becoming CCRx, in
 *     either direction (center-aligned CMS 01 flags it only counting down,
 *     10 only up, 11 both): CCxIF and, with CCxIE, the capture/compare
 *     interrupt. OCxM 001, 010 and 011 make OCxREF active, inactive or
 *     toggle on it, 100 and 101 force it, PWM mode 1 (110) has it active
 *     while CNT < CCRx counting up and CNT <= CCRx counting down, mode 2
 *     (111) the opposite; CCRx is preloaded with OCxPE; OCxCE clears
 *     OCxREF while ETRF is high, until the next update event; CCxG makes a
 *     match;
 *   - input capture (CCxS 01: ICx on TIx, 10: on the neighbouring TIy, 11:
 *     on TRC, the trigger input): with CCxE, the edge CCxP and CCxNP select
 *     (rising, falling or both), counted by ICxPSC (every 1, 2, 4 or 8),
 *     copies CNT into CCRx, sets CCxIF (CCxOF if it was set) and pends the
 *     interrupt; reading CCRx clears CCxIF. The input's filter (ICxF:
 *     N samples at fCK_INT or fDTS/2 to /32) passes a level only once it
 *     held for N sampling periods, so shorter pulses vanish; TI1S makes TI1
 *     the XOR of CH1, CH2 and CH3;
 *   - the pins: with CCxE (and BDTR.MOE on the advanced timers) the
 *     channel's output OCx is OCxREF inverted by CCxP; the advanced
 *     timers' complementary OCxN (CCxNE, CCxNP) comes with the dead time
 *     BDTR.DTG gives (a rising edge of each delayed by it, a pulse shorter
 *     than it lost), MOE 0 gives the idle levels OISx and OISxN when OSSI
 *     is set and lets the pins go when not, and OSSR keeps a disabled
 *     channel's pin at its inactive level while MOE is set; a channel
 *     off lets its pin go. The pins are AFIO's on the F1 (TIM1-TIM4, the
 *     remaps of MAPR) and the alternate functions of io.alternate on the F4
 *     (TIMx_CHy, TIMx_CHyN, TIMx_BKIN, TIMx_ETR);
 *   - break (BDTR.BKE): the BKIN pin at the BKP level, or EGR.BG, clears
 *     MOE at once, sets BIF and pends the break interrupt; MOE cannot be
 *     set while the break input is active; LOCK's levels 1 to 3 freeze the
 *     bits RM0390 lists until reset;
 *   - COM: with CR2.CCPC, CCxE, CCxNE and OCxM act only at a commutation
 *     event (EGR.COMG, or TRGI's rising edge with CCUS), which sets COMIF
 *     and pends the trigger/commutation interrupt.
 *
 * Slave and master:
 *   - SMCR's trigger input TRGI (TS): ITR0-ITR3 (other timers' TRGO, by
 *     RM0390's and RM0008's interconnection tables), TI1F_ED (both edges of
 *     TI1), TI1FP1, TI2FP2 (polarity CC1P, CC2P) or ETRF;
 *   - the slave mode (SMS): reset (TRGI's rising edge reinitializes the
 *     counter with an update event), gated (counting while TRGI is high),
 *     trigger (its rising edge sets CEN), external clock mode 1 (each
 *     rising edge one tick of the prescaler) and the encoder modes 1-3
 *     (TI2's, TI1's or both inputs' edges counted up or down by the other
 *     input's level, DIR showing the direction); every trigger sets TIF and
 *     with TIE pends the trigger interrupt; EGR.TG makes one;
 *   - external clock mode 2 (SMCR.ECE): ETR's rising edges, inverted by
 *     ETP, divided by ETPS and filtered by ETF, clock the prescaler;
 *   - CR2.MMS's TRGO for the timers that listen: reset (UG), enable (CEN),
 *     update, compare pulse (a channel 1 match) or OC1REF-OC4REF.
 *
 * Inside: the counter moves after each instruction by the timer's ticks
 * since; the edges a pin sees between instructions are taken at the next
 * instruction (input_lead counted back for a capture from outside). A pin
 * changes at the instruction that brings the counter to its edge, and a
 * sleeping core's devices wake at it.
 *
 * DMA: DIER's UDE, CCxDE (at the update with CR2.CCDS), TDE and COMDE make
 *   the requests TIMn_UP, TIMn_CHx, TIMn_TRIG and TIMn_COM, which wait for
 *   the DMA; DMAR reaches the DBL + 1 registers from DCR's DBA in turn (the
 *   burst).
 *
 * TIMx_OR's internal remaps (the F4's): TIM5's IT4_RMP puts LSI (32 kHz,
 * once RCC's LSIRDY) or LSE (once LSERDY) on TI4 instead of the pin, as
 * HAL's way of measuring LSI does; TIM11's RMP puts HSE_RTC (HSE over
 * CFGR's RTCPRE) on TI1; TIM2's ITR1_RMP takes ITR1 from TIM8's TRGO or
 * the OTG controllers' SOF (none here). A remapped clock's edges come at
 * its half periods, at the core clock.
 *
 * Not modelled: TIM5's TI4 on the RTC's wake-up interrupt (IT4_RMP 11:
 * no edges), the OTG controllers' SOF on TIM2's ITR1, MSM's delay (TRGI acts at once
 * anyway), the forced idle levels' dead time after a break, the fast
 * output enable (OCxFE: the compare already acts at the match), the
 * F1's TIM1 complementary, break and ETR pins and TIM5-TIM8 (no F1 part
 * in the catalog has them).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/stm32-tim.c (the time
 * base and output compare), extended to the whole timer from the
 * reference manuals. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

/* CR1 */
#define CEN (1u << 0)
#define UDIS (1u << 1)
#define URS (1u << 2)
#define OPM (1u << 3)
#define DIR (1u << 4)
#define ARPE (1u << 7)
/* CR2 */
#define CCPC (1u << 0)
#define CCUS (1u << 2)
#define CCDS (1u << 3)
#define TI1S (1u << 7)
/* SMCR */
#define ECE (1u << 14)
#define ETP (1u << 15)
/* DIER and SR */
#define UIF (1u << 0)
#define COMIF (1u << 5)
#define TIF (1u << 6)
#define BIF (1u << 7)
/* EGR */
#define UG (1u << 0)
#define COMG (1u << 5)
#define TG (1u << 6)
#define BG (1u << 7)
/* BDTR */
#define OSSI (1u << 10)
#define OSSR (1u << 11)
#define BKE (1u << 12)
#define BKP (1u << 13)
#define AOE (1u << 14)
#define MOE (1u << 15)

enum { SMS_OFF, SMS_ENC1, SMS_ENC2, SMS_ENC3, SMS_RESET, SMS_GATED, SMS_TRIGGER, SMS_EXT1 };
enum { IN_ETR = 4, IN_BKIN = 5 };

struct tim {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr1, *cr2, *smcr, *dier, *sr, *egr, *cnt, *psc, *arr, *rcr, *bdtr;
    struct esk_rf_reg *ccmr[2], *ccer, *ccr[4];
    int nch;                        /* capture/compare channels */
    int apb;                        /* 1 or 2 */
    int unit;                       /* TIMn's n */
    int itr[4];                     /* ITR0-ITR3: the masters' units, 0 none */
    u64 cntmask;
    u32 psc_now, arr_now, rcr_now, rep, ccr_now[4];
    u32 pc;                         /* the prescaler's counter */
    u32 frac;                       /* core cycles toward the next tick */
    u64 now;                        /* core cycles since creation */
    u64 next_at;                    /* the earliest deadline below, 0 none */
    int irq_up, irq_cc, irq_brk, irq_com, running;
    /* outputs */
    u8 ref[4], cleared[4];
    u8 dref[4], drefn[4];           /* OCxREF and its complement after the dead time */
    u64 dref_at[4], drefn_at[4];    /* when they take their new level, 0 none */
    int out[4], outn[4];            /* the pins' levels, or -1: not driven */
    u32 ccer_act, ocm_act[4];       /* CCxE/CCxNE and OCxM as they act (CCPC) */
    int lock, lock_written;         /* BDTR.LOCK, written once after reset */
    int brk;                        /* the break input is active */
    /* inputs: CH1-CH4, ETR, BKIN */
    int raw[6];                     /* the pins' levels, -1 never seen */
    int flt[6];                     /* after the filter */
    int pend[6];                    /* the level the filter waits on */
    u64 pend_at[6];
    u8 icpsc[4];                    /* captures counted toward ICxPSC */
    u32 etr_edges;                  /* ETR's rising edges, for ETPS */
    int etrp;                       /* ETR after polarity and prescaler */
    int trgi;                       /* TRGI's level */
    int trgo;                       /* TRGO's level */
    /* DMA: the requests' names, the burst's register */
    char sig_up[12], sig_ch[4][12], sig_trig[12], sig_com[12];
    struct esk_rf_reg *dcr, *dmar;
    u32 burst_idx;
    /* the trigger outputs the ADCs and the DAC listen to: TIMx_CCy (an
     * output channel's OCyREF, a pulse at a capture or a frozen channel's
     * match) and TIMx_TRGO */
    char sig_cc[4][12], sig_trgo[12];
    int cc_level[4];
    /* TIMx_OR's internal remaps: the input (0 for TI1, 3 for TI4) a clock
     * inside the part drives instead of its pin, -1 none; its kind; the
     * next edge, counted from t0 at the core clock it was scheduled at */
    struct esk_rf_reg *orr;
    int src_x, src_kind;
    u64 src_t0, src_k, src_at;
    u32 src_core_hz, src_hz_last;
};

enum { SRC_NONE, SRC_LSI, SRC_LSE, SRC_HSE_RTC };

static void interconnect(struct tim *t);

/* a DMA request (DIER's UDE, CCxDE, TDE, COMDE): it waits for the DMA */
static void dma_req(struct tim *t, u32 de, const char *sig)
{
    if (t->dier->value & de)
        esk_stm32_dma_request(t->s, sig, 1);
}

static u32 bits(const struct esk_rf_reg *r, int lsb, int width)
{
    return r ? (u32)(r->value >> lsb) & ((1u << width) - 1) : 0;
}

static int gated(const struct tim *t)
{
    return t->p->gate && !(t->p->gate->value & t->p->gate_mask);
}

static u32 cms(const struct tim *t) { return bits(t->cr1, 5, 2); }
static u32 sms(const struct tim *t) { return bits(t->smcr, 0, 3); }
static int encoder(const struct tim *t) { return sms(t) >= SMS_ENC1 && sms(t) <= SMS_ENC3; }
static int counting_down(const struct tim *t) { return (t->cr1->value & DIR) != 0; }

/* core cycles per tick of the timer's clock: 1, 2, 4 or 8 */
static u32 divisor(const struct tim *t)
{
    u32 h = esk_stm32_hclk(t->s), k = esk_stm32_timclk(t->s, t->apb);
    return h && k && h > k ? h / k : 1;
}

/* fDTS's period in timer ticks (CR1.CKD) */
static u32 dts(const struct tim *t)
{
    u32 ckd = bits(t->cr1, 8, 2);
    return ckd == 1 ? 2 : ckd == 2 ? 4 : 1;
}

static void irq(struct tim *t, int n)
{
    if (n >= 0)
        esk_rf_interrupt(t->p, n);
}

static void irq_check(struct tim *t)
{
    u32 sr = (u32)t->sr->value, ie = (u32)t->dier->value;
    if (sr & ie & UIF)
        irq(t, t->irq_up);
    if (sr & ie & 0x1Eu)
        irq(t, t->irq_cc);
    if (sr & ie & (COMIF | TIF))
        irq(t, t->irq_com);
    if (sr & ie & BIF)
        irq(t, t->irq_brk);
}

static void set_flag(struct tim *t, u32 f)
{
    esk_rf_hw(t->sr, t->sr->value | f);
    irq_check(t);
}

/* ---- channels ------------------------------------------------------------ */

/* channel ch's CCMR byte: CCxS 1:0, OCxPE 3, OCxM 6:4, OCxCE 7 (output);
 * ICxPSC 3:2, ICxF 7:4 (input) */
static u32 ccm(const struct tim *t, int ch)
{
    return t->ccmr[ch / 2] ? (u32)(t->ccmr[ch / 2]->value >> (8 * (ch & 1))) & 0xffu : 0;
}

static int is_output(const struct tim *t, int ch) { return ch < t->nch && (ccm(t, ch) & 3u) == 0; }
static int is_input(const struct tim *t, int ch) { return ch < t->nch && (ccm(t, ch) & 3u) != 0; }
static u32 ccer(const struct tim *t) { return t->ccer ? (u32)t->ccer->value : 0; }

/* CCxE and CCxNE as they act, and OCxM: the registers', or with CCPC the
 * values of the last commutation event */
static int ccpc(const struct tim *t) { return t->cr2 && (t->cr2->value & CCPC); }
static u32 ccer_act(const struct tim *t) { return ccpc(t) ? t->ccer_act : ccer(t); }
static u32 ocm(const struct tim *t, int ch) { return ccpc(t) ? t->ocm_act[ch] : ccm(t, ch) >> 4 & 7u; }

static void commutation(struct tim *t)
{
    int ch;
    t->ccer_act = ccer(t);
    for (ch = 0; ch < 4; ch++)
        t->ocm_act[ch] = ccm(t, ch) >> 4 & 7u;
}

static void trgo_set(struct tim *t, int level);
static void trgo_pulse(struct tim *t);

/* the counter became CCRx on output channel ch, counting down or up */
static void match(struct tim *t, int ch, int down)
{
    u32 m = ocm(t, ch), c = cms(t);
    if (!c || c == 3 || (c == 1 && down) || (c == 2 && !down)) {
        set_flag(t, 1u << (ch + 1));
        if (!(t->cr2 && (t->cr2->value & CCDS)))
            dma_req(t, 1u << (ch + 9), t->sig_ch[ch]);
    }
    if (ch == 0 && t->cr2 && bits(t->cr2, 4, 3) == 3)
        trgo_pulse(t); /* MMS: compare pulse */
    if (m == 0) {
        esk_stm32_trigger(t->s, t->sig_cc[ch], 1); /* frozen: the match's event */
        esk_stm32_trigger(t->s, t->sig_cc[ch], 0);
    }
    if (m == 1)
        t->ref[ch] = 1;
    else if (m == 2)
        t->ref[ch] = 0;
    else if (m == 3)
        t->ref[ch] ^= 1;
}

/* the counter passed every value in [lo, hi], counting down or up */
static void matches(struct tim *t, u64 lo, u64 hi, int down)
{
    int ch;
    for (ch = 0; ch < t->nch; ch++)
        if (is_output(t, ch) && lo <= t->ccr_now[ch] && t->ccr_now[ch] <= hi)
            match(t, ch, down);
}

/* some channel or event is to be kept up with: its counts are worth
 * stepping through rather than jumping. An interrupt or a DMA request
 * enabled in DIER, one-pulse mode, a TRGO from an event, or an output */
static int watched(const struct tim *t)
{
    int ch;
    if ((t->dier->value & 0x7FFFu) || (t->cr1->value & OPM) || (t->cr2 && bits(t->cr2, 4, 3) >= 2))
        return 1;
    for (ch = 0; ch < t->nch; ch++)
        if (is_output(t, ch) && (ocm(t, ch) || (ccer(t) >> (4 * ch) & 5)))
            return 1;
    return 0;
}

/* ---- the outputs ------------------------------------------------------------ */

/* the dead time in core cycles (BDTR.DTG, RM0390 17.4.18) */
static u64 dead_time(const struct tim *t)
{
    u32 g = bits(t->bdtr, 0, 8), n;
    u64 tdts = (u64)dts(t) * divisor(t);
    if (!(g & 0x80))
        n = g;
    else if ((g & 0xC0) == 0x80)
        n = (64 + (g & 0x3F)) * 2;
    else if ((g & 0xE0) == 0xC0)
        n = (32 + (g & 0x1F)) * 8;
    else
        n = (32 + (g & 0x1F)) * 16;
    return n * tdts;
}

static int moe(const struct tim *t) { return !t->bdtr || (t->bdtr->value & MOE); }

/* the earliest filter or dead-time deadline, kept for the tick */
static void deadlines(struct tim *t)
{
    u64 best = 0;
    int i;
    for (i = 0; i < 6; i++)
        if (t->pend_at[i] && (!best || t->pend_at[i] < best))
            best = t->pend_at[i];
    for (i = 0; i < 3; i++) {
        if (t->dref_at[i] && (!best || t->dref_at[i] < best))
            best = t->dref_at[i];
        if (t->drefn_at[i] && (!best || t->drefn_at[i] < best))
            best = t->drefn_at[i];
    }
    if (t->src_at && (!best || t->src_at < best))
        best = t->src_at;
    t->next_at = best;
}

/* OCxREF from the counter, its dead-time copies, and the pins */
static void outputs(struct tim *t)
{
    u64 cnt = t->cnt->value & t->cntmask;
    u32 e = ccer_act(t), pol = ccer(t), cr2 = t->cr2 ? (u32)t->cr2->value : 0;
    u32 bd = t->bdtr ? (u32)t->bdtr->value : 0;
    int ch, changed = 0, down = counting_down(t);
    u64 dt = t->bdtr ? dead_time(t) : 0;
    int etrf = t->flt[IN_ETR] > 0;
    for (ch = 0; ch < t->nch; ch++) {
        u32 m = is_output(t, ch) ? ocm(t, ch) : 0;
        int cce = (e >> (4 * ch) & 1) != 0, ccne = (e >> (4 * ch + 2) & 1) != 0;
        int ccp = (int)(pol >> (4 * ch + 1) & 1), ccnp = (int)(pol >> (4 * ch + 3) & 1);
        int o = -1, on = -1;
        if (m == 4)
            t->ref[ch] = 0;
        else if (m == 5)
            t->ref[ch] = 1;
        else if (m == 6)
            t->ref[ch] = down ? cnt <= t->ccr_now[ch] : cnt < t->ccr_now[ch];
        else if (m == 7)
            t->ref[ch] = down ? !(cnt <= t->ccr_now[ch]) : !(cnt < t->ccr_now[ch]);
        if (m && (ccm(t, ch) & 0x80) && etrf)
            t->cleared[ch] = 1; /* OCxCE: ETRF high clears it until the update */
        if (t->cleared[ch])
            t->ref[ch] = 0;
        if (!is_output(t, ch)) {
            if (t->out[ch] != -1 || t->outn[ch] != -1)
                changed = 1;
            t->out[ch] = t->outn[ch] = -1;
            continue;
        }
        /* the dead-time generator: OCxREF and its complement, each rising
         * edge delayed by the dead time when both outputs are on */
        {
            int complementary = t->bdtr && ch < 3 && cce && ccne && dt;
            int want = t->ref[ch];
            if (!want) {
                t->dref[ch] = 0;
                t->dref_at[ch] = 0;
            } else if (!t->dref[ch] && !t->dref_at[ch]) {
                if (complementary)
                    t->dref_at[ch] = t->now + dt;
                else
                    t->dref[ch] = 1;
            }
            if (want) {
                t->drefn[ch] = 0;
                t->drefn_at[ch] = 0;
            } else if (!t->drefn[ch] && !t->drefn_at[ch]) {
                if (complementary)
                    t->drefn_at[ch] = t->now + dt;
                else
                    t->drefn[ch] = 1;
            }
        }
        if (!t->bdtr) {
            o = cce ? t->ref[ch] ^ ccp : -1;
        } else if (moe(t)) {
            int ossr = (bd & OSSR) != 0;
            if (ch < 3 && cce && ccne) {
                o = t->dref[ch] ^ ccp;
                on = t->drefn[ch] ^ ccnp;
            } else {
                /* one output on: OCxREF with its polarity, no dead time;
                 * the other off, at its inactive level with OSSR */
                o = cce ? t->ref[ch] ^ ccp : ossr && ccne ? ccp : -1;
                if (ch < 3)
                    on = ccne ? t->ref[ch] ^ ccnp : ossr && cce ? ccnp : -1;
            }
        } else if ((bd & OSSI) && (cce || ccne)) {
            o = (int)(cr2 >> (8 + 2 * ch) & 1);                 /* OISx */
            on = ch < 3 ? (int)(cr2 >> (9 + 2 * ch) & 1) : -1;  /* OISxN */
        }
        if (o != t->out[ch] || on != t->outn[ch]) {
            t->out[ch] = o;
            t->outn[ch] = on;
            changed = 1;
        }
    }
    deadlines(t);
    for (ch = 0; ch < t->nch; ch++) {
        int lvl = is_output(t, ch) && ocm(t, ch) ? t->ref[ch] : 0;
        if (lvl != t->cc_level[ch]) {
            t->cc_level[ch] = lvl;
            esk_stm32_trigger(t->s, t->sig_cc[ch], lvl);
        }
    }
    if (t->cr2 && bits(t->cr2, 4, 3) >= 4)
        trgo_set(t, t->ref[bits(t->cr2, 4, 3) - 4]); /* MMS: OCxREF */
    if (changed && t->s->pins_changed)
        t->s->pins_changed(t->s);
}

/* the dead-time copies whose time has come */
static int dead_times(struct tim *t)
{
    int ch, any = 0;
    for (ch = 0; ch < 3; ch++) {
        if (t->dref_at[ch] && t->now >= t->dref_at[ch]) {
            t->dref[ch] = 1;
            t->dref_at[ch] = 0;
            any = 1;
        }
        if (t->drefn_at[ch] && t->now >= t->drefn_at[ch]) {
            t->drefn[ch] = 1;
            t->drefn_at[ch] = 0;
            any = 1;
        }
    }
    deadlines(t);
    return any;
}

/* ---- the time base --------------------------------------------------------- */

static void run_clock(struct tim *t);

/* an update event; `ug`: by EGR.UG or a slave reset */
static void update(struct tim *t, int ug)
{
    int ch;
    if (t->cr1->value & UDIS)
        return;
    t->psc_now = (u32)t->psc->value & 0xffff;
    t->arr_now = (u32)(t->arr->value & t->cntmask);
    t->rcr_now = t->rcr ? (u32)t->rcr->value & 0xff : 0;
    t->rep = t->rcr_now;
    for (ch = 0; ch < t->nch; ch++) {
        if (t->ccr[ch])
            t->ccr_now[ch] = (u32)(t->ccr[ch]->value & t->cntmask);
        t->cleared[ch] = 0;
    }
    if (t->bdtr && (t->bdtr->value & AOE) && !t->brk)
        esk_rf_hw(t->bdtr, t->bdtr->value | MOE);
    if (!(ug && (t->cr1->value & URS)))
        set_flag(t, UIF);
    dma_req(t, 1u << 8, t->sig_up);
    if (t->cr2 && (t->cr2->value & CCDS))
        for (ch = 0; ch < t->nch; ch++)
            dma_req(t, 1u << (ch + 9), t->sig_ch[ch]); /* CCDS: the CCx requests at the update */
    if (t->cr2 && bits(t->cr2, 4, 3) == 2)
        trgo_pulse(t); /* MMS: update */
    if (!ug && (t->cr1->value & OPM)) {
        esk_rf_hw(t->cr1, t->cr1->value & ~(u64)CEN);
        run_clock(t);
        if (t->cr2 && bits(t->cr2, 4, 3) == 1)
            trgo_set(t, 0); /* MMS: enable */
    }
}

/* an overflow or underflow: the update event once the repetition counter
 * is spent */
static void wrap_event(struct tim *t)
{
    if (t->rep) {
        t->rep--;
        return;
    }
    update(t, 0);
}

static void set_dir(struct tim *t, int down)
{
    u64 v = down ? t->cr1->value | DIR : t->cr1->value & ~(u64)DIR;
    if (v != t->cr1->value)
        esk_rf_hw(t->cr1, v);
}

/* `n` counts of the counter, by its mode */
static void advance(struct tim *t, u64 n)
{
    u64 cnt = t->cnt->value & t->cntmask, arr = t->arr_now, step;
    int wrapped = 0;
    while (n && (t->cr1->value & CEN) && arr) {
        u32 c = cms(t);
        if (wrapped && !c && !watched(t) && n > arr + 1) {
            /* whole periods nothing watches: their update events at once
             * (the shadows already hold the registers' values) */
            u64 per = arr + 1, full = n / per - 1, r = (u64)t->rcr_now + 1;
            if (full > t->rep && !(t->cr1->value & UDIS))
                set_flag(t, UIF);
            t->rep = (u32)((t->rep + r - full % r) % r);
            n -= full * per;
        }
        wrapped = 1;
        if (c && !encoder(t)) {
            if (!counting_down(t)) {
                if (cnt >= arr) {
                    set_dir(t, 1);
                    continue;
                }
                step = arr - cnt;
                if (n < step) {
                    matches(t, cnt + 1, cnt + n, 0);
                    cnt += n;
                    break;
                }
                matches(t, cnt + 1, arr, 0);
                n -= step;
                cnt = arr;
                t->cnt->value = (t->cnt->value & ~t->cntmask) | cnt;
                set_dir(t, 1);
                wrap_event(t); /* overflow */
            } else {
                if (cnt > arr)
                    cnt = arr;
                if (cnt == 0) {
                    set_dir(t, 0);
                    continue;
                }
                step = cnt;
                if (n < step) {
                    matches(t, cnt - n, cnt - 1, 1);
                    cnt -= n;
                    break;
                }
                matches(t, 0, cnt - 1, 1);
                n -= step;
                cnt = 0;
                t->cnt->value = (t->cnt->value & ~t->cntmask) | cnt;
                set_dir(t, 0);
                wrap_event(t); /* underflow */
            }
        } else if (!counting_down(t)) {
            int event = cnt <= arr;
            step = event ? arr - cnt + 1 : t->cntmask - cnt + 1; /* to 0 */
            if (n < step) {
                matches(t, cnt + 1, cnt + n, 0);
                cnt += n;
                break;
            }
            if (event)
                matches(t, cnt + 1, arr, 0);
            n -= step;
            cnt = 0;
            t->cnt->value = (t->cnt->value & ~t->cntmask) | cnt;
            if (event)
                wrap_event(t); /* overflow */
            matches(t, 0, 0, 0);
        } else {
            step = cnt + 1; /* to 0, then ARR */
            if (n < step) {
                matches(t, cnt - n, cnt - 1, 1);
                cnt -= n;
                break;
            }
            if (cnt)
                matches(t, 0, cnt - 1, 1);
            n -= step;
            cnt = arr;
            t->cnt->value = (t->cnt->value & ~t->cntmask) | cnt;
            wrap_event(t); /* underflow */
            matches(t, arr, arr, 1);
            arr = t->arr_now;
        }
        arr = t->arr_now;
    }
    t->cnt->value = (t->cnt->value & ~t->cntmask) | cnt;
}

/* `clocks` ticks into the prescaler */
static void prescale(struct tim *t, u64 clocks)
{
    u64 all = (u64)t->pc + clocks, per = (u64)t->psc_now + 1;
    if (all < per) {
        t->pc = (u32)all;
        return;
    }
    t->pc = (u32)(all % per);
    advance(t, all / per);
}

/* the counter counts the timer's own clock now */
static int internal_clock(const struct tim *t)
{
    u32 m = sms(t);
    if (!(t->cr1->value & CEN) || !t->arr_now || gated(t) || encoder(t) || m == SMS_EXT1)
        return 0;
    if (t->smcr && (t->smcr->value & ECE))
        return 0;
    return m != SMS_GATED || t->trgi > 0;
}

static void reinit(struct tim *t)
{
    u64 v = counting_down(t) && !cms(t) ? t->arr->value & t->cntmask : 0;
    t->cnt->value = (t->cnt->value & ~t->cntmask) | v;
    t->pc = 0;
}

/* ---- the slave mode controller ---------------------------------------- */

static struct tim *tim_unit(struct esk_mach *s, int unit)
{
    char name[8];
    struct esk_rf_periph *p;
    snprintf(name, sizeof name, "TIM%d", unit);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_stm32_tim_ops && !esk_rf_gated(p) ? (struct tim *)p->ctx : 0;
}

/* a trigger event: TIF, and what the slave mode does with TRGI's rising
 * edge (gated mode's both edges are triggers too) */
static void trgi_edge(struct tim *t, int rising)
{
    u32 m = sms(t);
    if (m == SMS_OFF || encoder(t) || (!rising && m != SMS_GATED))
        return;
    set_flag(t, TIF);
    dma_req(t, 1u << 14, t->sig_trig);
    if (rising && t->cr2 && (t->cr2->value & CCUS) && ccpc(t)) {
        commutation(t);
        set_flag(t, COMIF);
        dma_req(t, 1u << 13, t->sig_com);
    }
    switch (m) {
    case SMS_RESET:
        if (rising) {
            reinit(t);
            update(t, 1);
        }
        break;
    case SMS_TRIGGER:
        if (rising && !(t->cr1->value & CEN)) {
            esk_rf_hw(t->cr1, t->cr1->value | CEN);
            run_clock(t);
        }
        break;
    case SMS_EXT1:
        if (rising && (t->cr1->value & CEN) && !gated(t))
            prescale(t, 1);
        break;
    default:
        break;
    }
    if (t->cr2 && bits(t->cr2, 4, 3) == 1)
        trgo_set(t, (t->cr1->value & CEN) != 0);
    outputs(t);
}

/* TRGI's source changed level; `src` is TS's code */
static void trgi_input(struct tim *t, u32 src, int level, int both)
{
    u32 ts = bits(t->smcr, 4, 3);
    if (!t->smcr || ts != src)
        return;
    if (both) { /* TI1F_ED: each edge a rising one */
        trgi_edge(t, 1);
        t->trgi = level;
        return;
    }
    if (level == t->trgi)
        return;
    t->trgi = level;
    trgi_edge(t, level);
}

static void trgo_set(struct tim *t, int level)
{
    struct esk_rf *rf = t->s->rf;
    int i, k;
    if (level == t->trgo)
        return;
    t->trgo = level;
    esk_stm32_trigger(t->s, t->sig_trgo, level);
    for (i = 0; rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(rf, i);
        struct tim *u;
        if (!p)
            break;
        if (p->ops != &esk_stm32_tim_ops || p->ctx == t || esk_rf_gated(p))
            continue;
        u = (struct tim *)p->ctx;
        for (k = 0; k < 4; k++)
            if (u->itr[k] == t->unit)
                trgi_input(u, (u32)k, level, 0);
    }
}

static void trgo_pulse(struct tim *t)
{
    trgo_set(t, 1);
    trgo_set(t, 0);
}

/* ---- inputs ------------------------------------------------------------------ */

/* the filter of channel x's input or ETR: N samples, the sampling period
 * in timer ticks (RM0390's ICxF and ETF table) */
static void filter_of(const struct tim *t, u32 f, u32 *n, u32 *per)
{
    static const u8 nn[16] = { 0, 2, 4, 8, 6, 8, 6, 8, 6, 8, 5, 6, 8, 5, 6, 8 };
    static const u8 dv[16] = { 1, 1, 1, 1, 2, 2, 4, 4, 8, 8, 16, 16, 16, 32, 32, 32 };
    *n = nn[f & 15];
    *per = (f & 15) < 4 ? 1 : dv[f & 15] * dts(t);
}

static void capture(struct tim *t, int ch)
{
    u32 div = 1u << bits(t->ccmr[ch / 2], 8 * (ch & 1) + 2, 2), v;
    u64 cnt = t->cnt->value & t->cntmask, lead;
    if (!(ccer(t) >> (4 * ch) & 1) || !t->ccr[ch])
        return;
    if (++t->icpsc[ch] < div)
        return;
    t->icpsc[ch] = 0;
    /* an edge from outside came `input_lead` cycles before this instruction
     * ended: the counter as it was then */
    lead = t->s->input_lead / ((u64)divisor(t) * ((u64)t->psc_now + 1));
    if (lead && !counting_down(t) && lead <= cnt)
        cnt -= lead;
    v = (u32)cnt;
    if (t->sr->value & ((u64)1u << (ch + 1)))
        esk_rf_hw(t->sr, t->sr->value | ((u64)1u << (ch + 9))); /* CCxOF */
    esk_rf_hw(t->ccr[ch], (t->ccr[ch]->value & ~t->cntmask) | v);
    set_flag(t, 1u << (ch + 1));
    if (!(t->cr2 && (t->cr2->value & CCDS)))
        dma_req(t, 1u << (ch + 9), t->sig_ch[ch]);
    esk_stm32_trigger(t->s, t->sig_cc[ch], 1); /* the capture's event */
    esk_stm32_trigger(t->s, t->sig_cc[ch], 0);
}

/* the edge a channel's polarity selects on its input (CCxNP:CCxP 00
 * rising, 01 falling, 11 both) */
static int edge_selected(const struct tim *t, int ch, int rising)
{
    u32 p = ccer(t) >> (4 * ch + 1) & 1, np = ccer(t) >> (4 * ch + 3) & 1;
    return (p && np) || (rising ? !p : p);
}

/* TIxFP: the input after its polarity, as the slave controller and the
 * encoder see it */
static int tifp(const struct tim *t, int x)
{
    return t->flt[x] > 0 ? !(ccer(t) >> (4 * x + 1) & 1) : (int)(ccer(t) >> (4 * x + 1) & 1);
}

static void encoder_edge(struct tim *t, int x, int rising)
{
    u32 m = sms(t);
    int other, up;
    if (!(t->cr1->value & CEN) || gated(t))
        return;
    if ((m == SMS_ENC1 && x != 1) || (m == SMS_ENC2 && x != 0) || x > 1)
        return;
    rising = rising ^ (int)(ccer(t) >> (4 * x + 1) & 1); /* TIxFPx's polarity */
    other = tifp(t, 1 - x);
    up = x == 0 ? (rising ? !other : other) : (rising ? other : !other);
    set_dir(t, !up);
    prescale(t, 1);
}

/* input x's level after the filter changed */
static void filtered(struct tim *t, int x, int level)
{
    int ch, rising = level > 0;
    t->flt[x] = level;
    if (x == IN_BKIN) {
        int act = t->bdtr && (t->bdtr->value & BKE) && level == (int)((t->bdtr->value & BKP) != 0);
        t->brk = act;
        if (act) {
            esk_rf_hw(t->bdtr, t->bdtr->value & ~(u64)MOE);
            set_flag(t, BIF);
        }
        outputs(t);
        return;
    }
    if (x == IN_ETR) {
        int v = level ^ (t->smcr && (t->smcr->value & ETP) ? 1 : 0);
        u32 ps = bits(t->smcr, 12, 2);
        if (ps) {
            if (v)
                t->etr_edges++;
            v = (int)(t->etr_edges >> (ps - 1) & 1);
        }
        if (v == t->etrp)
            return;
        t->etrp = v;
        if (v && t->smcr && (t->smcr->value & ECE) && (t->cr1->value & CEN) && !gated(t))
            prescale(t, 1); /* external clock mode 2 */
        trgi_input(t, 7, v, 0);
        outputs(t);
        return;
    }
    /* captures: ICx on TIx (CCxS 01) or on the neighbour (10) */
    for (ch = 0; ch < t->nch; ch++) {
        u32 s = ccm(t, ch) & 3u;
        int src = s == 1 ? ch : s == 2 ? (ch ^ 1) : -1;
        if (src == x && edge_selected(t, ch, rising))
            capture(t, ch);
    }
    if (encoder(t))
        encoder_edge(t, x, rising);
    if (x == 0) {
        trgi_input(t, 4, level, 1); /* TI1F_ED */
        trgi_input(t, 5, tifp(t, 0), 0);
    } else if (x == 1) {
        trgi_input(t, 6, tifp(t, 1), 0);
    }
    outputs(t);
}

/* the level TIx (or ETR, BKIN) gets from its pin */
static int input_level(const struct tim *t, int x)
{
    if (x == 0 && t->cr2 && (t->cr2->value & TI1S)) /* TI1: CH1 ^ CH2 ^ CH3 */
        return (t->raw[0] > 0) ^ (t->raw[1] > 0) ^ (t->raw[2] > 0);
    return t->raw[x] > 0;
}

static void input_changed(struct tim *t, int x)
{
    int lvl = input_level(t, x);
    u32 n = 0, per = 1;
    if (x < 4)
        filter_of(t, bits(t->ccmr[x / 2], 8 * (x & 1) + 4, 4), &n, &per);
    else if (x == IN_ETR)
        filter_of(t, bits(t->smcr, 8, 4), &n, &per);
    if (lvl == t->flt[x]) {
        t->pend_at[x] = 0; /* a glitch shorter than the filter */
        deadlines(t);
        return;
    }
    if (!n) {
        t->pend_at[x] = 0;
        deadlines(t);
        filtered(t, x, lvl);
        return;
    }
    t->pend[x] = lvl;
    t->pend_at[x] = t->now + (u64)n * per * divisor(t);
    deadlines(t);
    run_clock(t);
}

static int filters(struct tim *t)
{
    int x, any = 0;
    for (x = 0; x < 6; x++)
        if (t->pend_at[x] && t->now >= t->pend_at[x]) {
            t->pend_at[x] = 0;
            deadlines(t);
            filtered(t, x, t->pend[x]);
            any = 1;
        }
    return any;
}

static void input(struct tim *t, int x, int level)
{
    if (level == t->raw[x])
        return;
    t->raw[x] = level;
    input_changed(t, x);
    if (x < 3 && t->cr2 && (t->cr2->value & TI1S))
        input_changed(t, 0);
}

/* ---- the clock ---------------------------------------------------------------- */

static int pending(const struct tim *t) { return t->next_at != 0; }

static void run_clock(struct tim *t)
{
    esk_rf_clock(t->p, &t->running, (t->cr1->value & CEN) || pending(t));
}

/* ---- TIMx_OR's internal sources ------------------------------------------------ */

static struct esk_rf_reg *rcc_reg(struct esk_mach *s, const char *name)
{
    struct esk_rf_periph *r = s->rf ? esk_rf_periph(s->rf, "RCC") : 0;
    return r ? esk_rf_reg(r, name) : 0;
}

/* the frequency of the clock remapped onto the input now, 0 while it does
 * not run: LSI (RCC's LSIRDY), LSE (LSERDY), HSE_RTC (HSE / RTCPRE, which
 * is off below 2) */
static u32 src_hz(const struct tim *t)
{
    struct esk_rf_reg *r;
    u32 pre;
    switch (t->src_kind) {
    case SRC_LSI:
        r = rcc_reg(t->s, "CSR");
        return r && (r->value & 2u) ? 32000u : 0;
    case SRC_LSE:
        r = rcc_reg(t->s, "BDCR");
        return r && (r->value & 2u) ? (t->s->lse_hz ? t->s->lse_hz : 32768u) : 0;
    case SRC_HSE_RTC:
        r = rcc_reg(t->s, "CR");
        pre = (u32)bits(rcc_reg(t->s, "CFGR"), 16, 5);
        return r && (r->value & (1u << 17)) && pre >= 2 ? t->s->hse_hz / pre : 0;
    default: return 0;
    }
}

/* the next edge of the source: half its period on, from t0 at the core
 * clock; while it does not run, it is looked at again every 10 us */
static void src_schedule(struct tim *t)
{
    u32 core = esk_mach_core_hz(t->s), hz;
    if (t->src_x < 0) {
        t->src_at = 0;
        return;
    }
    hz = src_hz(t);
    if (core != t->src_core_hz || hz != t->src_hz_last) {
        t->src_t0 = t->now; /* a new clock: its edges from now */
        t->src_k = 0;
        t->src_core_hz = core;
        t->src_hz_last = hz;
    }
    if (!hz) {
        t->src_at = t->now + core / 100000u + 1;
        return;
    }
    t->src_k++;
    t->src_at = t->src_t0 + t->src_k * (u64)core / (2u * (u64)hz);
    if (t->src_at <= t->now)
        t->src_at = t->now + 1;
}

static void src_edges(struct tim *t)
{
    if (!t->src_at || t->now < t->src_at)
        return;
    if (src_hz(t))
        input(t, t->src_x, t->raw[t->src_x] <= 0);
    src_schedule(t);
    deadlines(t);
}

/* OR as written: which input a clock inside the part drives (RM0390
 * 13.4.20, 14.4.17, 18.5.13; TIM2's ITR1 remap is interconnect's) */
static void remap(struct tim *t)
{
    u32 v = t->orr ? (u32)t->orr->value : 0;
    int kind = SRC_NONE, x = -1;
    if (t->unit == 5) {
        u32 r = v >> 6 & 3u; /* IT4_RMP: GPIO, LSI, LSE, the RTC's wake-up interrupt */
        kind = r == 1 ? SRC_LSI : r == 2 ? SRC_LSE : SRC_NONE;
        x = kind ? 3 : -1;
    } else if (t->unit == 11 && (v & 3u) == 2u) { /* RMP: HSE_RTC */
        kind = SRC_HSE_RTC;
        x = 0;
    } else if (t->unit == 2) {
        /* ITR1_RMP: TIM8's TRGO, or the OTG controllers' SOF (no model) */
        t->itr[1] = (v >> 10 & 3u) == 0 ? 8 : 0;
    }
    if (kind == t->src_kind && x == t->src_x)
        return;
    t->src_kind = kind;
    t->src_x = x;
    t->src_core_hz = t->src_hz_last = 0;
    t->src_at = 0;
    src_schedule(t);
    deadlines(t);
    run_clock(t);
}

/* the timer's own clock for `cycles` core cycles: 1 when the counter moved
 * past its prescaler */
static int clock_cycles(struct tim *t, u32 cycles)
{
    u32 d;
    u64 clocks;
    if (!internal_clock(t) || !cycles)
        return 0;
    d = divisor(t);
    clocks = cycles;
    if (d > 1) {
        u32 all = t->frac + cycles;
        clocks = all / d;
        t->frac = all % d;
    }
    if ((u64)t->pc + clocks <= t->psc_now) {
        t->pc += (u32)clocks;
        return 0;
    }
    prescale(t, clocks);
    return 1;
}

/* called after each instruction while the clock runs: kept short. A
 * deadline inside the instruction acts at its own time, the counter
 * brought up to it first. */
static void tim_tick(void *ctx, u32 cycles)
{
    struct tim *t = (struct tim *)ctx;
    u64 end = t->now + cycles, at;
    int any = 0;
    while ((at = t->next_at) != 0 && at <= end) {
        if (at > t->now)
            any |= clock_cycles(t, (u32)(at - t->now));
        t->now = at > t->now ? at : t->now;
        filters(t);
        src_edges(t);
        dead_times(t);
        outputs(t);
        any = 1;
    }
    any |= clock_cycles(t, (u32)(end - t->now));
    t->now = end;
    if (any) {
        outputs(t);
        if (!pending(t) && !(t->cr1->value & CEN))
            run_clock(t);
    }
}

/* counts from the counter until something happens: a wrap, a turn, a match */
static u64 counts_to_event(const struct tim *t)
{
    u64 cnt = t->cnt->value & t->cntmask, arr = t->arr_now, best = 0, c;
    int ch, down = counting_down(t);
    for (ch = -1; ch < t->nch; ch++) {
        u64 v = ch < 0 ? (down ? 0 : arr) : t->ccr_now[ch];
        if (ch >= 0 && !is_output(t, ch))
            continue;
        if (cms(t)) {
            if (!down)
                c = v > cnt && v <= arr ? v - cnt : (arr - cnt) + (v < arr ? arr - v : 0);
            else
                c = v < cnt ? cnt - v : cnt + (v > 0 && v <= arr ? v : 0);
        } else if (!down) {
            u64 wrap = cnt > arr ? t->cntmask - cnt + 1 : arr - cnt + 1;
            c = v > cnt && (v <= arr || cnt > arr) ? v - cnt : v <= arr ? wrap + v : 0;
            if (ch < 0)
                c = wrap;
        } else {
            c = v < cnt ? cnt - v : cnt + 1 + (arr - v);
            if (ch < 0)
                c = cnt + 1;
        }
        if (c && (!best || c < best))
            best = c;
    }
    return best;
}

static int tim_next_event(void *ctx, u32 *cycles)
{
    struct tim *t = (struct tim *)ctx;
    u64 best = 0, c;
    int i;
    for (i = 0; i < 6; i++)
        if (t->pend_at[i] && (!best || t->pend_at[i] - t->now < best))
            best = t->pend_at[i] > t->now ? t->pend_at[i] - t->now : 1;
    if (t->src_at && (!best || t->src_at - t->now < best))
        best = t->src_at > t->now ? t->src_at - t->now : 1;
    for (i = 0; i < 3; i++) {
        u64 at[2];
        int k;
        at[0] = t->dref_at[i];
        at[1] = t->drefn_at[i];
        for (k = 0; k < 2; k++)
            if (at[k] && (!best || at[k] - t->now < best))
                best = at[k] > t->now ? at[k] - t->now : 1;
    }
    if (internal_clock(t) && watched(t) && (c = counts_to_event(t)) != 0) {
        u64 per = (u64)t->psc_now + 1, d = divisor(t);
        u64 clocks = (per - t->pc) + (c - 1) * per;
        u64 cy = clocks * d > t->frac ? clocks * d - t->frac : 1;
        if (!best || cy < best)
            best = cy;
    }
    if (!best)
        return 0;
    *cycles = best > 0xffffffffULL ? 0xffffffffu : (u32)best;
    return 1;
}

/* ---- the registers ----------------------------------------------------------- */

static void tim_write(void *ctx, u32 off, int n, u32 v);

/* DMAR: the register DCR's DBA points to, then the next, DBL + 1 of them
 * (the DMA burst) */
static u32 burst_target(struct tim *t)
{
    u32 dba = bits(t->dcr, 0, 5), dbl = bits(t->dcr, 8, 5), at = 4 * (dba + t->burst_idx);
    if (++t->burst_idx > dbl)
        t->burst_idx = 0;
    return at;
}

static u32 tim_read(void *ctx, u32 off, int n)
{
    struct tim *t = (struct tim *)ctx;
    u32 v;
    int ch;
    if (t->dmar && off == esk_rf_off(t->dmar) && !t->s->bus.debug)
        return tim_read(ctx, burst_target(t), 4);
    v = esk_rf_read(t->p, off, n);
    if (t->s->bus.debug)
        return v;
    for (ch = 0; ch < t->nch; ch++)
        if (t->ccr[ch] && off == esk_rf_off(t->ccr[ch]) && is_input(t, ch) &&
            (t->sr->value & ((u64)1u << (ch + 1))))
            esk_rf_hw(t->sr, t->sr->value & ~((u64)1u << (ch + 1))); /* reading CCRx clears CCxIF */
    return v;
}

/* the bits LOCK keeps (RM0390 17.4.18): BDTR's, CR2's OISx, CCER's
 * polarities and CCMR's OCxM and OCxPE */
static void keep_locked(struct tim *t, struct esk_rf_reg *r, u64 was)
{
    u64 m = 0;
    int ch;
    if (!t->lock)
        return;
    if (r == t->bdtr) {
        m = 0xFF | BKE | BKP | AOE;
        if (t->lock >= 2)
            m |= OSSR | OSSI;
    } else if (r == t->cr2) {
        m = 0x7F00; /* OISx, OISxN */
    } else if (r == t->ccer && t->lock >= 2) {
        for (ch = 0; ch < t->nch; ch++)
            if (is_output(t, ch))
                m |= (u64)0xA << (4 * ch);
    } else if ((r == t->ccmr[0] || r == t->ccmr[1]) && t->lock >= 3) {
        for (ch = 0; ch < t->nch; ch++)
            if (t->ccmr[ch / 2] == r && is_output(t, ch))
                m |= (u64)0x78 << (8 * (ch & 1));
    }
    if ((r->value ^ was) & m)
        esk_rf_hw(r, (r->value & ~m) | (was & m));
}

static void tim_write(void *ctx, u32 off, int n, u32 v)
{
    struct tim *t = (struct tim *)ctx;
    struct esk_rf_reg *r[6];
    if (t->dmar && off == esk_rf_off(t->dmar)) {
        tim_write(ctx, burst_target(t), 4, v);
        return;
    }
    if (t->dcr && off == esk_rf_off(t->dcr))
        t->burst_idx = 0;
    u64 was[6], cr1 = t->cr1->value, bdtr = t->bdtr ? t->bdtr->value : 0;
    int ccpc0 = ccpc(t);
    u32 cen0 = (u32)(cr1 & CEN);
    int i, ch;
    r[0] = t->bdtr;
    r[1] = t->cr2;
    r[2] = t->ccer;
    r[3] = t->ccmr[0];
    r[4] = t->ccmr[1];
    r[5] = t->cr1;
    for (i = 0; i < 6; i++)
        was[i] = r[i] ? r[i]->value : 0;
    esk_rf_write(t->p, off, n, v);
    for (i = 0; i < 5; i++)
        if (r[i])
            keep_locked(t, r[i], was[i]);
    if (t->bdtr && off == esk_rf_off(t->bdtr)) {
        /* LOCK is written once after reset, then frozen */
        if (t->lock_written)
            esk_rf_hw(t->bdtr, (t->bdtr->value & ~(u64)0x300) | (bdtr & 0x300));
        else
            t->lock = (int)bits(t->bdtr, 8, 2);
        t->lock_written = 1;
        if (t->brk)
            esk_rf_hw(t->bdtr, t->bdtr->value & ~(u64)MOE); /* not while the break is active */
    }
    /* DIR is the hardware's in center-aligned and encoder modes */
    if (off == esk_rf_off(t->cr1) && (cms(t) || encoder(t)))
        esk_rf_hw(t->cr1, (t->cr1->value & ~(u64)DIR) | (cr1 & DIR));
    if (off == esk_rf_off(t->arr) && !(t->cr1->value & ARPE))
        t->arr_now = (u32)(t->arr->value & t->cntmask);
    if (t->cr2 && off == esk_rf_off(t->cr2) && ccpc(t) && !ccpc0)
        commutation(t);
    for (ch = 0; ch < t->nch; ch++)
        if (t->ccr[ch] && off == esk_rf_off(t->ccr[ch]) && is_output(t, ch) && !(ccm(t, ch) & 8u))
            t->ccr_now[ch] = (u32)(t->ccr[ch]->value & t->cntmask);
    if (off == esk_rf_off(t->egr)) {
        if (v & UG) {
            reinit(t);
            t->rep = t->rcr ? (u32)t->rcr->value & 0xff : 0;
            update(t, 1);
        }
        for (ch = 0; ch < t->nch; ch++)
            if (v >> (ch + 1) & 1) {
                if (is_output(t, ch))
                    match(t, ch, counting_down(t)); /* CCxG */
                else
                    capture(t, ch);
            }
        if ((v & COMG) && t->cr2) {
            if (ccpc(t))
                commutation(t);
            set_flag(t, COMIF);
            dma_req(t, 1u << 13, t->sig_com);
        }
        if ((v & TG) && t->smcr) {
            set_flag(t, TIF);
            if (sms(t) == SMS_TRIGGER && !(t->cr1->value & CEN))
                esk_rf_hw(t->cr1, t->cr1->value | CEN);
            else if (sms(t) == SMS_RESET) {
                reinit(t);
                update(t, 1);
            }
        }
        if ((v & BG) && t->bdtr) {
            esk_rf_hw(t->bdtr, t->bdtr->value & ~(u64)MOE);
            set_flag(t, BIF);
        }
        esk_rf_hw(t->egr, 0);
    }
    /* a filter or TI1S changed: the inputs again */
    if ((t->ccmr[0] && off == esk_rf_off(t->ccmr[0])) || (t->ccmr[1] && off == esk_rf_off(t->ccmr[1])) ||
        (t->cr2 && off == esk_rf_off(t->cr2)))
        for (i = 0; i < 4; i++)
            if (t->raw[i] >= 0 && input_level(t, i) != t->flt[i] && !t->pend_at[i])
                input_changed(t, i);
    if (t->bdtr && off == esk_rf_off(t->bdtr) && t->raw[IN_BKIN] >= 0)
        filtered(t, IN_BKIN, t->flt[IN_BKIN]);
    if (t->cr2 && bits(t->cr2, 4, 3) == 1)
        trgo_set(t, (t->cr1->value & CEN) != 0);
    else if (t->cr2 && bits(t->cr2, 4, 3) == 0 && off == esk_rf_off(t->egr) && (v & UG))
        trgo_pulse(t); /* MMS: reset */
    if (cen0 != (u32)(t->cr1->value & CEN) && !(t->cr1->value & CEN))
        t->frac = 0;
    if (off == esk_rf_off(t->dier)) {
        /* a request whose enable went off is withdrawn */
        if (!(t->dier->value & (1u << 8)))
            esk_stm32_dma_request(t->s, t->sig_up, 0);
        for (ch = 0; ch < t->nch; ch++)
            if (!(t->dier->value & ((u64)1u << (ch + 9))))
                esk_stm32_dma_request(t->s, t->sig_ch[ch], 0);
        if (!(t->dier->value & (1u << 14)))
            esk_stm32_dma_request(t->s, t->sig_trig, 0);
        if (!(t->dier->value & (1u << 13)))
            esk_stm32_dma_request(t->s, t->sig_com, 0);
    }
    if (t->orr && off == esk_rf_off(t->orr))
        remap(t);
    run_clock(t);
    irq_check(t);
    outputs(t);
}

static void tim_reset(void *ctx)
{
    struct tim *t = (struct tim *)ctx;
    int ch, x;
    t->psc_now = (u32)t->psc->value & 0xffff;
    t->arr_now = (u32)(t->arr->value & t->cntmask);
    t->rcr_now = t->rep = 0;
    t->pc = t->frac = 0;
    t->running = 0;
    t->lock = t->lock_written = t->brk = 0;
    t->trgi = t->trgo = 0;
    t->etr_edges = 0;
    t->etrp = 0;
    t->ccer_act = 0;
    for (ch = 0; ch < 4; ch++) {
        t->ccr_now[ch] = t->ccr[ch] ? (u32)(t->ccr[ch]->value & t->cntmask) : 0;
        t->ref[ch] = t->cleared[ch] = 0;
        t->cc_level[ch] = 0;
        t->dref[ch] = 0;
        t->drefn[ch] = 1;
        t->dref_at[ch] = t->drefn_at[ch] = 0;
        t->out[ch] = t->outn[ch] = -1;
        t->ocm_act[ch] = 0;
        t->icpsc[ch] = 0;
    }
    for (x = 0; x < 6; x++) {
        t->flt[x] = t->raw[x] > 0;
        t->pend_at[x] = 0;
    }
    t->next_at = 0;
    t->src_kind = SRC_NONE;
    t->src_x = -1;
    t->src_at = 0;
    interconnect(t);
}

const struct esk_dev_ops esk_stm32_tim_ops = {
    "stm32-tim", tim_read, tim_write, tim_reset, tim_tick, tim_next_event,
};

/* ---- the pins ------------------------------------------------------------------ */

/* "TIM1_CH2N" -> unit 1, input 1, complementary; "TIM2_ETR" -> 2, IN_ETR */
static int parse_signal(const char *sig, int *unit, int *x, int *neg)
{
    char *end;
    long u;
    if (strncmp(sig, "TIM", 3))
        return -1;
    u = strtol(sig + 3, &end, 10);
    if (end == sig + 3 || *end != '_' || u < 1 || u > 17)
        return -1;
    *unit = (int)u;
    *neg = 0;
    end++;
    if (!strncmp(end, "CH", 2) && end[2] >= '1' && end[2] <= '4') {
        *x = end[2] - '1';
        if (end[3] == 'N' && !end[4])
            *neg = 1;
        else if (end[3])
            return -1;
        return 0;
    }
    if (!strcmp(end, "ETR")) {
        *x = IN_ETR;
        return 0;
    }
    if (!strcmp(end, "BKIN")) {
        *x = IN_BKIN;
        return 0;
    }
    return -1;
}

int esk_stm32_tim_signal(struct esk_mach *s, const char *sig)
{
    int unit, x, neg;
    struct tim *t;
    if (parse_signal(sig, &unit, &x, &neg) || x >= 4 || !(t = tim_unit(s, unit)))
        return -1;
    return neg ? t->outn[x] : t->out[x];
}

void esk_stm32_tim_signal_input(struct esk_mach *s, const char *sig, int level)
{
    int unit, x, neg;
    struct tim *t;
    if (parse_signal(sig, &unit, &x, &neg) || neg || !(t = tim_unit(s, unit)))
        return;
    if (x == t->src_x)
        return; /* remapped: the pin does not reach it */
    input(t, x, level != 0);
}

/* the F1: channel ch's pin of TIMunit, as AFIO's MAPR places it: port * 16
 * + bit (RM0008 9.3.7) */
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
        for (ch = 0; ch < t->nch; ch++)
            if (t->out[ch] >= 0 && tim_pin_of(mapr, unit, ch) == port * 16 + bit)
                return t->out[ch];
    }
    return -1;
}

void esk_stm32f1_tim_input(struct esk_mach *s, int port, int bit, int level)
{
    u32 mapr = esk_stm32f1_mapr(s);
    int unit, ch;
    for (unit = 1; unit <= 4; unit++) {
        struct tim *t = tim_unit(s, unit);
        if (!t)
            continue;
        for (ch = 0; ch < t->nch; ch++)
            if (tim_pin_of(mapr, unit, ch) == port * 16 + bit)
                input(t, ch, level != 0);
    }
}

/* ---- creation ------------------------------------------------------------------ */

/* ITR0-ITR3's masters (RM0390 tables 76, 82, 89, 93; RM0008's agree for
 * TIM1-TIM5 and TIM8) */
static void interconnect(struct tim *t)
{
    static const int map[15][4] = {
        { 0, 0, 0, 0 },     /* - */
        { 5, 2, 3, 4 },     /* TIM1 */
        { 1, 8, 3, 4 },     /* TIM2 */
        { 1, 2, 5, 4 },     /* TIM3 */
        { 1, 2, 3, 8 },     /* TIM4 */
        { 2, 3, 4, 8 },     /* TIM5 */
        { 0, 0, 0, 0 },     /* TIM6 */
        { 0, 0, 0, 0 },     /* TIM7 */
        { 1, 2, 4, 5 },     /* TIM8 */
        { 2, 3, 10, 11 },   /* TIM9 */
        { 0, 0, 0, 0 },     /* TIM10 */
        { 0, 0, 0, 0 },     /* TIM11 */
        { 4, 5, 13, 14 },   /* TIM12 */
        { 0, 0, 0, 0 },     /* TIM13 */
        { 0, 0, 0, 0 },     /* TIM14 */
    };
    int k;
    for (k = 0; k < 4; k++)
        t->itr[k] = t->unit > 0 && t->unit < 15 ? map[t->unit][k] : 0;
}

static struct esk_rf_reg *reg2(struct esk_rf_periph *p, const char *a, const char *b)
{
    struct esk_rf_reg *r = esk_rf_reg(p, a);
    return r ? r : esk_rf_reg(p, b);
}

void *esk_stm32_tim_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct tim t, *c;
    int ch, i, x;
    char *end;
    memset(&t, 0, sizeof t);
    t.s = s;
    t.p = p;
    t.cr1 = esk_rf_reg(p, "CR1");
    t.cr2 = esk_rf_reg(p, "CR2");
    t.smcr = esk_rf_reg(p, "SMCR");
    t.dier = esk_rf_reg(p, "DIER");
    t.sr = esk_rf_reg(p, "SR");
    t.egr = esk_rf_reg(p, "EGR");
    t.cnt = esk_rf_reg(p, "CNT");
    t.psc = esk_rf_reg(p, "PSC");
    t.arr = esk_rf_reg(p, "ARR");
    t.rcr = esk_rf_reg(p, "RCR");
    t.bdtr = esk_rf_reg(p, "BDTR");
    if (!t.cr1 || !t.dier || !t.sr || !t.egr || !t.cnt || !t.psc || !t.arr)
        return 0;
    t.ccmr[0] = reg2(p, "CCMR1", "CCMR1_Output");
    t.ccmr[1] = reg2(p, "CCMR2", "CCMR2_Output");
    t.ccer = esk_rf_reg(p, "CCER");
    for (ch = 0; ch < 4; ch++) {
        char n[8];
        snprintf(n, sizeof n, "CCR%d", ch + 1);
        t.ccr[ch] = esk_rf_reg(p, n);
        if (t.ccr[ch] && t.ccmr[ch / 2] && t.ccer)
            t.nch = ch + 1;
    }
    for (i = 0; i < t.cnt->nf; i++) {
        int top = t.cnt->f[i].lsb + t.cnt->f[i].width;
        u64 m = top >= 64 ? ~0ULL : (1ULL << top) - 1;
        if (m > t.cntmask)
            t.cntmask = m;
    }
    if (t.cntmask > 0xffffffffULL || !t.cntmask)
        t.cntmask = 0xffff;
    esk_rf_set_mwv(t.sr, ~0ULL, ESK_MWV_0CLR);
    t.irq_up = esk_rf_irq(p, "_UP");
    t.irq_cc = esk_rf_irq(p, "_CC");
    t.irq_brk = esk_rf_irq(p, "_BRK");
    t.irq_com = esk_rf_irq(p, "_TRG_COM");
    if (!strncmp(p->name, "TIM", 3)) {
        long u = strtol(p->name + 3, &end, 10);
        if (end != p->name + 3 && !*end)
            t.unit = (int)u;
    }
    interconnect(&t);
    t.apb = p->gate_name && strstr(p->gate_name, "APB2") ? 2 : 1;
    for (x = 0; x < 6; x++)
        t.raw[x] = -1;
    t.dcr = esk_rf_reg(p, "DCR");
    t.dmar = esk_rf_reg(p, "DMAR");
    t.orr = t.unit == 2 || t.unit == 5 || t.unit == 11 ? esk_rf_reg(p, "OR") : 0;
    t.src_x = -1;
    snprintf(t.sig_up, sizeof t.sig_up, "%s_UP", p->name);
    snprintf(t.sig_trig, sizeof t.sig_trig, "%s_TRIG", p->name);
    snprintf(t.sig_com, sizeof t.sig_com, "%s_COM", p->name);
    snprintf(t.sig_trgo, sizeof t.sig_trgo, "%s_TRGO", p->name);
    for (ch = 0; ch < 4; ch++)
        snprintf(t.sig_ch[ch], sizeof t.sig_ch[ch], "%s_CH%d", p->name, ch + 1);
    for (ch = 0; ch < 4; ch++)
        snprintf(t.sig_cc[ch], sizeof t.sig_cc[ch], "%s_CC%d", p->name, ch + 1);
    c = (struct tim *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = t;
    tim_reset(c);
    return c;
}
