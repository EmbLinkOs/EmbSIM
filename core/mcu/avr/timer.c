/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* timer.c -- the ATmega328P's Timer/Counters over the register file: TC0
 * and TC2, the 8-bit ones (datasheet 19, 22), and TC1, the 16-bit one
 * (datasheet 20).
 *
 *   - the counter counts the core's cycles through the prescaler (CSn2:0;
 *     TC0 and TC1 stopped, /1, /8, /64, /256, /1024, TC2 stopped, /1, /8,
 *     /32, /64, /128, /256, /1024), which runs free as the part's does, so
 *     a timer started mid-way has a partial first period;
 *   - every waveform mode counts as the datasheet says (normal, CTC with
 *     OCRnA or ICR1 as TOP, fast PWM and the dual-slope phase correct and
 *     phase and frequency correct modes) and sets its flags: TOVn at MAX
 *     (normal), at TOP (fast PWM) or at BOTTOM (dual-slope); OCFnA and
 *     OCFnB when the counter reaches the compare value; ICF1 at TOP when
 *     ICR1 is TOP. The flags clear by writing them a one, or as their
 *     vector is entered;
 *   - in the PWM modes OCRnx is double-buffered: a write takes effect at
 *     BOTTOM (fast PWM, phase and frequency correct) or at TOP (phase
 *     correct); in normal and CTC modes at once;
 *   - the output compare units drive their pins (OC0A PD6, OC0B PD5, OC1A
 *     PB1, OC1B PB2, OC2A PB3, OC2B PD3) while the pin's DDxn is 1: toggle,
 *     clear or set on a match (non-PWM), and the PWM modes' waveforms with
 *     the datasheet's extreme values (OCRnx at TOP in fast PWM, at BOTTOM
 *     or TOP in the dual-slope modes, give a constant level); FOCnx forces
 *     a match's action in the non-PWM modes. The time to the next pin edge
 *     is a device event, so a PWM output keeps its timing while the core
 *     sleeps;
 *   - TC1's 16-bit registers are reached through TEMP, the high byte's
 *     latch, as on the part.
 *
 * Not modelled: the external clock inputs T0 and T1, input capture from
 * the ICP1 pin, TC2's asynchronous clock (ASSR.AS2) and the prescaler
 * resets (GTCCR).
 *
 * Provenance: TC1's counting and flags follow EmbLinkOs/EmbCC aa4f10a
 * tools/embsim/avr-timer16.c; the 8-bit timers, the output compare pins,
 * the double buffering and the register file are EmbSIM's. */
#include <stdio.h>
#include <string.h>

#include "mcu/avr/avr.h"

#define TOV 1u
#define OCFA 2u
#define OCFB 4u
#define ICF 32u

enum { K_NORMAL, K_CTC, K_FAST, K_PC, K_PFC };

/* what a clock of the counter did */
enum { EV_BOTTOM = 1, EV_TOP = 2 };

struct avr_tc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    int wide; /* TC1 */
    int unit; /* 0, 1 or 2 */
    struct esk_rf_reg *tifr, *timsk, *tccra, *tccrb, *tccrc;
    struct esk_rf_reg *tcnt, *ocr[2];             /* the 8-bit timers' */
    struct esk_rf_reg *tcntl, *tcnth, *ocrl[2], *ocrh[2], *icrl, *icrh; /* TC1's */
    const u32 *div;
    u16 cnt, ocr_now[2], ocr_buf[2], icr;
    u8 temp;
    int down;
    int running;
    int oc[2];      /* the output compare units' levels */
    int oc_on[2];   /* the unit drives its pin */
    int oc_port[2], oc_bit[2];
    int vec[4];     /* overflow, compare A, compare B, capture */
};

static const u32 div01[8] = { 0, 1, 8, 64, 256, 1024, 0, 0 };
static const u32 div2[8] = { 0, 1, 8, 32, 64, 128, 256, 1024 };

