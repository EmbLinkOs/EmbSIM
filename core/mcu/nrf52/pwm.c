/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* pwm.c -- the nRF52840's PWM0-3 (Product Specification 6.17), over the
 * register file: four channels each, played from sequences in Data RAM by
 * EasyDMA, on their PSEL.OUT pins.
 *
 *   - the counter counts the 16 MHz clock divided by 2^PRESCALER: up to
 *     COUNTERTOP and back to 0 (MODE Up), or up and down again (UpAndDown,
 *     a period twice as long); a channel's output is active while the
 *     counter is below its compare value, high when active with the
 *     value's bit 15 (FallingEdge) and low when active without it;
 *   - SEQSTART[n] (SEQSTARTED[n]) plays SEQ[n]'s CNT 16-bit values from
 *     SEQ[n].PTR, by DECODER.LOAD: one value for all four channels
 *     (Common), one for channels 0-1 and one for 2-3 (Grouped), one each
 *     (Individual), or three and COUNTERTOP (WaveForm); each step lasts
 *     REFRESH + 1 periods (DECODER.MODE RefreshCount) or until NEXTSTEP
 *     (NextStep); SEQEND[n] as its last step is loaded; ENDDELAY periods
 *     after it;
 *   - LOOP.CNT 0 plays the sequence once and holds its last step; a
 *     non-zero LOOP.CNT plays SEQ0 then SEQ1 that many times (a start at
 *     SEQ1 skips the first SEQ0), then LOOPSDONE; SHORTS' SEQEND0_STOP,
 *     SEQEND1_STOP, LOOPSDONE_SEQSTART0, LOOPSDONE_SEQSTART1 and
 *     LOOPSDONE_STOP; PWMPERIODEND every period;
 *   - STOP stops it at the end of the period (STOPPED); the pins keep the
 *     levels they had;
 *   - the pins are PSEL.OUT[n]'s, driven by the PWM from its first
 *     sequence while ENABLE is set; an edge comes at the instruction that
 *     brings the counter to it, and a sleeping core's devices wake at it.
 *
 * Not modelled: the outputs' idle level before the first sequence (the
 * pin is the GPIO's until then), the timing of a step's load before the
 * period it starts. */
#include <string.h>

#include "mcu/nrf52/nrf52.h"

struct npwm {
    struct nrfp x;
    int unit, running;
    int taken;           /* the pins are the PWM's */
    int playing;         /* the counter runs */
    int seq;             /* the sequence playing, -1 none */
    u32 step, steps;     /* the step loaded and the sequence's steps */
    u32 refresh_left;    /* periods the step still lasts */
    u32 delay_left;      /* ENDDELAY periods still to play */
    int at_end;          /* the last step is loaded */
    u32 loops;           /* loops played */
    int next_step;       /* NEXTSTEP asked for */
    int stop_req;
    u32 cnt;             /* the counter */
    int down;            /* UpAndDown, counting down */
    u32 top;
    u16 val[4];          /* each channel's value: bit 15 polarity, 14:0 compare */
    int out[4];
    u64 acc;             /* core cycles toward the next tick */
    u64 since;
};

static u32 rd(const struct npwm *w, u32 off) { return esk_nrfp_rd(&w->x, off); }

static int on(const struct npwm *w) { return (rd(w, 0x500) & 1u) != 0; }

static u32 tick_cycles(const struct npwm *w) { return 4u << (rd(w, 0x50c) & 7u); }

static int updown(const struct npwm *w) { return (rd(w, 0x504) & 1u) != 0; }

static struct npwm *unit_of(struct esk_mach *s, int unit)
{
    char name[8] = "PWMx";
    struct esk_rf_periph *p;
    name[3] = (char)('0' + unit);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_nrf_pwm_ops ? (struct npwm *)p->ctx : 0;
}

void esk_nrf_pwm_roles(struct esk_mach *s, signed char *drive)
{
    int u, c;
    for (u = 0; u < 4; u++) {
        struct npwm *w = unit_of(s, u);
        if (!w || !on(w) || !w->taken)
            continue;
        for (c = 0; c < 4; c++) {
            int n = esk_nrf_psel(rd(w, 0x560u + 4u * (u32)c));
            if (n >= 0 && n < NRF_PINS)
                drive[n] = (signed char)w->out[c];
        }
    }
}

static int levels(struct npwm *w)
{
    int c, changed = 0;
    for (c = 0; c < 4; c++) {
        u32 cmp = w->val[c] & 0x7fffu;
        int active = w->cnt < cmp, o = (w->val[c] & 0x8000u) ? active : !active;
        if (o != w->out[c])
            changed = 1;
        w->out[c] = o;
    }
    return changed;
}

static u16 mem16(struct npwm *w, u32 a)
{
    u32 v = 0;
    (void)esk_bus_debug_read(&w->x.s->bus, a, 2, &v);
    return (u16)v;
}

static u32 per_step(const struct npwm *w)
{
    u32 load = rd(w, 0x510) & 3u;
    return load == 0 ? 1u : load == 1 ? 2u : 4u;
}

