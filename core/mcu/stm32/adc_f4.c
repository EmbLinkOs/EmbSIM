/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* adc_f4.c -- the STM32F2/F4/F7's 12-bit ADCs and their common block
 * (RM0390 13), over the register file; and the triggers the timers and
 * EXTI give the ADCs and the DAC (esk_stm32_trigger).
 *
 * Each ADC (ADC1-3):
 *   - CR2.ADON powers it; the regular group (SQR3 to SQR1: SQ1, or with
 *     SCAN SQ1 to SQ(L+1)) starts on SWSTART or the edge EXTEN picks of the
 *     trigger EXTSEL picks (TIM1_CC1 ... TIM8_TRGO, EXTI_11); the injected
 *     group (JSQR: JL+1 channels, the last JL+1 of JSQ1-4) on JSWSTART or
 *     JEXTEN's edge of JEXTSEL's trigger (TIM1_CC4 ... EXTI_15), or after
 *     the regular group with JAUTO. STRT and JSTRT set as they start;
 *   - a conversion samples its channel for SMPRx's time (3 to 480 ADC
 *     clocks), holds it, and converts for RES's 12, 10, 8 or 6 more; the
 *     ADC clock is PCLK2 divided by the common CCR's ADCPRE (2, 4, 6, 8);
 *     the code is floor(Vin * 2^bits / VREF+), VREF+ the part's supply;
 *   - an injected trigger during a regular conversion stops it; the
 *     injected group runs and the regular conversion starts again after
 *     it. A trigger during its own group's conversions is ignored;
 *   - a regular result goes to DR (ALIGN right, or left: 12, 10 and 8
 *     bits from bit 15, 6 bits from bit 7); EOC sets after each conversion
 *     (EOCS) or after the group; CONT starts the group again; DISCEN
 *     converts DISCNUM+1 channels a trigger. A result while DR still holds
 *     an unread one (with DMA or EOCS) is lost: OVR sets and the group
 *     stops until the next trigger after OVR is cleared;
 *   - an injected result minus JOFRx goes to JDRx (signed: right-aligned
 *     sign-extended, left-aligned with the sign at bit 15, 6 bits at bit
 *     7); JEOC at the group's end; JDISCEN converts one channel a trigger;
 *   - the analog watchdog (AWDEN regular, JAWDEN injected; all channels, or
 *     AWDSGL's AWDCH) sets AWD when a result, at 12 bits, is above HTR or
 *     below LTR;
 *   - DMA (CR2.DMA) makes the request ADCx after each regular result,
 *     until the DMA's last item unless DDS; a DMA request is not made
 *     while OVR is set;
 *   - EOCIE, JEOCIE, AWDIE and OVRIE keep the shared ADC interrupt pending
 *     while their flag is set; SR's flags clear by writing 0, EOC also by
 *     reading DR;
 *   - the channels: the description's io.analog pins (the pin's voltage
 *     from the circuit, a digital net's level as 0 V or the supply; 0 V
 *     for a channel no pin carries); ADC1's 17 is VREFINT (1.21 V) and 18
 *     the temperature sensor (0.76 V at 25 degC, 2.5 mV/degC, at the
 *     world's temperature, else 25 degC) with TSVREFE, or VBAT / 4 with
 *     VBATE (VBAT the supply), else 0 V;
 *   - clearing ADON stops a conversion.
 *
 * The common block (CSR, CCR, CDR):
 *   - CSR shows the three ADCs' flags;
 *   - CCR.MULTI makes ADC1 the master of ADC2 (dual) or ADC2 and ADC3
 *     (triple): its triggers start the others' groups too, theirs being
 *     ignored: regular simultaneous, injected simultaneous, both, regular
 *     simultaneous with alternate trigger, interleaved (ADC2, then ADC3,
 *     starting DELAY+5 ADC clocks after the one before), alternate trigger
 *     (each injected trigger to the next ADC);
 *   - with CCR.DMA the results go to CDR and the request is ADC1's: mode 1
 *     one 16-bit result a request, mode 2 two in a word (DATA2:DATA1, or
 *     in triple mode alternately), mode 3 two 8-bit results in a halfword;
 *     DDS as in an ADC; a result while CDR is full is lost (OVR on the
 *     ADC that made it).
 *
 * Not modelled: the start-up time after ADON (tSTAB: conversions are
 * exact at once), the input's source impedance against the sampling time
 * (each sample takes the net's voltage as it is), the converter's
 * offset, gain and linearity errors and noise (the code is the ideal
 * one), the die's self-heating (the sensor reads the room's temperature). */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

#define SR_AWD (1u << 0)
#define SR_EOC (1u << 1)
#define SR_JEOC (1u << 2)
#define SR_JSTRT (1u << 3)
#define SR_STRT (1u << 4)
#define SR_OVR (1u << 5)
#define CR1_EOCIE (1u << 5)
#define CR1_AWDIE (1u << 6)
#define CR1_JEOCIE (1u << 7)
#define CR1_SCAN (1u << 8)
#define CR1_AWDSGL (1u << 9)
#define CR1_JAUTO (1u << 10)
#define CR1_DISCEN (1u << 11)
#define CR1_JDISCEN (1u << 12)
#define CR1_JAWDEN (1u << 22)
#define CR1_AWDEN (1u << 23)
#define CR1_OVRIE (1u << 26)
#define CR2_ADON (1u << 0)
#define CR2_CONT (1u << 1)
#define CR2_DMA (1u << 8)
#define CR2_DDS (1u << 9)
#define CR2_EOCS (1u << 10)
#define CR2_ALIGN (1u << 11)
#define CR2_JSWSTART (1u << 22)
#define CR2_SWSTART (1u << 30)
#define CCR_DDS (1u << 13)
#define CCR_VBATE (1u << 22)
#define CCR_TSVREFE (1u << 23)

/* the external triggers EXTSEL and JEXTSEL pick (RM0390 tables 70, 71) */
static const char *const reg_triggers[16] = {
    "TIM1_CC1", "TIM1_CC2", "TIM1_CC3", "TIM2_CC2", "TIM2_CC3", "TIM2_CC4", "TIM2_TRGO", "TIM3_CC1",
    "TIM3_TRGO", "TIM4_CC4", "TIM5_CC1", "TIM5_CC2", "TIM5_CC3", "TIM8_CC1", "TIM8_TRGO", "EXTI11",
};
static const char *const inj_triggers[16] = {
    "TIM1_CC4", "TIM1_TRGO", "TIM2_CC1", "TIM2_TRGO", "TIM3_CC2", "TIM3_CC4", "TIM4_CC1", "TIM4_CC2",
    "TIM4_CC3", "TIM4_TRGO", "TIM5_CC4", "TIM5_TRGO", "TIM8_CC2", "TIM8_CC3", "TIM8_CC4", "EXTI15",
};

struct adcc;

struct adc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *sr, *cr1, *cr2, *smpr1, *smpr2, *jofr[4], *htr, *ltr, *sqr1, *sqr2, *sqr3, *jsqr;
    struct esk_rf_reg *jdr[4], *dr;
    int unit, irq, running;
    char sig_dma[8];
    /* the groups */
    int reg_on, reg_pos, reg_disc;  /* the regular group under way, its next place, its channels left */
    int inj_on, inj_pos;            /* the injected group under way and its next place */
    int inj_next;                   /* JDISCEN: the place the next trigger converts */
    int reg_seq_pos;                /* DISCEN: the place the next trigger starts at */
    /* the conversion under way */
    int conv, conv_inj, conv_ch, holding;
    double held;
    u64 left, since;
    u64 delay;                      /* interleaved: ADC clocks before a slave starts, 0 none */
    int delayed;
    int dma_done;                   /* the DMA's last item: no more requests (DDS 0) */
    int unread;                     /* DR holds a result not read yet */
    int ovr_stop;                   /* OVR stopped the regular group */
};

