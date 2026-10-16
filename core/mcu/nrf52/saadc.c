/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* saadc.c -- the nRF52840's SAADC (Product Specification 6.23), over the
 * register file: eight channels on the analog inputs, results to Data RAM
 * by EasyDMA.
 *
 *   - START (STARTED) takes RESULT.PTR and RESULT.MAXCNT; SAMPLE converts
 *     each channel whose PSELP is connected, in order, each for TACQ and 2
 *     us (STATUS busy meanwhile): DONE after each conversion, and the
 *     result to the buffer with RESULTDONE; RESULT.AMOUNT counts them and
 *     END comes when MAXCNT are in (a START is needed for the next
 *     buffer); SAMPLERATE's local timer (MODE 1) starts the next scan CC
 *     cycles of 16 MHz after one ends, until END; STOP (STOPPED) ends a
 *     buffer;
 *   - the inputs: AIN0-7 (P0.02-P0.05, P0.28-P0.31) at their voltages in
 *     the circuit, VDD and VDDH / 5 at the supply's (VDDH tied to VDD);
 *     PSELN too with CONFIG.MODE differential;
 *   - the result is (V(P) - V(N)) * GAIN / REFERENCE * 2^(RESOLUTION - m)
 *     (m 1 differential, else 0), floored and limited to the resolution:
 *     the reference 0.6 V, or VDD / 4 with REFSEL; the gains 1/6 to 4;
 *     OVERSAMPLE averages 2^n samples, all at one SAMPLE with BURST, else
 *     one per SAMPLE;
 *   - CH[n].LIMIT: LIMITH while a result is above HIGH, LIMITL below LOW;
 *   - CALIBRATEOFFSET gives CALIBRATEDONE 20 us later (offset errors are
 *     not modelled, so there is none to calibrate);
 *   - a conversion's end is a device event, at the instruction that
 *     reaches it.
 *
 * Not modelled: the inputs' resistor ladders (RESP, RESN) and the sample
 * and hold's load on the circuit, noise, offset and gain errors, the
 * buffer's EasyDMA list. (The undocumented registers nrfx's workaround for
 * anomaly 212 saves and restores are reserved ranges of the register file:
 * they read 0.) */
#include <string.h>

#include "mcu/nrf52/nrf52.h"

#define NCH 8

struct nsaadc {
    struct nrfp x;
    int running;
    u64 left, since;
    int armed;          /* a buffer is taken */
    u32 ptr, max, amount;
    int busy;           /* converting channel `ch` */
    int ch;
    int timer;          /* the local timer samples again */
    int calibrating;
    double sum[NCH];    /* oversampling's sums */
    u32 nsum[NCH];
};

static u32 rd(const struct nsaadc *a, u32 off) { return esk_nrfp_rd(&a->x, off); }

static int on(const struct nsaadc *a) { return (rd(a, 0x500) & 1u) != 0; }

static u32 cfg(const struct nsaadc *a, int ch) { return rd(a, 0x518u + 0x10u * (u32)ch); }

static int connected(const struct nsaadc *a, int ch) { return (rd(a, 0x510u + 0x10u * (u32)ch) & 31u) != 0; }

static double input(struct nsaadc *a, u32 sel)
{
    static const int ain[8] = { 2, 3, 4, 5, 28, 29, 30, 31 };
    struct esk_mach *s = a->x.s;
    double v = 0.0;
    sel &= 31u;
    if (sel >= 1 && sel <= 8) {
        int pin = ain[sel - 1];
        if (s->host.pin_volts && s->host.pin_volts(s->host.ctx, pin / 16, pin % 16, &v))
            return v;
        return 0.0;
    }
    if (sel == 9)
        return s->vdd;
    if (sel == 0x0d)
        return s->vdd / 5.0;
    return 0.0;
}

static int resolution(const struct nsaadc *a)
{
    static const int bits[4] = { 8, 10, 12, 14 };
    return bits[rd(a, 0x5f0) & 3u];
}

static double sample(struct nsaadc *a, int ch)
{
    static const double gains[8] = { 1.0 / 6, 1.0 / 5, 1.0 / 4, 1.0 / 3, 1.0 / 2, 1.0, 2.0, 4.0 };
    u32 c = cfg(a, ch);
    int diff = (c >> 20 & 1u) != 0;
    double vp = input(a, rd(a, 0x510u + 0x10u * (u32)ch)), vn = diff ? input(a, rd(a, 0x514u + 0x10u * (u32)ch)) : 0.0;
    double ref = (c >> 12 & 1u) ? a->x.s->vdd / 4.0 : 0.6;
    int m = diff ? 1 : 0;
    return (vp - vn) * gains[c >> 8 & 7u] / ref * (double)(1u << (resolution(a) - m));
}

