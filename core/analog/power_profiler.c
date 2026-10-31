/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* power_profiler.c -- the measurement core of Nordic's Power Profiler Kit II
 * (ADR-033, docs/engineering/bench-instruments.md), from its user guide
 * v1.0.1 (4461_012), section 8.
 *
 * Pins VIN, VOUT, GND. Two modes (`mode`):
 *
 *   - source meter: it powers the board from VOUT at `voltage` (0.8 to
 *     5.0 V), at most 600 mA (Max_I in Source Meter mode): below the limit
 *     a source behind `r_out`, at it a constant 600 mA, the output falling
 *     as a supply in current limit does. VIN is not used;
 *   - ampere meter: in series with the board's own supply, VIN to VOUT,
 *     through `r_series` (the measurement resistors and switches; 1 A
 *     continuous at most).
 *
 * The current into the board is measured as the PPK2 does: averaged over
 * each 10 us (100 kS/s), each sample at the resolution of the range its
 * current falls in (the guide's table 8: 200 nA-50 uA at 0.2 uA, 50-500 uA
 * at 0.5 uA, 0.5-5 mA at 5 uA, 5-50 mA at 50 uA, 50-1000 mA at 1 mA, the
 * five ranges switched automatically). It cannot measure a negative
 * current: those samples read 0. Readings above 1 A read 1 A.
 *
 * Sampling runs from the `run` input's true (Start) to false (Stop). The
 * samples are integrated from the accepted solutions, the current held
 * between them, without a wake every 10 us: they are completed when read
 * (power_profiler_read, the instrument.read method) or as steps are
 * accepted, so a quiet island costs nothing until it is looked at. At most
 * PP_CAP samples are held (the last 40 s); older ones are dropped.
 *
 * Not modelled: the measurement resistors' values and the voltage they drop
 * (not published: `r_series` is an assumption); the accuracy (+-10 %, +-15 %
 * in the top range) as an error: the readings are exact to their
 * resolution; the transients when the range switches; a current limit in
 * ampere-meter mode beyond the rating; sinking current in source mode. */
#include "analog/named.h"
#include "util/num.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PP_PERIOD ((es_time_t)10000000) /* 10 us in ps */
#define PP_CAP ((size_t)4000000)        /* 40 s of samples */
#define PP_SOURCE_MAX 0.6               /* A: Max_I, Source Meter mode */
#define PP_READ_MAX 1.0                 /* A: the top of the top range */

typedef struct pp_state {
    int source;           /* 1: source meter, 0: ampere meter */
    int limited;          /* the source is at its current limit (as last accepted) */
    int trial_lim;        /* ...as the last Newton iteration had it */
    double i;             /* the current into the board, as last accepted */
    es_time_t t_last;     /* integrated up to here */
    int running;
    uint64_t k0;          /* the first sample's window (k0 * PP_PERIOD) */
    uint64_t produced;    /* samples completed since Start */
    double win_q;         /* this window's charge so far */
    float *buf;           /* a ring of the last PP_CAP samples */
    double sum_q, peak;   /* since Start: charge (C) and the largest sample */
} pp_state;

static double par(const esk_adev *d, const char *name, double fallback)
{
    double v;
    return d->comp && esk_comp_param_double(d->comp, name, &v) == 0 ? v : fallback;
}

static double node_v(const esk_adev *d, int k) { return esk_island_node_v(d->isl, d->node[k]); }

static int source_mode(const esk_adev *d)
{
    return !d->comp || strcmp(esk_comp_param_str(d->comp, "mode", "source"), "ampere") != 0;
}

static double set_voltage(const esk_adev *d)
{
    double v = par(d, "voltage", 3.3);
    return v < 0.8 ? 0.8 : v > 5.0 ? 5.0 : v;
}

/* a sample at its range's resolution (table 8), never negative, at most 1 A */
double esk_ppk2_quantize(double i)
{
    double step;
    if (!(i > 0.0))
        return 0.0;
    if (i > PP_READ_MAX)
        i = PP_READ_MAX;
    step = i < 50e-6 ? 0.2e-6 : i < 500e-6 ? 0.5e-6 : i < 5e-3 ? 5e-6 : i < 50e-3 ? 50e-6 : 1e-3;
    return floor(i / step + 0.5) * step;
}

