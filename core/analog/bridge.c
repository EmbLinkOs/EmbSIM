/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* bridge.c -- the mixed-signal bridges of SPEC-004 §8 (SPEC-003 §4): a
 * Thevenin driver for digital_out, digital_io and power pins on an analog
 * net (esk_bridge_out_ops) and a comparator with a pull for digital_in pins
 * (esk_bridge_in_ops). The comparator's crossings are detected and bisected
 * by the solver (mna.c) from the pin model's vih/vil and the pin's sampled
 * state; this file stamps the pull. A digital_io pin keeps one bridge_out
 * device that stamps the driver when the pin is an output and the pull when
 * it is an input. */
#include "analog/analog.h"
#include <math.h>
#include <string.h>

typedef struct bridge_state {
    int is_power;
    int disabled; /* a duplicate or driven supply: nothing stamped */
    int open_cur; /* the driver is open (Z) */
    double v_cur; /* the source value when not ramping */
    double r;     /* series resistance (0: ideal branch) */
    int ramping;
    es_time_t t0, t1;
    double v_from, v_to;
    double pull_g, pull_v; /* the input pull (0 when none) */
    int supply_noted;      /* the implicit-supply note was raised */
    double v_src_trial, i_trial;
} bridge_state;

static int out_role(const esk_adev *d)
{
    const esk_pin *p = d->pin;
    if (p->model.kind == ESK_PIN_DIGITAL_OUT || p->model.kind == ESK_PIN_POWER)
        return 1;
    if (p->model.kind == ESK_PIN_DIGITAL_IO)
        return p->is_output;
    return 0;
}

static es_time_t s_to_ps(double s)
{
    double v;
    if (!(s > 0.0))
        return 0;
    v = s * 1e12 + 0.5;
    if (v >= 1.8e19)
        return ES_TIME_MAX;
    return (es_time_t)v; /* bounded and positive */
}

static void raise_pin(esk_adev *d, es_severity_t sev, const char *code, const char *msg)
{
    esk_engine *e = d->isl->e;
    char subject[160];
    esk_snprintf(subject, sizeof subject, "%s.%s",
                 d->pin->comp && d->pin->comp->id ? d->pin->comp->id : "?",
                 d->pin->name ? d->pin->name : "?");
    esk_diag_raise(e->diag, sev, code, e->sched.now, "pin", subject, NULL, 0, "%s", msg);
}

/* does device `o` hold node `node` with a source of its own? Only the output
 * pins count: a regulator's IN or an op-amp's inputs on a net do not drive it. */
static int device_drives_node(const esk_adev *o, int node)
{
    const char *name = o->ops->name;
    if (!strcmp(name, "vsource"))
        return (o->nnodes > 0 && o->node[0] == node) || (o->nnodes > 1 && o->node[1] == node);
    if (!strcmp(name, "regulator"))
        return o->nnodes > 1 && o->node[1] == node;
    if (!strcmp(name, "opamp"))
        return o->nnodes > 2 && o->node[2] == node;
    return 0;
}

/* a power pin's ideal source is dropped when the node is already held */
static void check_duplicate_supply(esk_adev *d, bridge_state *s)
{
    esk_island *isl = d->isl;
    size_t i;
    int node = d->node[0];
    if (node == 0) {
        s->disabled = 1; /* a ground pin: node 0 is fixed already */
        return;
    }
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *o = isl->devs.data[i];
        if (o == d)
            break;
        if (o->pin && o->ops == &esk_bridge_out_ops && o->pin->model.kind == ESK_PIN_POWER &&
            o->node[0] == node) {
            const bridge_state *os = (const bridge_state *)o->state;
            if (!os->disabled) {
                if (fabs(os->v_cur - d->pin->model.nominal_v) > 1e-9)
                    raise_pin(
                        d, ES_SEV_ERROR, "analog.singular_topology",
                        "two power pins with different voltages on one net; the first one is used");
                s->disabled = 1;
                return;
            }
        }
    }
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *o = isl->devs.data[i];
        if (o->pin || !device_drives_node(o, node))
            continue;
        s->disabled = 1;
        raise_pin(
            d, ES_SEV_INFO, "analog.supply_driven",
            "the net is driven by a component model; the power pin's ideal source is not added");
        return;
    }
}

static void setup_pull(esk_adev *d, bridge_state *s)
{
    const esk_pin_model *m = &d->pin->model;
    s->pull_g = 0.0;
    s->pull_v = 0.0;
    if (m->pull == ESK_PULL_NONE)
        return;
    s->pull_g = m->rpull > 0.0 ? 1.0 / m->rpull : 1.0 / 40000.0;
    if (m->pull == ESK_PULL_UP) {
        if (m->nominal_v > 0.0) {
            s->pull_v = m->nominal_v;
        } else {
            s->pull_v = d->isl->e->default_supply_v;
            if (!s->supply_noted)
                raise_pin(d, ES_SEV_INFO, "analog.implicit_supply",
                          "pull-up to the session's default supply voltage");
            s->supply_noted = 1;
        }
    }
}

static es_status bridge_setup(esk_adev *d)
{
    bridge_state *s = (bridge_state *)d->state;
    double v, r;
    int open;
    if (!d->pin)
        return ES_EINVAL;
    esk_pin_bridge_target(d->pin, &v, &r, &open);
    s->is_power = d->pin->model.kind == ESK_PIN_POWER;
    s->open_cur = open;
    s->v_cur = v;
    s->r = r;
    s->ramping = 0;
    setup_pull(d, s);
    if (s->is_power) {
        check_duplicate_supply(d, s);
        d->nbranch = s->disabled ? 0 : 1;
    }
    return ES_OK;
}

