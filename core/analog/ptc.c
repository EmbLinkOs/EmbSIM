/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ptc.c -- a polymeric PTC resettable fuse (the Uno's MF-MSMF050-2): a
 * resistance that heats itself.
 *
 * The polymer core heats with the power through it and loses it to its
 * package, which loses it to the room (two thermal nodes: c_core, r_core,
 * c_case, r_case). The resistance rises gently with the core's temperature
 * (r0 at 23 C, times e^(slope (T - 23))) until the polymer switches at
 * t_switch, then steeply (a factor e every trip_k kelvin): above a current
 * the heat runs away and the fuse trips; tripped, it settles where the
 * power it passes balances what it loses, a few kelvin above the switch,
 * and passes a leakage current until the fault is removed and it cools.
 * The room's temperature is the input `temperature` (the world's), so a
 * warm room trips it sooner, as the datasheet's derating says.
 *
 * Inside: the temperatures are integrated on their own clock, not the
 * island's: from one look to the next (about half a kelvin of change
 * apart, a second at most once settled) at the voltage of the last accepted
 * solution, sub-stepped against the core's time constant. The matrix holds
 * the resistance as last stamped; when the temperatures have moved it by
 * 1 % the island is touched and solves again, so a quiet circuit sleeps
 * between looks while the fuse warms over minutes. */
#include "analog/named.h"
#include "util/num.h"
#include <math.h>

typedef struct ptc_state {
    double t_core, t_case; /* C */
    double v;              /* volts across it in the accepted solution */
    double r_stamped;      /* the resistance the matrix holds */
    es_time_t t_last;      /* the temperatures are up to here */
    es_time_t wake;        /* the next look, 0 when none is pending */
} ptc_state;

#define PTC_WAKE 0         /* its component's wake token */
#define PTC_RESTAMP 1e-2   /* a relative change of resistance that re-solves the circuit (1 %) */

static double par(const esk_adev *d, const char *name, double fallback)
{
    double v;
    return d->comp && esk_comp_param_double(d->comp, name, &v) == 0 ? v : fallback;
}

static double ambient(const esk_adev *d)
{
    double t = d->comp ? esk_comp_input_double(d->comp, "temperature", 23.0) : 23.0;
    return t < -60.0 ? -60.0 : t > 150.0 ? 150.0 : t;
}

static double resistance(const esk_adev *d, double t)
{
    double r0 = par(d, "r0", 0.3), slope = par(d, "slope", 0.009), ts = par(d, "t_switch", 125.0);
    double k = par(d, "trip_k", 2.0), x;
    if (t <= ts)
        return r0 * exp(slope * (t - 23.0));
    x = (t - ts) / k;
    if (x > 30.0)
        x = 30.0; /* 1e13 times: open for every purpose */
    return r0 * exp(slope * (ts - 23.0)) * exp(x);
}

/* the temperatures' rates (K/s) at `v` volts across it */
static void rates(const esk_adev *d, const ptc_state *s, double v, double *d1, double *d2)
{
    double ta = ambient(d), rc = par(d, "r_core", 21.0), rk = par(d, "r_case", 113.0);
    double p = v * v / resistance(d, s->t_core);
    double flow = (s->t_core - s->t_case) / rc;
    *d1 = (p - flow) / par(d, "c_core", 0.0037);
    *d2 = (flow - (s->t_case - ta) / rk) / par(d, "c_case", 0.19);
}

/* the temperatures carried to `now` at the voltage held since t_last, in
 * sub-steps short against the core's time constant */
static void advance(esk_adev *d, es_time_t now)
{
    ptc_state *s = (ptc_state *)d->state;
    double dt = now > s->t_last ? (double)(now - s->t_last) * 1e-12 : 0.0;
    double tau = par(d, "c_core", 0.0037) * par(d, "r_core", 21.0);
    s->t_last = now > s->t_last ? now : s->t_last;
    while (dt > 0.0) {
        double d1, d2, sub = dt < tau / 10.0 ? dt : tau / 10.0;
        rates(d, s, s->v, &d1, &d2);
        s->t_core += d1 * sub;
        s->t_case += d2 * sub;
        dt -= sub;
    }
}