static void load_step(struct npwm *w)
{
    u32 base = rd(w, 0x520u + 0x20u * (u32)w->seq), at = base + 2u * w->step * per_step(w);
    u32 load = rd(w, 0x510) & 3u;
    int c;
    if (load == 0) {
        u16 v = mem16(w, at);
        for (c = 0; c < 4; c++)
            w->val[c] = v;
    } else if (load == 1) {
        u16 a = mem16(w, at), b = mem16(w, at + 2);
        w->val[0] = w->val[1] = a;
        w->val[2] = w->val[3] = b;
    } else {
        for (c = 0; c < 4; c++)
            w->val[c] = mem16(w, at + 2u * (u32)c);
        if (load == 3) {
            w->top = w->val[3] & 0x7fffu; /* WaveForm: the fourth is COUNTERTOP */
            w->val[3] = 0;
        }
    }
    w->refresh_left = (rd(w, 0x528u + 0x20u * (u32)w->seq) & 0xffffffu) + 1u;
    w->step++;
    if (w->step >= w->steps) {
        w->at_end = 1;
        w->delay_left = rd(w, 0x52cu + 0x20u * (u32)w->seq) & 0xffffffu;
        esk_nrfp_event(&w->x, 0x110u + 4u * (u32)w->seq); /* SEQEND[n] */
        if (rd(w, 0x200) & (1u << w->seq))
            w->stop_req = 1; /* SEQEND[n]_STOP */
    }
}

static void start_seq(struct npwm *w, int n)
{
    u32 cnt = rd(w, 0x524u + 0x20u * (u32)n) & 0x7fffu;
    u32 ptr = rd(w, 0x520u + 0x20u * (u32)n);
    w->seq = n;
    w->step = 0;
    w->steps = cnt / per_step(w);
    w->at_end = 0;
    w->next_step = 0;
    w->top = rd(w, 0x508) & 0x7fffu;
    if (w->top < 3)
        w->top = 3;
    if (cnt && (ptr < NRF_DATA_RAM || ptr - NRF_DATA_RAM >= NRF_DATA_RAM_SIZE)) {
        esk_rf_diag(w->x.s, ES_SEV_WARNING, "mcu.easydma_range",
                    "%s's SEQ[%d].PTR 0x%08lx is not in Data RAM: EasyDMA reaches only that; nothing is played",
                    w->x.p->name, n, (unsigned long)ptr);
        w->steps = 0;
    }
    esk_nrfp_event(&w->x, 0x108u + 4u * (u32)n); /* SEQSTARTED[n] */
    if (!w->steps) {
        w->at_end = 1;
        w->delay_left = 0;
        esk_nrfp_event(&w->x, 0x110u + 4u * (u32)n);
        return;
    }
    load_step(w);
}

static void stopped(struct npwm *w)
{
    w->playing = 0;
    w->stop_req = 0;
    w->seq = -1;
    esk_rf_clock(w->x.p, &w->running, 0);
    esk_nrfp_event(&w->x, 0x104); /* STOPPED */
}

static void begin(struct npwm *w, int n)
{
    w->loops = 0;
    w->playing = 1;
    w->taken = 1;
    w->cnt = 0;
    w->down = 0;
    w->acc = 0;
    w->since = w->x.s->cycles;
    w->stop_req = 0;
    start_seq(w, n);
    levels(w);
    esk_rf_clock(w->x.p, &w->running, 1);
    esk_nrf_pins_changed(w->x.s);
}

/* a period ended: the sequence goes on */
static void period_end(struct npwm *w)
{
    u32 loop = rd(w, 0x514) & 0xffffu, sh = rd(w, 0x200);
    esk_nrfp_event(&w->x, 0x118); /* PWMPERIODEND */
    if (w->stop_req) {
        stopped(w);
        return;
    }
    if (w->seq < 0)
        return;
    if (!w->at_end) {
        int nextstep = (rd(w, 0x510) >> 8 & 1u) != 0;
        if (nextstep) {
            if (w->next_step) {
                w->next_step = 0;
                load_step(w);
            }
        } else if (--w->refresh_left == 0) {
            load_step(w);
        }
        return;
    }
    if (w->refresh_left > 1) {
        w->refresh_left--;
        return;
    }
    if (w->delay_left) {
        w->delay_left--;
        return;
    }
    /* the sequence has been played */
    if (!loop) {
        w->seq = -1; /* the last step holds */
        return;
    }
    if (w->seq == 0) {
        start_seq(w, 1);
        return;
    }
    if (++w->loops < loop) {
        start_seq(w, 0);
        return;
    }
    esk_nrfp_event(&w->x, 0x11c); /* LOOPSDONE */
    w->seq = -1;
    if (sh & (1u << 4)) {
        stopped(w); /* LOOPSDONE_STOP */
        return;
    }
    if (sh & (1u << 2)) {
        w->loops = 0;
        start_seq(w, 0);
    } else if (sh & (1u << 3)) {
        w->loops = 0;
        start_seq(w, 1);
    }
}

