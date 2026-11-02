/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* oscilloscope.c -- a 4-channel digital storage oscilloscope, the Rigol
 * DS1054Z (ADR-033, docs/engineering/bench-instruments.md), from Rigol's
 * MSO1000Z/DS1000Z User's Guide, chapter 18.
 *
 * Pins CH1..CH4 and GND. Each input loads its node as the real one with its
 * probe: (1 MOhm || 15 pF) at 1x, 10 MOhm || 15 pF through a 10x probe
 * (`chN_probe`), until the channel is switched off it still loads (a probe
 * clipped on loads the circuit whatever the screen shows).
 *
 * Acquisition, as the scope does it:
 *   - the screen is 12 divisions of `timebase` across; the trigger sits at
 *     the centre plus `delay`. The real-time sample rate is 1 GSa/s with one
 *     channel on, 500 MSa/s with two, 250 MSa/s with three or four, lowered
 *     when the memory (12, 6 or 3 Mpts by channels on; twice that with
 *     `memory` 24M) cannot hold 12 divisions at it;
 *   - each channel is digitised at 8 bits over 10 divisions (25 codes a
 *     division) of its `chN_scale` (V/div, 1 mV to 10 V at 1x, times the
 *     probe) about its `chN_offset`, clipped at the ADC's ends; its analog
 *     bandwidth is 50 MHz (20 MHz with `bw_limit`), a first-order low-pass;
 *     DC, AC (a first-order high-pass at 5 Hz) or GND coupling;
 *   - the edge trigger on `trig_source` at `trig_level`, `trig_slope`
 *     rising, falling or either, with `holdoff` (16 ns to 10 s) between
 *     triggers; `sweep` Auto (a trigger forced when none came within the
 *     screen's time, at least 10 ms: an assumption, the guide does not give
 *     it), Normal (only on a trigger) or Single (one acquisition, then
 *     stopped). The `run` input is Run/Stop.
 *
 * Inside: the engine's accepted steps are kept for each channel (their
 * times and voltages, the coupling applied as they come); an acquisition is
 * those points resampled at the sample rate, through the bandwidth filter,
 * then quantised. The record is decimated to at most SC_SHOW points a
 * channel for the screen (as the scope's display is), the full record's
 * length and rate reported. The trigger's crossing is placed between two
 * accepted steps by linear interpolation. History beyond SC_HIST steps is
 * forgotten, so a very busy circuit on a slow timebase shows only what it
 * holds.
 *
 * Not modelled: the other trigger types and the serial decoders, math, FFT,
 * XY and Roll, the probe's compensation and its own bandwidth, averaging,
 * peak detect and high resolution modes, the DC gain and offset errors (the
 * readings are exact to their 8 bits), noise. */
#include "analog/named.h"
#include "util/num.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SC_CH 4
#define SC_HIST ((size_t)1 << 18) /* accepted steps kept */
#define SC_SHOW 1200               /* points a channel on the screen */
#define SC_WAKE 2u
#define PI_D 3.14159265358979323846

enum { SC_STOP, SC_WAIT, SC_TRIGD, SC_AUTO };

typedef struct sc_point {
    es_time_t t;
    float v[SC_CH];
} sc_point;

typedef struct sc_state {
    /* the inputs' capacitors: charge and current as last accepted, and as tried */
    double q_prev[SC_CH], i_prev[SC_CH], q_trial[SC_CH], i_trial[SC_CH];
    /* AC coupling's high-pass, per channel: its last input and output */
    double hp_x[SC_CH], hp_y[SC_CH];
    es_time_t hp_t;
    sc_point *hist;
    size_t head, count;  /* a ring of the accepted steps */
    int mode;            /* SC_* */
    int armed;
    int run_prev;        /* the run input as last seen: arming on its rise */
    es_time_t trig_t;    /* the trigger being completed */
    es_time_t last_trig; /* for holdoff */
    es_time_t armed_at;  /* Auto's wait starts here */
    es_time_t wake;
    /* the last acquisition */
    uint64_t seq;
    es_time_t acq_trig, acq_x0, acq_dx;
    double acq_rate;
    uint64_t acq_points;
    int acq_forced;
    int acq_n;
    float *acq[SC_CH];
    int acq_on;
} sc_state;

static double par(const esk_adev *d, const char *name, double fallback)
{
    double v;
    return d->comp && esk_comp_param_double(d->comp, name, &v) == 0 ? v : fallback;
}

static const char *spar(const esk_adev *d, const char *name, const char *fallback)
{
    return d->comp ? esk_comp_param_str(d->comp, name, fallback) : fallback;
}

static double chpar(const esk_adev *d, int ch, const char *what, double fallback)
{
    char name[24];
    snprintf(name, sizeof name, "ch%d_%s", ch + 1, what);
    return par(d, name, fallback);
}

static const char *chspar(const esk_adev *d, int ch, const char *what, const char *fallback)
{
    char name[24];
    snprintf(name, sizeof name, "ch%d_%s", ch + 1, what);
    return spar(d, name, fallback);
}

static int ch_on(const esk_adev *d, int ch)
{
    int on = ch == 0;
    char name[24];
    snprintf(name, sizeof name, "ch%d_on", ch + 1);
    if (d->comp)
        esk_comp_param_bool(d->comp, name, &on);
    return on;
}

static double probe(const esk_adev *d, int ch) { return !strcmp(chspar(d, ch, "probe", "10x"), "1x") ? 1.0 : 10.0; }
static double input_r(const esk_adev *d, int ch) { return 1e6 * probe(d, ch); }
#define INPUT_C 15e-12

static double node_v(const esk_adev *d, int k) { return esk_island_node_v(d->isl, d->node[k]); }

static es_status sc_setup(esk_adev *d)
{
    sc_state *s = (sc_state *)d->state;
    sc_point *keep = s->hist;
    float *acq[SC_CH];
    int k;
    if (d->nnodes != 5)
        return ES_EINVAL;
    memcpy(acq, s->acq, sizeof acq);
    memset(s, 0, sizeof *s);
    s->hist = keep;
    memcpy(s->acq, acq, sizeof acq);
    for (k = 0; k < SC_CH; k++)
        s->q_prev[k] = s->i_prev[k] = 0.0;
    return ES_OK;
}

static void sc_load_dc(esk_adev *d, esk_amatrix *m)
{
    sc_state *s = (sc_state *)d->state;
    int k, gnd = d->node[4];
    for (k = 0; k < SC_CH; k++) {
        esk_am_stamp_g(m, d->node[k], gnd, 1.0 / input_r(d, k));
        s->q_trial[k] = INPUT_C * esk_am_v(m, d->node[k], gnd);
        s->i_trial[k] = 0.0;
    }
}

static void sc_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    sc_state *s = (sc_state *)d->state;
    int k, gnd = d->node[4];
    for (k = 0; k < SC_CH; k++) {
        double v = esk_am_v(m, d->node[k], gnd), q = INPUT_C * v, geq, ieq;
        esk_integ_companion(in, q, s->q_prev[k], s->i_prev[k], INPUT_C, &geq, &ieq);
        /* the capacitor's companion: geq with ieq at v, then the input's resistance */
        esk_am_stamp_g(m, d->node[k], gnd, geq);
        esk_am_stamp_i(m, d->node[k], gnd, ieq - geq * v);
        esk_am_stamp_g(m, d->node[k], gnd, 1.0 / input_r(d, k));
        s->q_trial[k] = q;
        s->i_trial[k] = ieq;
    }
}