static int wgm(const struct avr_tc *t)
{
    int a = (int)(t->tccra->value & 3);
    if (t->wide)
        return (int)((t->tccrb->value >> 3) & 3) << 2 | a;
    return (int)((t->tccrb->value >> 3) & 1) << 2 | a;
}

static int kind(const struct avr_tc *t)
{
    static const int k8[8] = { K_NORMAL, K_PC, K_CTC, K_FAST, K_NORMAL, K_PC, K_NORMAL, K_FAST };
    static const int k16[16] = { K_NORMAL, K_PC, K_PC, K_PC, K_CTC, K_FAST, K_FAST, K_FAST,
                                 K_PFC, K_PFC, K_PC, K_PC, K_CTC, K_NORMAL, K_FAST, K_FAST };
    return t->wide ? k16[wgm(t)] : k8[wgm(t)];
}

/* OCRnA is TOP */
static int a_is_top(const struct avr_tc *t)
{
    int m = wgm(t);
    return t->wide ? (m == 4 || m == 9 || m == 11 || m == 15) : (m == 2 || m == 5 || m == 7);
}

static int icr_is_top(const struct avr_tc *t)
{
    int m = wgm(t);
    return t->wide && (m == 8 || m == 10 || m == 12 || m == 14);
}

static u16 top(const struct avr_tc *t)
{
    if (a_is_top(t))
        return t->ocr_now[0];
    if (icr_is_top(t))
        return t->icr;
    if (t->wide) {
        switch (wgm(t)) {
        case 1: case 5: return 0xff;
        case 2: case 6: return 0x1ff;
        case 3: case 7: return 0x3ff;
        }
        return 0xffff;
    }
    return 0xff;
}

static u16 max_of(const struct avr_tc *t) { return t->wide ? 0xffff : 0xff; }

static u32 prescale(const struct avr_tc *t) { return t->div[t->tccrb->value & 7]; }

static int dual(int k) { return k == K_PC || k == K_PFC; }

/* the clocks of one period */
static u32 period(const struct avr_tc *t)
{
    u32 tp = top(t);
    if (dual(kind(t)))
        return tp ? 2 * tp : 1;
    return kind(t) == K_NORMAL ? (u32)max_of(t) + 1 : tp + 1;
}

/* One timer clock on (cnt, down): the flags it sets, and in *ev whether
 * it reached BOTTOM or TOP. */
static u8 clock1(const struct avr_tc *t, u16 *cnt, int *down, int *ev)
{
    int k = kind(t);
    u16 tp = top(t);
    u8 f = 0;
    *ev = 0;
    if (dual(k)) {
        if (!*down) {
            if (*cnt >= tp) {
                *down = 1;
                *cnt = (u16)(*cnt - 1);
            } else {
                (*cnt)++;
                if (*cnt == tp)
                    *ev |= EV_TOP;
            }
        } else if (*cnt == 0) {
            *down = 0;
            (*cnt)++;
        } else {
            (*cnt)--;
            if (*cnt == 0) {
                f |= TOV; /* at BOTTOM */
                *ev |= EV_BOTTOM;
            }
        }
        if (*cnt == tp && tp == 0)
            *ev |= EV_TOP;
    } else {
        u16 wrap = k == K_NORMAL ? max_of(t) : tp;
        if (*cnt == wrap || *cnt >= max_of(t)) {
            /* at TOP; or past it at MAX (TOP was lowered under the count) */
            if (*cnt == max_of(t) && k != K_FAST)
                f |= TOV; /* MAX to BOTTOM */
            *cnt = 0;
            *ev |= EV_BOTTOM;
        } else {
            (*cnt)++;
        }
        if (k == K_FAST && *cnt == tp) {
            f |= TOV; /* at TOP */
            *ev |= EV_TOP;
        }
    }
    if (*cnt == t->ocr_now[0])
        f |= OCFA;
    if (*cnt == t->ocr_now[1])
        f |= OCFB;
    if (icr_is_top(t) && *cnt == tp && (k != K_CTC || *cnt != 0))
        f |= ICF;
    return f;
}