/* the common block */
struct adcc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *csr, *ccr, *cdr;
    u32 fifo[4];                    /* results for CDR, oldest first */
    int nfifo;
    u32 half;                       /* DMA mode 2/3: the first of the pair */
    int has_half;
    int alt;                        /* alternate trigger: the next ADC's index */
    int dma_done;
};

static u32 reg(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }

static struct adcc *common_of(struct esk_mach *s);
static struct adc *adc_unit(struct esk_mach *s, int unit);

static u32 ccr(struct esk_mach *s)
{
    struct adcc *c = common_of(s);
    return c ? reg(c->ccr) : 0;
}

/* CCR.MULTI: 0 independent, else the mode's low four bits; 2 or 3 ADCs */
static int multi(struct esk_mach *s, int *n)
{
    u32 m = ccr(s) & 31u;
    *n = m & 16u ? 3 : 2;
    return m ? (int)(m & 15u) : 0;
}

static int res_bits(const struct adc *a)
{
    static const int bits[4] = { 12, 10, 8, 6 };
    return bits[reg(a->cr1) >> 24 & 3u];
}

/* core cycles per ADC clock */
static u32 per_clock(const struct adc *a)
{
    u32 core = esk_mach_core_hz(a->s), pclk2 = esk_stm32_pclk(a->s, 2);
    u32 ratio = core && pclk2 && core >= pclk2 ? core / pclk2 : 1;
    return ratio * 2u * ((ccr(a->s) >> 16 & 3u) + 1u);
}

