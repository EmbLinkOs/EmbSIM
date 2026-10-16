/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Behavioural catalog devices. DC/low-frequency models, not vendor SPICE
 * macromodels. Pin order and approximation boundaries are documented in
 * SPEC-004 and catalog/README.md. No trial state is committed before accept. */
#include "analog/named.h"
#include <math.h>
#include "util/num.h"

static double param(const esk_adev *d, const char *key, double fallback)
{
    double value;
    return d->comp && esk_comp_param_double(d->comp, key, &value) == 0 ? value : fallback;
}

static void voltage(esk_adev *d, esk_amatrix *m, int out, int ref, double v, double r)
{
    int b = d->branch[0];
    double i = esk_am_x(m, b);
    esk_am_stamp_j(m, out, b, 1.0);
    esk_am_stamp_j(m, ref, b, -1.0);
    esk_am_stamp_f(m, out, i);
    esk_am_stamp_f(m, ref, -i);
    esk_am_stamp_j(m, b, out, 1.0);
    esk_am_stamp_j(m, b, ref, -1.0);
    esk_am_stamp_j(m, b, b, -r);
    esk_am_stamp_f(m, b, esk_am_v(m, out, ref) - v - r * i);
}

static void output_observe(esk_adev *d, esk_json *out)
{
    esk_json_set_number(d->isl->a, out, "vout", esk_island_node_v(d->isl, d->node[0]) - esk_island_node_v(d->isl, d->node[2]));
}

/* OUT, VS, GND. Temperature is an external input in degrees Celsius. */
static es_status temperature_setup(esk_adev *d)
{
    if (d->nnodes != 3 || param(d, "rout", 0.1) < 0.0)
        return ES_EINVAL;
    d->nbranch = 1;
    return ES_OK;
}

static void temperature_load(esk_adev *d, esk_amatrix *m)
{
    double temp = d->comp ? esk_comp_input_double(d->comp, "temperature", 25.0) : 25.0;
    double requested = param(d, "offset", 0.0) + param(d, "slope", 0.01) * temp;
    double supply = esk_am_v(m, d->node[1], d->node[2]);
    double headroom = param(d, "headroom", 0.1);
    double limit = supply > headroom ? supply - headroom : 0.0;
    double v = requested < 0.0 ? 0.0 : requested > limit ? limit : requested;
    voltage(d, m, d->node[0], d->node[2], v, param(d, "rout", 0.1));
    if (requested > limit && supply > headroom) {
        esk_am_stamp_j(m, d->branch[0], d->node[1], -1.0);
        esk_am_stamp_j(m, d->branch[0], d->node[2], 1.0);
    }
}

const esk_adev_ops esk_temperature_ops = {
    "temperature_sensor", 0, temperature_setup, temperature_load, NULL, NULL, NULL, NULL,
    NULL, output_observe, NULL
};

/* +, -, OUT, VS, GND. Open collector requires an external pull-up. A smooth
 * transition avoids a discontinuous Jacobian; it is not input offset/noise. */
static es_status comparator_setup(esk_adev *d)
{
    return d->nnodes == 5 && param(d, "ron", 40.0) > 0.0 &&
           param(d, "roff", 1e12) > 0.0 && param(d, "transition", 0.001) > 0.0 ? ES_OK : ES_EINVAL;
}

static void comparator_load(esk_adev *d, esk_amatrix *m)
{
    int np = d->node[0], nm = d->node[1], no = d->node[2], ng = d->node[4];
    double off = 1.0 / param(d, "roff", 1e12);
    double on = 1.0 / param(d, "ron", 40.0);
    double scale = param(d, "transition", 0.001);
    double th = tanh(esk_am_v(m, np, nm) / scale);
    double g = off, deriv = 0.0, vo = esk_am_v(m, no, ng);
    if (esk_am_v(m, d->node[3], ng) >= param(d, "min_supply", 2.0)) {
        g += 0.5 * (on - off) * (1.0 - th);
        deriv = -0.5 * (on - off) * (1.0 - th * th) / scale;
    }
    esk_am_stamp_g(m, no, ng, g);
    esk_am_stamp_j(m, no, np, vo * deriv);
    esk_am_stamp_j(m, no, nm, -vo * deriv);
    esk_am_stamp_j(m, ng, np, -vo * deriv);
    esk_am_stamp_j(m, ng, nm, vo * deriv);
}

