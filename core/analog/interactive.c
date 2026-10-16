/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Interactive environmental and isolated circuit devices. Nominal low-frequency
 * approximations: see each catalog entry for the omitted physical effects. */
#include "analog/named.h"
#include "util/num.h"
#include <math.h>
#include <string.h>

static double par(const esk_adev *d, const char *name, double fallback)
{
    double v;
    return d->comp && esk_comp_param_double(d->comp, name, &v) == 0 ? v : fallback;
}
static double input(const esk_adev *d, const char *name, double fallback)
{
    return d->comp ? esk_comp_input_double(d->comp, name, fallback) : fallback;
}
static double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static double accepted(const esk_adev *d, int pin)
{
    return esk_island_node_v(d->isl, d->node[pin]);
}

/* Two-terminal NTC beta law or LDR power law; external inputs never add a
 * conductive path to the environment. Clamp exponents before exp/pow. */
static double environmental_r(const esk_adev *d)
{
    const char *law = d->comp ? esk_comp_param_str(d->comp, "law", "ntc") : "ntc";
    if (!strcmp(law, "ldr")) {
        double lux = clamp(input(d, "illuminance", 10.0), 0.0, 1e6);
        double dark = par(d, "r_dark", 1e6), ref = par(d, "lux_ref", 10.0);
        return lux <= 0.0 ? dark : clamp(par(d, "r_ref", 1e4) * pow(lux / ref, -par(d, "gamma", 0.7)), 1.0, dark);
    }
    {
        double kelvin = clamp(input(d, "temperature", 25.0), -100.0, 300.0) + 273.15;
        double exponent = par(d, "beta", 3977.0) * (1.0 / kelvin - 1.0 / 298.15);
        return clamp(par(d, "r25", 1e4) * exp(clamp(exponent, -40.0, 40.0)), 0.01, 1e12);
    }
}
static es_status environmental_setup(esk_adev *d)
{
    return d->nnodes == 2 && par(d, "r25", 1e4) > 0.0 && par(d, "beta", 3977.0) > 0.0 &&
        par(d, "r_dark", 1e6) > 0.0 && par(d, "r_ref", 1e4) > 0.0 && par(d, "lux_ref", 10.0) > 0.0 &&
        par(d, "gamma", 0.7) > 0.0 ? ES_OK : ES_EINVAL;
}
static void environmental_load(esk_adev *d, esk_amatrix *m)
{
    esk_am_stamp_g(m, d->node[0], d->node[1], 1.0 / environmental_r(d));
}
static void environmental_observe(esk_adev *d, esk_json *out)
{
    double r = environmental_r(d), v = accepted(d, 0) - accepted(d, 1);
    esk_json_set_number(d->isl->a, out, "resistance", r);
    esk_json_set_number(d->isl->a, out, "current", v / r);
}
const esk_adev_ops esk_environmental_ops = { "environment_resistor", 0, environmental_setup,
    environmental_load, NULL, NULL, NULL, NULL, NULL, environmental_observe, NULL };

/* Photodiode: reverse photocurrent K->A and a deliberately simple forward
 * knee. sensitivity is A per W/m2 at the documented reference wavelength. */
static es_status photo_setup(esk_adev *d)
{
    return d->nnodes == 2 && par(d, "sensitivity", 5e-6) >= 0.0 &&
        par(d, "r_forward", 20.0) > 0.0 && par(d, "r_shunt", 1e10) > 0.0 ? ES_OK : ES_EINVAL;
}
static double photocurrent(const esk_adev *d)
{
    return clamp(input(d, "irradiance", 0.0), 0.0, 10000.0) * par(d, "sensitivity", 5e-6);
}
static void photo_load(esk_adev *d, esk_amatrix *m)
{
    int a = d->node[0], k = d->node[1];
    double v = esk_am_v(m, a, k), vf = par(d, "vf", 0.6), g = 1.0 / par(d, "r_shunt", 1e10);
    esk_am_stamp_g(m, a, k, g);
    esk_am_stamp_i(m, k, a, photocurrent(d) * esk_am_source_scale(m));
    if (v > vf) {
        g = 1.0 / par(d, "r_forward", 20.0);
        esk_am_stamp_g(m, a, k, g);
        esk_am_stamp_i(m, a, k, -vf * g);
    }
}
static void photo_observe(esk_adev *d, esk_json *out)
{
    esk_json_set_number(d->isl->a, out, "photocurrent", photocurrent(d));
}
const esk_adev_ops esk_photodiode_ops = { "photodiode", 0, photo_setup, photo_load,
    NULL, NULL, NULL, NULL, NULL, photo_observe, NULL };