/* the samples carried to `now` with the current held since t_last */
static void advance(esk_adev *d, es_time_t now)
{
    pp_state *s = (pp_state *)d->state;
    if (!s->running) {
        s->t_last = now;
        return;
    }
    while (s->t_last < now) {
        es_time_t end = (es_time_t)((s->k0 + s->produced + 1) * PP_PERIOD), to = now < end ? now : end;
        s->win_q += s->i * (double)(to - s->t_last) * 1e-12;
        s->t_last = to;
        if (to == end) {
            double avg = s->win_q / ((double)PP_PERIOD * 1e-12), q = esk_ppk2_quantize(avg);
            s->buf[s->produced % PP_CAP] = (float)q;
            s->sum_q += q * (double)PP_PERIOD * 1e-12;
            if (q > s->peak)
                s->peak = q;
            s->produced++;
            s->win_q = 0.0;
        }
    }
}

static void start(esk_adev *d, es_time_t now)
{
    pp_state *s = (pp_state *)d->state;
    if (!s->buf) {
        s->buf = (float *)malloc(PP_CAP * sizeof *s->buf);
        if (!s->buf)
            return;
    }
    s->running = 1;
    s->k0 = (uint64_t)((now + PP_PERIOD - 1) / PP_PERIOD);
    s->produced = 0;
    s->win_q = 0.0;
    s->sum_q = s->peak = 0.0;
    s->t_last = (es_time_t)(s->k0 * PP_PERIOD);
}

/* the `run` input followed: Start and Stop take effect at `now` */
static void follow_run(esk_adev *d, es_time_t now)
{
    pp_state *s = (pp_state *)d->state;
    int want = d->comp ? esk_comp_input_bool(d->comp, "run", 0) : 0;
    if (want && !s->running)
        start(d, now);
    else if (!want && s->running) {
        advance(d, now);
        s->running = 0;
    }
}

static es_status pp_setup(esk_adev *d)
{
    pp_state *s = (pp_state *)d->state;
    float *keep = s->buf;
    if (d->nnodes != 3 || par(d, "r_out", 0.05) <= 0.0 || par(d, "r_series", 0.05) <= 0.0)
        return ES_EINVAL;
    memset(s, 0, sizeof *s);
    s->buf = keep;
    s->source = source_mode(d);
    s->t_last = d->comp ? d->comp->e->sched.now : 0;
    return ES_OK;
}

/* the source's current into VOUT at vout: below the limit, behind r_out; at it, the limit */
static double source_current(const esk_adev *d, double vout, int *limited)
{
    double g = 1.0 / par(d, "r_out", 0.05), i = (set_voltage(d) - vout) * g;
    *limited = i > PP_SOURCE_MAX;
    return *limited ? PP_SOURCE_MAX : i;
}

static void pp_load(esk_adev *d, esk_amatrix *m)
{
    if (source_mode(d)) {
        int vout = d->node[1], gnd = d->node[2], lim;
        double g = 1.0 / par(d, "r_out", 0.05);
        (void)source_current(d, esk_am_v(m, vout, gnd), &lim);
        if (!lim) {
            /* a source of `voltage` behind r_out, as its Norton equivalent */
            esk_am_stamp_g(m, vout, gnd, g);
            esk_am_stamp_i(m, gnd, vout, set_voltage(d) * g * esk_am_source_scale(m));
        } else {
            /* in current limit: a constant 600 mA */
            esk_am_stamp_g(m, vout, gnd, 1e-9);
            esk_am_stamp_i(m, gnd, vout, PP_SOURCE_MAX * esk_am_source_scale(m));
        }
        /* a change of region since the last iteration: not converged yet */
        if (lim != ((pp_state *)d->state)->trial_lim) {
            ((pp_state *)d->state)->trial_lim = lim;
            esk_am_noncon(m);
        }
    } else {
        esk_am_stamp_g(m, d->node[0], d->node[1], 1.0 / par(d, "r_series", 0.05));
    }
}

static void pp_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    ES_UNUSED(in);
    pp_load(d, m);
}