const esk_adev_ops esk_comparator_ops = {
    "comparator_oc", 0, comparator_setup, comparator_load, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL
};

/* TRIG, THRESH, RESET, CTRL, OUT, DISCH, VCC, GND. Switching thresholds
 * are sampled on solver steps: use h_max <= the required timing resolution. */
typedef struct timer_state { int high, trial; } timer_state;

static es_status timer_setup(esk_adev *d)
{
    timer_state *s = (timer_state *)d->state;
    if (d->nnodes != 8 || param(d, "rout", 20.0) <= 0.0 || param(d, "rdis", 10.0) <= 0.0)
        return ES_EINVAL;
    s->high = 0;
    s->trial = 0;
    d->nbranch = 1;
    return ES_OK;
}

static void timer_load(esk_adev *d, esk_amatrix *m)
{
    timer_state *s = (timer_state *)d->state;
    int ng = d->node[7], ctrl = d->node[3], power = d->node[6];
    double vs = esk_am_v(m, power, ng), vc = esk_am_v(m, ctrl, ng);
    double trig = esk_am_v(m, d->node[0], ng), thresh = esk_am_v(m, d->node[1], ng);
    double high = vs > 1.7 ? vs - 1.7 : 0.0;
    int q = s->high, next = q;
    esk_am_stamp_g(m, power, ctrl, 1.0 / 5000.0);
    esk_am_stamp_g(m, ctrl, ng, 1.0 / 10000.0);
    if (vs < 4.5 || esk_am_v(m, d->node[2], ng) < 0.7)
        next = 0;
    else if (trig < vc * 0.5)
        next = 1;
    else if (thresh > vc)
        next = 0;
    voltage(d, m, d->node[4], ng, vs < 4.5 ? 0.0 : q ? high : 0.1, param(d, "rout", 20.0));
    if (vs >= 4.5 && q) {
        esk_am_stamp_j(m, d->branch[0], power, -1.0);
        esk_am_stamp_j(m, d->branch[0], ng, 1.0);
    }
    esk_am_stamp_g(m, d->node[5], ng, q ? 1e-12 : 1.0 / param(d, "rdis", 10.0));
    s->trial = next;
}

/* Observe thresholds at most 1 us apart by default. State changes are
 * committed only at accept, then affect the next solve. This deliberately
 * avoids Newton chatter at a capacitor/discharge switching boundary. */
static int timer_breakpoint(esk_adev *d, es_time_t after, es_time_t *at)
{
    double interval = param(d, "sample_interval", 1e-6) * 1e12;
    if (!isfinite(interval) || interval < 1.0 || interval >= 1.8e19) return 0;
    return esk_time_add(after, (es_time_t)interval, at) == 0;
}

static void timer_accept(esk_adev *d, es_time_t at)
{
    timer_state *s = (timer_state *)d->state;
    ES_UNUSED(at);
    s->high = s->trial;
}

static void timer_observe(esk_adev *d, esk_json *out)
{
    const timer_state *s = (const timer_state *)d->state;
    esk_json_set_bool(d->isl->a, out, "high", s->high);
    esk_json_set_number(d->isl->a, out, "vout", esk_island_node_v(d->isl, d->node[4]));
}

const esk_adev_ops esk_timer555_ops = {
    "timer555", sizeof(timer_state), timer_setup, timer_load, NULL, timer_accept, NULL, timer_breakpoint,
    NULL, timer_observe, NULL
};