/* ticks to the next edge or the period's end */
static u32 ticks_to_next(const struct npwm *w)
{
    u32 best, c;
    if (!w->down) {
        best = w->top - w->cnt; /* to the top: the period's end (Up) or the turn */
        for (c = 0; c < 4; c++) {
            u32 cmp = w->val[c] & 0x7fffu;
            if (cmp > w->cnt && cmp - w->cnt < best)
                best = cmp - w->cnt;
        }
    } else {
        best = w->cnt; /* to 0: the period's end */
        for (c = 0; c < 4; c++) {
            u32 cmp = w->val[c] & 0x7fffu;
            if (cmp && cmp <= w->cnt && w->cnt - cmp + 1 < best)
                best = w->cnt - cmp + 1;
        }
    }
    return best ? best : 1;
}

static void advance(struct npwm *w, u32 n)
{
    while (n && w->playing) {
        u32 k = ticks_to_next(w);
        if (k > n)
            k = n;
        n -= k;
        if (!w->down) {
            w->cnt += k;
            if (w->cnt >= w->top) {
                if (updown(w)) {
                    w->cnt = w->top;
                    w->down = 1;
                } else {
                    w->cnt = 0;
                    period_end(w);
                }
            }
        } else {
            w->cnt -= k;
            if (w->cnt == 0) {
                w->down = 0;
                period_end(w);
            }
        }
    }
}

static void pwm_tick(void *ctx, u32 cycles)
{
    struct npwm *w = (struct npwm *)ctx;
    u32 tc, n;
    int changed;
    if (!w->playing)
        return;
    cycles = esk_mach_after(w->x.s, cycles, w->since);
    w->since = 0;
    tc = tick_cycles(w);
    w->acc += cycles;
    n = (u32)(w->acc / tc);
    w->acc %= tc;
    if (!n)
        return;
    advance(w, n);
    changed = levels(w);
    if (changed)
        esk_nrf_pins_changed(w->x.s);
}

static int pwm_next(void *ctx, u32 *cycles)
{
    struct npwm *w = (struct npwm *)ctx;
    u64 need;
    if (!w->playing)
        return 0;
    need = (u64)ticks_to_next(w) * tick_cycles(w);
    need = need > w->acc ? need - w->acc : 1;
    *cycles = need > 0xffffffffu ? 0xffffffffu : (u32)need;
    return 1;
}

static u32 pwm_read(void *ctx, u32 off, int n)
{
    struct npwm *w = (struct npwm *)ctx;
    esk_nrfp_inten_read(&w->x, off);
    return esk_rf_read(w->x.p, off, n);
}

static void pwm_write(void *ctx, u32 off, int n, u32 v)
{
    struct npwm *w = (struct npwm *)ctx;
    int was = esk_nrfp_line(&w->x);
    esk_rf_write(w->x.p, off, n, v);
    esk_nrfp_inten_write(&w->x, off, v);
    if (off < 0x100 && (v & 1) && on(w)) {
        switch (off) {
        case 0x004: /* STOP */
            if (w->playing)
                w->stop_req = 1;
            else
                esk_nrfp_event(&w->x, 0x104);
            break;
        case 0x008: /* SEQSTART[0] */
        case 0x00c: /* SEQSTART[1] */
            begin(w, off == 0x00c);
            break;
        case 0x010: /* NEXTSTEP */
            w->next_step = 1;
            break;
        default:
            break;
        }
    }
    if (off == 0x500) {
        if (!on(w)) {
            w->playing = 0;
            w->taken = 0;
            w->seq = -1;
            esk_rf_clock(w->x.p, &w->running, 0);
        }
        esk_nrf_pins_changed(w->x.s);
    } else if (off >= 0x560 && off <= 0x56c) {
        esk_nrf_pins_changed(w->x.s);
    }
    esk_nrfp_irq(&w->x, was, 1);
}

static void pwm_reset(void *ctx)
{
    struct npwm *w = (struct npwm *)ctx;
    int c;
    w->running = 0;
    w->playing = w->taken = 0;
    w->seq = -1;
    w->stop_req = 0;
    w->acc = 0;
    for (c = 0; c < 4; c++) {
        w->val[c] = 0;
        w->out[c] = 0;
    }
    w->x.inten = 0;
}

const struct esk_dev_ops esk_nrf_pwm_ops = {
    "nrf-pwm", pwm_read, pwm_write, pwm_reset, pwm_tick, pwm_next,
};

void *esk_nrf_pwm_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct npwm *w;
    if (strncmp(p->name, "PWM", 3) || p->name[3] < '0' || p->name[3] > '3' || p->name[4])
        return 0;
    w = (struct npwm *)esk_mach_alloc(s, sizeof *w);
    if (!w)
        return 0;
    memset(w, 0, sizeof *w);
    if (esk_nrfp_init(&w->x, s, p))
        return 0;
    w->unit = p->name[3] - '0';
    w->seq = -1;
    return w;
}