/* the sample rate for the channels on and the timebase (the memory's limit) */
static double sample_rate(const esk_adev *d, int on, double *depth)
{
    double max = on <= 1 ? 1e9 : on == 2 ? 500e6 : 250e6;
    double mem = (on <= 1 ? 12e6 : on == 2 ? 6e6 : 3e6) * (!strcmp(spar(d, "memory", "12M"), "24M") ? 2.0 : 1.0);
    double span = 12.0 * par(d, "timebase", 1e-3), rate = max;
    if (span * rate > mem)
        rate = mem / span;
    *depth = span * rate;
    return rate;
}

/* the voltage on channel k at time t: the accepted steps, linear between them */
static double hist_v(const sc_state *s, int k, es_time_t t)
{
    size_t lo = 0, hi, n = s->count, base = (s->head + SC_HIST - s->count) % SC_HIST;
    const sc_point *a, *b;
    if (!n)
        return 0.0;
    a = &s->hist[base];
    if (t <= a->t)
        return a->v[k];
    b = &s->hist[(base + n - 1) % SC_HIST];
    if (t >= b->t)
        return b->v[k];
    hi = n - 1;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (s->hist[(base + mid) % SC_HIST].t <= t)
            lo = mid;
        else
            hi = mid;
    }
    a = &s->hist[(base + lo) % SC_HIST];
    b = &s->hist[(base + hi) % SC_HIST];
    return b->t > a->t ? a->v[k] + (b->v[k] - a->v[k]) * (double)(t - a->t) / (double)(b->t - a->t) : b->v[k];
}

