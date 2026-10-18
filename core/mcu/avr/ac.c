/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ac.c -- the ATmega328P's analog comparator (datasheet 23), over the
 * register file.
 *
 *   - it compares the positive input, AIN0 (PD6) or the 1.1 V bandgap
 *     with ACBG, with the negative one, AIN1 (PD7) or, with ADCSRB's ACME
 *     set and the ADC off (ADEN clear), the ADC multiplexer's pin (MUX2:0,
 *     ADC0-5 on PC0-5); ACO is 1 while the positive input is the higher;
 *   - the pins' voltages come from the circuit. On an analog net the
 *     comparator is a threshold on each input pin at the other's voltage,
 *     crossed in the analog solver at its time (exact when one input is
 *     steady, as a reference divider or the bandgap is); a digital net is
 *     0 V or the supply;
 *   - ACI sets on ACO's toggle, falling or rising edge (ACIS 00, 10, 11)
 *     and requests the ANALOG_COMP vector with ACIE; entering it, or
 *     writing ACI 1, clears it;
 *   - ACD switches it off: ACO reads 0 and nothing sets ACI.
 *
 *   - with ACIC its output is Timer/Counter1's input capture source
 *     (timer.c).
 *
 * Not modelled: ACO's synchronisation delay (one to two cycles), the input
 * offset and hysteresis, the ADC's auto-trigger from it, DIDR1's digital
 * input buffers. */
#include <string.h>

#include "mcu/avr/avr.h"

#define ACIS 0x03u
#define ACIC 0x04u
#define ACIE 0x08u
#define ACI 0x10u
#define ACO 0x20u
#define ACBG 0x40u
#define ACD 0x80u
#define BANDGAP 1.1

/* ports as the runtime numbers them */
#define PORT_C 2
#define PORT_D 3

struct avr_ac {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *acsr;
    struct esk_rf_reg *adcsra, *adcsrb, *admux; /* the ADC's, for ACME */
    int vec;
    int watch[2][2]; /* the pins (port, bit) a threshold is on, -1 when none */
};

static u8 acsr(const struct avr_ac *a) { return (u8)a->acsr->value; }

static double volts(const struct avr_ac *a, int port, int bit)
{
    struct esk_mach *s = a->s;
    double v = 0.0;
    if (s->host.pin_volts && s->host.pin_volts(s->host.ctx, port, bit, &v))
        return v;
    return esk_avr_pin_level(s, port, bit) ? s->vdd : 0.0;
}

/* the negative input's pin: AIN1, or the ADC multiplexer's with ACME */
static void negative_pin(const struct avr_ac *a, int *port, int *bit)
{
    int acme = a->adcsrb && (a->adcsrb->value & 0x40u) && a->adcsra && !(a->adcsra->value & 0x80u);
    if (acme && a->admux) {
        *port = PORT_C;
        *bit = (int)(a->admux->value & 7u);
        if (*bit > 5) /* ADC6 and ADC7 are not on the DIP-28 */
            *port = -1;
        return;
    }
    *port = PORT_D;
    *bit = 7;
}

static void set_watch(struct avr_ac *a, int k, int port, int bit, double v)
{
    struct esk_mach *s = a->s;
    if (!s->host.pin_watch)
        return;
    if (a->watch[k][0] >= 0 && (a->watch[k][0] != port || a->watch[k][1] != bit))
        s->host.pin_watch(s->host.ctx, a->watch[k][0], a->watch[k][1], 0, 0.0);
    a->watch[k][0] = port;
    a->watch[k][1] = bit;
    if (port >= 0)
        s->host.pin_watch(s->host.ctx, port, bit, 1, v);
}