/* ---- the output compare units ---------------------------------------------- */

static int com(const struct avr_tc *t, int ch)
{
    return (int)((t->tccra->value >> (ch ? 4 : 6)) & 3);
}

/* the unit drives its pin, and with what level a PWM extreme holds it
 * (-1: no constant level) */
static int connected(const struct avr_tc *t, int ch, int *constant)
{
    int c = com(t, ch), k = kind(t);
    u16 o = t->ocr_now[ch], tp = top(t);
    *constant = -1;
    if (!c)
        return 0;
    if (k == K_NORMAL || k == K_CTC)
        return 1;
    if (c == 1)
        return ch == 0 && a_is_top(t); /* toggle: OCnA with OCRnA as TOP, else not connected */
    if (k == K_FAST && o == tp && !(ch == 0 && a_is_top(t)))
        *constant = c == 2 ? 1 : 0;
    if (dual(k) && o == 0)
        *constant = c == 2 ? 0 : 1;
    if (dual(k) && o == tp && !(ch == 0 && a_is_top(t)))
        *constant = c == 2 ? 1 : 0;
    return 1;
}

static void drive(struct avr_tc *t, int ch)
{
    int constant, on = connected(t, ch, &constant);
    if (on && constant >= 0)
        t->oc[ch] = constant;
    if (on || t->oc_on[ch])
        esk_avr_pin_override(t->s, t->oc_port[ch], t->oc_bit[ch], on ? t->oc[ch] : -1);
    t->oc_on[ch] = on;
}

/* a match's or BOTTOM's action on the unit's level */
static void oc_action(struct avr_tc *t, int ch, int match, int ev, int down)
{
    int c = com(t, ch), k = kind(t), constant;
    if (!connected(t, ch, &constant) || constant >= 0)
        return;
    if (k == K_NORMAL || k == K_CTC) {
        if (match)
            t->oc[ch] = c == 1 ? !t->oc[ch] : c == 2 ? 0 : 1;
        return;
    }
    if (c == 1) {
        if (match)
            t->oc[ch] = !t->oc[ch];
        return;
    }
    if (k == K_FAST) {
        if (ev & EV_BOTTOM)
            t->oc[ch] = c == 2;
        if (match)
            t->oc[ch] = c == 3;
        return;
    }
    if (match) /* dual slope: cleared counting up, set counting down (COM 2) */
        t->oc[ch] = (c == 2) == (down != 0);
}

/* ---- time ------------------------------------------------------------------- */

static void mirror(struct avr_tc *t)
{
    if (t->wide) {
        esk_rf_hw(t->tcntl, t->cnt & 0xffu);
        esk_rf_hw(t->tcnth, t->cnt >> 8);
    } else {
        esk_rf_hw(t->tcnt, t->cnt);
    }
}

static int any_connected(const struct avr_tc *t)
{
    int c;
    return connected(t, 0, &c) || connected(t, 1, &c);
}