/* the next look at its temperatures: about half a kelvin of change away,
 * between 10 us and a second. A look already due by then is kept: moving it
 * on every accepted step would leave a cancelled event in the scheduler for
 * each step until its time came, and every step scans them */
static void schedule(esk_adev *d, es_time_t now)
{
    ptc_state *s = (ptc_state *)d->state;
    double d1, d2, fast, h;
    es_time_t t;
    if (!d->comp)
        return;
    rates(d, s, s->v, &d1, &d2);
    fast = fabs(d1) > fabs(d2) ? fabs(d1) : fabs(d2);
    h = fast > 0.5 ? 0.5 / fast : 1.0;
    if (h < 1e-5)
        h = 1e-5;
    t = now + (es_time_t)(h * 1e12);
    if (s->wake > now && s->wake <= t)
        return;
    if (esk_comp_wake_at(d->comp, t, PTC_WAKE) == ES_OK)
        s->wake = t;
}

static es_status ptc_setup(esk_adev *d)
{
    ptc_state *s = (ptc_state *)d->state;
    if (d->nnodes != 2 || par(d, "r0", 0.3) <= 0.0 || par(d, "c_core", 0.0037) <= 0.0 ||
        par(d, "c_case", 0.19) <= 0.0 || par(d, "r_core", 21.0) <= 0.0 || par(d, "r_case", 113.0) <= 0.0 ||
        par(d, "trip_k", 2.0) <= 0.0)
        return ES_EINVAL;
    s->t_core = s->t_case = ambient(d);
    s->v = 0.0;
    s->r_stamped = resistance(d, s->t_core);
    s->t_last = d->comp ? d->comp->e->sched.now : 0;
    s->wake = 0;
    return ES_OK;
}

static void ptc_load(esk_adev *d, esk_amatrix *m)
{
    const ptc_state *s = (const ptc_state *)d->state;
    esk_am_stamp_g(m, d->node[0], d->node[1], 1.0 / s->r_stamped);
}

static void ptc_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    ES_UNUSED(in);
    ptc_load(d, m);
}

static double across(const esk_adev *d)
{
    return esk_island_node_v(d->isl, d->node[0]) - esk_island_node_v(d->isl, d->node[1]);
}

/* an accepted solution: the heat so far at the voltage before it, then the
 * new voltage from here on, and the next look rescheduled for it (a fault
 * that starts now is seen at once, not at the next quiet look) */
static void ptc_accept(esk_adev *d, es_time_t t)
{
    ptc_state *s = (ptc_state *)d->state;
    advance(d, t);
    s->v = across(d);
    schedule(d, t);
}

/* a look between solutions: the temperatures carried to now; when the
 * resistance has moved, the circuit solves again with it */
void esk_ptc_on_time(esk_adev *d, es_time_t now)
{
    ptc_state *s = (ptc_state *)d->state;
    double r;
    s->wake = 0;
    advance(d, now);
    r = resistance(d, s->t_core);
    if (fabs(r - s->r_stamped) > PTC_RESTAMP * s->r_stamped) {
        s->r_stamped = r;
        esk_island_touch(d->isl, now); /* accept reschedules */
        return;
    }
    schedule(d, now);
}

static void ptc_observe(esk_adev *d, esk_json *out)
{
    const ptc_state *s = (const ptc_state *)d->state;
    double r = s->r_stamped, v = across(d);
    esk_json_set_number(d->isl->a, out, "resistance", r);
    esk_json_set_number(d->isl->a, out, "current", v / r);
    esk_json_set_number(d->isl->a, out, "power", v * v / r);
    esk_json_set_number(d->isl->a, out, "temperature", s->t_core);
    esk_json_set_number(d->isl->a, out, "case_temperature", s->t_case);
    esk_json_set_bool(d->isl->a, out, "tripped", s->t_core > par(d, "t_switch", 125.0));
}

const esk_adev_ops esk_ptc_fuse_ops = { "ptc_fuse", sizeof(ptc_state), ptc_setup, ptc_load, ptc_load_tr,
                                        ptc_accept, NULL, NULL, NULL, ptc_observe, NULL };