static double quantize(double v, double scale, double offset, double *code)
{
    double lsb = scale / 25.0, c = floor((v + offset) / lsb + 0.5);
    if (c > 127.0)
        c = 127.0;
    if (c < -128.0)
        c = -128.0;
    *code = c;
    return c * lsb - offset;
}

/* the acquisition around trigger time `t` (forced: Auto's, with no trigger) */
static void acquire(esk_adev *d, es_time_t t, int forced)
{
    sc_state *s = (sc_state *)d->state;
    int k, on = 0, i;
    double depth, rate, tb = par(d, "timebase", 1e-3), delay = par(d, "delay", 0.0);
    int limit = 0;
    double bw;
    es_time_t span, x0, dx;
    if (d->comp)
        esk_comp_param_bool(d->comp, "bw_limit", &limit);
    bw = limit ? 20e6 : 50e6;
    for (k = 0; k < SC_CH; k++)
        on += ch_on(d, k);
    rate = sample_rate(d, on ? on : 1, &depth);
    span = (es_time_t)(12.0 * tb * 1e12);
    x0 = (es_time_t)((double)t - (6.0 * tb - delay) * 1e12 < 0 ? 0 : (double)t - (6.0 * tb - delay) * 1e12);
    dx = span / SC_SHOW ? span / SC_SHOW : 1;
    s->acq_n = SC_SHOW;
    for (k = 0; k < SC_CH; k++) {
        double scale = chpar(d, k, "scale", 1.0), offset = chpar(d, k, "offset", 0.0), y = 0.0, code;
        double si = 1.0 / rate, tau = 1.0 / (2.0 * PI_D * bw), alpha = si / (tau + si);
        int gnd = !strcmp(chspar(d, k, "coupling", "dc"), "gnd");
        if (!s->acq[k]) {
            s->acq[k] = (float *)malloc(SC_SHOW * sizeof(float));
            if (!s->acq[k])
                return;
        }
        if (!ch_on(d, k))
            continue;
        /* the filter settles over the samples before each shown point: run it from a few
         * time constants (or one shown interval) before */
        for (i = 0; i < SC_SHOW; i++) {
            es_time_t ti = x0 + (es_time_t)i * dx;
            double settle = tau * 6.0 < (double)dx * 1e-12 ? tau * 6.0 : (double)dx * 1e-12, tt;
            if (gnd) {
                s->acq[k][i] = (float)quantize(0.0, scale, offset, &code);
                continue;
            }
            y = hist_v(s, k, ti - (es_time_t)(settle * 1e12));
            for (tt = -settle + si; tt <= 1e-15; tt += si)
                y += alpha * (hist_v(s, k, ti + (es_time_t)(tt * 1e12)) - y);
            s->acq[k][i] = (float)quantize(y, scale, offset, &code);
        }
    }
    s->acq_trig = t;
    s->acq_x0 = x0;
    s->acq_dx = dx;
    s->acq_rate = rate;
    s->acq_points = (uint64_t)depth;
    s->acq_forced = forced;
    s->acq_on = 0;
    for (k = 0; k < SC_CH; k++)
        if (ch_on(d, k))
            s->acq_on |= 1 << k;
    s->seq++;
}

static es_time_t post_time(const esk_adev *d)
{
    double tb = par(d, "timebase", 1e-3), delay = par(d, "delay", 0.0), post = 6.0 * tb + delay;
    return (es_time_t)((post > 0 ? post : 0) * 1e12);
}

static es_time_t auto_wait(const esk_adev *d)
{
    double w = 12.0 * par(d, "timebase", 1e-3);
    return (es_time_t)((w > 10e-3 ? w : 10e-3) * 1e12);
}

static void schedule(esk_adev *d, es_time_t t)
{
    sc_state *s = (sc_state *)d->state;
    if (!d->comp || (s->wake && s->wake <= t && s->wake > d->comp->e->sched.now))
        return;
    if (esk_comp_wake_at(d->comp, t, SC_WAKE) == ES_OK)
        s->wake = t;
}