/* the counter moves on by `k` timer clocks */
static void advance(struct avr_tc *t, u64 k)
{
    u8 f = 0;
    int ch, oc0 = t->oc[0], oc1 = t->oc[1], buffered = 0;
    if (!any_connected(t)) {
        /* no pins: whole periods only set the flags a period sets */
        u64 p = period(t);
        if (k > 2 * p) {
            u64 skip = ((k - p) / p) * p;
            u64 i;
            for (i = 0; i < p; i++) {
                int ev;
                f |= clock1(t, &t->cnt, &t->down, &ev);
                if ((ev & EV_BOTTOM) && (kind(t) == K_FAST || kind(t) == K_PFC))
                    memcpy(t->ocr_now, t->ocr_buf, sizeof t->ocr_now);
                if ((ev & EV_TOP) && kind(t) == K_PC)
                    memcpy(t->ocr_now, t->ocr_buf, sizeof t->ocr_now);
            }
            k -= p + skip;
        }
    }
    while (k--) {
        int ev, k2 = kind(t);
        u8 g = clock1(t, &t->cnt, &t->down, &ev);
        f |= g;
        for (ch = 0; ch < 2; ch++)
            oc_action(t, ch, (g & (ch ? OCFB : OCFA)) != 0, ev, t->down);
        if ((((ev & EV_BOTTOM) && (k2 == K_FAST || k2 == K_PFC)) || ((ev & EV_TOP) && k2 == K_PC)) &&
            memcmp(t->ocr_now, t->ocr_buf, sizeof t->ocr_now)) {
            memcpy(t->ocr_now, t->ocr_buf, sizeof t->ocr_now);
            buffered = 1;
        }
    }
    mirror(t);
    if (t->oc[0] != oc0 || t->oc[1] != oc1 || buffered)
        for (ch = 0; ch < 2; ch++)
            drive(t, ch);
    if (f & ~(u8)t->tifr->value) {
        esk_rf_hw(t->tifr, t->tifr->value | f);
        esk_avr_irq_changed(t->s);
    }
}

static void tc_tick(void *ctx, u32 cycles)
{
    struct avr_tc *t = (struct avr_tc *)ctx;
    u32 n = prescale(t);
    u64 now, before;
    if (!n)
        return;
    /* the prescaler runs free: the timer clocks between the cycles before
     * this instruction and after it */
    now = t->s->cycles;
    before = now - cycles;
    advance(t, now / n - before / n);
}

/* the cycles until a flag whose interrupt is enabled sets, or a pin edge */
static int tc_next(void *ctx, u32 *cycles)
{
    struct avr_tc *t = (struct avr_tc *)ctx;
    u32 n = prescale(t), k, limit;
    u8 en = (u8)(t->timsk->value & (TOV | OCFA | OCFB | ICF));
    int pins = any_connected(t), down = t->down, ch;
    u16 c = t->cnt;
    if (!n || (!en && !pins))
        return 0;
    if (t->tifr->value & en) {
        *cycles = 0;
        return 1;
    }
    limit = 2 * period(t) + 2;
    for (k = 1; k <= limit; k++) {
        int ev, edge = 0;
        u8 g = clock1(t, &c, &down, &ev);
        for (ch = 0; pins && ch < 2; ch++)
            if (t->oc_on[ch] && ((g & (ch ? OCFB : OCFA)) || (ev & EV_BOTTOM)))
                edge = 1;
        if ((g & en) || edge) {
            u64 first = n - t->s->cycles % n;
            u64 left = first + (u64)(k - 1) * n;
            *cycles = left > 0xffffffffu ? 0xffffffffu : (u32)left;
            return 1;
        }
    }
    return 0;
}

/* ---- the registers ----------------------------------------------------------- */

static int is(const struct esk_rf_reg *r, u32 off) { return r && off == esk_rf_off(r); }

static u32 tc_read(void *ctx, u32 off, int n)
{
    struct avr_tc *t = (struct avr_tc *)ctx;
    int debug = t->s->bus.debug;
    if (t->wide) {
        if (is(t->tcntl, off)) {
            if (!debug)
                t->temp = (u8)(t->cnt >> 8);
            return t->cnt & 0xffu;
        }
        if (is(t->icrl, off)) {
            if (!debug)
                t->temp = (u8)(t->icr >> 8);
            return t->icr & 0xffu;
        }
        if (is(t->tcnth, off) || is(t->icrh, off))
            return debug ? (u32)esk_rf_read(t->p, off, n) : t->temp;
    } else if (is(t->tcnt, off)) {
        return t->cnt;
    }
    return esk_rf_read(t->p, off, n);
}

static int pwm(const struct avr_tc *t) { return kind(t) == K_FAST || dual(kind(t)); }

static void set_ocr(struct avr_tc *t, int ch, u16 v)
{
    t->ocr_buf[ch] = v;
    if (!pwm(t))
        t->ocr_now[ch] = v;
}

