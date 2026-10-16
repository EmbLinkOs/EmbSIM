/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* periph.c -- Nordic's peripheral interface (nrf52.h says what it is): the
 * tasks, events and interrupt every nRF52840 model shares. */
#include <string.h>

#include "mcu/nrf52/nrf52.h"

#define NSLOT 1024 /* offsets 0 to 0xffc */

int esk_nrfp_init(struct nrfp *x, struct esk_mach *s, struct esk_rf_periph *p)
{
    int i;
    x->s = s;
    x->p = p;
    x->irq = esk_rf_irq(p, 0);
    x->inten = 0;
    x->at = (struct esk_rf_reg **)esk_mach_alloc(s, NSLOT * sizeof *x->at);
    if (!x->at)
        return -1;
    memset(x->at, 0, NSLOT * sizeof *x->at);
    for (i = 0; i < p->nreg; i++) {
        u32 off = esk_rf_off(p->reg[i]);
        if (off < 4u * NSLOT && !(off & 3))
            x->at[off / 4] = p->reg[i];
    }
    return 0;
}

struct esk_rf_reg *esk_nrfp_reg(const struct nrfp *x, u32 off)
{
    return off < 4u * NSLOT && !(off & 3) ? x->at[off / 4] : 0;
}

u32 esk_nrfp_rd(const struct nrfp *x, u32 off)
{
    struct esk_rf_reg *r = esk_nrfp_reg(x, off);
    return r ? (u32)r->value : 0;
}

void esk_nrfp_hw(struct nrfp *x, u32 off, u32 v)
{
    struct esk_rf_reg *r = esk_nrfp_reg(x, off);
    if (r && r->value != v)
        esk_rf_hw(r, v);
}

int esk_nrfp_line(const struct nrfp *x)
{
    int n;
    for (n = 0; n < 32; n++)
        if ((x->inten >> n & 1) && esk_nrfp_rd(x, 0x100u + 4u * (u32)n))
            return 1;
    return 0;
}

void esk_nrfp_irq(struct nrfp *x, int was, int again)
{
    if (esk_nrfp_line(x) && (!was || again) && x->irq >= 0)
        esk_rf_interrupt(x->p, x->irq);
}

void esk_nrfp_event(struct nrfp *x, u32 off)
{
    int was = esk_nrfp_line(x);
    esk_nrfp_hw(x, off, 1);
    esk_nrfp_irq(x, was, 0);
}

int esk_nrfp_inten_read(struct nrfp *x, u32 off)
{
    if (off < 0x300 || off > 0x308)
        return 0;
    esk_nrfp_hw(x, off, x->inten);
    return 1;
}

int esk_nrfp_inten_write(struct nrfp *x, u32 off, u32 v)
{
    if (off == 0x300)
        x->inten = v;
    else if (off == 0x304)
        x->inten |= v;
    else if (off == 0x308)
        x->inten &= ~v;
    else
        return 0;
    esk_nrfp_hw(x, off, x->inten);
    return 1;
}