static void arm(esk_adev *d, es_time_t now)
{
    sc_state *s = (sc_state *)d->state;
    s->armed = 1;
    s->mode = SC_WAIT;
    s->trig_t = 0;
    s->armed_at = now;
    if (!strcmp(spar(d, "sweep", "auto"), "auto"))
        schedule(d, now + auto_wait(d));
}

/* the trigger or Auto's wait completed by `now`: acquire and re-arm (or stop after Single) */
static void progress(esk_adev *d, es_time_t now)
{
    sc_state *s = (sc_state *)d->state;
    int running = d->comp ? esk_comp_input_bool(d->comp, "run", 1) : 1;
    const char *sweep = spar(d, "sweep", "auto");
    if (!running) {
        s->armed = 0;
        s->mode = SC_STOP;
        s->run_prev = 0;
        return;
    }
    if (!s->run_prev) {
        /* Run pressed (or the scope powered up running): armed for the next trigger */
        s->run_prev = 1;
        arm(d, now);
    } else if (!s->armed && s->mode != SC_STOP)
        arm(d, now);
    if (s->trig_t && now >= s->trig_t + post_time(d)) {
        acquire(d, s->trig_t, 0);
        s->last_trig = s->trig_t;
        s->trig_t = 0;
        if (!strcmp(sweep, "single")) {
            s->armed = 0;
            s->mode = SC_STOP;
            return;
        }
        s->mode = SC_TRIGD;
        arm(d, now);
        s->mode = SC_TRIGD;
        return;
    }
    if (!s->trig_t && s->armed && !strcmp(sweep, "auto") && now >= s->armed_at + auto_wait(d)) {
        /* Auto: no trigger came, the screen is refreshed anyway */
        acquire(d, now > post_time(d) ? now - post_time(d) : 0, 1);
        arm(d, now);
        s->mode = SC_AUTO;
    }
}

static void sc_accept(esk_adev *d, es_time_t t)
{
    sc_state *s = (sc_state *)d->state;
    sc_point p;
    int k, src = (int)par(d, "trig_source", 1) - 1;
    double rc = 1.0 / (2.0 * PI_D * 5.0), dt = s->count ? (double)(t - s->hp_t) * 1e-12 : 0.0;
    if (!s->hist) {
        s->hist = (sc_point *)malloc(SC_HIST * sizeof *s->hist);
        if (!s->hist)
            return;
    }
    for (k = 0; k < SC_CH; k++) {
        double v = node_v(d, k) - node_v(d, 4);
        s->q_prev[k] = s->q_trial[k];
        s->i_prev[k] = s->i_trial[k];
        d->i_last[k] = v / input_r(d, k) + s->i_trial[k];
        if (!strcmp(chspar(d, k, "coupling", "dc"), "ac")) {
            /* the input's AC coupling: a first-order high-pass at 5 Hz */
            double a = rc / (rc + dt);
            s->hp_y[k] = s->count ? a * (s->hp_y[k] + v - s->hp_x[k]) : 0.0;
            s->hp_x[k] = v;
            v = s->hp_y[k];
        }
        p.v[k] = (float)v;
    }
    s->hp_t = t;
    p.t = t;
    /* the trigger: a crossing of the level on the source, between the last step and this */
    if (s->armed && !s->trig_t && s->count && src >= 0 && src < SC_CH) {
        const sc_point *prev = &s->hist[(s->head + SC_HIST - 1) % SC_HIST];
        double lv = par(d, "trig_level", 0.0), a = prev->v[src], b = p.v[src];
        const char *slope = spar(d, "trig_slope", "rising");
        int rise = a < lv && b >= lv, fall = a > lv && b <= lv;
        int hit = !strcmp(slope, "falling") ? fall : !strcmp(slope, "either") ? rise || fall : rise;
        if (hit) {
            es_time_t tc = prev->t + (es_time_t)((double)(t - prev->t) * (b != a ? (lv - a) / (b - a) : 1.0));
            es_time_t hold = (es_time_t)(par(d, "holdoff", 16e-9) * 1e12);
            if (!s->last_trig || tc >= s->last_trig + hold) {
                s->trig_t = tc;
                schedule(d, tc + post_time(d));
            }
        }
    }
    s->hist[s->head] = p;
    s->head = (s->head + 1) % SC_HIST;
    if (s->count < SC_HIST)
        s->count++;
    progress(d, t);
}