/* the comparator now: ACO, ACI on its edge, and its thresholds again */
static void evaluate(struct avr_ac *a)
{
    u8 c = acsr(a), out;
    int np, nb;
    double pos, neg;
    if (c & ACD) {
        set_watch(a, 0, -1, 0, 0.0);
        set_watch(a, 1, -1, 0, 0.0);
        if (c & ACO) {
            esk_rf_hw(a->acsr, c & ~ACO);
            esk_avr_icp(a->s, 1, 0);
        }
        return;
    }
    negative_pin(a, &np, &nb);
    pos = (c & ACBG) ? BANDGAP : volts(a, PORT_D, 6);
    neg = np >= 0 ? volts(a, np, nb) : 0.0;
    out = pos > neg ? ACO : 0;
    if (out != (c & ACO)) {
        u8 edge = (c & ACIS) == 0 ? 1 : (c & ACIS) == 2 ? !out : (c & ACIS) == 3 ? out != 0 : 0;
        c = (u8)((c & ~ACO) | out);
        if (edge)
            c |= ACI;
        esk_rf_hw(a->acsr, c);
        esk_avr_icp(a->s, 1, out != 0); /* with ACIC, Timer/Counter1 captures on it */
        if (edge)
            esk_avr_irq_changed(a->s);
    }
    /* each input pin switches the comparator where it meets the other */
    if (c & ACBG)
        set_watch(a, 0, -1, 0, 0.0);
    else
        set_watch(a, 0, PORT_D, 6, neg);
    set_watch(a, 1, np, nb, pos);
}

void esk_avr_ac_pins(struct esk_mach *m)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_avr_ac_ops && p->ctx)
            evaluate((struct avr_ac *)p->ctx);
    }
}

static u32 ac_read(void *ctx, u32 off, int n)
{
    struct avr_ac *a = (struct avr_ac *)ctx;
    if (off == esk_rf_off(a->acsr))
        evaluate(a);
    return esk_rf_read(a->p, off, n);
}

static void ac_write(void *ctx, u32 off, int n, u32 v)
{
    struct avr_ac *a = (struct avr_ac *)ctx;
    u8 c;
    if (off != esk_rf_off(a->acsr)) {
        esk_rf_write(a->p, off, n, v);
        return;
    }
    c = acsr(a);
    c = (u8)((c & (ACO | ACI)) | (v & (ACD | ACBG | ACIE | ACIC | ACIS)));
    if (v & ACI)
        c = (u8)(c & ~ACI);
    esk_rf_hw(a->acsr, c);
    evaluate(a);
    esk_avr_irq_changed(a->s);
}

static void ac_reset(void *ctx)
{
    struct avr_ac *a = (struct avr_ac *)ctx;
    evaluate(a); /* ACSR is 0 out of reset: on, AIN0 against AIN1 */
}

static int ac_pending(void *ctx, int vec)
{
    struct avr_ac *a = (struct avr_ac *)ctx;
    ES_UNUSED(vec);
    return (acsr(a) & (ACIE | ACI)) == (ACIE | ACI);
}

static void ac_ack(void *ctx, int vec)
{
    struct avr_ac *a = (struct avr_ac *)ctx;
    ES_UNUSED(vec);
    esk_rf_hw(a->acsr, acsr(a) & ~ACI);
}

const struct esk_dev_ops esk_avr_ac_ops = {
    "avr-ac", ac_read, ac_write, ac_reset, 0, 0,
};

void *esk_avr_ac_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct avr_ac a, *c;
    struct esk_rf_periph *adc;
    memset(&a, 0, sizeof a);
    a.s = s;
    a.p = p;
    a.acsr = esk_rf_reg(p, "ACSR");
    if (!a.acsr)
        return 0;
    adc = p->rf ? esk_rf_periph(p->rf, "ADC") : 0;
    if (adc) {
        a.adcsra = esk_rf_reg(adc, "ADCSRA");
        a.adcsrb = esk_rf_reg(adc, "ADCSRB");
        a.admux = esk_rf_reg(adc, "ADMUX");
    }
    a.watch[0][0] = a.watch[1][0] = -1;
    a.vec = esk_rf_irq(p, "ANALOG_COMP");
    c = (struct avr_ac *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = a;
    if (c->vec > 0)
        esk_avr_irq_source(s, c->vec, ac_pending, ac_ack, c);
    return c;
}
