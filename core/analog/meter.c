/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* meter.c -- an inline USB power meter (a "USB tester", ADR-033): plugged
 * between a host's port and a board's cable, it measures what the board
 * draws on VBUS.
 *
 * Its shunt and connectors are a resistance in VBUS (`resistance`, 50 mOhm
 * by default) between IN and OUT; GND passes through. It powers itself from
 * IN (`self_a`, 12 mA), upstream of its shunt, so its own draw is not in
 * what it shows. Its display refreshes every `refresh` seconds (0.5) with
 * the voltage at OUT, the current and the power averaged over the period,
 * at its resolution (1 mV, 0.1 mA, 1 mW); it counts the charge (mAh) and
 * energy (mWh) since it was powered. Below `v_min` (3.5 V) at IN it is dark
 * and counts nothing.
 *
 * Inside: the averages integrate the accepted solutions, the current and
 * the voltage held between them; a refresh with no step since (a quiet
 * island) shows what was held. Not yet: the data lines (D+ and D-) and
 * their voltages, the shunt's own heating, quick-charge protocols. */
#include "analog/named.h"
#include "util/num.h"
#include <math.h>
#include <string.h>

typedef struct meter_state {
    double i, v;         /* the current through the shunt and VOUT, as last accepted */
    double win_q, win_e; /* this period's charge (C) and energy (J) */
    es_time_t t_last;    /* integrated up to here */
    es_time_t win_t;     /* this period's start */
    double shown_v, shown_i, shown_p; /* the display */
    double mah, mwh;
    int lit;
    es_time_t wake;
} meter_state;

#define METER_WAKE 0

static double par(const esk_adev *d, const char *name, double fallback)
{
    double v;
    return d->comp && esk_comp_param_double(d->comp, name, &v) == 0 ? v : fallback;
}

static double node_v(const esk_adev *d, int k) { return esk_island_node_v(d->isl, d->node[k]); }

static es_time_t refresh_ps(const esk_adev *d)
{
    double r = par(d, "refresh", 0.5);
    return (es_time_t)((r > 0.01 ? r : 0.01) * 1e12);
}

static double quantize(double x, double step)
{
    return step > 0.0 ? floor(x / step + 0.5) * step : x;
}

/* the averages carried to `now` with what was held since t_last; the
 * display refreshed at each period's end passed */
static void advance(esk_adev *d, es_time_t now)
{
    meter_state *s = (meter_state *)d->state;
    es_time_t per = refresh_ps(d);
    while (s->t_last < now) {
        es_time_t end = s->win_t + per, to = now < end ? now : end;
        double dt = (double)(to - s->t_last) * 1e-12;
        if (s->lit) {
            s->win_q += s->i * dt;
            s->win_e += s->i * s->v * dt;
            s->mah += s->i * dt / 3.6;
            s->mwh += s->i * s->v * dt / 3.6;
        }
        s->t_last = to;
        if (to == end) {
            double span = (double)per * 1e-12;
            s->shown_i = s->lit ? quantize(s->win_q / span, par(d, "i_resolution", 1e-4)) : 0.0;
            s->shown_p = s->lit ? quantize(s->win_e / span, 1e-3) : 0.0;
            s->shown_v = s->lit ? quantize(s->v, par(d, "v_resolution", 1e-3)) : 0.0;
            s->win_q = s->win_e = 0.0;
            s->win_t = end;
        }
    }
}

static void schedule(esk_adev *d)
{
    meter_state *s = (meter_state *)d->state;
    es_time_t t = s->win_t + refresh_ps(d);
    if (!d->comp || (s->wake && s->wake <= t))
        return;
    if (esk_comp_wake_at(d->comp, t, METER_WAKE) == ES_OK)
        s->wake = t;
}

static es_status meter_setup(esk_adev *d)
{
    meter_state *s = (meter_state *)d->state;
    if (d->nnodes != 3 || par(d, "resistance", 0.05) <= 0.0 || par(d, "refresh", 0.5) <= 0.0)
        return ES_EINVAL;
    memset(s, 0, sizeof *s);
    s->t_last = s->win_t = d->comp ? d->comp->e->sched.now : 0;
    return ES_OK;
}

static void meter_load(esk_adev *d, esk_amatrix *m)
{
    double self = par(d, "self_a", 0.012);
    esk_am_stamp_g(m, d->node[0], d->node[1], 1.0 / par(d, "resistance", 0.05));
    /* its own electronics, from IN: a resistor at its nominal draw at 5 V */
    if (self > 0.0)
        esk_am_stamp_g(m, d->node[0], d->node[2], self / 5.0);
}

static void meter_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    ES_UNUSED(in);
    meter_load(d, m);
}

static void meter_accept(esk_adev *d, es_time_t t)
{
    meter_state *s = (meter_state *)d->state;
    advance(d, t);
    s->i = (node_v(d, 0) - node_v(d, 1)) / par(d, "resistance", 0.05);
    s->v = node_v(d, 1) - node_v(d, 2);
    s->lit = node_v(d, 0) - node_v(d, 2) >= par(d, "v_min", 3.5);
    d->i_last[0] = s->i;
    schedule(d);
}

void esk_usb_meter_on_time(esk_adev *d, es_time_t now)
{
    meter_state *s = (meter_state *)d->state;
    s->wake = 0;
    advance(d, now);
    schedule(d);
}

static void meter_observe(esk_adev *d, esk_json *out)
{
    meter_state *s = (meter_state *)d->state;
    esk_alloc *a = d->isl->a;
    esk_json_set_bool(a, out, "on", s->lit);
    esk_json_set_number(a, out, "voltage", s->shown_v);
    esk_json_set_number(a, out, "current", s->shown_i);
    esk_json_set_number(a, out, "power", s->shown_p);
    esk_json_set_number(a, out, "capacity_mah", quantize(s->mah, 1.0));
    esk_json_set_number(a, out, "energy_mwh", quantize(s->mwh, 1.0));
    esk_json_set_number(a, out, "i", s->i);
}

const esk_adev_ops esk_usb_meter_ops = { "usb_meter", sizeof(meter_state), meter_setup, meter_load, meter_load_tr,
                                         meter_accept, NULL, NULL, NULL, meter_observe, NULL };