/* the code: floored, within the resolution's range */
static int code_of(const struct nsaadc *a, int ch, double x)
{
    int m = (cfg(a, ch) >> 20 & 1u) ? 1 : 0;
    long hi = (1L << (resolution(a) - m)) - 1, lo = -(1L << (resolution(a) - m)), f;
    if (x >= (double)hi)
        return (int)hi;
    if (x <= (double)lo)
        return (int)lo;
    f = (long)x;
    if ((double)f > x)
        f--;
    return (int)f;
}

/* core cycles a conversion of channel ch takes */
static u64 conv_cycles(const struct nsaadc *a, int ch)
{
    static const u32 tacq_us[8] = { 3, 5, 10, 15, 20, 40, 40, 40 };
    return (u64)(tacq_us[cfg(a, ch) >> 16 & 7u] + 2u) * (NRF_CPU_HZ / 1000000u);
}

static int first_ch(const struct nsaadc *a, int from)
{
    int ch;
    for (ch = from; ch < NCH; ch++)
        if (connected(a, ch))
            return ch;
    return -1;
}

static void wait_n(struct nsaadc *a, u64 n)
{
    a->left = n ? n : 1;
    a->since = a->x.s->cycles;
    esk_rf_clock(a->x.p, &a->running, 1);
}

static void status(struct nsaadc *a) { esk_nrfp_hw(&a->x, 0x400, a->busy ? 1u : 0u); }

static void begin_scan(struct nsaadc *a)
{
    int ch = first_ch(a, 0);
    if (ch < 0 || !a->armed)
        return;
    a->busy = 1;
    a->ch = ch;
    status(a);
    wait_n(a, conv_cycles(a, ch));
}

static void limits(struct nsaadc *a, int ch, int v)
{
    u32 lim = rd(a, 0x51cu + 0x10u * (u32)ch);
    int lo = (int)(lim & 0xffffu), hi = (int)(lim >> 16);
    if (lo >= 0x8000)
        lo -= 0x10000;
    if (hi >= 0x8000)
        hi -= 0x10000;
    if (v > hi)
        esk_nrfp_event(&a->x, 0x118u + 8u * (u32)ch); /* LIMITH */
    if (v < lo)
        esk_nrfp_event(&a->x, 0x11cu + 8u * (u32)ch); /* LIMITL */
}

/* a conversion of a->ch ended */
static void converted(struct nsaadc *a)
{
    u32 os = rd(a, 0x5f4) & 15u, n = os ? 1u << (os > 8 ? 8 : os) : 1u;
    int burst = (cfg(a, a->ch) >> 24 & 1u) != 0, ch = a->ch, next;
    a->sum[ch] += sample(a, ch);
    a->nsum[ch]++;
    esk_nrfp_event(&a->x, 0x108); /* DONE */
    if (a->nsum[ch] < n && burst) {
        wait_n(a, conv_cycles(a, ch)); /* the burst goes on */
        return;
    }
    if (a->nsum[ch] >= n) {
        int v = code_of(a, ch, a->sum[ch] / (double)a->nsum[ch]);
        a->sum[ch] = 0.0;
        a->nsum[ch] = 0;
        if (a->armed && a->amount < a->max) {
            (void)esk_bus_debug_write(&a->x.s->bus, a->ptr + 2u * a->amount, 2, (u32)v & 0xffffu);
            a->amount++;
            esk_nrfp_hw(&a->x, 0x634, a->amount);
            esk_nrfp_event(&a->x, 0x10c); /* RESULTDONE */
        }
        limits(a, ch, v);
        if (a->armed && a->amount >= a->max) {
            a->armed = 0;
            a->busy = 0;
            a->timer = 0;
            status(a);
            esk_rf_clock(a->x.p, &a->running, 0);
            esk_nrfp_event(&a->x, 0x104); /* END */
            return;
        }
    }
    next = first_ch(a, ch + 1);
    if (next >= 0) {
        a->ch = next;
        wait_n(a, conv_cycles(a, next));
        return;
    }
    a->busy = 0;
    status(a);
    if (a->timer) {
        /* the local timer: the next scan CC cycles of 16 MHz after this one began */
        u32 cc = rd(a, 0x5f8) & 0x7ffu;
        wait_n(a, (u64)(cc ? cc : 80u) * 4u);
        a->ch = -1;
        return;
    }
    esk_rf_clock(a->x.p, &a->running, 0);
}