/* Optocoupler A,K,C,E. An input LED knee drives an isolated collector current
 * through nominal CTR, limited by a finite saturation resistance. There is no
 * galvanic stamp between input and output; cross terms are optical control. */
static es_status opto_setup(esk_adev *d)
{
    return d->nnodes == 4 && par(d, "r_led", 20.0) > 0.0 && par(d, "ctr", 1.5) >= 0.0 &&
        par(d, "r_sat", 30.0) > 0.0 && par(d, "r_off", 1e10) > 0.0 ? ES_OK : ES_EINVAL;
}
static void opto_load(esk_adev *d, esk_amatrix *m)
{
    int a = d->node[0], k = d->node[1], c = d->node[2], e = d->node[3];
    double vled = esk_am_v(m, a, k), vf = par(d, "vf", 1.1), rin = par(d, "r_led", 20.0);
    double g = vled > vf ? 1.0 / rin : 0.0;
    double led = vled > vf ? (vled - vf) / rin : 0.0;
    double ctr = par(d, "ctr", 1.5), rsat = par(d, "r_sat", 30.0);
    double ce = esk_am_v(m, c, e), available = ctr * led;
    int saturated = ce < available * rsat;
    double gc = saturated ? 1.0 / rsat : 0.0;
    double ic = saturated ? ce / rsat : available;
    double control = saturated ? 0.0 : ctr * g;
    esk_am_stamp_g(m, a, k, g + 1e-12);
    if (g > 0.0) esk_am_stamp_i(m, a, k, -vf * g);
    esk_am_stamp_g(m, c, e, 1.0 / par(d, "r_off", 1e10));
    esk_am_stamp_f(m, c, ic); esk_am_stamp_f(m, e, -ic);
    esk_am_stamp_j(m, c, c, gc); esk_am_stamp_j(m, c, e, -gc);
    esk_am_stamp_j(m, e, c, -gc); esk_am_stamp_j(m, e, e, gc);
    esk_am_stamp_j(m, c, a, control); esk_am_stamp_j(m, c, k, -control);
    esk_am_stamp_j(m, e, a, -control); esk_am_stamp_j(m, e, k, control);
}
static void opto_observe(esk_adev *d, esk_json *out)
{
    double v = accepted(d, 0) - accepted(d, 1) - par(d, "vf", 1.1);
    esk_json_set_number(d->isl->a, out, "led_current", v > 0.0 ? v / par(d, "r_led", 20.0) : 0.0);
    esk_json_set_number(d->isl->a, out, "vce", accepted(d, 2) - accepted(d, 3));
}
const esk_adev_ops esk_optocoupler_ops = { "optocoupler", 0, opto_setup, opto_load,
    NULL, NULL, NULL, NULL, NULL, opto_observe, NULL };

/* Relay contact controller S+,S-,COM,NC,NO senses the coil resistor's
 * voltage. Coil R/L are separate reusable components in the package composite.
 * Pickup/dropout use hysteresis and accepted-time persistence; rejected Newton
 * trials cannot switch contacts or advance a deadline. */