static void update(struct adc *a)
{
    u32 sr, cr1 = reg(a->cr1), c = ccr(a->s);
    int dma = (reg(a->cr2) & CR2_DMA) && !(c >> 14 & 3u);
    esk_stm32_dma_request(a->s, a->sig_dma, dma && a->unread && !a->dma_done && !(reg(a->sr) & SR_OVR));
    sr = reg(a->sr);
    if (((sr & SR_EOC) && (cr1 & CR1_EOCIE)) || ((sr & SR_JEOC) && (cr1 & CR1_JEOCIE)) ||
        ((sr & SR_AWD) && (cr1 & CR1_AWDIE)) || ((sr & SR_OVR) && (cr1 & CR1_OVRIE)))
        esk_rf_interrupt(a->p, a->irq);
}

static void sr_set(struct adc *a, u32 set) { esk_rf_hw(a->sr, a->sr->value | set); }

/* ---- channels -------------------------------------------------------------------- */

static int reg_len(const struct adc *a) { return (reg(a->cr1) & CR1_SCAN) ? (int)(reg(a->sqr1) >> 20 & 15u) + 1 : 1; }

static int reg_channel(const struct adc *a, int pos)
{
    const struct esk_rf_reg *r = pos < 6 ? a->sqr3 : pos < 12 ? a->sqr2 : a->sqr1;
    return (int)(reg(r) >> (5 * (pos % 6)) & 31u);
}

static int inj_len(const struct adc *a) { return (int)(reg(a->jsqr) >> 20 & 3u) + 1; }

/* the injected group's k-th conversion: JSQ(4 - len + k) */
static int inj_channel(const struct adc *a, int k)
{
    int slot = 4 - inj_len(a) + k;
    return (int)(reg(a->jsqr) >> (5 * slot) & 31u);
}

/* the sampling time in ADC clocks */
static u32 sample_clocks(const struct adc *a, int ch)
{
    static const u32 t[8] = { 3, 15, 28, 56, 84, 112, 144, 480 };
    u32 smp = ch < 10 ? reg(a->smpr2) >> (3 * ch) : reg(a->smpr1) >> (3 * (ch - 10));
    return t[smp & 7u];
}

