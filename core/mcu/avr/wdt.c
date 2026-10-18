/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* wdt.c -- the ATmega328P's watchdog timer (datasheet 10.9) and its reset
 * flags (MCUSR, 11.9.1), over the register file.
 *
 *   - it counts the 128 kHz watchdog oscillator: a time-out after
 *     2048 << WDP of its cycles (WDP3:0 from 0, 16 ms, to 9, 8 s; 10 to 15
 *     are reserved and taken as 9), counted in core cycles at the clock in
 *     use; WDR starts the count again;
 *   - it runs while WDE, WDIE or MCUSR's WDRF is set. A time-out in
 *     interrupt mode (WDIE) sets WDIF and requests the WDT vector; in
 *     system reset mode (WDE, or WDRF which overrides it) it resets the
 *     part; with both, the first time-out sets WDIF and entering the vector
 *     clears WDIE and WDIF, so the next one resets unless the program sets
 *     WDIE again, and a time-out with WDIF still set resets;
 *   - safety level 1 (WDTON unprogrammed, the Uno's fuses): WDE can be set
 *     at any time; clearing it or changing WDP takes the timed sequence,
 *     WDCE and WDE written 1 together and then, within four cycles, the
 *     new WDE and WDP with WDCE 0. WDE stays set while WDRF is;
 *   - MCUSR records the reset's cause: a power-on reset leaves PORF alone,
 *     a reset from the RESET pin sets EXTRF and keeps the other flags;
 *   - a watchdog reset keeps MCUSR's flags and sets WDRF in it, and the
 *     watchdog comes out of it running with WDE set and WDP 0 (16 ms): a
 *     program that does not clear WDRF and WDE early resets again.
 *
 * Not modelled: the oscillator's spread with supply and temperature (it
 * runs at exactly 128 kHz), safety level 2 (WDTON programmed). */
#include <string.h>

#include "mcu/avr/avr.h"

#define WDP_LO 0x07u
#define WDE 0x08u
#define WDCE 0x10u
#define WDP3 0x20u
#define WDIE 0x40u
#define WDIF 0x80u
#define WDRF 0x08u /* MCUSR */
#define EXTRF 0x02u
#define WD_HZ 128000u

struct avr_wdt {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *csr, *mcusr;
    int vec;
    int running;
    int wd_reset; /* the coming reset is this watchdog's */
    u8 kept;      /* MCUSR as the watchdog reset the part */
    u8 pin_kept;  /* MCUSR as the reset pin went low */
    u64 window;   /* the cycle the timed sequence's window closes, 0 when shut */
    u64 count;    /* core cycles since the count started */
    u64 since;    /* the cycle it started */
};

static u8 csr(const struct avr_wdt *w) { return (u8)w->csr->value; }

static int wdrf(const struct avr_wdt *w) { return w->mcusr && (w->mcusr->value & WDRF); }

/* the time-out in core cycles */
static u64 limit(const struct avr_wdt *w)
{
    u32 wdp = (csr(w) & WDP_LO) | (csr(w) & WDP3) >> 2;
    u64 n = (u64)2048u << (wdp > 9 ? 9 : wdp);
    u64 c = n * esk_mach_core_hz(w->s) / WD_HZ;
    return c ? c : 1;
}

static void restart(struct avr_wdt *w)
{
    w->count = 0;
    w->since = w->s->cycles;
}

/* runs while it has something to do: a change starts the count */
static void update(struct avr_wdt *w)
{
    int on = (csr(w) & (WDE | WDIE)) || wdrf(w);
    if (on && !w->running)
        restart(w);
    esk_rf_clock(w->p, &w->running, on);
}

static void wdt_wdr(void *ctx) { restart((struct avr_wdt *)ctx); }

static u32 wdt_read(void *ctx, u32 off, int n)
{
    struct avr_wdt *w = (struct avr_wdt *)ctx;
    if (off == esk_rf_off(w->csr) && w->window && w->s->cycles > w->window) {
        w->window = 0;
        esk_rf_hw(w->csr, csr(w) & ~WDCE);
    }
    return esk_rf_read(w->p, off, n);
}

static void wdt_write(void *ctx, u32 off, int n, u32 v)
{
    struct avr_wdt *w = (struct avr_wdt *)ctx;
    struct esk_mach *m = w->s;
    u8 now = csr(w), next;
    if (off != esk_rf_off(w->csr)) {
        esk_rf_write(w->p, off, n, v);
        return;
    }
    /* WDIE writes through, WDIF clears by a 1 */
    next = (u8)((now & ~(WDIE | WDCE)) | (v & WDIE));
    if (v & WDIF)
        next = (u8)(next & ~WDIF);
    if ((v & WDCE) && (v & WDE)) {
        /* the timed sequence opens; WDE written 1 sets it as always */
        w->window = m->cycles + 4;
        next |= WDCE | WDE;
    } else if (w->window && m->cycles <= w->window) {
        w->window = 0;
        next = (u8)((next & ~(WDE | WDP3 | WDP_LO)) | (v & (WDE | WDP3 | WDP_LO)));
    } else {
        w->window = 0;
        next |= (u8)(v & WDE);
    }
    if (wdrf(w))
        next |= WDE;
    esk_rf_hw(w->csr, next);
    update(w);
    esk_avr_irq_changed(m);
}