static void saadc_tick(void *ctx, u32 cycles)
{
    struct nsaadc *a = (struct nsaadc *)ctx;
    int was;
    if (!a->running)
        return;
    cycles = esk_mach_after(a->x.s, cycles, a->since);
    a->since = 0;
    if (a->left > cycles) {
        a->left -= cycles;
        return;
    }
    a->left = 0;
    was = esk_nrfp_line(&a->x);
    if (a->calibrating) {
        a->calibrating = 0;
        if (!a->busy && !a->timer)
            esk_rf_clock(a->x.p, &a->running, 0);
        esk_nrfp_event(&a->x, 0x110); /* CALIBRATEDONE */
    } else if (a->busy) {
        converted(a);
    } else if (a->timer) {
        begin_scan(a);
    } else {
        esk_rf_clock(a->x.p, &a->running, 0);
    }
    esk_nrfp_irq(&a->x, was, 0);
}

static int saadc_next(void *ctx, u32 *cycles)
{
    struct nsaadc *a = (struct nsaadc *)ctx;
    if (!a->running)
        return 0;
    *cycles = a->left > 0xffffffffu ? 0xffffffffu : (u32)(a->left ? a->left : 1);
    return 1;
}

static u32 saadc_read(void *ctx, u32 off, int n)
{
    struct nsaadc *a = (struct nsaadc *)ctx;
    esk_nrfp_inten_read(&a->x, off);
    return esk_rf_read(a->x.p, off, n);
}

static void saadc_write(void *ctx, u32 off, int n, u32 v)
{
    struct nsaadc *a = (struct nsaadc *)ctx;
    int was = esk_nrfp_line(&a->x);
    esk_rf_write(a->x.p, off, n, v);
    esk_nrfp_inten_write(&a->x, off, v);
    if (off < 0x100 && (v & 1) && on(a)) {
        switch (off) {
        case 0x000: /* START */
            a->ptr = rd(a, 0x62c);
            a->max = rd(a, 0x630) & 0x7fffu;
            a->amount = 0;
            esk_nrfp_hw(&a->x, 0x634, 0);
            if (a->max && (a->ptr < NRF_DATA_RAM || a->ptr - NRF_DATA_RAM >= NRF_DATA_RAM_SIZE)) {
                esk_rf_diag(a->x.s, ES_SEV_WARNING, "mcu.easydma_range",
                            "SAADC's RESULT.PTR 0x%08lx is not in Data RAM: EasyDMA reaches only that; nothing is stored",
                            (unsigned long)a->ptr);
                a->max = 0;
            }
            a->armed = 1;
            esk_nrfp_event(&a->x, 0x100); /* STARTED */
            break;
        case 0x004: /* SAMPLE */
            if (!a->busy && a->armed) {
                a->timer = (rd(a, 0x5f8) >> 12 & 1u) != 0;
                begin_scan(a);
            }
            break;
        case 0x008: /* STOP */
            a->armed = 0;
            a->busy = 0;
            a->timer = 0;
            a->calibrating = 0;
            status(a);
            esk_rf_clock(a->x.p, &a->running, 0);
            esk_nrfp_event(&a->x, 0x114); /* STOPPED */
            break;
        case 0x00c: /* CALIBRATEOFFSET */
            a->calibrating = 1;
            wait_n(a, 20u * (NRF_CPU_HZ / 1000000u));
            break;
        default:
            break;
        }
    }
    if (off == 0x500 && !on(a)) {
        a->armed = a->busy = a->timer = a->calibrating = 0;
        status(a);
        esk_rf_clock(a->x.p, &a->running, 0);
    }
    esk_nrfp_irq(&a->x, was, 1);
}

static void saadc_reset(void *ctx)
{
    struct nsaadc *a = (struct nsaadc *)ctx;
    int ch;
    a->running = 0;
    a->left = 0;
    a->armed = a->busy = a->timer = a->calibrating = 0;
    for (ch = 0; ch < NCH; ch++) {
        a->sum[ch] = 0.0;
        a->nsum[ch] = 0;
    }
    a->x.inten = 0;
}

const struct esk_dev_ops esk_nrf_saadc_ops = {
    "nrf-saadc", saadc_read, saadc_write, saadc_reset, saadc_tick, saadc_next,
};

void *esk_nrf_saadc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct nsaadc *a;
    if (strcmp(p->name, "SAADC"))
        return 0;
    a = (struct nsaadc *)esk_mach_alloc(s, sizeof *a);
    if (!a)
        return 0;
    memset(a, 0, sizeof *a);
    if (esk_nrfp_init(&a->x, s, p))
        return 0;
    return a;
}