static void tc_write(void *ctx, u32 off, int n, u32 v)
{
    struct avr_tc *t = (struct avr_tc *)ctx;
    int ch;
    u8 b = (u8)v;
    esk_rf_write(t->p, off, n, v);
    if (t->wide) {
        if (is(t->tcnth, off) || is(t->icrh, off) || is(t->ocrh[0], off) || is(t->ocrh[1], off))
            t->temp = b;
        else if (is(t->tcntl, off))
            t->cnt = (u16)(t->temp << 8 | b);
        else if (is(t->icrl, off))
            t->icr = (u16)(t->temp << 8 | b);
        else if (is(t->ocrl[0], off))
            set_ocr(t, 0, (u16)(t->temp << 8 | b));
        else if (is(t->ocrl[1], off))
            set_ocr(t, 1, (u16)(t->temp << 8 | b));
        if (is(t->tccrc, off))
            for (ch = 0; ch < 2; ch++)
                if (b & (ch ? 0x40 : 0x80) && !pwm(t))
                    oc_action(t, ch, 1, 0, 0); /* FOC1x */
    } else {
        if (is(t->tcnt, off))
            t->cnt = b;
        else if (is(t->ocr[0], off))
            set_ocr(t, 0, b);
        else if (is(t->ocr[1], off))
            set_ocr(t, 1, b);
        if (is(t->tccrb, off))
            for (ch = 0; ch < 2; ch++)
                if (b & (ch ? 0x40 : 0x80) && !pwm(t))
                    oc_action(t, ch, 1, 0, 0); /* FOCnx */
    }
    if (is(t->tccra, off) || is(t->tccrb, off)) {
        /* a mode without double buffering takes the buffered values now */
        if (!pwm(t))
            memcpy(t->ocr_now, t->ocr_buf, sizeof t->ocr_now);
        esk_rf_clock(t->p, &t->running, prescale(t) != 0);
    }
    if (t->wide) {
        esk_rf_hw(t->tcntl, t->cnt & 0xffu);
        esk_rf_hw(t->tcnth, t->cnt >> 8);
    } else {
        esk_rf_hw(t->tcnt, t->cnt);
    }
    for (ch = 0; ch < 2; ch++)
        drive(t, ch);
    if (is(t->tifr, off) || is(t->timsk, off))
        esk_avr_irq_changed(t->s);
}

static void tc_reset(void *ctx)
{
    struct avr_tc *t = (struct avr_tc *)ctx;
    int ch;
    t->cnt = t->icr = 0;
    t->temp = 0;
    t->down = 0;
    t->running = 0;
    for (ch = 0; ch < 2; ch++) {
        t->ocr_now[ch] = t->ocr_buf[ch] = 0;
        t->oc[ch] = 0;
        t->oc_on[ch] = 0;
    }
}

/* the vectors: overflow, compare A, compare B, capture */
static u8 vec_flag(const struct avr_tc *t, int vec)
{
    return vec == t->vec[0] ? TOV : vec == t->vec[1] ? OCFA : vec == t->vec[2] ? OCFB : ICF;
}

static int tc_pending(void *ctx, int vec)
{
    struct avr_tc *t = (struct avr_tc *)ctx;
    return (t->tifr->value & t->timsk->value & vec_flag(t, vec)) != 0;
}

static void tc_ack(void *ctx, int vec)
{
    struct avr_tc *t = (struct avr_tc *)ctx;
    esk_rf_hw(t->tifr, t->tifr->value & ~(u64)vec_flag(t, vec));
}

const struct esk_dev_ops esk_avr_tc8_ops = {
    "avr-tc8", tc_read, tc_write, tc_reset, tc_tick, tc_next,
};
const struct esk_dev_ops esk_avr_tc16_ops = {
    "avr-tc16", tc_read, tc_write, tc_reset, tc_tick, tc_next,
};