static double input(const struct adc *a, int ch)
{
    struct esk_mach *s = a->s;
    u32 c = ccr(s);
    double v = 0.0, vdd = s->vdd > 0.0 ? s->vdd : 3.3;
    int at;
    if (ch >= 16) {
        if (a->unit != 1)
            return 0.0; /* ADC2's and ADC3's 16-18 go to VSSA */
        if (ch == 17)
            return (c & CCR_TSVREFE) ? 1.21 : 0.0;
        if (ch == 18 && (c & CCR_VBATE))
            return vdd / 4.0;
        if (ch == 18 && (c & CCR_TSVREFE)) {
            double t = 25.0, w;
            if (s->host.temperature && s->host.temperature(s->host.ctx, &w))
                t = w;
            return 0.76 + (t - 25.0) * 0.0025;
        }
        return 0.0;
    }
    at = esk_rf_analog_pin(s->rf, a->unit, ch);
    if (at < 0)
        return 0.0;
    if (s->host.pin_volts && s->host.pin_volts(s->host.ctx, at / 32, at % 32, &v))
        return v;
    return 0.0;
}

/* ---- conversions ----------------------------------------------------------------- */

static void phase(struct adc *a, u32 clocks)
{
    a->left = (u64)clocks * per_clock(a);
    if (!a->left)
        a->left = 1;
    a->since = a->s->cycles;
    esk_rf_clock(a->p, &a->running, 1);
}

static void idle(struct adc *a)
{
    a->conv = 0;
    if (!a->delayed)
        esk_rf_clock(a->p, &a->running, 0);
}

/* the next conversion: the injected group's first, then the regular's */
static void next(struct adc *a)
{
    if (a->inj_on) {
        a->conv = 1;
        a->conv_inj = 1;
        a->conv_ch = inj_channel(a, a->inj_pos);
    } else if (a->reg_on) {
        a->conv = 1;
        a->conv_inj = 0;
        a->conv_ch = reg_channel(a, a->reg_pos);
    } else {
        idle(a);
        return;
    }
    a->holding = 0;
    phase(a, sample_clocks(a, a->conv_ch));
}

static void start_regular(struct adc *a)
{
    if (!(reg(a->cr2) & CR2_ADON) || a->reg_on || (reg(a->sr) & SR_OVR))
        return;
    esk_rf_hw(a->cr2, a->cr2->value & ~(u64)CR2_SWSTART);
    sr_set(a, SR_STRT);
    a->reg_on = 1;
    a->ovr_stop = 0;
    if (reg(a->cr1) & CR1_DISCEN) {
        a->reg_pos = a->reg_seq_pos;
        a->reg_disc = (int)(reg(a->cr1) >> 13 & 7u) + 1;
    } else {
        a->reg_pos = 0;
        a->reg_disc = 0;
    }
    if (!a->conv)
        next(a);
}

static void start_injected(struct adc *a)
{
    if (!(reg(a->cr2) & CR2_ADON) || a->inj_on)
        return;
    esk_rf_hw(a->cr2, a->cr2->value & ~(u64)CR2_JSWSTART);
    sr_set(a, SR_JSTRT);
    a->inj_on = 1;
    a->inj_pos = (reg(a->cr1) & CR1_JDISCEN) ? a->inj_next : 0;
    if (a->conv && !a->conv_inj)
        a->conv = 0; /* the regular conversion stops; it starts again after */
    if (!a->conv)
        next(a);
}

/* ADC1's triggers, as CCR.MULTI shares them */
static void group_start(struct adc *a, int inj)
{
    int n, m = multi(a->s, &n), k;
    struct adcc *c = common_of(a->s);
    if (m && a->unit != 1)
        return; /* a slave's own triggers are ignored */
    if (!m) {
        if (inj)
            start_injected(a);
        else
            start_regular(a);
        return;
    }
    if (inj) {
        if (m == 1 || m == 5) { /* injected simultaneous */
            for (k = 1; k <= n; k++)
                if (adc_unit(a->s, k))
                    start_injected(adc_unit(a->s, k));
        } else if (m == 2 || m == 9) { /* alternate trigger */
            struct adc *u = adc_unit(a->s, (c ? c->alt : 0) + 1);
            if (u)
                start_injected(u);
            if (c)
                c->alt = (c->alt + 1) % n;
        } else {
            start_injected(a);
        }
        return;
    }
    if (m == 1 || m == 2 || m == 6) { /* regular simultaneous */
        for (k = 1; k <= n; k++)
            if (adc_unit(a->s, k))
                start_regular(adc_unit(a->s, k));
    } else if (m == 7) { /* interleaved */
        u32 delay = (ccr(a->s) >> 8 & 15u) + 5u;
        start_regular(a);
        for (k = 2; k <= n; k++) {
            struct adc *u = adc_unit(a->s, k);
            if (!u)
                continue;
            u->delayed = 1;
            u->delay = (u64)delay * (u32)(k - 1) * per_clock(u);
            u->since = u->s->cycles;
            esk_rf_clock(u->p, &u->running, 1);
        }
    } else {
        start_regular(a);
    }
}

