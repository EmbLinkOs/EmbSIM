/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* osc.c -- a PIC24FJ's oscillator (OSCCON, CLKDIV: the OSC peripheral) and
 * its Peripheral Pin Select lock (PPS).
 *
 *   - Out of reset, OSCCON's COSC and NOSC are the configuration words'
 *     FNOSC (FOSCSEL<2:0>, in the last page of program memory), the
 *     oscillator the part starts on.
 *   - OSCCON's bytes take a write only right after their unlock sequence:
 *     0x46 then 0x57 written to the low byte, 0x78 then 0x9A to the high
 *     one (what XC16's __builtin_write_OSCCONL and _OSCCONH do); without
 *     it the write is ignored, as on the part, and the first such is a
 *     warning (mcu.locked_write). Setting OSWEN switches to NOSC at once:
 *     COSC takes it, OSWEN clears, LOCK says a PLL is locked.
 *   - IOLOCK (OSCCON<6>) locks the PPS registers (RPINRx, RPORx): a write
 *     to them is ignored while it is set; with the IOL1WAY configuration
 *     bit (FOSC<4>) set, once set it stays.
 * The core's clock (Fcy, the machine's cycle) follows COSC and CLKDIV
 * (esk_pic24_fcy). Not modelled: the fail-safe clock monitor, two-speed
 * start-up, the FRC's tuning and self-tuning, the reference clock output.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/pic24-osc.c. Changes for
 * EmbSIM: the configuration words' addresses come from the register
 * description; the clock is the machine's core clock, and a change of it
 * yields to the runtime. */
#include <string.h>

#include "mcu/pic24/pic24.h"

struct osc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *osccon;
    int key_lo, key_hi; /* unlock sequence steps seen */
    int warned;
};

/* a configuration word: its 24 bits, or all ones (erased) */
static u32 config(struct esk_mach *s, const char *name, u32 dflt)
{
    return esk_pic24_prog(s, esk_rf_config(s->rf, name, dflt));
}

static u32 core_hz(struct esk_mach *m)
{
    return esk_pic24_fcy(m);
}

static u32 osc_read(void *ctx, u32 off, int n)
{
    struct osc *o = (struct osc *)ctx;
    return esk_rf_read(o->p, off, n);
}

static void locked(struct osc *o, const char *half)
{
    struct esk_mach *s = o->s;
    if (o->warned++ || s->bus.debug)
        return;
    esk_rf_diag(s, ES_SEV_WARNING, "mcu.locked_write",
                "a write at pc 0x%06lx to OSCCON's %s byte is ignored: it needs its unlock "
                "sequence first",
                (unsigned long)s->cpu->ops->pc(s->cpu), half);
}

static void osc_write(void *ctx, u32 off, int n, u32 v)
{
    struct osc *o = (struct osc *)ctx;
    u32 at = esk_rf_off(o->osccon), old, nv, before = esk_pic24_fcy(o->s);
    int b;
    if (off != at && off != at + 1) {
        esk_rf_write(o->p, off, n, v);
        if (esk_pic24_fcy(o->s) != before)
            o->s->yield = 1;
        return;
    }
    if (o->s->bus.debug) {
        esk_rf_write(o->p, off, n, v);
        return;
    }
    old = (u32)o->osccon->value;
    nv = old;
    /* each byte the write touches: the unlock sequence's step, or the
     * value it lets through */
    for (b = 0; b < n; b++) {
        int hi = off + (u32)b == at + 1;
        u32 x = v >> (8 * b) & 0xffu;
        int *key = hi ? &o->key_hi : &o->key_lo;
        u32 k1 = hi ? 0x78u : 0x46u, k2 = hi ? 0x9au : 0x57u;
        if (*key == 0 && x == k1) {
            *key = 1;
            continue;
        }
        if (*key == 1 && x == k2) {
            *key = 2;
            continue;
        }
        if (*key != 2) {
            *key = 0;
            locked(o, hi ? "high" : "low");
            continue;
        }
        *key = 0;
        if (hi) {
            nv = (nv & 0x00ffu) | (x & 0x07u) << 8 | (nv & 0x7000u); /* NOSC */
        } else {
            u32 keep = nv & 0x20u; /* LOCK: the hardware's */
            if ((nv & 0x40u) && (config(o->s, "FOSC", 0xabf1cu) >> 4 & 1u))
                x |= 0x40u; /* IOL1WAY: IOLOCK stays */
            nv = (nv & 0xff00u) | (x & ~0x20u) | keep;
        }
    }
    if (nv & 1u) { /* OSWEN: switch now */
        u32 nosc = nv >> 8 & 7u;
        nv = (nv & ~0x7001u) | nosc << 12;
        nv = (nosc == 1 || nosc == 3) ? nv | 0x20u : nv & ~0x20u;
    }
    if (nv != old)
        esk_rf_hw(o->osccon, nv);
    if (esk_pic24_fcy(o->s) != before)
        o->s->yield = 1; /* the runtime sees the new clock now */
}

static void osc_reset(void *ctx)
{
    struct osc *o = (struct osc *)ctx;
    u32 fnosc = config(o->s, "FOSCSEL", 0xabf18u) & 7u;
    u32 v = (u32)(o->osccon->value & ~(u64)0x7700) | fnosc << 12 | fnosc << 8;
    v = (fnosc == 1 || fnosc == 3) ? v | 0x20u : v & ~0x20u;
    esk_rf_hw(o->osccon, v);
    o->key_lo = o->key_hi = 0;
    o->warned = 0;
}

const struct esk_dev_ops esk_pic24_osc_ops = {
    "pic24-osc", osc_read, osc_write, osc_reset, 0, 0,
};

void *esk_pic24_osc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct osc *o;
    struct esk_rf_reg *r = esk_rf_reg(p, "OSCCON");
    if (!r || !esk_pic24_is(s->cpu))
        return 0;
    o = (struct osc *)esk_mach_alloc(s, sizeof *o);
    if (!o)
        return 0;
    memset(o, 0, sizeof *o);
    o->s = s;
    o->p = p;
    o->osccon = r;
    s->clock_ctx = o;
    s->core_hz = core_hz;
    return o;
}

/* ---- the PPS registers, locked by IOLOCK ----------------------------------- */

struct pps {
    struct esk_mach *s;
    struct esk_rf_periph *p;
};

static u32 pps_read(void *ctx, u32 off, int n)
{
    struct pps *q = (struct pps *)ctx;
    return esk_rf_read(q->p, off, n);
}

static void pps_write(void *ctx, u32 off, int n, u32 v)
{
    struct pps *q = (struct pps *)ctx;
    struct esk_rf_periph *osc = esk_rf_periph(q->s->rf, "OSC");
    struct esk_rf_reg *con = osc ? esk_rf_reg(osc, "OSCCON") : 0;
    if (con && (con->value & 0x40u) && !q->s->bus.debug)
        return; /* IOLOCK */
    esk_rf_write(q->p, off, n, v);
    esk_pic24_pps_changed(q->s); /* a pin's output, or an input's pin */
}

const struct esk_dev_ops esk_pic24_pps_ops = {
    "pic24-pps", pps_read, pps_write, 0, 0, 0,
};

void *esk_pic24_pps_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct pps *q;
    if (!esk_pic24_is(s->cpu))
        return 0;
    q = (struct pps *)esk_mach_alloc(s, sizeof *q);
    if (!q)
        return 0;
    q->s = s;
    q->p = p;
    return q;
}
