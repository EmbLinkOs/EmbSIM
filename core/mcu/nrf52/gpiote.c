/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* gpiote.c -- the nRF52840's GPIO tasks and events (Product Specification
 * 6.9), over the register file: eight channels and the PORT event.
 *
 *   - CONFIG[n]'s MODE: Event (1) watches the pin PSEL and PORT name, its
 *     input buffer connected whatever PIN_CNF says, and sets IN[n] on the
 *     edge POLARITY names (LoToHi, HiToLo, Toggle: either); Task (3) drives
 *     the pin, from OUTINIT when the mode is set, and OUT[n] does what
 *     POLARITY says (set, clear, toggle), SET[n] and CLR[n] set and clear
 *     it; Disabled (0) gives the pin back to the GPIO;
 *   - PORT is set when the ports' DETECT signal rises (a pin whose SENSE
 *     condition is met, gpio.c);
 *   - INTENSET and INTENCLR enable IN[n] (bit n) and PORT (bit 31), and an
 *     enabled event raises GPIOTE's interrupt. */
#include <string.h>

#include "mcu/nrf52/nrf52.h"

#define NCH 8

struct gpiote {
    struct nrfp x;
    u32 last[2];  /* the ports' levels last seen */
    int last_detect;
    int known;
    signed char level[NCH]; /* a task-mode channel's level */
};

static struct gpiote *gpiote_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "GPIOTE") : 0;
    return p && p->ops == &esk_nrf_gpiote_ops ? (struct gpiote *)p->ctx : 0;
}

static u32 config(const struct gpiote *t, int n) { return esk_nrfp_rd(&t->x, 0x510u + 4u * (u32)n); }
static u32 mode(u32 c) { return c & 3u; }
static int pin_of(u32 c) { return (int)((c >> 13 & 1u) * 32u + (c >> 8 & 31u)); }
static u32 polarity(u32 c) { return c >> 16 & 3u; }

void esk_nrf_gpiote_roles(struct esk_mach *s, signed char *drive, u8 *watch)
{
    struct gpiote *t = gpiote_of(s);
    int n;
    memset(drive, -1, NRF_PINS);
    memset(watch, 0, NRF_PINS);
    for (n = 0; t && n < NCH; n++) {
        u32 c = config(t, n);
        int pin = pin_of(c);
        if (pin >= NRF_PINS)
            continue;
        if (mode(c) == 3)
            drive[pin] = t->level[n];
        else if (mode(c) == 1)
            watch[pin] = 1;
    }
}

void esk_nrf_gpiote_levels(struct esk_mach *s, const u32 *levels, int detect)
{
    struct gpiote *t = gpiote_of(s);
    int n;
    if (!t)
        return;
    if (t->known) {
        for (n = 0; n < NCH; n++) {
            u32 c = config(t, n);
            int pin = pin_of(c), now, was;
            if (mode(c) != 1 || pin >= NRF_PINS)
                continue;
            now = (int)(levels[pin / 32] >> (pin % 32) & 1u);
            was = (int)(t->last[pin / 32] >> (pin % 32) & 1u);
            if (now != was && (polarity(c) == 3 || (polarity(c) == 1 && now) || (polarity(c) == 2 && !now)))
                esk_nrfp_event(&t->x, 0x100u + 4u * (u32)n);
        }
        if (detect && !t->last_detect)
            esk_nrfp_event(&t->x, 0x17c); /* PORT */
    }
    t->last[0] = levels[0];
    t->last[1] = levels[1];
    t->last_detect = detect;
    t->known = 1;
}

static void task(struct gpiote *t, int n, int what)
{
    u32 c = config(t, n);
    if (mode(c) != 3)
        return;
    if (what == 0) /* OUT: as POLARITY says */
        what = polarity(c) == 1 ? 1 : polarity(c) == 2 ? 2 : polarity(c) == 3 ? 3 : 0;
    if (what == 1)
        t->level[n] = 1;
    else if (what == 2)
        t->level[n] = 0;
    else if (what == 3)
        t->level[n] = (signed char)!t->level[n];
}

static u32 te_read(void *ctx, u32 off, int n)
{
    struct gpiote *t = (struct gpiote *)ctx;
    esk_nrfp_inten_read(&t->x, off);
    return esk_rf_read(t->x.p, off, n);
}

static void te_write(void *ctx, u32 off, int n, u32 v)
{
    struct gpiote *t = (struct gpiote *)ctx;
    int was = esk_nrfp_line(&t->x), ch, pins = 0;
    u32 before = off >= 0x510 && off < 0x530 ? config(t, (int)(off - 0x510) / 4) : 0;
    esk_rf_write(t->x.p, off, n, v);
    esk_nrfp_inten_write(&t->x, off, v);
    if (off < 0x090 && (v & 1)) {
        ch = (int)(off % 0x30u) / 4;
        if (ch < NCH) {
            task(t, ch, off < 0x30 ? 0 : off < 0x60 ? 1 : 2);
            pins = 1;
        }
    }
    if (off >= 0x510 && off < 0x530) {
        ch = (int)(off - 0x510) / 4;
        if (mode(config(t, ch)) == 3 && mode(before) != 3)
            t->level[ch] = (signed char)(config(t, ch) >> 20 & 1u); /* OUTINIT */
        pins = 1;
    }
    if (pins)
        esk_nrf_pins_changed(t->x.s);
    esk_nrfp_irq(&t->x, was, 1);
}

static void te_reset(void *ctx)
{
    struct gpiote *t = (struct gpiote *)ctx;
    memset(t->level, 0, sizeof t->level);
    t->known = 0;
    t->last_detect = 0;
    t->x.inten = 0;
}

const struct esk_dev_ops esk_nrf_gpiote_ops = {
    "nrf-gpiote", te_read, te_write, te_reset, 0, 0,
};

void *esk_nrf_gpiote_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct gpiote *t = (struct gpiote *)esk_mach_alloc(s, sizeof *t);
    if (!t)
        return 0;
    memset(t, 0, sizeof *t);
    if (esk_nrfp_init(&t->x, s, p))
        return 0;
    return t;
}
