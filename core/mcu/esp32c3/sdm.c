/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* sdm.c -- the ESP32-C3's sigma-delta modulation (GPIO_SD at 0x60004F00,
 * TRM v1.4 section 5.13): four channels of pulse-density modulation,
 * gpio_sd0_out to gpio_sd3_out (signals 55-58 of the GPIO matrix), which
 * ESP-IDF's sdm driver and its sdm_led and sdm_dac examples use.
 *
 *   - A channel's bit clock is APB_CLK divided by SDn_PRESCALE + 1; each
 *     bit it outputs one or zero, so that (SDn_IN + 128) / 256 of its bits
 *     are ones: SDn_IN is a signed duty, -128 for always low to 127 for 255
 *     of every 256 bits high. A new SDn_IN or SDn_PRESCALE takes effect at
 *     the next bit.
 *   - SIGMADELTA_MISC's FUNCTION_CLK_EN clocks the four channels; without
 *     it they hold their outputs. Out of reset it is off, SDn_IN is 0 and
 *     SDn_PRESCALE 255, and the outputs are low.
 * Where it differs inside: the TRM gives the density, not the bit pattern;
 * the pattern here is a first-order modulator's (an 8-bit accumulator adds
 * SDn_IN + 128 each bit and outputs its carry), the evenest spread of the
 * ones. Not yet: SIGMADELTA_CG's register clock gate (the registers are
 * always writable), SPI_SWAP, light-sleep. */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define NCH 4
#define SIG_OUT0 55
#define FS_PER_S 1000000000000000ull
#define MISC_CLK_EN (1u << 30)

struct sdm_ch {
    struct esk_rf_reg *conf;
    u32 acc;  /* the accumulator */
    u64 pos;  /* fs into the current bit */
    int out;
};