/* the result's code at RES's resolution */
static u32 code_of(const struct adc *a)
{
    int bits = res_bits(a);
    double full = (double)(1u << bits);
    return a->held <= 0.0 ? 0u : a->held >= full - 1.0 ? (1u << bits) - 1u : (u32)a->held;
}

static void watchdog(struct adc *a, u32 code, int inj)
{
    u32 cr1 = reg(a->cr1), v = code << (12 - res_bits(a));
    if (!(cr1 & (inj ? CR1_JAWDEN : CR1_AWDEN)))
        return;
    if ((cr1 & CR1_AWDSGL) && (int)(cr1 & 31u) != a->conv_ch)
        return;
    if (v > (reg(a->htr) & 0xfffu) || v < (reg(a->ltr) & 0xfffu))
        sr_set(a, SR_AWD);
}

/* a regular result for CDR (a multi-ADC mode with CCR.DMA) */
static void to_cdr(struct adc *a, u32 code)
{
    struct adcc *c = common_of(a->s);
    u32 mode = ccr(a->s) >> 14 & 3u, word;
    if (!c)
        return;
    if (mode == 1) {
        word = code;
    } else {
        if (!c->has_half) {
            c->half = code;
            c->has_half = 1;
            return;
        }
        c->has_half = 0;
        word = mode == 2 ? (code << 16 | c->half) : ((code & 0xffu) << 8 | (c->half & 0xffu));
    }
    if (c->nfifo == 4) {
        sr_set(a, SR_OVR);
        return;
    }
    c->fifo[c->nfifo++] = word;
    esk_rf_hw(c->cdr, c->fifo[0]);
    {
        struct adc *m1 = adc_unit(a->s, 1);
        if (m1 && !c->dma_done)
            esk_stm32_dma_request(a->s, m1->sig_dma, 1);
    }
}

static void regular_done(struct adc *a, u32 code)
{
    u32 cr2 = reg(a->cr2), val;
    int bits = res_bits(a), n, m = multi(a->s, &n);
    int seq_end;
    val = !(cr2 & CR2_ALIGN) ? code : bits == 6 ? code << 2 : code << (16 - bits);
    if (a->unread && ((cr2 & CR2_DMA) || (cr2 & CR2_EOCS))) {
        sr_set(a, SR_OVR); /* the unread result's successor is lost */
        a->reg_on = 0;
        a->ovr_stop = 1;
        update(a);
        return;
    }
    esk_rf_hw(a->dr, val);
    a->unread = 1;
    watchdog(a, code, 0);
    if (m && (ccr(a->s) >> 14 & 3u))
        to_cdr(a, val);
    a->reg_pos++;
    seq_end = a->reg_pos >= reg_len(a);
    if ((cr2 & CR2_EOCS) || seq_end)
        sr_set(a, SR_EOC);
    if (reg(a->cr1) & CR1_DISCEN) {
        a->reg_seq_pos = seq_end ? 0 : a->reg_pos;
        if (seq_end || --a->reg_disc <= 0)
            a->reg_on = 0;
    } else if (seq_end) {
        a->reg_pos = 0;
        if (!(cr2 & CR2_CONT))
            a->reg_on = 0;
    }
    if (seq_end && (reg(a->cr1) & CR1_JAUTO)) {
        a->inj_on = 1; /* the injected group after the regular one */
        a->inj_pos = 0;
        sr_set(a, SR_JSTRT);
    }
    update(a);
}