void esk_oscilloscope_on_time(esk_adev *d, es_time_t now)
{
    sc_state *s = (sc_state *)d->state;
    s->wake = 0;
    progress(d, now);
    if (s->armed && !s->trig_t && !strcmp(spar(d, "sweep", "auto"), "auto"))
        schedule(d, s->armed_at + auto_wait(d));
}

void esk_oscilloscope_on_input(esk_adev *d, es_time_t now)
{
    progress(d, now);
}

static const char *mode_name(int m)
{
    return m == SC_WAIT ? "WAIT" : m == SC_TRIGD ? "T'D" : m == SC_AUTO ? "AUTO" : "STOP";
}

static void sc_observe(esk_adev *d, esk_json *out)
{
    sc_state *s = (sc_state *)d->state;
    esk_alloc *a = d->isl->a;
    int k, on = 0;
    double depth;
    if (d->comp)
        progress(d, d->comp->e->sched.now);
    for (k = 0; k < SC_CH; k++)
        on += ch_on(d, k);
    esk_json_set_string(a, out, "status", mode_name(s->mode));
    esk_json_set_number(a, out, "sample_rate", sample_rate(d, on ? on : 1, &depth));
    esk_json_set_number(a, out, "memory_points", depth);
    esk_json_set_integer(a, out, "acquisitions", (int64_t)s->seq);
    for (k = 0; k < SC_CH; k++) {
        char name[24];
        snprintf(name, sizeof name, "ch%d_v", k + 1);
        esk_json_set_number(a, out, name, node_v(d, k) - node_v(d, 4));
    }
}

es_status esk_oscilloscope_read(esk_adev *d, uint64_t since, esk_json *out)
{
    sc_state *s;
    esk_alloc *a;
    esk_json *chans;
    int k, i;
    char num[32];
    if (!d || d->ops != &esk_oscilloscope_ops)
        return ES_EINVAL;
    s = (sc_state *)d->state;
    a = d->isl->a;
    if (d->comp)
        progress(d, d->comp->e->sched.now);
    esk_json_set_string(a, out, "kind", "scope");
    esk_json_set_string(a, out, "status", mode_name(s->mode));
    esk_json_set_integer(a, out, "seq", (int64_t)s->seq);
    chans = esk_json_new_array(a);
    if (!chans)
        return ES_ENOMEM;
    if (s->seq && s->seq > since) {
        snprintf(num, sizeof num, "%llu", (unsigned long long)s->acq_trig);
        esk_json_set_string(a, out, "trigger_ps", num);
        snprintf(num, sizeof num, "%llu", (unsigned long long)s->acq_x0);
        esk_json_set_string(a, out, "x0_ps", num);
        snprintf(num, sizeof num, "%llu", (unsigned long long)s->acq_dx);
        esk_json_set_string(a, out, "dx_ps", num);
        esk_json_set_number(a, out, "sample_rate", s->acq_rate);
        esk_json_set_number(a, out, "memory_points", (double)s->acq_points);
        esk_json_set_bool(a, out, "forced", s->acq_forced);
        for (k = 0; k < SC_CH; k++) {
            esk_json *ch = esk_json_new_object(a), *pts;
            if (!ch)
                continue;
            esk_json_set_integer(a, ch, "channel", k + 1);
            esk_json_set_bool(a, ch, "on", (s->acq_on >> k) & 1);
            if ((s->acq_on >> k) & 1 && s->acq[k]) {
                pts = esk_json_new_array(a);
                for (i = 0; pts && i < s->acq_n; i++)
                    esk_json_array_push(a, pts, esk_json_new_number(a, s->acq[k][i]));
                if (pts)
                    esk_json_object_set(a, ch, "points", pts);
            }
            esk_json_array_push(a, chans, ch);
        }
    }
    esk_json_object_set(a, out, "channels", chans);
    return ES_OK;
}

void esk_oscilloscope_free(esk_adev *d)
{
    sc_state *s = d && d->ops == &esk_oscilloscope_ops ? (sc_state *)d->state : NULL;
    int k;
    if (!s)
        return;
    free(s->hist);
    s->hist = NULL;
    for (k = 0; k < SC_CH; k++) {
        free(s->acq[k]);
        s->acq[k] = NULL;
    }
}

const esk_adev_ops esk_oscilloscope_ops = { "oscilloscope", sizeof(sc_state), sc_setup, sc_load_dc, sc_load_tr,
                                            sc_accept, NULL, NULL, NULL, sc_observe, NULL };
