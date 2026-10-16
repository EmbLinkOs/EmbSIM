/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* adc.c -- the ATmega328P's 10-bit ADC (datasheet 28), over the register
 * file.
 *
 *   - a conversion starts when ADSC is written 1 with ADEN set: 25 ADC
 *     clocks for the first after ADEN, 13 for the others, each ADC clock
 *     the core's divided by ADPS (2, 2, 4, 8, 16, 32, 64, 128);
 *   - the input is sampled as the conversion starts: ADC0-5 are PC0-5's
 *     voltages from the circuit (an analog net's node voltage; a digital
 *     net's level as 0 V or the supply), 14 the 1.1 V bandgap and 15 0 V;
 *     the reference is AVCC (REFS 01) or the internal 1.1 V (11); AREF
 *     (00) is taken as AVCC, the Uno's;
 *   - the result is floor(Vin * 1024 / Vref), 0 to 1023, right-adjusted in
 *     ADCH:ADCL (ADLAR left-adjusts it); ADIF sets, ADSC clears, and with
 *     ADIE the vector is requested; entering it clears ADIF;
 *   - auto-triggering in free-running mode (ADATE with ADTS 000) starts
 *     the next conversion as one ends.
 *
 * Not modelled: the other auto-trigger sources, the temperature sensor
 * (channel 8 reads 0.314 V, its datasheet value at 25 C), noise reduction
 * sleep, ADCL locking ADCH. */
#include <string.h>

#include "mcu/avr/avr.h"

#define ADEN 0x80u
#define ADSC 0x40u
#define ADATE 0x20u
#define ADIF 0x10u
#define ADIE 0x08u

struct avr_adc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *adcl, *adch, *sra, *srb, *mux;
    int vec;
    int running;
    int first; /* the next conversion is the first since ADEN */
    u64 left;
    u64 since; /* the cycle it started */
    u16 result;
};

static double input(const struct avr_adc *a)
{
    struct esk_mach *s = a->s;
    unsigned ch = (unsigned)(a->mux->value & 15u);
    double v = 0.0;
    if (ch <= 5) {
        if (s->host.pin_volts && s->host.pin_volts(s->host.ctx, 2, (int)ch, &v))
            return v;
        return esk_avr_pin_level(s, 2, (int)ch) ? s->vdd : 0.0;
    }
    if (ch == 8)
        return 0.314;
    if (ch == 14)
        return 1.1;
    return 0.0;
}

static void start(struct avr_adc *a)
{
    static const u32 div[8] = { 2, 2, 4, 8, 16, 32, 64, 128 };
    u32 d = div[a->sra->value & 7u];
    double vref = ((a->mux->value >> 6) & 3u) == 3u ? 1.1 : a->s->vdd, v = input(a), x;
    a->left = (u64)(a->first ? 25u : 13u) * d;
    a->since = a->s->cycles;
    a->first = 0;
    x = vref > 0.0 ? v * 1024.0 / vref : 0.0;
    a->result = x <= 0.0 ? 0 : x >= 1023.0 ? 1023 : (u16)x;
    esk_rf_clock(a->p, &a->running, 1);
}

static void finish(struct avr_adc *a)
{
    u16 r = a->result;
    if (a->mux->value & 0x20u) { /* ADLAR */
        esk_rf_hw(a->adch, r >> 2);
        esk_rf_hw(a->adcl, (u64)(r & 3u) << 6);
    } else {
        esk_rf_hw(a->adch, r >> 8);
        esk_rf_hw(a->adcl, r & 0xffu);
    }
    esk_rf_hw(a->sra, (a->sra->value | ADIF) & ~(u64)ADSC);
    esk_rf_clock(a->p, &a->running, 0);
    if ((a->sra->value & ADATE) && (a->srb->value & 7u) == 0 && (a->sra->value & ADEN)) {
        esk_rf_hw(a->sra, a->sra->value | ADSC);
        start(a);
    }
    esk_avr_irq_changed(a->s);
}

static u32 adc_read(void *ctx, u32 off, int n)
{
    return esk_rf_read(((struct avr_adc *)ctx)->p, off, n);
}

static void adc_write(void *ctx, u32 off, int n, u32 v)
{
    struct avr_adc *a = (struct avr_adc *)ctx;
    int was_on = (a->sra->value & ADEN) != 0;
    esk_rf_write(a->p, off, n, v);
    if (off == esk_rf_off(a->sra)) {
        if (!(a->sra->value & ADEN)) {
            /* off: a conversion in progress is abandoned */
            esk_rf_hw(a->sra, a->sra->value & ~(u64)ADSC);
            esk_rf_clock(a->p, &a->running, 0);
            a->first = 1;
        } else {
            if (!was_on)
                a->first = 1;
            if ((v & ADSC) && !a->running)
                start(a);
            else if (!a->running)
                esk_rf_hw(a->sra, a->sra->value & ~(u64)ADSC);
        }
    }
    esk_avr_irq_changed(a->s);
}

static void adc_tick(void *ctx, u32 cycles)
{
    struct avr_adc *a = (struct avr_adc *)ctx;
    if (!a->running)
        return;
    cycles = esk_mach_after(a->s, cycles, a->since);
    if (a->left > cycles) {
        a->left -= cycles;
        return;
    }
    a->left = 0;
    finish(a);
}

static int adc_next(void *ctx, u32 *cycles)
{
    struct avr_adc *a = (struct avr_adc *)ctx;
    if (!a->running)
        return 0;
    *cycles = (u32)a->left;
    return 1;
}

static void adc_reset(void *ctx)
{
    struct avr_adc *a = (struct avr_adc *)ctx;
    a->running = 0;
    a->left = 0;
    a->first = 1;
    a->result = 0;
}

static int adc_pending(void *ctx, int vec)
{
    struct avr_adc *a = (struct avr_adc *)ctx;
    ES_UNUSED(vec);
    return (a->sra->value & ADIF) && (a->sra->value & ADIE);
}

static void adc_ack(void *ctx, int vec)
{
    struct avr_adc *a = (struct avr_adc *)ctx;
    ES_UNUSED(vec);
    esk_rf_hw(a->sra, a->sra->value & ~(u64)ADIF);
}

const struct esk_dev_ops esk_avr_adc_ops = {
    "avr-adc", adc_read, adc_write, adc_reset, adc_tick, adc_next,
};

void *esk_avr_adc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct avr_adc a, *c;
    memset(&a, 0, sizeof a);
    a.s = s;
    a.p = p;
    a.adcl = esk_rf_reg(p, "ADCL");
    a.adch = esk_rf_reg(p, "ADCH");
    a.sra = esk_rf_reg(p, "ADCSRA");
    a.srb = esk_rf_reg(p, "ADCSRB");
    a.mux = esk_rf_reg(p, "ADMUX");
    if (!a.adcl || !a.adch || !a.sra || !a.srb || !a.mux)
        return 0;
    a.vec = esk_rf_irq(p, "ADC");
    a.first = 1;
    c = (struct avr_adc *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = a;
    if (c->vec > 0)
        esk_avr_irq_source(s, c->vec, adc_pending, adc_ack, c);
    return c;
}