static void injected_done(struct adc *a, u32 code)
{
    int k = a->inj_pos, bits = res_bits(a), len = inj_len(a), seq_end;
    long v = (long)code - (long)(reg(a->jofr[k]) & 0xfffu);
    u32 out;
    if (!(reg(a->cr2) & CR2_ALIGN))
        out = (u32)v & 0xffffu;
    else
        out = (u32)(v * (bits == 6 ? 2L : (1L << (15 - bits)))) & 0xffffu;
    esk_rf_hw(a->jdr[k], out);
    watchdog(a, code, 1);
    a->inj_pos++;
    seq_end = a->inj_pos >= len;
    if (reg(a->cr1) & CR1_JDISCEN) {
        a->inj_next = seq_end ? 0 : a->inj_pos;
        a->inj_on = 0;
    } else if (seq_end) {
        a->inj_on = 0;
    }
    if (seq_end) {
        sr_set(a, SR_JEOC);
        if ((reg(a->cr1) & CR1_JAUTO) && (reg(a->cr2) & CR2_CONT) && !a->reg_on && !a->ovr_stop) {
            a->reg_on = 1; /* JAUTO with CONT: the regular group again */
            a->reg_pos = 0;
        }
    }
    update(a);
}

static void phase_end(struct adc *a)
{
    if (!a->holding) {
        double vref = a->s->vdd > 0.0 ? a->s->vdd : 3.3;
        a->held = input(a, a->conv_ch) * (double)(1u << res_bits(a)) / vref;
        a->holding = 1;
        phase(a, (u32)res_bits(a));
        return;
    }
    a->conv = 0;
    if (a->conv_inj)
        injected_done(a, code_of(a));
    else
        regular_done(a, code_of(a));
    next(a);
}

static void adc_tick(void *ctx, u32 cycles)
{
    struct adc *a = (struct adc *)ctx;
    if (!a->running)
        return;
    cycles = esk_mach_after(a->s, cycles, a->since);
    a->since = 0;
    if (a->delayed) {
        if (a->delay > cycles) {
            a->delay -= cycles;
        } else {
            a->delayed = 0;
            a->delay = 0;
            start_regular(a); /* interleaved: this slave's turn */
        }
    }
    while (a->running && a->conv) {
        if (a->left > cycles) {
            a->left -= cycles;
            return;
        }
        cycles -= (u32)a->left;
        a->left = 0;
        a->since = 0;
        phase_end(a);
    }
    if (!a->conv && !a->delayed)
        esk_rf_clock(a->p, &a->running, 0);
}

