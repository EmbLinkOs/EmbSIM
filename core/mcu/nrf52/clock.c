/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* clock.c -- the nRF52840's CLOCK (Product Specification 5.4), over the
 * register file, in the space it shares with POWER.
 *
 *   - HFCLKSTART starts the 64 MHz crystal (HFXO): HFCLKSTARTED and
 *     HFCLKSTAT (Xtal, running) once HFXODEBOUNCE x 16 us have passed (the
 *     crystal's power-up time is not counted); HFCLKSTOP stops it. The core
 *     runs at 64 MHz either way (from HFINT before);
 *   - LFCLKSTART starts the 32.768 kHz clock from the source LFCLKSRC names
 *     (copied to LFCLKSRCCOPY): LFCLKSTARTED and LFCLKSTAT at once;
 *     LFCLKSTOP stops it. The RTCs count on it.
 * POWER's registers keep what is written. */
#include <string.h>

#include "mcu/nrf52/nrf52.h"

struct nclock {
    struct nrfp x;
    u64 hf_at; /* the HFXO starting: ready at this cycle */
    int hf_starting, hf_on, lf_on, running;
};

static struct nclock *clock_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "CLOCK") : 0;
    return p && p->ops == &esk_nrf_clock_ops ? (struct nclock *)p->ctx : 0;
}

int esk_nrf_lfclk_on(struct esk_mach *s)
{
    struct nclock *c = clock_of(s);
    return c && c->lf_on;
}

static void clock_status(struct nclock *c)
{
    esk_nrfp_hw(&c->x, 0x40c, c->hf_on ? 0x10001u : 0);
    esk_nrfp_hw(&c->x, 0x418, c->lf_on ? 0x10000u | (esk_nrfp_rd(&c->x, 0x41c) & 3u) : 0);
}

static void clock_run(struct nclock *c)
{
    esk_rf_clock(c->x.p, &c->running, c->hf_starting);
}

static void clock_task(struct nclock *c, u32 off)
{
    switch (off) {
    case 0x000: /* HFCLKSTART */
        esk_nrfp_hw(&c->x, 0x408, 1);
        if (!c->hf_on && !c->hf_starting) {
            c->hf_starting = 1;
            c->hf_at = c->x.s->cycles + (u64)(esk_nrfp_rd(&c->x, 0x528) & 0xffu) * 16u * (NRF_CPU_HZ / 1000000u);
        }
        break;
    case 0x004: /* HFCLKSTOP */
        c->hf_on = c->hf_starting = 0;
        esk_nrfp_hw(&c->x, 0x408, 0);
        break;
    case 0x008: /* LFCLKSTART */
        esk_nrfp_hw(&c->x, 0x414, 1);
        esk_nrfp_hw(&c->x, 0x41c, esk_nrfp_rd(&c->x, 0x518) & 3u);
        c->lf_on = 1;
        clock_status(c);
        esk_nrfp_event(&c->x, 0x104);
        break;
    case 0x00c: /* LFCLKSTOP */
        c->lf_on = 0;
        esk_nrfp_hw(&c->x, 0x414, 0);
        break;
    default:
        break;
    }
    clock_status(c);
    clock_run(c);
}

static void clock_tick(void *ctx, u32 cycles)
{
    struct nclock *c = (struct nclock *)ctx;
    ES_UNUSED(cycles);
    if (c->hf_starting && c->x.s->cycles >= c->hf_at) {
        c->hf_starting = 0;
        c->hf_on = 1;
        clock_status(c);
        esk_nrfp_event(&c->x, 0x100); /* HFCLKSTARTED */
        clock_run(c);
    }
}

static int clock_next(void *ctx, u32 *cycles)
{
    struct nclock *c = (struct nclock *)ctx;
    u64 now = c->x.s->cycles;
    if (!c->hf_starting)
        return 0;
    *cycles = c->hf_at > now ? (u32)(c->hf_at - now) : 1u;
    return 1;
}

static u32 clock_read(void *ctx, u32 off, int n)
{
    struct nclock *c = (struct nclock *)ctx;
    esk_nrfp_inten_read(&c->x, off);
    return esk_rf_read(c->x.p, off, n);
}

static void clock_write(void *ctx, u32 off, int n, u32 v)
{
    struct nclock *c = (struct nclock *)ctx;
    int was = esk_nrfp_line(&c->x);
    esk_rf_write(c->x.p, off, n, v);
    esk_nrfp_inten_write(&c->x, off, v);
    if (off < 0x010 && (v & 1))
        clock_task(c, off);
    esk_nrfp_irq(&c->x, was, 1);
}

static void clock_reset(void *ctx)
{
    struct nclock *c = (struct nclock *)ctx;
    c->hf_starting = c->hf_on = c->lf_on = 0;
    c->running = 0;
    c->x.inten = 0;
    clock_status(c);
}

const struct esk_dev_ops esk_nrf_clock_ops = {
    "nrf-clock", clock_read, clock_write, clock_reset, clock_tick, clock_next,
};

void *esk_nrf_clock_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct nclock *c = (struct nclock *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    memset(c, 0, sizeof *c);
    if (esk_nrfp_init(&c->x, s, p))
        return 0;
    return c;
}