struct c3_sdm {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *misc;
    struct sdm_ch c[NCH];
    int clocked;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static u32 density(const struct sdm_ch *c) { return ((reg(c->conf) & 0xffu) + 128u) & 0xffu; }

/* a bit's length in fs, or 0 while the channels are not clocked */
static u64 bit_fs(const struct c3_sdm *k, const struct sdm_ch *c)
{
    u32 hz = esk_esp32c3_apb_hz(k->s);
    if (!(reg(k->misc) & MISC_CLK_EN) || !hz)
        return 0;
    return (u64)(((reg(c->conf) >> 8) & 0xffu) + 1u) * (FS_PER_S / hz);
}

static u64 fs_per_cycle(const struct c3_sdm *k)
{
    u32 hz = esk_mach_core_hz(k->s);
    return hz ? FS_PER_S / hz : 0;
}

static int step(struct sdm_ch *c)
{
    c->acc += density(c);
    if (c->acc >= 256u) {
        c->acc -= 256u;
        return 1;
    }
    return 0;
}

/* the bits until the output changes (the last of them changes it), or 0
 * when it never does */
static u32 bits_to_change(const struct sdm_ch *c)
{
    struct sdm_ch t = *c;
    u32 n;
    for (n = 1; n <= 512u; n++)
        if (step(&t) != c->out)
            return n;
    return 0;
}

static u64 due(const struct c3_sdm *k, const struct sdm_ch *c)
{
    u64 b = bit_fs(k, c);
    u32 n;
    if (!b || !(n = bits_to_change(c)))
        return 0;
    return (u64)n * b - c->pos;
}

static void drive(struct c3_sdm *k, int i, int out)
{
    if (out != k->c[i].out) {
        k->c[i].out = out;
        esk_esp32c3_signal(k->s, SIG_OUT0 + i, out, 1);
    }
}

/* whole bits elapsed that change nothing */
static void settle(struct c3_sdm *k, struct sdm_ch *c)
{
    u64 b = bit_fs(k, c);
    if (!b)
        return;
    while (c->pos >= b) {
        (void)step(c);
        c->pos -= b;
        if (!bits_to_change(c)) { /* constant from here: the phase only */
            c->pos %= b;
            break;
        }
    }
}

static void advance(struct c3_sdm *k, u64 budget)
{
    struct esk_mach *s = k->s;
    u64 fpc = fs_per_cycle(k);
    int i;
    if (!fpc)
        return;
    for (;;) {
        u64 d = 0;
        int which = -1;
        for (i = 0; i < NCH; i++) {
            u64 di = due(k, &k->c[i]);
            if (di && (which < 0 || di < d)) {
                d = di;
                which = i;
            }
        }
        if (which < 0 || d > budget) {
            for (i = 0; i < NCH; i++) {
                if (!bit_fs(k, &k->c[i]))
                    continue;
                k->c[i].pos += budget;
                settle(k, &k->c[i]);
            }
            return;
        }
        budget -= d;
        s->output_lag = (u32)(budget / fpc);
        for (i = 0; i < NCH; i++) {
            struct sdm_ch *c = &k->c[i];
            u64 b = bit_fs(k, c), di;
            if (!b)
                continue;
            di = due(k, c);
            if (di == d) { /* its change, now: the bits up to it */
                u32 n = bits_to_change(c), j;
                int out = c->out;
                for (j = 0; j < n; j++)
                    out = step(c);
                c->pos = 0;
                drive(k, i, out);
            } else {
                c->pos += d;
                settle(k, c);
            }
        }
        s->output_lag = 0;
    }
}

static void sdm_tick(void *ctx, u32 cycles)
{
    struct c3_sdm *k = (struct c3_sdm *)ctx;
    advance(k, (u64)cycles * fs_per_cycle(k));
}

static int sdm_next(void *ctx, u32 *cycles)
{
    struct c3_sdm *k = (struct c3_sdm *)ctx;
    u64 fpc = fs_per_cycle(k), best = 0;
    int i, any = 0;
    if (!fpc)
        return 0;
    for (i = 0; i < NCH; i++) {
        u64 d = due(k, &k->c[i]), c;
        if (!d)
            continue;
        c = (d + fpc - 1u) / fpc;
        if (!any || c < best)
            best = c;
        any = 1;
    }
    if (!any)
        return 0;
    *cycles = best == 0 ? 1u : best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static u32 sdm_read(void *ctx, u32 off, int n)
{
    struct c3_sdm *k = (struct c3_sdm *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void clocking(struct c3_sdm *k)
{
    esk_rf_clock(k->p, &k->clocked, (reg(k->misc) & MISC_CLK_EN) != 0);
}

static void sdm_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_sdm *k = (struct c3_sdm *)ctx;
    int i;
    u32 was = reg(k->misc) & MISC_CLK_EN;
    esk_rf_write(k->p, off, n, v);
    if (!was && (reg(k->misc) & MISC_CLK_EN))
        for (i = 0; i < NCH; i++)
            k->c[i].pos = 0; /* the bit clock starts */
    for (i = 0; i < NCH; i++) /* a new prescale: its bit from the current phase */
        if (bit_fs(k, &k->c[i]) && k->c[i].pos >= bit_fs(k, &k->c[i]))
            k->c[i].pos = bit_fs(k, &k->c[i]) - 1u;
    clocking(k);
}

static void sdm_reset(void *ctx)
{
    struct c3_sdm *k = (struct c3_sdm *)ctx;
    int i;
    for (i = 0; i < NCH; i++) {
        k->c[i].acc = 0;
        k->c[i].pos = 0;
        k->c[i].out = -1;
        drive(k, i, 0);
    }
    k->clocked = 0;
    clocking(k);
}

const struct esk_dev_ops esk_esp32c3_sdm_ops = {
    "esp32c3-sdm", sdm_read, sdm_write, sdm_reset, sdm_tick, sdm_next,
};

void *esk_esp32c3_sdm_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_sdm *k = (struct c3_sdm *)esk_mach_alloc(s, sizeof *k);
    char name[24];
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->misc = esk_rf_reg(p, "SIGMADELTA_MISC");
    if (!k->misc)
        return 0;
    for (i = 0; i < NCH; i++) {
        snprintf(name, sizeof name, "SIGMADELTA%d", i);
        k->c[i].conf = esk_rf_reg(p, name);
        if (!k->c[i].conf)
            return 0;
    }
    return k;
}