static struct esk_rf_reg *named(struct esk_rf_periph *p, const char *fmt, int unit, const char *suffix)
{
    char name[16];
    (void)snprintf(name, sizeof name, fmt, unit, suffix);
    return esk_rf_reg(p, name);
}

static void *create(struct esk_mach *s, struct esk_rf_periph *p, int wide)
{
    /* the OC pins: port index (B 1, D 3) and bit, by unit and channel */
    static const int pins[3][2][2] = { { { 3, 6 }, { 3, 5 } }, { { 1, 1 }, { 1, 2 } }, { { 1, 3 }, { 3, 3 } } };
    struct avr_tc t, *c;
    int ch, i;
    memset(&t, 0, sizeof t);
    if (strncmp(p->name, "TC", 2) || p->name[2] < '0' || p->name[2] > '2' || p->name[3])
        return 0;
    t.s = s;
    t.p = p;
    t.wide = wide;
    t.unit = p->name[2] - '0';
    t.div = t.unit == 2 ? div2 : div01;
    t.tifr = named(p, "TIFR%d%s", t.unit, "");
    t.timsk = named(p, "TIMSK%d%s", t.unit, "");
    t.tccra = named(p, "TCCR%d%s", t.unit, "A");
    t.tccrb = named(p, "TCCR%d%s", t.unit, "B");
    if (!t.tifr || !t.timsk || !t.tccra || !t.tccrb)
        return 0;
    if (wide) {
        t.tccrc = named(p, "TCCR%d%s", t.unit, "C");
        t.tcntl = named(p, "TCNT%d%s", t.unit, "L");
        t.tcnth = named(p, "TCNT%d%s", t.unit, "H");
        t.icrl = named(p, "ICR%d%s", t.unit, "L");
        t.icrh = named(p, "ICR%d%s", t.unit, "H");
        t.ocrl[0] = named(p, "OCR%d%s", t.unit, "AL");
        t.ocrh[0] = named(p, "OCR%d%s", t.unit, "AH");
        t.ocrl[1] = named(p, "OCR%d%s", t.unit, "BL");
        t.ocrh[1] = named(p, "OCR%d%s", t.unit, "BH");
        if (!t.tcntl || !t.tcnth || !t.icrl || !t.icrh || !t.ocrl[0] || !t.ocrh[0] || !t.ocrl[1] || !t.ocrh[1])
            return 0;
    } else {
        t.tcnt = named(p, "TCNT%d%s", t.unit, "");
        t.ocr[0] = named(p, "OCR%d%s", t.unit, "A");
        t.ocr[1] = named(p, "OCR%d%s", t.unit, "B");
        if (!t.tcnt || !t.ocr[0] || !t.ocr[1])
            return 0;
    }
    for (ch = 0; ch < 2; ch++) {
        t.oc_port[ch] = pins[t.unit][ch][0];
        t.oc_bit[ch] = pins[t.unit][ch][1];
    }
    for (i = 0; i < 4; i++)
        t.vec[i] = -1;
    for (i = 0; i < p->nirq; i++) {
        const char *nm = p->irq_names[i];
        if (strstr(nm, "_OVF"))
            t.vec[0] = p->irq_nums[i];
        else if (strstr(nm, "_COMPA"))
            t.vec[1] = p->irq_nums[i];
        else if (strstr(nm, "_COMPB"))
            t.vec[2] = p->irq_nums[i];
        else if (strstr(nm, "_CAPT"))
            t.vec[3] = p->irq_nums[i];
    }
    c = (struct avr_tc *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = t;
    for (i = 0; i < 4; i++)
        if (c->vec[i] > 0)
            esk_avr_irq_source(s, c->vec[i], tc_pending, tc_ack, c);
    return c;
}

void *esk_avr_tc8_create(struct esk_mach *s, struct esk_rf_periph *p) { return create(s, p, 0); }
void *esk_avr_tc16_create(struct esk_mach *s, struct esk_rf_periph *p) { return create(s, p, 1); }