static int adc_next(void *ctx, u32 *cycles)
{
    struct adc *a = (struct adc *)ctx;
    u64 best = ~(u64)0;
    if (!a->running)
        return 0;
    if (a->conv)
        best = a->left ? a->left : 1;
    if (a->delayed && a->delay < best)
        best = a->delay ? a->delay : 1;
    if (best == ~(u64)0)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static u32 adc_read(void *ctx, u32 off, int n)
{
    struct adc *a = (struct adc *)ctx;
    u32 v = esk_rf_read(a->p, off, n);
    if (off == esk_rf_off(a->dr) && !a->s->bus.debug) {
        a->unread = 0;
        esk_rf_hw(a->sr, a->sr->value & ~(u64)SR_EOC);
        update(a);
    }
    return v;
}

static void adc_write(void *ctx, u32 off, int n, u32 v)
{
    struct adc *a = (struct adc *)ctx;
    u32 before = reg(a->cr2);
    if (off == esk_rf_off(a->sr)) {
        esk_rf_hw(a->sr, a->sr->value & (v | ~0x3fu)); /* rc_w0 */
        update(a);
        return;
    }
    esk_rf_write(a->p, off, n, v);
    if (off != esk_rf_off(a->cr2))
        return;
    if (!(reg(a->cr2) & CR2_ADON)) {
        a->reg_on = a->inj_on = 0;
        a->delayed = 0;
        idle(a);
        return;
    }
    if (!(before & CR2_DMA) && (reg(a->cr2) & CR2_DMA))
        a->dma_done = 0;
    if (reg(a->cr2) & CR2_SWSTART)
        group_start(a, 0);
    if (reg(a->cr2) & CR2_JSWSTART && !(reg(a->cr1) & CR1_JAUTO))
        group_start(a, 1);
    update(a);
}

static void adc_reset(void *ctx)
{
    struct adc *a = (struct adc *)ctx;
    a->running = 0;
    a->reg_on = a->inj_on = a->conv = a->holding = 0;
    a->reg_pos = a->inj_pos = a->inj_next = a->reg_seq_pos = 0;
    a->delayed = 0;
    a->left = a->delay = 0;
    a->dma_done = a->ovr_stop = a->unread = 0;
}

const struct esk_dev_ops esk_stm32f4_adc_ops = {
    "stm32f4-adc", adc_read, adc_write, adc_reset, adc_tick, adc_next,
};

static struct adc *adc_unit(struct esk_mach *s, int unit)
{
    char name[8] = "ADCx";
    struct esk_rf_periph *p;
    name[3] = (char)('0' + unit);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_stm32f4_adc_ops && !esk_rf_gated(p) ? (struct adc *)p->ctx : 0;
}

void *esk_stm32f4_adc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct adc a, *c;
    int k;
    memset(&a, 0, sizeof a);
    a.s = s;
    a.p = p;
    a.sr = esk_rf_reg(p, "SR");
    a.cr1 = esk_rf_reg(p, "CR1");
    a.cr2 = esk_rf_reg(p, "CR2");
    a.smpr1 = esk_rf_reg(p, "SMPR1");
    a.smpr2 = esk_rf_reg(p, "SMPR2");
    a.htr = esk_rf_reg(p, "HTR");
    a.ltr = esk_rf_reg(p, "LTR");
    a.sqr1 = esk_rf_reg(p, "SQR1");
    a.sqr2 = esk_rf_reg(p, "SQR2");
    a.sqr3 = esk_rf_reg(p, "SQR3");
    a.jsqr = esk_rf_reg(p, "JSQR");
    a.dr = esk_rf_reg(p, "DR");
    for (k = 0; k < 4; k++) {
        char n1[8], n2[8];
        snprintf(n1, sizeof n1, "JOFR%d", k + 1);
        snprintf(n2, sizeof n2, "JDR%d", k + 1);
        a.jofr[k] = esk_rf_reg(p, n1);
        a.jdr[k] = esk_rf_reg(p, n2);
        if (!a.jofr[k] || !a.jdr[k])
            return 0;
    }
    if (!a.sr || !a.cr1 || !a.cr2 || !a.smpr1 || !a.smpr2 || !a.htr || !a.ltr || !a.sqr1 || !a.sqr2 ||
        !a.sqr3 || !a.jsqr || !a.dr || strncmp(p->name, "ADC", 3) || p->name[3] < '1' || p->name[3] > '3' ||
        p->name[4])
        return 0; /* not this layout: the registers alone */
    a.unit = p->name[3] - '0';
    a.irq = esk_rf_irq(p, "ADC");
    snprintf(a.sig_dma, sizeof a.sig_dma, "%s", p->name);
    c = (struct adc *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = a;
    return c;
}

/* ---- the common block --------------------------------------------------------- */

static struct adcc *common_of(struct esk_mach *s)
{
    int i;
    struct esk_rf_periph *p;
    for (i = 0; s->rf && (p = esk_rf_periph_at(s->rf, i)) != 0; i++)
        if (p->ops == &esk_stm32f4_adc_common_ops)
            return (struct adcc *)p->ctx;
    return 0;
}

static u32 adcc_read(void *ctx, u32 off, int n)
{
    struct adcc *c = (struct adcc *)ctx;
    if (off == esk_rf_off(c->csr)) {
        u32 v = 0;
        int k;
        for (k = 1; k <= 3; k++) {
            struct adc *a = adc_unit(c->s, k);
            if (a)
                v |= (reg(a->sr) & 0x3fu) << (8 * (k - 1));
        }
        esk_rf_hw(c->csr, v);
    }
    if (off == esk_rf_off(c->cdr) && !c->s->bus.debug && c->nfifo) {
        u32 v = c->fifo[0];
        struct adc *m1 = adc_unit(c->s, 1);
        memmove(c->fifo, c->fifo + 1, (size_t)(c->nfifo - 1) * sizeof c->fifo[0]);
        c->nfifo--;
        esk_rf_hw(c->cdr, c->nfifo ? c->fifo[0] : v);
        if (m1)
            esk_stm32_dma_request(c->s, m1->sig_dma, c->nfifo > 0 && !c->dma_done);
        return v;
    }
    return esk_rf_read(c->p, off, n);
}

static void adcc_write(void *ctx, u32 off, int n, u32 v)
{
    struct adcc *c = (struct adcc *)ctx;
    u32 was = reg(c->ccr);
    esk_rf_write(c->p, off, n, v);
    if (off == esk_rf_off(c->ccr) && (was & 0xe01fu) != (reg(c->ccr) & 0xe01fu)) {
        c->nfifo = 0;
        c->has_half = 0;
        c->alt = 0;
        c->dma_done = 0;
    }
}

static void adcc_reset(void *ctx)
{
    struct adcc *c = (struct adcc *)ctx;
    c->nfifo = 0;
    c->has_half = 0;
    c->alt = 0;
    c->dma_done = 0;
}

const struct esk_dev_ops esk_stm32f4_adc_common_ops = {
    "stm32f4-adc-common", adcc_read, adcc_write, adcc_reset, 0, 0,
};

void *esk_stm32f4_adc_common_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct adcc c, *q;
    memset(&c, 0, sizeof c);
    c.s = s;
    c.p = p;
    c.csr = esk_rf_reg(p, "CSR");
    c.ccr = esk_rf_reg(p, "CCR");
    c.cdr = esk_rf_reg(p, "CDR");
    if (!c.csr || !c.ccr || !c.cdr)
        return 0;
    q = (struct adcc *)esk_mach_alloc(s, sizeof *q);
    if (q)
        *q = c;
    return q;
}