typedef struct relay_state { int active, pending, trial; es_time_t due; } relay_state;
static es_status relay_setup(esk_adev *d)
{
    relay_state *s = (relay_state *)d->state;
    s->pending = -1;
    return d->nnodes == 5 && par(d, "pickup", 3.75) > par(d, "dropout", 0.5) &&
        par(d, "dropout", 0.5) >= 0.0 && par(d, "ron", 0.1) > 0.0 && par(d, "roff", 1e10) > 0.0 &&
        par(d, "operate_s", 0.01) >= 0.0 && par(d, "operate_s", 0.01) <= 10.0 &&
        par(d, "release_s", 0.005) >= 0.0 && par(d, "release_s", 0.005) <= 10.0 ? ES_OK : ES_EINVAL;
}
static void changeover(esk_adev *d, esk_amatrix *m, int offset, int active)
{
    double on = 1.0 / par(d, "ron", 0.1), off = 1.0 / par(d, "roff", 1e10);
    esk_am_stamp_g(m, d->node[offset], d->node[offset + 1], active ? off : on);
    esk_am_stamp_g(m, d->node[offset], d->node[offset + 2], active ? on : off);
}
static void relay_load(esk_adev *d, esk_amatrix *m)
{
    relay_state *s = (relay_state *)d->state;
    double sense = fabs(esk_am_v(m, d->node[0], d->node[1]));
    s->trial = s->active ? sense > par(d, "dropout", 0.5) : sense >= par(d, "pickup", 3.75);
    esk_am_stamp_g(m, d->node[0], d->node[1], 1e-12);
    changeover(d, m, 2, s->active);
}
static void relay_accept(esk_adev *d, es_time_t at)
{
    relay_state *s = (relay_state *)d->state;
    if (s->trial == s->active) { s->pending = -1; return; }
    if (s->pending != s->trial) {
        double seconds = par(d, s->trial ? "operate_s" : "release_s", s->trial ? 0.01 : 0.005);
        s->pending = s->trial;
        if (esk_time_add(at, (es_time_t)(seconds * 1e12), &s->due) != 0) { s->pending = -1; return; }
    }
    if (at >= s->due) { s->active = s->trial; s->pending = -1; }
}
static int relay_breakpoint(esk_adev *d, es_time_t after, es_time_t *at)
{
    const relay_state *s = (const relay_state *)d->state;
    if (s->pending >= 0 && s->due > after && s->due - after < 100 * ES_PS_PER_US) { *at = s->due; return 1; }
    return esk_time_add(after, 100 * ES_PS_PER_US, at) == 0;
}
static void relay_observe(esk_adev *d, esk_json *out)
{
    const relay_state *s = (const relay_state *)d->state;
    esk_json_set_bool(d->isl->a, out, "energized", s->active);
}
const esk_adev_ops esk_relay_contacts_ops = { "relay_contacts", sizeof(relay_state), relay_setup,
    relay_load, NULL, relay_accept, NULL, relay_breakpoint, NULL, relay_observe, NULL };

static es_status spdt_setup(esk_adev *d)
{
    return d->nnodes == 3 && par(d, "ron", 0.1) > 0.0 && par(d, "roff", 1e10) > 0.0 ? ES_OK : ES_EINVAL;
}
static void spdt_load(esk_adev *d, esk_amatrix *m)
{
    changeover(d, m, 0, d->comp ? esk_comp_input_bool(d->comp, "pressed", 0) : 0);
}
const esk_adev_ops esk_spdt_ops = { "spdt_contact", 0, spdt_setup, spdt_load,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL };

/* Four-pad tactile button: pads 1/2 and 3/4 are permanently paired. Unused
 * duplicate pads may be left open; only connected nodes enter the matrix. */
static int button_node(esk_adev *d, const char *name)
{
    esk_pin *p = d->comp ? esk_comp_pin(d->comp, name) : NULL;
    return p && p->net ? p->analog_node : -1;
}
static es_status button4_setup(esk_adev *d)
{
    return d->comp && d->comp->npins == 4 && d->nnodes >= 1 && d->nnodes <= 4 &&
        par(d, "ron", 0.1) > 0.0 && par(d, "roff", 1e10) > 0.0 ? ES_OK : ES_EINVAL;
}
static void button4_load(esk_adev *d, esk_amatrix *m)
{
    int a = button_node(d, "1"), b = button_node(d, "2");
    int c = button_node(d, "3"), e = button_node(d, "4");
    int first = a >= 0 ? a : b, second = c >= 0 ? c : e;
    int pressed = esk_comp_input_bool(d->comp, "pressed", 0);
    if (a >= 0 && b >= 0) esk_am_stamp_g(m, a, b, 1000.0);
    if (c >= 0 && e >= 0) esk_am_stamp_g(m, c, e, 1000.0);
    if (first >= 0 && second >= 0)
        esk_am_stamp_g(m, first, second, 1.0 / par(d, pressed ? "ron" : "roff", pressed ? 0.1 : 1e10));
}
static void button4_observe(esk_adev *d, esk_json *out)
{
    esk_json_set_bool(d->isl->a, out, "closed", esk_comp_input_bool(d->comp, "pressed", 0));
}
const esk_adev_ops esk_button4_ops = { "pushbutton4", 0, button4_setup, button4_load,
    NULL, NULL, NULL, NULL, NULL, button4_observe, NULL };