static void time_out(struct avr_wdt *w)
{
    struct esk_mach *s = w->s;
    u8 c = csr(w);
    int reset_mode = (c & WDE) || wdrf(w);
    if ((c & WDIE) && !(c & WDIF)) {
        esk_rf_hw(w->csr, c | WDIF);
        esk_avr_irq_changed(s);
        return;
    }
    if (!reset_mode || s->reset_req)
        return;
    esk_rf_diag(s, ES_SEV_INFO, "mcu.watchdog_reset", "%s",
                "the watchdog timed out in system reset mode: it resets the part");
    w->kept = w->mcusr ? (u8)w->mcusr->value : 0;
    w->wd_reset = 1;
    s->reset_req = 1;
    s->yield = 1;
}

static void wdt_tick(void *ctx, u32 cycles)
{
    struct avr_wdt *w = (struct avr_wdt *)ctx;
    u64 lim;
    if (!w->running)
        return;
    w->count += esk_mach_after(w->s, cycles, w->since);
    lim = limit(w);
    while (w->running && w->count >= lim && !w->s->reset_req) {
        w->count -= lim;
        time_out(w);
    }
}

static int wdt_next(void *ctx, u32 *cycles)
{
    struct avr_wdt *w = (struct avr_wdt *)ctx;
    u64 lim, left;
    if (!w->running)
        return 0;
    lim = limit(w);
    left = w->count >= lim ? 1 : lim - w->count;
    *cycles = left > 0xffffffffu ? 0xffffffffu : (u32)left;
    return 1;
}

static void wdt_reset(void *ctx)
{
    struct avr_wdt *w = (struct avr_wdt *)ctx;
    w->running = 0;
    w->window = 0;
    if (w->wd_reset) {
        w->wd_reset = 0;
        if (w->mcusr)
            esk_rf_hw(w->mcusr, w->kept | WDRF);
        esk_rf_hw(w->csr, WDE);
        w->count = 0;
        w->since = 0; /* the machine's cycles start again at 0 */
        esk_rf_clock(w->p, &w->running, 1);
    }
}

static int wdt_pending(void *ctx, int vec)
{
    struct avr_wdt *w = (struct avr_wdt *)ctx;
    ES_UNUSED(vec);
    return (csr(w) & (WDIE | WDIF)) == (WDIE | WDIF);
}

/* entering the vector clears WDIF and, in interrupt and system reset mode,
 * WDIE: the next time-out resets */
static void wdt_ack(void *ctx, int vec)
{
    struct avr_wdt *w = (struct avr_wdt *)ctx;
    u8 c = (u8)(csr(w) & ~WDIF);
    ES_UNUSED(vec);
    if ((c & WDE) || wdrf(w))
        c = (u8)(c & ~WDIE);
    esk_rf_hw(w->csr, c);
}

/* an external reset sets EXTRF and keeps MCUSR's other flags; only a
 * power-on reset clears them */
static void wdt_pin_reset(struct esk_mach *s, int after)
{
    struct avr_wdt *w = (struct avr_wdt *)s->pin_reset_ctx;
    if (!w->mcusr)
        return;
    if (!after)
        w->pin_kept = (u8)w->mcusr->value;
    else
        esk_rf_hw(w->mcusr, w->pin_kept | EXTRF);
}

const struct esk_dev_ops esk_avr_wdt_ops = {
    "avr-wdt", wdt_read, wdt_write, wdt_reset, wdt_tick, wdt_next,
};

void *esk_avr_wdt_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct avr_wdt w, *c;
    struct esk_rf_periph *cpu;
    memset(&w, 0, sizeof w);
    w.s = s;
    w.p = p;
    w.csr = esk_rf_reg(p, "WDTCSR");
    if (!w.csr)
        return 0;
    cpu = p->rf ? esk_rf_periph(p->rf, "CPU") : 0;
    w.mcusr = cpu ? esk_rf_reg(cpu, "MCUSR") : 0;
    w.vec = esk_rf_irq(p, "WDT");
    c = (struct avr_wdt *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = w;
    if (c->vec > 0)
        esk_avr_irq_source(s, c->vec, wdt_pending, wdt_ack, c);
    esk_avr_wdr_hook(s, wdt_wdr, c);
    s->pin_reset = wdt_pin_reset;
    s->pin_reset_ctx = c;
    return c;
}