/* ---- the triggers ----------------------------------------------------------------- */

/* the edge EXTEN (or JEXTEN) waits for: 01 rising, 10 falling, 11 both */
static int edge_wanted(u32 exten, int level) { return (exten == 1 && level) || (exten == 2 && !level) || exten == 3; }

void esk_stm32_dma_ended_adc(struct esk_mach *s, const char *sig)
{
    int k;
    for (k = 1; k <= 3; k++) {
        struct adc *a = adc_unit(s, k);
        struct adcc *c = common_of(s);
        if (!a || strcmp(a->sig_dma, sig))
            continue;
        if ((ccr(s) >> 14 & 3u) && c) {
            if (!(ccr(s) & CCR_DDS))
                c->dma_done = 1;
        } else if (!(reg(a->cr2) & CR2_DDS)) {
            a->dma_done = 1;
        }
        update(a);
    }
}

void esk_stm32_trigger(struct esk_mach *s, const char *sig, int level)
{
    int k;
    for (k = 1; k <= 3; k++) {
        struct adc *a = adc_unit(s, k);
        u32 cr2;
        if (!a)
            continue;
        cr2 = reg(a->cr2);
        if (!(cr2 & CR2_ADON))
            continue;
        if ((cr2 >> 28 & 3u) && edge_wanted(cr2 >> 28 & 3u, level) && !strcmp(reg_triggers[cr2 >> 24 & 15u], sig))
            group_start(a, 0);
        if ((cr2 >> 20 & 3u) && !(reg(a->cr1) & CR1_JAUTO) && edge_wanted(cr2 >> 20 & 3u, level) &&
            !strcmp(inj_triggers[cr2 >> 16 & 15u], sig))
            group_start(a, 1);
    }
    esk_stm32_dac_trigger(s, sig, level);
}