static void pp_accept(esk_adev *d, es_time_t t)
{
    pp_state *s = (pp_state *)d->state;
    advance(d, t);
    follow_run(d, t);
    s->source = source_mode(d);
    if (s->source) {
        s->i = source_current(d, node_v(d, 1) - node_v(d, 2), &s->limited);
        d->i_last[1] = -s->i; /* out of VOUT */
    } else {
        s->i = (node_v(d, 0) - node_v(d, 1)) / par(d, "r_series", 0.05);
        d->i_last[0] = s->i;
        d->i_last[1] = -s->i;
    }
}

static void pp_observe(esk_adev *d, esk_json *out)
{
    pp_state *s = (pp_state *)d->state;
    esk_alloc *a = d->isl->a;
    double last;
    if (d->comp) {
        /* a quiet island accepts no steps: the samples are completed to now when looked at */
        follow_run(d, d->comp->e->sched.now);
        advance(d, d->comp->e->sched.now);
    }
    last = s->produced ? s->buf[(s->produced - 1) % PP_CAP] : 0.0;
    esk_json_set_string(a, out, "mode", s->source ? "source" : "ampere");
    esk_json_set_string(a, out, "state", s->running ? "sampling" : "idle");
    esk_json_set_number(a, out, "vout", node_v(d, 1) - node_v(d, 2));
    esk_json_set_number(a, out, "current", last);
    esk_json_set_number(a, out, "average", s->produced ? s->sum_q / ((double)s->produced * (double)PP_PERIOD * 1e-12) : 0.0);
    esk_json_set_number(a, out, "peak", s->peak);
    esk_json_set_number(a, out, "charge_uc", s->sum_q * 1e6);
    esk_json_set_integer(a, out, "samples", (int64_t)s->produced);
    esk_json_set_bool(a, out, "limited", s->source && s->limited);
}

es_status esk_power_profiler_read(esk_adev *d, uint64_t since, size_t max, esk_json *out)
{
    pp_state *s;
    esk_alloc *a;
    esk_json *list;
    uint64_t first, i;
    char num[32];
    if (!d || d->ops != &esk_power_profiler_ops)
        return ES_EINVAL;
    s = (pp_state *)d->state;
    a = d->isl->a;
    if (d->comp) {
        follow_run(d, d->comp->e->sched.now);
        advance(d, d->comp->e->sched.now);
    }
    first = s->produced > PP_CAP ? s->produced - PP_CAP : 0;
    if (since < first)
        since = first;
    if (!max)
        max = 65536;
    list = esk_json_new_array(a);
    if (!list)
        return ES_ENOMEM;
    for (i = since; i < s->produced && i - since < max; i++)
        esk_json_array_push(a, list, esk_json_new_number(a, s->buf[i % PP_CAP]));
    esk_json_set_string(a, out, "kind", "power");
    esk_json_set_string(a, out, "state", s->running ? "sampling" : "idle");
    esk_json_set_string(a, out, "mode", s->source ? "source" : "ampere");
    esk_json_set_number(a, out, "sample_rate", 1e12 / (double)PP_PERIOD);
    snprintf(num, sizeof num, "%llu", (unsigned long long)(s->k0 * PP_PERIOD));
    esk_json_set_string(a, out, "start_ps", num);
    snprintf(num, sizeof num, "%llu", (unsigned long long)PP_PERIOD);
    esk_json_set_string(a, out, "period_ps", num);
    esk_json_set_integer(a, out, "first", (int64_t)first);
    esk_json_set_integer(a, out, "since", (int64_t)since);
    esk_json_set_integer(a, out, "next", (int64_t)i);
    esk_json_object_set(a, out, "samples", list);
    return ES_OK;
}

void esk_power_profiler_free(esk_adev *d)
{
    pp_state *s = d && d->ops == &esk_power_profiler_ops ? (pp_state *)d->state : NULL;
    if (s) {
        free(s->buf);
        s->buf = NULL;
    }
}

const esk_adev_ops esk_power_profiler_ops = { "power_profiler", sizeof(pp_state), pp_setup, pp_load, pp_load_tr,
                                              pp_accept, NULL, NULL, NULL, pp_observe, NULL };
