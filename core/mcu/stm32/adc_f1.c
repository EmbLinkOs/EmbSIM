/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* adc_f1.c -- the STM32F1's 12-bit ADC (RM0008 11), its regular group,
 * over the register file.
 *
 *   - CR2.ADON powers it; a conversion of the regular group starts on
 *     SWSTART with EXTTRIG and EXTSEL 111 (software, as ST's HAL starts
 *     it), or when ADON is written 1 again with nothing else in CR2
 *     changed; SR.STRT sets and SWSTART clears;
 *   - the group is SQR3's SQ1, or with CR1.SCAN SQ1 to SQ(L+1) (SQR1.L);
 *     each channel samples for its SMPRx time (1.5 to 239.5 ADC clocks)
 *     and is held at the sampling's end, then converts for 12.5 more; the
 *     ADC clock is PCLK2 divided by RCC's ADCPRE (2, 4, 6 or 8);
 *   - channels 0-7 are PA0-PA7, 8 and 9 PB0 and PB1, 10-15 PC0-PC5: the
 *     pin's voltage from the circuit (a digital net's level as 0 V or the
 *     supply); 16 is the temperature sensor's 1.43 V (its V25) and 17
 *     VREFINT's 1.20 V, with CR2.TSVREFE, else 0 V; VREF+ is the part's
 *     supply;
 *   - the result is floor(Vin * 4096 / VREF+), 0 to 4095, in DR, right-
 *     or (CR2.ALIGN) left-aligned; at the group's end SR.EOC sets and,
 *     with CR1.EOCIE, ADC1_2 is made pending; CR2.CONT starts the group
 *     again; reading DR clears EOC;
 *   - CR2.RSTCAL clears at once; CR2.CAL clears 83 ADC clocks after it is
 *     set (the calibration's value is not modelled: DR is not touched);
 *   - clearing ADON stops a conversion.
 *
 * Not modelled: the injected group (JSQR, JDRx, JEOC), the analog
 * watchdog, discontinuous mode, the external triggers other than
 * SWSTART, dual mode, DMA requests. */
#include <string.h>

#include "mcu/stm32/stm32.h"

#define CR1_EOCIE (1u << 5)
#define CR1_SCAN (1u << 8)
#define CR2_ADON (1u << 0)
#define CR2_CONT (1u << 1)
#define CR2_CAL (1u << 2)
#define CR2_RSTCAL (1u << 3)
#define CR2_ALIGN (1u << 11)
#define CR2_EXTSEL (7u << 17)
#define CR2_EXTTRIG (1u << 20)
#define CR2_SWSTART (1u << 22)
#define CR2_TSVREFE (1u << 23)
#define SR_EOC (1u << 1)
#define SR_STRT (1u << 4)

struct f1adc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *sr, *cr1, *cr2, *smpr1, *smpr2, *sqr1, *sqr2, *sqr3, *dr, *cfgr;
    u64 adcpre;
    int adcpre_lsb, irq, running;
    /* the conversion under way */
    int busy, pos, holding; /* pos: the channel's place in the group; holding: sampled */
    u64 left;               /* core cycles to the phase's end */
    u64 since;
    double held;
    int cal;                /* calibrating */
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

/* core cycles per ADC clock */
static u32 per_clock(const struct f1adc *a)
{
    u32 hclk = esk_stm32f1_hclk(a->s), pclk2 = esk_stm32f1_pclk(a->s, 2);
    u32 pre = a->adcpre ? (u32)((a->cfgr->value & a->adcpre) >> a->adcpre_lsb) : 0;
    u32 ratio = hclk && pclk2 ? hclk / pclk2 : 1;
    return (ratio ? ratio : 1) * 2u * (pre + 1u);
}

static int seq_len(const struct f1adc *a)
{
    return (reg(a->cr1) & CR1_SCAN) ? (int)(reg(a->sqr1) >> 20 & 15u) + 1 : 1;
}

static int channel_at(const struct f1adc *a, int pos)
{
    const struct esk_rf_reg *r = pos < 6 ? a->sqr3 : pos < 12 ? a->sqr2 : a->sqr1;
    return (int)(reg(r) >> (5 * (pos % 6)) & 31u);
}

/* the sampling time in half ADC clocks: 1.5, 7.5, 13.5, 28.5, 41.5, 55.5,
 * 71.5, 239.5 */
static u32 sample_halves(const struct f1adc *a, int ch)
{
    static const u32 h[8] = { 3, 15, 27, 57, 83, 111, 143, 479 };
    u32 smp = ch < 10 ? reg(a->smpr2) >> (3 * ch) : reg(a->smpr1) >> (3 * (ch - 10));
    return h[smp & 7u];
}

static double input(const struct f1adc *a, int ch)
{
    struct esk_mach *s = a->s;
    int port = ch < 8 ? 0 : ch < 10 ? 1 : 2, bit = ch < 8 ? ch : ch < 10 ? ch - 8 : ch - 10;
    double v = 0.0;
    if (ch == 16 || ch == 17)
        return (reg(a->cr2) & CR2_TSVREFE) ? (ch == 16 ? 1.43 : 1.20) : 0.0;
    if (ch > 15)
        return 0.0;
    if (s->host.pin_volts && s->host.pin_volts(s->host.ctx, port, bit, &v))
        return v;
    return 0.0;
}

static void phase(struct f1adc *a, u32 halves)
{
    a->left = (u64)halves * per_clock(a) / 2u;
    if (!a->left)
        a->left = 1;
    a->since = a->s->cycles;
    esk_rf_clock(a->p, &a->running, 1);
}

static void sample(struct f1adc *a)
{
    a->holding = 0;
    phase(a, sample_halves(a, channel_at(a, a->pos)));
}

static void start(struct f1adc *a)
{
    esk_rf_hw(a->cr2, a->cr2->value & ~(u64)CR2_SWSTART);
    esk_rf_hw(a->sr, a->sr->value | SR_STRT);
    a->busy = 1;
    a->pos = 0;
    sample(a);
}

static void stop(struct f1adc *a)
{
    a->busy = 0;
    if (!a->cal)
        esk_rf_clock(a->p, &a->running, 0);
}

/* the phase that was under way ended */
static void phase_end(struct f1adc *a)
{
    if (a->cal) {
        a->cal = 0;
        esk_rf_hw(a->cr2, a->cr2->value & ~(u64)CR2_CAL);
        if (!a->busy)
            esk_rf_clock(a->p, &a->running, 0);
        return;
    }
    if (!a->holding) {
        double vref = a->s->vdd > 0.0 ? a->s->vdd : 3.3;
        a->held = input(a, channel_at(a, a->pos)) * 4096.0 / vref;
        a->holding = 1;
        phase(a, 25); /* 12.5 ADC clocks */
        return;
    }
    {
        u32 code = a->held <= 0.0 ? 0u : a->held >= 4095.0 ? 4095u : (u32)a->held;
        esk_rf_hw(a->dr, (reg(a->cr2) & CR2_ALIGN) ? code << 4 : code);
    }
    if (++a->pos < seq_len(a)) {
        sample(a);
        return;
    }
    esk_rf_hw(a->sr, a->sr->value | SR_EOC);
    if (reg(a->cr1) & CR1_EOCIE)
        esk_rf_interrupt(a->p, a->irq);
    if (reg(a->cr2) & CR2_CONT) {
        a->pos = 0;
        sample(a);
    } else {
        stop(a);
    }
}

static void adc_tick(void *ctx, u32 cycles)
{
    struct f1adc *a = (struct f1adc *)ctx;
    if (!a->running)
        return;
    cycles = esk_mach_after(a->s, cycles, a->since);
    while (a->running) {
        if (a->left > cycles) {
            a->left -= cycles;
            return;
        }
        cycles -= (u32)a->left;
        a->left = 0;
        a->since = 0;
        phase_end(a);
    }
}

static int adc_next(void *ctx, u32 *cycles)
{
    struct f1adc *a = (struct f1adc *)ctx;
    if (!a->running)
        return 0;
    *cycles = a->left > 0xffffffffu ? 0xffffffffu : (u32)a->left;
    return 1;
}

static u32 adc_read(void *ctx, u32 off, int n)
{
    struct f1adc *a = (struct f1adc *)ctx;
    u32 v = esk_rf_read(a->p, off, n);
    if (off == esk_rf_off(a->dr) && !a->s->bus.debug)
        esk_rf_hw(a->sr, a->sr->value & ~(u64)SR_EOC);
    return v;
}

static void adc_write(void *ctx, u32 off, int n, u32 v)
{
    struct f1adc *a = (struct f1adc *)ctx;
    u32 before = reg(a->cr2);
    esk_rf_write(a->p, off, n, v);
    if (off != esk_rf_off(a->cr2))
        return;
    if (!(reg(a->cr2) & CR2_ADON)) {
        stop(a);
        return;
    }
    if (reg(a->cr2) & CR2_RSTCAL)
        esk_rf_hw(a->cr2, a->cr2->value & ~(u64)CR2_RSTCAL);
    if ((reg(a->cr2) & CR2_CAL) && !(before & CR2_CAL) && !a->cal) {
        a->cal = 1;
        a->left = 83u * (u64)per_clock(a);
        a->since = a->s->cycles;
        esk_rf_clock(a->p, &a->running, 1);
    }
    if (a->busy || a->cal)
        return;
    if ((reg(a->cr2) & CR2_SWSTART) && (reg(a->cr2) & CR2_EXTTRIG) && (reg(a->cr2) & CR2_EXTSEL) == CR2_EXTSEL)
        start(a);
    else if ((before & CR2_ADON) && (v & CR2_ADON) && (reg(a->cr2) & ~CR2_SWSTART) == (before & ~CR2_SWSTART))
        start(a); /* ADON written again, nothing else changed */
}

static void adc_reset(void *ctx)
{
    struct f1adc *a = (struct f1adc *)ctx;
    a->running = 0;
    a->busy = a->cal = a->holding = 0;
    a->pos = 0;
    a->left = 0;
}

const struct esk_dev_ops esk_stm32f1_adc_ops = {
    "stm32f1-adc", adc_read, adc_write, adc_reset, adc_tick, adc_next,
};

void *esk_stm32f1_adc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct f1adc a, *c;
    struct esk_rf_periph *rcc;
    memset(&a, 0, sizeof a);
    a.s = s;
    a.p = p;
    a.sr = esk_rf_reg(p, "SR");
    a.cr1 = esk_rf_reg(p, "CR1");
    a.cr2 = esk_rf_reg(p, "CR2");
    a.smpr1 = esk_rf_reg(p, "SMPR1");
    a.smpr2 = esk_rf_reg(p, "SMPR2");
    a.sqr1 = esk_rf_reg(p, "SQR1");
    a.sqr2 = esk_rf_reg(p, "SQR2");
    a.sqr3 = esk_rf_reg(p, "SQR3");
    a.dr = esk_rf_reg(p, "DR");
    if (!a.sr || !a.cr1 || !a.cr2 || !a.smpr1 || !a.smpr2 || !a.sqr1 || !a.sqr2 || !a.sqr3 || !a.dr)
        return 0;
    a.irq = esk_rf_irq(p, "ADC");
    rcc = esk_rf_periph(s->rf, "RCC");
    a.cfgr = rcc ? esk_rf_reg(rcc, "CFGR") : 0;
    a.adcpre = a.cfgr ? esk_rf_mask(a.cfgr, "ADCPRE") : 0;
    while (a.adcpre && !(a.adcpre >> a.adcpre_lsb & 1))
        a.adcpre_lsb++;
    c = (struct f1adc *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = a;
    return c;
}
