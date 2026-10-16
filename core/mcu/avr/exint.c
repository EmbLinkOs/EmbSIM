/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* exint.c -- the ATmega328P's external interrupts (datasheet 13): INT0 on
 * PD2 and INT1 on PD3, and the pin-change interrupts PCINT0-7 (PB0-7),
 * PCINT8-14 (PC0-6) and PCINT16-23 (PD0-7), over the register file.
 *
 *   - EICRA's ISCn picks INTn's sense: 00 the low level (requested while
 *     the pin is low; INTFn stays 0), 01 any change, 10 the falling edge,
 *     11 the rising edge; an edge sets INTFn, and with EIMSK's INTn the
 *     vector is requested; entering it clears INTFn;
 *   - a change on a pin PCMSKx enables sets PCIFx; with PCICR's PCIEx the
 *     vector is requested; entering it clears PCIFx;
 *   - the edges come from the ports (port.c), so a pin the program drives
 *     interrupts as one the circuit drives does, as on the part. */
#include <string.h>

#include "mcu/avr/avr.h"

struct avr_exint {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *eicra, *eimsk, *eifr, *pcicr, *pcifr, *pcmsk[3];
    int vec_int[2], vec_pc[3];
};

static struct avr_exint *exint_of(struct esk_mach *m)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_avr_exint_ops)
            return (struct avr_exint *)p->ctx;
    }
    return NULL;
}

void esk_avr_exint_pin(struct esk_mach *m, int port, int bit, int level)
{
    struct avr_exint *x = exint_of(m);
    int group, n;
    if (!x)
        return;
    /* INT0 is PD2, INT1 PD3 */
    if (port == 3 && (bit == 2 || bit == 3)) {
        n = bit - 2;
        switch ((x->eicra->value >> (2 * n)) & 3) {
        case 1: esk_rf_hw(x->eifr, x->eifr->value | ((u64)1 << n)); break;
        case 2: if (!level) esk_rf_hw(x->eifr, x->eifr->value | ((u64)1 << n)); break;
        case 3: if (level) esk_rf_hw(x->eifr, x->eifr->value | ((u64)1 << n)); break;
        default: break; /* the low level: no flag */
        }
    }
    group = port == 1 ? 0 : port == 2 ? 1 : port == 3 ? 2 : -1;
    if (group >= 0 && (x->pcmsk[group]->value >> bit & 1))
        esk_rf_hw(x->pcifr, x->pcifr->value | ((u64)1 << group));
    esk_avr_irq_changed(m);
}

static int ex_pending(void *ctx, int vec)
{
    struct avr_exint *x = (struct avr_exint *)ctx;
    int n;
    for (n = 0; n < 2; n++)
        if (vec == x->vec_int[n]) {
            if (!(x->eimsk->value >> n & 1))
                return 0;
            if (((x->eicra->value >> (2 * n)) & 3) == 0)
                return !esk_avr_pin_level(x->s, 3, 2 + n);
            return (int)(x->eifr->value >> n & 1);
        }
    for (n = 0; n < 3; n++)
        if (vec == x->vec_pc[n])
            return (x->pcicr->value >> n & 1) && (x->pcifr->value >> n & 1);
    return 0;
}

static void ex_ack(void *ctx, int vec)
{
    struct avr_exint *x = (struct avr_exint *)ctx;
    int n;
    for (n = 0; n < 2; n++)
        if (vec == x->vec_int[n])
            esk_rf_hw(x->eifr, x->eifr->value & ~((u64)1 << n));
    for (n = 0; n < 3; n++)
        if (vec == x->vec_pc[n])
            esk_rf_hw(x->pcifr, x->pcifr->value & ~((u64)1 << n));
}

static u32 ex_read(void *ctx, u32 off, int n)
{
    return esk_rf_read(((struct avr_exint *)ctx)->p, off, n);
}

static void ex_write(void *ctx, u32 off, int n, u32 v)
{
    struct avr_exint *x = (struct avr_exint *)ctx;
    esk_rf_write(x->p, off, n, v);
    esk_avr_irq_changed(x->s);
}

const struct esk_dev_ops esk_avr_exint_ops = {
    "avr-exint", ex_read, ex_write, 0, 0, 0,
};

void *esk_avr_exint_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    static const char *const ints[2] = { "INT0", "INT1" };
    static const char *const pcs[3] = { "PCINT0", "PCINT1", "PCINT2" };
    struct avr_exint x, *c;
    int i;
    memset(&x, 0, sizeof x);
    x.s = s;
    x.p = p;
    x.eicra = esk_rf_reg(p, "EICRA");
    x.eimsk = esk_rf_reg(p, "EIMSK");
    x.eifr = esk_rf_reg(p, "EIFR");
    x.pcicr = esk_rf_reg(p, "PCICR");
    x.pcifr = esk_rf_reg(p, "PCIFR");
    x.pcmsk[0] = esk_rf_reg(p, "PCMSK0");
    x.pcmsk[1] = esk_rf_reg(p, "PCMSK1");
    x.pcmsk[2] = esk_rf_reg(p, "PCMSK2");
    if (!x.eicra || !x.eimsk || !x.eifr || !x.pcicr || !x.pcifr || !x.pcmsk[0] || !x.pcmsk[1] ||
        !x.pcmsk[2])
        return 0;
    for (i = 0; i < 2; i++)
        x.vec_int[i] = esk_rf_irq(p, ints[i]);
    for (i = 0; i < 3; i++)
        x.vec_pc[i] = -1;
    /* PCINT0 is a prefix of PCINT1 and PCINT2: match the names exactly */
    for (i = 0; i < p->nirq; i++) {
        int k;
        for (k = 0; k < 3; k++)
            if (!strcmp(p->irq_names[i], pcs[k]))
                x.vec_pc[k] = p->irq_nums[i];
        for (k = 0; k < 2; k++)
            if (!strcmp(p->irq_names[i], ints[k]))
                x.vec_int[k] = p->irq_nums[i];
    }
    c = (struct avr_exint *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = x;
    for (i = 0; i < 2; i++)
        if (c->vec_int[i] > 0)
            esk_avr_irq_source(s, c->vec_int[i], ex_pending, ex_ack, c);
    for (i = 0; i < 3; i++)
        if (c->vec_pc[i] > 0)
            esk_avr_irq_source(s, c->vec_pc[i], ex_pending, ex_ack, c);
    return c;
}