static double source_value(const bridge_state *s, es_time_t t)
{
    if (!s->ramping)
        return s->v_cur;
    if (t <= s->t0)
        return s->v_from;
    if (t >= s->t1)
        return s->v_to;
    return s->v_from + (s->v_to - s->v_from) * (double)(t - s->t0) / (double)(s->t1 - s->t0);
}

/* the pin's target changed at `at`: ramp from the present value at the pin's slew rate */
static void retarget(esk_adev *d, bridge_state *s, es_time_t at)
{
    double v, r, from, slew = d->pin->model.slew_v_per_us;
    int open;
    esk_pin_bridge_target(d->pin, &v, &r, &open);
    if (open) {
        s->open_cur = 1;
        s->ramping = 0;
        s->v_cur = v;
        return;
    }
    if (s->open_cur) {
        /* turning on from Z: start from where the node sits */
        from = esk_island_node_v(d->isl, d->node[0]);
        s->open_cur = 0;
    } else {
        from = source_value(s, at);
    }
    s->r = r;
    if (slew > 0.0 && fabs(v - from) > 0.0) {
        double dur_s = fabs(v - from) / (slew * 1e6);
        es_time_t dur = s_to_ps(dur_s);
        s->ramping = 1;
        s->t0 = at;
        s->t1 = at + (dur ? dur : 1);
        s->v_from = from;
        s->v_to = v;
        s->v_cur = v;
    } else {
        s->ramping = 0;
        s->v_cur = v;
    }
}

static void bridge_stamp(esk_adev *d, esk_amatrix *m, es_time_t t)
{
    bridge_state *s = (bridge_state *)d->state;
    int n = d->node[0];
    double scale = esk_am_source_scale(m);
    s->i_trial = 0.0;
    s->v_src_trial = 0.0;
    if (s->disabled)
        return;
    if (out_role(d)) {
        double v;
        if (s->open_cur)
            return;
        v = source_value(s, t);
        s->v_src_trial = v;
        if (s->r > 0.0) {
            double g = 1.0 / s->r;
            esk_am_stamp_g(m, n, 0, g);
            esk_am_stamp_i(m, 0, n, g * v * scale);
            s->i_trial = g * (esk_am_x(m, n) - v * scale);
        } else {
            int b = d->branch[0];
            double ib = esk_am_x(m, b);
            esk_am_stamp_j(m, n, b, 1.0);
            esk_am_stamp_f(m, n, ib);
            esk_am_stamp_j(m, b, n, 1.0);
            esk_am_stamp_f(m, b, esk_am_x(m, n) - v * scale);
            s->i_trial = ib;
        }
        return;
    }
    if (s->pull_g > 0.0) {
        esk_am_stamp_g(m, n, 0, s->pull_g);
        esk_am_stamp_i(m, 0, n, s->pull_g * s->pull_v * scale);
        s->i_trial = s->pull_g * (esk_am_x(m, n) - s->pull_v * scale);
    }
}

static void bridge_load_dc(esk_adev *d, esk_amatrix *m)
{
    bridge_stamp(d, m, d->isl->t_accepted);
}
static void bridge_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    bridge_stamp(d, m, esk_integ_t(in));
}

static void bridge_accept(esk_adev *d, es_time_t t)
{
    bridge_state *s = (bridge_state *)d->state;
    if (s->ramping && t >= s->t1) {
        s->ramping = 0;
        s->v_cur = s->v_to;
    }
    d->i_last[0] = s->i_trial;
}

static int bridge_breakpoint(esk_adev *d, es_time_t after, es_time_t *at)
{
    const bridge_state *s = (const bridge_state *)d->state;
    if (!s->ramping || s->disabled)
        return 0;
    if (s->t0 > after) {
        *at = s->t0;
        return 1;
    }
    if (s->t1 > after) {
        *at = s->t1;
        return 1;
    }
    return 0;
}

static void bridge_set_logic(esk_adev *d, esk_logic v, es_time_t at)
{
    bridge_state *s = (bridge_state *)d->state;
    esk_island *isl = d->isl;
    ES_UNUSED(v);
    if (s->is_power)
        return;
    /* integrate up to `at` with the old source, then retarget from there; the
     * pin's pull may have changed with its mode (an MCU's GPIO) */
    esk_island_touch(isl, at);
    setup_pull(d, s);
    retarget(d, s, isl->t_accepted > at ? isl->t_accepted : at);
}

static void bridge_observe(esk_adev *d, esk_json *o)
{
    const bridge_state *s = (const bridge_state *)d->state;
    esk_alloc *a = d->isl->a;
    esk_json_set_number(a, o, "v", esk_island_node_v(d->isl, d->node[0]));
    esk_json_set_number(a, o, "i", d->i_last[0]);
    if (out_role(d)) {
        esk_json_set_bool(a, o, "open", s->open_cur || s->disabled);
        esk_json_set_number(a, o, "source", s->v_src_trial);
    } else {
        esk_json_set_string(a, o, "logic",
                            esk_logic_name(d->pin->sampled == ESK_LZ ? ESK_L0 : d->pin->sampled));
    }
}

const esk_adev_ops esk_bridge_out_ops = {"bridge_out",   sizeof(bridge_state), bridge_setup,
                                         bridge_load_dc, bridge_load_tr,       bridge_accept,
                                         NULL,           bridge_breakpoint,    NULL,
                                         bridge_observe, bridge_set_logic};

const esk_adev_ops esk_bridge_in_ops = {
    "bridge_in",    sizeof(bridge_state), bridge_setup, bridge_load_dc,
    bridge_load_tr, bridge_accept,        NULL,         NULL,
    NULL,           bridge_observe,       NULL};
