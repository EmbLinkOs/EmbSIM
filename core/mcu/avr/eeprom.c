/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* eeprom.c -- the ATmega328P's 1 KiB EEPROM (datasheet 8.4, 8.6), over the
 * register file.
 *
 *   - EERE reads the byte at EEAR into EEDR;
 *   - EEMPE written 1 opens a 4-cycle window in which EEPE written 1
 *     starts a write by EEPM: 00 erase and write (3.4 ms), 01 erase only
 *     (1.8 ms), 10 write only (1.8 ms: the bits can only clear); EEPE
 *     stays set until it is done, and a read or a second write while it is
 *     set is ignored;
 *   - the EE_READY vector is requested while EERIE is set and EEPE clear.
 *
 * The contents start erased (0xFF) and outlive a reset, not the session. */
#include <string.h>

#include "mcu/avr/avr.h"

#define EERE 0x01u
#define EEPE 0x02u
#define EEMPE 0x04u
#define EERIE 0x08u
#define EE_SIZE 1024u

struct avr_eeprom {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr, *dr, *arl, *arh;
    u8 *mem;
    u64 window;
    u64 left;
    u64 since; /* the cycle the write started */
    int running;
    int vec;
    u16 at;
    u8 data, mode;
};

static u16 addr(const struct avr_eeprom *e)
{
    return (u16)(((e->arh->value & 3u) << 8 | (e->arl->value & 0xffu)) % EE_SIZE);
}

static u32 ee_read(void *ctx, u32 off, int n)
{
    return esk_rf_read(((struct avr_eeprom *)ctx)->p, off, n);
}

static void ee_write(void *ctx, u32 off, int n, u32 v)
{
    struct avr_eeprom *e = (struct avr_eeprom *)ctx;
    struct esk_mach *m = e->s;
    if (off != esk_rf_off(e->cr)) {
        esk_rf_write(e->p, off, n, v);
        return;
    }
    if (e->running) {
        /* busy: only EERIE changes */
        esk_rf_hw(e->cr, (e->cr->value & ~(u64)EERIE) | (v & EERIE));
        esk_avr_irq_changed(m);
        return;
    }
    if ((v & EEPE) && e->window && m->cycles <= e->window) {
        static const double ms[4] = { 3.4, 1.8, 1.8, 3.4 };
        e->mode = (u8)((v >> 4) & 3u);
        e->at = addr(e);
        e->data = (u8)e->dr->value;
        e->left = (u64)(ms[e->mode] * 1e-3 * (double)esk_mach_core_hz(m));
        e->window = 0;
        e->since = m->cycles;
        esk_rf_hw(e->cr, (v & (EERIE | 0x30u)) | EEPE);
        esk_rf_clock(e->p, &e->running, 1);
    } else {
        esk_rf_hw(e->cr, v & (EERIE | 0x30u | EEMPE));
        if (v & EEMPE)
            e->window = m->cycles + 4;
        if (v & EERE)
            esk_rf_hw(e->dr, e->mem[addr(e)]);
    }
    esk_avr_irq_changed(m);
}

static void ee_tick(void *ctx, u32 cycles)
{
    struct avr_eeprom *e = (struct avr_eeprom *)ctx;
    if (!e->running)
        return;
    if (e->s->cycles - cycles < e->since)
        cycles = (u32)(e->s->cycles - e->since);
    if (e->left > cycles) {
        e->left -= cycles;
        return;
    }
    switch (e->mode) {
    case 1: e->mem[e->at] = 0xff; break;
    case 2: e->mem[e->at] &= e->data; break;
    default: e->mem[e->at] = e->data; break;
    }
    e->left = 0;
    esk_rf_clock(e->p, &e->running, 0);
    esk_rf_hw(e->cr, e->cr->value & ~(u64)(EEPE | EEMPE));
    esk_avr_irq_changed(e->s);
}

static int ee_next(void *ctx, u32 *cycles)
{
    struct avr_eeprom *e = (struct avr_eeprom *)ctx;
    if (!e->running)
        return 0;
    *cycles = e->left > 0xffffffffu ? 0xffffffffu : (u32)e->left;
    return 1;
}

static void ee_reset(void *ctx)
{
    struct avr_eeprom *e = (struct avr_eeprom *)ctx;
    e->running = 0;
    e->left = 0;
    e->window = 0;
}

static int ee_pending(void *ctx, int vec)
{
    struct avr_eeprom *e = (struct avr_eeprom *)ctx;
    ES_UNUSED(vec);
    return (e->cr->value & EERIE) && !(e->cr->value & EEPE);
}

const struct esk_dev_ops esk_avr_eeprom_ops = {
    "avr-eeprom", ee_read, ee_write, ee_reset, ee_tick, ee_next,
};

void *esk_avr_eeprom_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct avr_eeprom e, *c;
    memset(&e, 0, sizeof e);
    e.s = s;
    e.p = p;
    e.cr = esk_rf_reg(p, "EECR");
    e.dr = esk_rf_reg(p, "EEDR");
    e.arl = esk_rf_reg(p, "EEARL");
    e.arh = esk_rf_reg(p, "EEARH");
    if (!e.cr || !e.dr || !e.arl || !e.arh)
        return 0;
    e.mem = (u8 *)esk_mach_alloc(s, EE_SIZE);
    if (!e.mem)
        return 0;
    memset(e.mem, 0xff, EE_SIZE);
    e.vec = esk_rf_irq(p, "EE_READY");
    c = (struct avr_eeprom *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = e;
    if (c->vec > 0)
        esk_avr_irq_source(s, c->vec, ee_pending, 0, c);
    return c;
}
