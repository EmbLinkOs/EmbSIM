/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* rtc.c -- the nRF52840's RTC0-2 (Product Specification 6.22), over the
 * register file, while the 32.768 kHz clock runs.
 *
 *   - the 24-bit COUNTER at 32768 Hz / (PRESCALER + 1), the prescaler
 *     latched by START, CLEAR and TRIGOVRFLW (which sets COUNTER to
 *     0xFFFFF0), PRESCALER read-only while it runs;
 *   - TICK at each count, OVRFLW from 0xFFFFFF to 0, COMPARE[n] when
 *     COUNTER becomes CC[n] -- each an event only when EVTEN or INTEN
 *     enables it. RTC0 has three CC registers, RTC1-2 four.
 * Not modelled: the "may not trigger" cases (writing N or N + 1 to CC when
 * COUNTER is N: here it triggers at the transition). */
#include <string.h>

#include "mcu/nrf52/nrf52.h"

struct nrtc {
    struct nrfp x;
    int ncc, on, running;
    u32 presc; /* the latched prescaler */
    u64 acc;   /* core cycles x 32768 toward a count */
    u64 since;
};

/* an RTC event, set only when EVTEN or INTEN has it */
static void rtc_event(struct nrtc *r, u32 off)
{
    u32 bit = 1u << ((off - 0x100u) / 4u);
    if ((esk_nrfp_rd(&r->x, 0x340) | r->x.inten) & bit)
        esk_nrfp_event(&r->x, off);
}

static void rtc_clock(struct nrtc *r)
{
    int was = r->running;
    esk_rf_clock(r->x.p, &r->running, r->on);
    if (r->running && !was)
        r->since = r->x.s->cycles;
}

static void rtc_count(struct nrtc *r)
{
    u32 c = (esk_nrfp_rd(&r->x, 0x504) + 1u) & 0xffffffu;
    int n;
    esk_nrfp_hw(&r->x, 0x504, c);
    rtc_event(r, 0x100); /* TICK */
    if (!c)
        rtc_event(r, 0x104); /* OVRFLW */
    for (n = 0; n < r->ncc; n++)
        if (c == (esk_nrfp_rd(&r->x, 0x540u + 4u * (u32)n) & 0xffffffu))
            rtc_event(r, 0x140u + 4u * (u32)n);
}

static void rtc_tick(void *ctx, u32 cycles)
{
    struct nrtc *r = (struct nrtc *)ctx;
    struct esk_mach *s = r->x.s;
    u64 one;
    if (!esk_nrf_lfclk_on(s))
        return;
    cycles = esk_mach_after(s, cycles, r->since);
    one = (u64)NRF_CPU_HZ * (r->presc + 1u);
    r->acc += (u64)cycles * 32768u;
    while (r->acc >= one) {
        r->acc -= one;
        rtc_count(r);
    }
}

static int rtc_next(void *ctx, u32 *cycles)
{
    struct nrtc *r = (struct nrtc *)ctx;
    u32 c, best = 0, en;
    u64 one, need, cy;
    int any = 0, n;
    if (!r->on || !esk_nrf_lfclk_on(r->x.s))
        return 0;
    c = esk_nrfp_rd(&r->x, 0x504);
    en = r->x.inten | esk_nrfp_rd(&r->x, 0x340);
    for (n = 0; n < 32; n++) {
        u32 d;
        if (!(en >> n & 1u))
            continue;
        if (n == 0)
            d = 1; /* TICK */
        else if (n == 1)
            d = (0x1000000u - c) & 0xffffffu; /* OVRFLW */
        else if (n >= 16 && n < 16 + r->ncc)
            d = (esk_nrfp_rd(&r->x, 0x540u + 4u * (u32)(n - 16)) - c) & 0xffffffu;
        else
            continue;
        if (!d)
            d = 0x1000000u;
        if (!any || d < best)
            best = d;
        any = 1;
    }
    if (!any)
        return 0;
    one = (u64)NRF_CPU_HZ * (r->presc + 1u);
    need = (u64)best * one - r->acc;
    cy = (need + 32767u) / 32768u;
    *cycles = cy > 0xffffffffu ? 0xffffffffu : (u32)cy;
    return 1;
}

static u32 rtc_read(void *ctx, u32 off, int n)
{
    struct nrtc *r = (struct nrtc *)ctx;
    esk_nrfp_inten_read(&r->x, off);
    if (off >= 0x340 && off <= 0x348) /* EVTEN, EVTENSET, EVTENCLR */
        esk_nrfp_hw(&r->x, off, esk_nrfp_rd(&r->x, 0x340));
    return esk_rf_read(r->x.p, off, n);
}

static void rtc_write(void *ctx, u32 off, int n, u32 v)
{
    struct nrtc *r = (struct nrtc *)ctx;
    int was = esk_nrfp_line(&r->x);
    u32 presc = esk_nrfp_rd(&r->x, 0x508), evten = esk_nrfp_rd(&r->x, 0x340);
    esk_rf_write(r->x.p, off, n, v);
    esk_nrfp_inten_write(&r->x, off, v);
    if (off == 0x508 && r->on)
        esk_nrfp_hw(&r->x, 0x508, presc); /* read-only while it runs */
    if (off == 0x344)
        esk_nrfp_hw(&r->x, 0x340, evten | v);
    if (off == 0x348)
        esk_nrfp_hw(&r->x, 0x340, evten & ~v);
    if (off < 0x100 && (v & 1)) {
        switch (off) {
        case 0x000: r->on = 1; break;                             /* START */
        case 0x004: r->on = 0; break;                             /* STOP */
        case 0x008: esk_nrfp_hw(&r->x, 0x504, 0); break;          /* CLEAR */
        case 0x00c: esk_nrfp_hw(&r->x, 0x504, 0xfffff0u); break;  /* TRIGOVRFLW */
        default: break;
        }
        if (off == 0x000 || off == 0x008 || off == 0x00c) {
            r->presc = esk_nrfp_rd(&r->x, 0x508) & 0xfffu;
            r->acc = 0;
        }
    }
    rtc_clock(r);
    esk_nrfp_irq(&r->x, was, 1);
}

static void rtc_reset(void *ctx)
{
    struct nrtc *r = (struct nrtc *)ctx;
    r->on = r->running = 0;
    r->presc = 0;
    r->acc = 0;
    r->x.inten = 0;
}

const struct esk_dev_ops esk_nrf_rtc_ops = {
    "nrf-rtc", rtc_read, rtc_write, rtc_reset, rtc_tick, rtc_next,
};

void *esk_nrf_rtc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct nrtc *r;
    if (strncmp(p->name, "RTC", 3))
        return 0;
    r = (struct nrtc *)esk_mach_alloc(s, sizeof *r);
    if (!r)
        return 0;
    memset(r, 0, sizeof *r);
    if (esk_nrfp_init(&r->x, s, p))
        return 0;
    r->ncc = !strcmp(p->name, "RTC0") ? 3 : 4;
    return r;
}
