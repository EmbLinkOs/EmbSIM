/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* devices.c -- the analog primitives of SPEC-004 §7 (version 1): stamps for
 * the MNA solver of mna.c, parameters read from the component instance,
 * interactive inputs for the switch and potentiometer, and observations.
 *
 * Pin order (d->node[k]): the engine binds the component's pins by these names
 * (or their aliases) when all are connected, else in the description's order:
 *
 *   resistor, capacitor, inductor, switch   "1", "2"
 *   vsource, isource                        "+", "-"      (current flows + -> - inside the source)
 *   potentiometer                           "1", "W", "2"
 *   diode, led, zener                       "A", "K"
 *   bjt_npn, bjt_pnp                        "C", "B", "E"
 *   nmos, pmos                              "D", "G", "S"
 *   opamp                                   "+", "-", "OUT" [, "V+", "V-"]
 *   regulator                               "IN", "OUT", "GND"
 *   pull                                    "1"
 *
 * Conventions: i_last[k] is the current flowing into pin k (amps); observe()
 * reports "i" (into pin 0) and "p" (power dissipated, watts). Junction devices
 * use SPICE's pnjlim voltage limiting on the exponentials. */
#include "analog/analog.h"
#include "analog/named.h"
#include "util/num.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define VT_27C      0.025852 /* kT/q at 27 degrees C, volts */
#define EXP_ARG_MAX 80.0     /* the exponential is continued linearly beyond this */
#define PI_D        3.14159265358979323846

/* ---- the pin names of each primitive (the table above) ---------------------- */

const char *const *esk_adev_pin_names(const esk_adev_ops *ops, int *n)
{
    static const char *const two[] = { "1", "2" }, *const src[] = { "+", "-" }, *const pot[] = { "1", "W", "2" },
                             *const dio[] = { "A", "K" }, *const rgb[] = { "R", "COM", "G", "B" }, *const bjt[] = { "C", "B", "E" }, *const mos[] = { "D", "G", "S" },
                             *const amp[] = { "+", "-", "OUT", "V+", "V-" }, *const reg[] = { "IN", "OUT", "GND" },
                             *const env[] = { "1", "2" }, *const opto[] = { "A", "K", "C", "E" },
                             *const relay[] = { "S+", "S-", "COM", "NC", "NO" },
                             *const spdt[] = { "COM", "NC", "NO" }, *const one[] = { "1" },
                             *const ppk[] = { "VIN", "VOUT", "GND" };
    static const struct { const char *name; const char *const *pins; int n; } table[] = {
        { "environment_resistor", env, 2 }, { "ptc_fuse", two, 2 }, { "photodiode", dio, 2 }, { "optocoupler", opto, 4 },
        { "relay_contacts", relay, 5 }, { "spdt_contact", spdt, 3 },
        { "resistor", two, 2 }, { "capacitor", two, 2 }, { "inductor", two, 2 }, { "switch", two, 2 },
        { "vsource", src, 2 }, { "isource", src, 2 }, { "potentiometer", pot, 3 }, { "diode", dio, 2 },
        { "led", dio, 2 }, { "rgb_led", rgb, 4 }, { "zener", dio, 2 }, { "bjt_npn", bjt, 3 }, { "bjt_pnp", bjt, 3 },
        { "nmos", mos, 3 }, { "pmos", mos, 3 }, { "opamp", amp, 5 }, { "regulator", reg, 3 }, { "pull", one, 1 },
        { "usb_meter", reg, 3 }, { "instrument_input", src, 2 }, { "power_profiler", ppk, 3 },
    };
    size_t i;
    for (i = 0; ops && i < sizeof table / sizeof table[0]; i++)
        if (!strcmp(table[i].name, ops->name)) {
            *n = table[i].n;
            return table[i].pins;
        }
    *n = 0;
    return NULL;
}

/* ---- shared helpers ----------------------------------------------------- */

int esk_adev_preserves_open_pins(const esk_adev_ops *ops)
{
    return ops && (!strcmp(ops->name, "led") || !strcmp(ops->name, "rgb_led") ||
                   !strcmp(ops->name, "instrument_input") || !strcmp(ops->name, "vsource") ||
                   !strcmp(ops->name, "power_profiler")); /* VIN is open in source meter mode */
}

static double pd(const esk_adev *d, const char *name, double def)
{
    double v;
    if (d->comp && esk_comp_param_double(d->comp, name, &v) == 0)
        return v;
    return def;
}

static int has_param(const esk_adev *d, const char *name)
{
    double v;
    return d->comp && esk_comp_param_double(d->comp, name, &v) == 0;
}

static const char *ps(const esk_adev *d, const char *name, const char *def)
{
    return d->comp ? esk_comp_param_str(d->comp, name, def) : def;
}

static double acc_v(const esk_adev *d, int k)
{
    return esk_island_node_v(d->isl, d->node[k]);
}
static double acc_x(const esk_adev *d, int idx)
{
    return esk_island_node_v(d->isl, idx);
}

static void param_error(esk_adev *d, const char *name, const char *why)
{
    if (d->comp)
        esk_comp_diag(d->comp, ES_SEV_ERROR, "analog.param_out_of_range", "%s: %s", name, why);
}

/* a nonlinear current i(v) with slope g, evaluated at the (limited) voltage
 * v_lim, as a current from n1 to n2: Jacobian g, residual i + g (v - v_lim) */
static void stamp_nl(esk_amatrix *m, int n1, int n2, double g, double i, double v_lim)
{
    esk_am_stamp_g(m, n1, n2, g);
    esk_am_stamp_i(m, n1, n2, i - g * v_lim);
}

/* current source from node n1 to n2 scaled by the source-stepping factor */
static void stamp_src_i(esk_amatrix *m, int n1, int n2, double i)
{
    esk_am_stamp_i(m, n1, n2, i * esk_am_source_scale(m));
}

/* an ideal voltage source v between n1 (+) and n2 (-) with branch current b, plus series r */
static void stamp_vsrc(esk_amatrix *m, int n1, int n2, int b, double v, double r)
{
    double ib = esk_am_x(m, b);
    esk_am_stamp_j(m, n1, b, 1.0);
    esk_am_stamp_j(m, n2, b, -1.0);
    esk_am_stamp_f(m, n1, ib);
    esk_am_stamp_f(m, n2, -ib);
    esk_am_stamp_j(m, b, n1, 1.0);
    esk_am_stamp_j(m, b, n2, -1.0);
    esk_am_stamp_j(m, b, b, -r);
    esk_am_stamp_f(m, b, esk_am_v(m, n1, n2) - v - r * ib);
}

static es_time_t s_to_ps(double s)
{
    double ps_d;
    if (!(s > 0.0))
        return 0;
    ps_d = s * 1e12 + 0.5;
    if (ps_d >= 1.8e19)
        return ES_TIME_MAX;
    return (es_time_t)ps_d; /* bounded and positive */
}

static double ps_to_s(es_time_t t)
{
    return (double)t * 1e-12;
}

static void set_pin_currents2(esk_adev *d, double i0)
{
    d->i_last[0] = i0;
    d->i_last[1] = -i0;
}

static double power_of(const esk_adev *d)
{
    double p = 0.0;
    int k;
    for (k = 0; k < d->nnodes; k++)
        p += acc_v(d, k) * d->i_last[k];
    return p;
}

static void observe_basic(esk_adev *d, esk_json *o)
{
    esk_json_set_number(d->isl->a, o, "i", d->i_last[0]);
    esk_json_set_number(d->isl->a, o, "p", power_of(d));
}

/* Shockley junction at a limited voltage: current and conductance */
static void pn_eval(double v, double is, double nvt, double *id, double *gd)
{
    double arg = v / nvt, ev;
    if (arg > EXP_ARG_MAX) {
        double e0 = exp(EXP_ARG_MAX);
        ev = e0 * (1.0 + (arg - EXP_ARG_MAX));
        *gd = is * e0 / nvt;
    } else {
        ev = exp(arg);
        *gd = is * ev / nvt;
    }
    *id = is * (ev - 1.0);
}

/* SPICE pnjlim: limit the junction voltage change per Newton iteration */
static double pnjlim(double vnew, double vold, double vt, double vcrit)
{
    if (vnew > vcrit && fabs(vnew - vold) > 2.0 * vt) {
        if (vold > 0.0) {
            double arg = 1.0 + (vnew - vold) / vt;
            if (arg > 0.0)
                vnew = vold + vt * log(arg);
            else
                vnew = vcrit;
        } else {
            vnew = vt * log(vnew / vt);
        }
    }
    return vnew;
}

static double vcrit_of(double is, double nvt)
{
    return nvt * log(nvt / (sqrt(2.0) * is));
}

/* LTE of a reactive element's state (charge or flux) from the history of its
 * derivative (current for a capacitor, voltage for an inductor): trapezoidal
 * h^3/12 |q'''|, backward Euler h^2/2 |q''|, by divided differences. The
 * caller converts it to volts: a capacitor divides by C (the voltage error),
 * an inductor divides by L and a current scale (see IND_LTE_SCALE_A). */
static double reactive_lte(const esk_integ *in, double i_now, double i_prev, double i_prev2,
                           int nhist)
{
    double h = esk_integ_h(in), hp = esk_integ_h_prev(in);
    if (!(h > 0.0) || nhist < 1)
        return 0.0;
    if (esk_integ_first_order(in) || nhist < 2 || !(hp > 0.0))
        return 0.5 * h * fabs(i_now - i_prev);
    {
        double d1 = (i_now - i_prev) / h, d0 = (i_prev - i_prev2) / hp;
        double q3 = 2.0 * (d1 - d0) / (h + hp);
        return h * h * h / 12.0 * fabs(q3);
    }
}

/* an inductor's current error is reported against a 1 mA scale (so the error
 * control holds it to lte_tol mA, which keeps an RL step within 0.1 %); a
 * voltage view (2/h · dphi) would not shrink with h on a first-order step */
#define IND_LTE_SCALE_A 1e-3

/* ---- resistor ------------------------------------------------------------ */

typedef struct res_state {
    double g;
} res_state;

static es_status res_setup(esk_adev *d)
{
    res_state *s = (res_state *)d->state;
    double r = pd(d, "r", 1e3);
    double tc1 = pd(d, "tc1", 0.0), tc2 = pd(d, "tc2", 0.0), temp = pd(d, "temp", 27.0);
    if (!(r > 0.0)) {
        param_error(d, "r", "must be > 0");
        r = 1e3;
    }
    r *= 1.0 + tc1 * (temp - 27.0) + tc2 * (temp - 27.0) * (temp - 27.0);
    s->g = 1.0 / (r > 1e-9 ? r : 1e-9);
    return ES_OK;
}

static void res_load(esk_adev *d, esk_amatrix *m)
{
    const res_state *s = (const res_state *)d->state;
    esk_am_stamp_g(m, d->node[0], d->node[1], s->g);
}

static void res_accept(esk_adev *d, es_time_t t)
{
    const res_state *s = (const res_state *)d->state;
    ES_UNUSED(t);
    set_pin_currents2(d, s->g * (acc_v(d, 0) - acc_v(d, 1)));
}

static const esk_adev_ops resistor_ops = {"resistor", sizeof(res_state), res_setup, res_load,
                                          NULL,       res_accept,        NULL,      NULL,
                                          NULL,       observe_basic,     NULL};

/* ---- capacitor ------------------------------------------------------------- */

typedef struct cap_state {
    double c, esr;
    int has_ic;
    double ic;
    double q_prev, i_prev, i_prev2;
    double q_trial, i_trial;
    int nhist, trial_first, trial_negligible;
} cap_state;

static es_status cap_setup(esk_adev *d)
{
    cap_state *s = (cap_state *)d->state;
    s->c = pd(d, "c", 1e-6);
    if (!(s->c > 0.0)) {
        param_error(d, "c", "must be > 0");
        s->c = 1e-6;
    }
    s->esr = pd(d, "esr", 0.0);
    s->has_ic = has_param(d, "ic");
    s->ic = pd(d, "ic", 0.0);
    d->nbranch = s->has_ic ? 1 : 0; /* an ideal source holds `ic` at the operating point */
    return ES_OK;
}

static void cap_load_dc(esk_adev *d, esk_amatrix *m)
{
    cap_state *s = (cap_state *)d->state;
    double v = esk_am_v(m, d->node[0], d->node[1]);
    if (s->has_ic)
        stamp_vsrc(m, d->node[0], d->node[1], d->branch[0], s->ic * esk_am_source_scale(m), 0.0);
    s->q_trial = s->c * v;
    s->i_trial = 0.0;
    s->trial_first = 1;
    s->trial_negligible = 0;
}

static void cap_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    cap_state *s = (cap_state *)d->state;
    double v = esk_am_v(m, d->node[0], d->node[1]);
    double q = s->c * v, geq, ieq;
    esk_integ_companion(in, q, s->q_prev, s->i_prev, s->c, &geq, &ieq);
    stamp_nl(m, d->node[0], d->node[1], geq, ieq, v);
    if (s->has_ic) {
        /* the initial-condition branch carries no current after the operating point */
        esk_am_stamp_j(m, d->branch[0], d->branch[0], 1.0);
        esk_am_stamp_f(m, d->branch[0], esk_am_x(m, d->branch[0]));
    }
    s->q_trial = q;
    s->i_trial = ieq;
    s->trial_first = esk_integ_first_order(in);
    s->trial_negligible = esk_integ_negligible(in);
}

static void cap_accept(esk_adev *d, es_time_t t)
{
    cap_state *s = (cap_state *)d->state;
    ES_UNUSED(t);
    if (s->trial_first) {
        s->nhist = 1;
        s->i_prev2 = s->i_prev;
    } else if (s->trial_negligible && s->nhist >= 1) {
        /* replace the newest point: the spacing to i_prev2 stays usable */
    } else {
        if (s->nhist < 2)
            s->nhist++;
        s->i_prev2 = s->i_prev;
    }
    s->i_prev = s->i_trial;
    s->q_prev = s->q_trial;
    set_pin_currents2(d, s->i_trial);
}

static double cap_lte(esk_adev *d, const esk_integ *in)
{
    const cap_state *s = (const cap_state *)d->state;
    return reactive_lte(in, s->i_trial, s->i_prev, s->i_prev2, s->nhist) / s->c;
}

static void cap_observe(esk_adev *d, esk_json *o)
{
    observe_basic(d, o);
    esk_json_set_number(d->isl->a, o, "v", acc_v(d, 0) - acc_v(d, 1));
}

static const esk_adev_ops capacitor_ops = {"capacitor", sizeof(cap_state), cap_setup, cap_load_dc,
                                           cap_load_tr, cap_accept,        NULL,      NULL,
                                           cap_lte,     cap_observe,       NULL};

/* Wired instrument terminals: a finite resistance in parallel with an optional
 * capacitance. Reuse the capacitor's integration/history and LTE control, so a
 * scope input participates in accepted/rejected steps exactly as a capacitor.
 * The first member must remain cap_state for those callbacks. */
typedef struct instrument_state {
    cap_state cap;
    double r;
} instrument_state;

static es_status instrument_setup(esk_adev *d)
{
    instrument_state *s = (instrument_state *)d->state;
    s->r = pd(d, "r", 1e7);
    s->cap.c = pd(d, "c", 0.0);
    if (d->nnodes != 2 || !isfinite(s->r) || !(s->r > 0.0) ||
        !isfinite(s->cap.c) || s->cap.c < 0.0) {
        param_error(d, "instrument impedance", "requires two terminals, finite r > 0 and c >= 0");
        return ES_EINVAL;
    }
    return ES_OK;
}

static void instrument_load_dc(esk_adev *d, esk_amatrix *m)
{
    const instrument_state *s = (const instrument_state *)d->state;
    cap_load_dc(d, m);
    esk_am_stamp_g(m, d->node[0], d->node[1], 1.0 / s->r);
}

static void instrument_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    const instrument_state *s = (const instrument_state *)d->state;
    if (s->cap.c > 0.0)
        cap_load_tr(d, m, in);
    esk_am_stamp_g(m, d->node[0], d->node[1], 1.0 / s->r);
}

static void instrument_accept(esk_adev *d, es_time_t t)
{
    const instrument_state *s = (const instrument_state *)d->state;
    cap_accept(d, t);
    set_pin_currents2(d, d->i_last[0] + (acc_v(d, 0) - acc_v(d, 1)) / s->r);
}

static double instrument_lte(esk_adev *d, const esk_integ *in)
{
    return ((const instrument_state *)d->state)->cap.c > 0.0 ? cap_lte(d, in) : 0.0;
}

static void instrument_observe(esk_adev *d, esk_json *o)
{
    observe_basic(d, o);
    esk_json_set_number(d->isl->a, o, "voltage", acc_v(d, 0) - acc_v(d, 1));
    esk_json_set_number(d->isl->a, o, "current", d->i_last[0]);
}

static const esk_adev_ops instrument_ops = {
    "instrument_input", sizeof(instrument_state), instrument_setup, instrument_load_dc,
    instrument_load_tr, instrument_accept, NULL, NULL, instrument_lte, instrument_observe, NULL};

/* ---- inductor ------------------------------------------------------------------- */

typedef struct ind_state {
    double l, dcr;
    int has_ic;
    double ic;
    double phi_prev, v_prev, v_prev2; /* flux and inductor voltage history */
    double phi_trial, v_trial, i_trial;
    int nhist, trial_first, trial_negligible;
} ind_state;

static es_status ind_setup(esk_adev *d)
{
    ind_state *s = (ind_state *)d->state;
    s->l = pd(d, "l", 1e-3);
    if (!(s->l > 0.0)) {
        param_error(d, "l", "must be > 0");
        s->l = 1e-3;
    }
    s->dcr = pd(d, "dcr", 0.0);
    s->has_ic = has_param(d, "ic");
    s->ic = pd(d, "ic", 0.0);
    d->nbranch = 1;
    return ES_OK;
}

static void ind_load_dc(esk_adev *d, esk_amatrix *m)
{
    ind_state *s = (ind_state *)d->state;
    int b = d->branch[0], n1 = d->node[0], n2 = d->node[1];
    double ib = esk_am_x(m, b);
    esk_am_stamp_j(m, n1, b, 1.0);
    esk_am_stamp_j(m, n2, b, -1.0);
    esk_am_stamp_f(m, n1, ib);
    esk_am_stamp_f(m, n2, -ib);
    if (s->has_ic) {
        esk_am_stamp_j(m, b, b, 1.0);
        esk_am_stamp_f(m, b, ib - s->ic * esk_am_source_scale(m));
    } else {
        esk_am_stamp_j(m, b, n1, 1.0);
        esk_am_stamp_j(m, b, n2, -1.0);
        esk_am_stamp_j(m, b, b, -s->dcr);
        esk_am_stamp_f(m, b, esk_am_v(m, n1, n2) - s->dcr * ib);
    }
    s->i_trial = ib;
    s->phi_trial = s->l * ib;
    s->v_trial = 0.0;
    s->trial_first = 1;
    s->trial_negligible = 0;
}

static void ind_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    ind_state *s = (ind_state *)d->state;
    int b = d->branch[0], n1 = d->node[0], n2 = d->node[1];
    double ib = esk_am_x(m, b);
    double phi = s->l * ib, req, vl;
    esk_integ_companion(in, phi, s->phi_prev, s->v_prev, s->l, &req, &vl);
    esk_am_stamp_j(m, n1, b, 1.0);
    esk_am_stamp_j(m, n2, b, -1.0);
    esk_am_stamp_f(m, n1, ib);
    esk_am_stamp_f(m, n2, -ib);
    esk_am_stamp_j(m, b, n1, 1.0);
    esk_am_stamp_j(m, b, n2, -1.0);
    esk_am_stamp_j(m, b, b, -(req + s->dcr));
    esk_am_stamp_f(m, b, esk_am_v(m, n1, n2) - vl - s->dcr * ib);
    s->i_trial = ib;
    s->phi_trial = phi;
    s->v_trial = vl;
    s->trial_first = esk_integ_first_order(in);
    s->trial_negligible = esk_integ_negligible(in);
}

static void ind_accept(esk_adev *d, es_time_t t)
{
    ind_state *s = (ind_state *)d->state;
    ES_UNUSED(t);
    if (s->trial_first) {
        s->nhist = 1;
        s->v_prev2 = s->v_prev;
    } else if (s->trial_negligible && s->nhist >= 1) {
        /* replace the newest point: the spacing to v_prev2 stays usable */
    } else {
        if (s->nhist < 2)
            s->nhist++;
        s->v_prev2 = s->v_prev;
    }
    s->v_prev = s->v_trial;
    s->phi_prev = s->phi_trial;
    set_pin_currents2(d, acc_x(d, d->branch[0]));
}

static double ind_lte(esk_adev *d, const esk_integ *in)
{
    const ind_state *s = (const ind_state *)d->state;
    return reactive_lte(in, s->v_trial, s->v_prev, s->v_prev2, s->nhist) / (s->l * IND_LTE_SCALE_A);
}

static const esk_adev_ops inductor_ops = {"inductor",  sizeof(ind_state), ind_setup, ind_load_dc,
                                          ind_load_tr, ind_accept,        NULL,      NULL,
                                          ind_lte,     observe_basic,     NULL};

/* ---- independent sources --------------------------------------------------------- */

enum { SRC_DC, SRC_PULSE, SRC_SINE, SRC_PWL, SRC_EXP };

typedef struct src_state {
    int kind;
    double dc;
    double v1, v2, td, tr, tf, pw, per; /* pulse */
    double vo, va, freq, phase, theta;  /* sine */
    double td1, tau1, td2, tau2;        /* exp */
    double value_trial, rout;
} src_state;

static void src_setup(esk_adev *d, src_state *s, const char *dc_name)
{
    const char *kind = ps(d, "kind", "dc");
    s->kind = SRC_DC;
    if (!strcmp(kind, "pulse"))
        s->kind = SRC_PULSE;
    else if (!strcmp(kind, "sine") || !strcmp(kind, "sin"))
        s->kind = SRC_SINE;
    else if (!strcmp(kind, "pwl"))
        s->kind = SRC_PWL;
    else if (!strcmp(kind, "exp"))
        s->kind = SRC_EXP;
    else if (strcmp(kind, "dc") != 0)
        param_error(d, "kind", "unknown source kind");
    s->dc = pd(d, dc_name, pd(d, "value", pd(d, "dc", 0.0)));
    s->v1 = pd(d, "v1", pd(d, "i1", 0.0));
    s->v2 = pd(d, "v2", pd(d, "i2", s->dc));
    s->td = pd(d, "td", 0.0);
    s->tr = pd(d, "tr", 1e-9);
    s->tf = pd(d, "tf", 1e-9);
    s->pw = pd(d, "pw", 1e-3);
    s->per = pd(d, "per", 2.0 * (s->tr + s->pw + s->tf));
    s->vo = pd(d, "vo", pd(d, "io", 0.0));
    s->va = pd(d, "va", pd(d, "ia", s->dc));
    s->freq = pd(d, "freq", 1e3);
    s->phase = pd(d, "phase", 0.0);
    s->theta = pd(d, "theta", 0.0);
    s->td1 = pd(d, "td1", 0.0);
    s->tau1 = pd(d, "tau1", 1e-6);
    s->td2 = pd(d, "td2", s->td1 + 5.0 * s->tau1);
    s->tau2 = pd(d, "tau2", 1e-6);
    if (s->tr < 0.0 || s->tf < 0.0 || s->pw < 0.0 || s->per <= 0.0)
        param_error(d, "pulse", "tr, tf, pw must be >= 0 and per > 0");
    if (s->freq <= 0.0)
        param_error(d, "freq", "must be > 0");
    if (s->tau1 <= 0.0 || s->tau2 <= 0.0)
        param_error(d, "tau", "must be > 0");
}

/* PWL points from params "points": [[t, v], ...] or [t0, v0, t1, v1, ...] */
static int pwl_point(const esk_adev *d, size_t k, double *t, double *v)
{
    const esk_json *pts = d->comp ? esk_json_get(d->comp->params, "points") : NULL;
    const esk_json *first = esk_json_at(pts, 0);
    if (!pts)
        return 0;
    if (first && first->type == ESK_JSON_ARRAY) {
        const esk_json *pair = esk_json_at(pts, k);
        if (!pair || esk_json_len(pair) < 2)
            return 0;
        return esk_json_to_double(esk_json_at(pair, 0), t) == 0 &&
               esk_json_to_double(esk_json_at(pair, 1), v) == 0;
    }
    if (2 * k + 1 >= esk_json_len(pts))
        return 0;
    return esk_json_to_double(esk_json_at(pts, 2 * k), t) == 0 &&
           esk_json_to_double(esk_json_at(pts, 2 * k + 1), v) == 0;
}

#define PWL_MAX_POINTS 100000

static double src_value(const esk_adev *d, const src_state *s, double t)
{
    switch (s->kind) {
    case SRC_PULSE: {
        double tt, edge;
        if (t < s->td)
            return s->v1;
        tt = t - s->td;
        if (s->per > 0.0)
            tt = fmod(tt, s->per);
        if (tt < s->tr)
            return s->tr > 0.0 ? s->v1 + (s->v2 - s->v1) * tt / s->tr : s->v2;
        edge = s->tr + s->pw;
        if (tt < edge)
            return s->v2;
        if (tt < edge + s->tf)
            return s->tf > 0.0 ? s->v2 + (s->v1 - s->v2) * (tt - edge) / s->tf : s->v1;
        return s->v1;
    }
    case SRC_SINE: {
        double arg, damp = 1.0;
        if (t < s->td)
            return s->vo + s->va * sin(s->phase * PI_D / 180.0);
        arg = 2.0 * PI_D * s->freq * (t - s->td) + s->phase * PI_D / 180.0;
        if (s->theta > 0.0)
            damp = exp(-(t - s->td) * s->theta);
        return s->vo + s->va * damp * sin(arg);
    }
    case SRC_PWL: {
        double t0 = 0.0, v0 = 0.0, t1, v1;
        size_t k;
        if (!pwl_point(d, 0, &t0, &v0))
            return s->dc;
        if (t <= t0)
            return v0;
        for (k = 1; k < PWL_MAX_POINTS && pwl_point(d, k, &t1, &v1); k++) {
            if (t <= t1)
                return t1 > t0 ? v0 + (v1 - v0) * (t - t0) / (t1 - t0) : v1;
            t0 = t1;
            v0 = v1;
        }
        return v0;
    }
    case SRC_EXP: {
        double v = s->v1;
        if (t >= s->td1)
            v += (s->v2 - s->v1) * (1.0 - exp(-(t - s->td1) / s->tau1));
        if (t >= s->td2)
            v += (s->v1 - s->v2) * (1.0 - exp(-(t - s->td2) / s->tau2));
        return v;
    }
    default:
        return s->dc;
    }
}

static int src_breakpoint(const esk_adev *d, const src_state *s, es_time_t after, es_time_t *at)
{
    double after_s = ps_to_s(after);
    switch (s->kind) {
    case SRC_PULSE: {
        double base, rel, k, cand[8];
        int i;
        es_time_t best = ES_TIME_MAX;
        if (after_s < s->td) {
            best = s_to_ps(s->td);
            if (best <= after)
                best = after + 1;
            *at = best;
            return 1;
        }
        rel = after_s - s->td;
        k = s->per > 0.0 ? floor(rel / s->per) : 0.0;
        base = s->td + k * s->per;
        cand[0] = base;
        cand[1] = base + s->tr;
        cand[2] = base + s->tr + s->pw;
        cand[3] = base + s->tr + s->pw + s->tf;
        base += s->per;
        cand[4] = base;
        cand[5] = base + s->tr;
        cand[6] = base + s->tr + s->pw;
        cand[7] = base + s->tr + s->pw + s->tf;
        for (i = 0; i < 8; i++) {
            es_time_t c = s_to_ps(cand[i]);
            if (c > after && c < best)
                best = c;
        }
        if (best == ES_TIME_MAX)
            return 0;
        *at = best;
        return 1;
    }
    case SRC_SINE: {
        /* not a discontinuity: a sampling bound of 16 points per period */
        double step = 1.0 / (16.0 * s->freq), t0 = s->td, n;
        es_time_t c;
        if (after_s < t0) {
            *at = s_to_ps(t0);
            return *at > after;
        }
        n = floor((after_s - t0) / step) + 1.0;
        c = s_to_ps(t0 + n * step);
        if (c <= after)
            c = after + 1;
        *at = c;
        return 1;
    }
    case SRC_PWL: {
        double t, v;
        size_t k;
        for (k = 0; k < PWL_MAX_POINTS && pwl_point(d, k, &t, &v); k++) {
            es_time_t c = s_to_ps(t);
            if (c > after) {
                *at = c;
                return 1;
            }
        }
        return 0;
    }
    case SRC_EXP: {
        es_time_t c1 = s_to_ps(s->td1), c2 = s_to_ps(s->td2);
        if (c1 > after && c1 <= c2) {
            *at = c1;
            return 1;
        }
        if (c2 > after) {
            *at = c2;
            return 1;
        }
        return 0;
    }
    default:
        return 0;
    }
}

static es_status vsrc_setup(esk_adev *d)
{
    src_state *s = (src_state *)d->state;
    src_setup(d, s, "v");
    s->rout = pd(d, "rout", 0.0);
    if (!isfinite(s->rout) || s->rout < 0.0) {
        param_error(d, "rout", "must be finite and >= 0");
        return ES_EINVAL;
    }
    d->nbranch = 1;
    return ES_OK;
}

static void vsrc_stamp(esk_adev *d, esk_amatrix *m, double t)
{
    src_state *s = (src_state *)d->state;
    double v = src_value(d, s, t) * esk_am_source_scale(m);
    s->value_trial = v;
    stamp_vsrc(m, d->node[0], d->node[1], d->branch[0], v, s->rout);
}

static void vsrc_load_dc(esk_adev *d, esk_amatrix *m)
{
    vsrc_stamp(d, m, ps_to_s(d->isl->t_accepted));
}
static void vsrc_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    vsrc_stamp(d, m, ps_to_s(esk_integ_t(in)));
}

static void vsrc_accept(esk_adev *d, es_time_t t)
{
    ES_UNUSED(t);
    set_pin_currents2(d, acc_x(d, d->branch[0]));
}

static int vsrc_breakpoint(esk_adev *d, es_time_t after, es_time_t *at)
{
    return src_breakpoint(d, (const src_state *)d->state, after, at);
}

static void src_observe(esk_adev *d, esk_json *o)
{
    const src_state *s = (const src_state *)d->state;
    observe_basic(d, o);
    esk_json_set_number(d->isl->a, o, "value", s->value_trial);
    esk_json_set_number(d->isl->a, o, "voltage", acc_v(d, 0) - acc_v(d, 1));
}

static const esk_adev_ops vsource_ops = {
    "vsource", sizeof(src_state), vsrc_setup, vsrc_load_dc, vsrc_load_tr, vsrc_accept,
    NULL,      vsrc_breakpoint,   NULL,       src_observe,  NULL};

static es_status isrc_setup(esk_adev *d)
{
    src_setup(d, (src_state *)d->state, "i");
    return ES_OK;
}

static void isrc_stamp(esk_adev *d, esk_amatrix *m, double t)
{
    src_state *s = (src_state *)d->state;
    double i = src_value(d, s, t);
    s->value_trial = i * esk_am_source_scale(m);
    stamp_src_i(m, d->node[0], d->node[1], i);
}

static void isrc_load_dc(esk_adev *d, esk_amatrix *m)
{
    isrc_stamp(d, m, ps_to_s(d->isl->t_accepted));
}
static void isrc_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    isrc_stamp(d, m, ps_to_s(esk_integ_t(in)));
}

static void isrc_accept(esk_adev *d, es_time_t t)
{
    const src_state *s = (const src_state *)d->state;
    ES_UNUSED(t);
    set_pin_currents2(d, s->value_trial);
}

static const esk_adev_ops isource_ops = {
    "isource", sizeof(src_state), isrc_setup, isrc_load_dc, isrc_load_tr, isrc_accept,
    NULL,      vsrc_breakpoint,   NULL,       src_observe,  NULL};

/* ---- switch ------------------------------------------------------------------------ */

#define SW_BOUNCE_TRANSITIONS 6

typedef struct sw_state {
    double gon, goff;
    int seen;          /* the settled input */
    int changing, raw; /* a change is in progress (bouncing) */
    es_time_t change_t;
    int bounce_count;
    es_time_t bounce_interval;
    double g_trial;
} sw_state;

static int sw_input(const esk_adev *d)
{
    int closed = 0;
    if (!d->comp)
        return 0;
    if (esk_comp_input_bool(d->comp, "pressed", 0) || esk_comp_input_bool(d->comp, "closed", 0) ||
        esk_comp_input_bool(d->comp, "state", 0))
        closed = 1;
    if (!esk_json_get(d->comp->inputs, "pressed") && !esk_json_get(d->comp->inputs, "closed") &&
        !esk_json_get(d->comp->inputs, "state")) {
        int v = 0;
        if (esk_comp_param_bool(d->comp, "closed", &v) == 0 ||
            esk_comp_param_bool(d->comp, "state", &v) == 0)
            closed = v;
    }
    return closed;
}

static es_status sw_setup(esk_adev *d)
{
    sw_state *s = (sw_state *)d->state;
    double ron = pd(d, "ron", 1e-3), roff = pd(d, "roff", 1e9);
    const esk_json *bounce = d->comp ? esk_json_get(d->comp->params, "bounce") : NULL;
    if (!(ron > 0.0) || !(roff > 0.0)) {
        param_error(d, "ron/roff", "must be > 0");
        ron = 1e-3;
        roff = 1e9;
    }
    s->gon = 1.0 / ron;
    s->goff = 1.0 / roff;
    s->seen = s->raw = sw_input(d);
    s->changing = 0;
    if (bounce && bounce->type == ESK_JSON_OBJECT) {
        double iv = esk_json_get_num(bounce, "interval", 0.0);
        const esk_json *ivj = esk_json_get(bounce, "interval");
        s->bounce_count = (int)esk_json_get_int(bounce, "count", 0);
        if (ivj && ivj->type == ESK_JSON_STRING) {
            es_time_t ps_v;
            if (esk_json_to_time(ivj, &ps_v) == 0)
                iv = ps_to_s(ps_v);
            else if (esk_json_to_double(ivj, &iv) != 0)
                iv = 0.0;
        }
        s->bounce_interval = s_to_ps(iv);
        if (s->bounce_count < 0 || s->bounce_count > 1000)
            s->bounce_count = 0;
    } else if (bounce && bounce->type == ESK_JSON_BOOL && bounce->u.boolean) {
        /* a component's `bounce: true` with `bounce_ms`: the contacts chatter
         * through six transitions spread evenly over that time */
        double ms = pd(d, "bounce_ms", 2.0);
        s->bounce_count = SW_BOUNCE_TRANSITIONS;
        s->bounce_interval = ms > 0.0 ? s_to_ps(ms * 1e-3 / SW_BOUNCE_TRANSITIONS) : 0;
    }
    return ES_OK;
}

static int sw_effective(sw_state *s, const esk_adev *d, es_time_t t)
{
    int raw = sw_input(d);
    if (raw != s->seen && !s->changing) {
        s->changing = 1;
        s->raw = raw;
        s->change_t = d->isl->t_accepted;
    } else if (s->changing) {
        s->raw = raw;
    }
    if (!s->changing)
        return s->seen;
    if (s->bounce_count > 0 && s->bounce_interval > 0 && t > s->change_t) {
        es_time_t k = (t - s->change_t - 1) / s->bounce_interval;
        if (k < (es_time_t)s->bounce_count)
            return (k % 2 == 0) ? s->raw : s->seen;
    }
    return s->raw;
}

static void sw_stamp(esk_adev *d, esk_amatrix *m, es_time_t t)
{
    sw_state *s = (sw_state *)d->state;
    s->g_trial = sw_effective(s, d, t) ? s->gon : s->goff;
    esk_am_stamp_g(m, d->node[0], d->node[1], s->g_trial);
}

static void sw_load_dc(esk_adev *d, esk_amatrix *m)
{
    sw_stamp(d, m, d->isl->t_accepted);
}
static void sw_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    sw_stamp(d, m, esk_integ_t(in));
}

static void sw_accept(esk_adev *d, es_time_t t)
{
    sw_state *s = (sw_state *)d->state;
    if (s->changing) {
        es_time_t settle = s->change_t + (es_time_t)s->bounce_count * s->bounce_interval;
        if (s->bounce_count == 0 || s->bounce_interval == 0 || t >= settle) {
            s->seen = s->raw;
            s->changing = 0;
        }
    }
    set_pin_currents2(d, s->g_trial * (acc_v(d, 0) - acc_v(d, 1)));
}

static int sw_breakpoint(esk_adev *d, es_time_t after, es_time_t *at)
{
    const sw_state *s = (const sw_state *)d->state;
    es_time_t k;
    if (!s->changing || s->bounce_count <= 0 || s->bounce_interval == 0)
        return 0;
    for (k = 1; k <= (es_time_t)s->bounce_count; k++) {
        es_time_t c = s->change_t + k * s->bounce_interval;
        if (c > after) {
            *at = c;
            return 1;
        }
    }
    return 0;
}

static void sw_observe(esk_adev *d, esk_json *o)
{
    const sw_state *s = (const sw_state *)d->state;
    observe_basic(d, o);
    esk_json_set_bool(d->isl->a, o, "closed", s->g_trial == s->gon);
}

static const esk_adev_ops switch_ops = {"switch",   sizeof(sw_state), sw_setup, sw_load_dc,
                                        sw_load_tr, sw_accept,        NULL,     sw_breakpoint,
                                        NULL,       sw_observe,       NULL};

/* ---- potentiometer --------------------------------------------------------------------- */

typedef struct pot_state {
    double r;
    int log_taper;
    double g1, g2;
} pot_state;

static es_status pot_setup(esk_adev *d)
{
    pot_state *s = (pot_state *)d->state;
    s->r = pd(d, "r", 10e3);
    if (!(s->r > 0.0)) {
        param_error(d, "r", "must be > 0");
        s->r = 10e3;
    }
    s->log_taper = !strcmp(ps(d, "taper", "linear"), "log");
    return ES_OK;
}

static void pot_load(esk_adev *d, esk_amatrix *m)
{
    pot_state *s = (pot_state *)d->state;
    double pos = d->comp ? esk_comp_input_double(d->comp, "position", pd(d, "position", 0.5)) : 0.5;
    double r1, r2, rmin = 1e-3;
    if (pos < 0.0)
        pos = 0.0;
    if (pos > 1.0)
        pos = 1.0;
    if (s->log_taper)
        pos = (pow(10.0, pos) - 1.0) / 9.0;
    r1 = s->r * pos;
    r2 = s->r * (1.0 - pos);
    if (r1 < rmin)
        r1 = rmin;
    if (r2 < rmin)
        r2 = rmin;
    s->g1 = 1.0 / r1;
    s->g2 = 1.0 / r2;
    esk_am_stamp_g(m, d->node[0], d->node[1], s->g1);
    esk_am_stamp_g(m, d->node[1], d->node[2], s->g2);
}

static void pot_accept(esk_adev *d, es_time_t t)
{
    const pot_state *s = (const pot_state *)d->state;
    double i1 = s->g1 * (acc_v(d, 0) - acc_v(d, 1)), i2 = s->g2 * (acc_v(d, 1) - acc_v(d, 2));
    ES_UNUSED(t);
    d->i_last[0] = i1;
    d->i_last[1] = i2 - i1;
    d->i_last[2] = -i2;
}

static void pot_observe(esk_adev *d, esk_json *out)
{
    double pos = d->comp ? esk_comp_input_double(d->comp, "position", pd(d, "position", 0.5)) : 0.5;
    observe_basic(d, out);
    esk_json_set_number(d->isl->a, out, "position", fmax(0.0, fmin(1.0, pos)));
}

static const esk_adev_ops potentiometer_ops = {"potentiometer",
                                               sizeof(pot_state),
                                               pot_setup,
                                               pot_load,
                                               NULL,
                                               pot_accept,
                                               NULL,
                                               NULL,
                                               NULL,
                                               pot_observe,
                                               NULL};

/* ---- diode, led, zener ------------------------------------------------------------------ */

enum { JN_DIODE, JN_LED, JN_ZENER };

typedef struct diode_state {
    int kind;
    double is, nvt, rs, bv, vcrit, if_nom, vf;
    int has_bv;
    double vd_old, vr_old; /* limiting history */
    double id_trial, vd_trial;
    double emission_s, emission_level;
    es_time_t emission_at;
} diode_state;

/* Integral of illustrative normalized emission over accepted electrical time.
 * A renderer can measure exposure between observations without aliasing PWM.
 * Rejected Newton/timestep trials never enter this history. */
static void emission_accept(diode_state *s, es_time_t t)
{
    if (t > s->emission_at)
        s->emission_s += s->emission_level * ps_to_s(t - s->emission_at);
    s->emission_at = t;
    s->emission_level = fmax(0.0, fmin(1.0, s->id_trial / s->if_nom));
}

static double emission_integral(const esk_adev *d, const diode_state *s)
{
    es_time_t now = d->isl->e->sched.now;
    return s->emission_s + (now > s->emission_at ? s->emission_level * ps_to_s(now - s->emission_at) : 0.0);
}

static void diode_common_setup(esk_adev *d, diode_state *s, double is_def, double n_def)
{
    s->is = pd(d, "is", is_def);
    s->nvt = pd(d, "n", n_def) * VT_27C;
    s->rs = pd(d, "rs", 0.0);
    s->has_bv = has_param(d, "bv");
    s->bv = pd(d, "bv", 0.0);
    if (!(s->is > 0.0)) {
        param_error(d, "is", "must be > 0");
        s->is = is_def;
    }
    if (!(s->nvt > 0.0)) {
        param_error(d, "n", "must be > 0");
        s->nvt = VT_27C;
    }
    if (s->rs < 0.0) {
        param_error(d, "rs", "must be >= 0");
        s->rs = 0.0;
    }
    s->vcrit = vcrit_of(s->is, s->nvt);
    if (s->has_bv) {
        /* as in SPICE, `bv` is the voltage at the breakdown current `ibv` (a
         * zener's `izt`), so the knee of the exponential is moved to match */
        double ibv = pd(d, "ibv", pd(d, "izt", s->kind == JN_ZENER ? 5e-3 : 1e-3));
        if (ibv > 0.0)
            s->bv -= s->nvt * log(ibv / s->is + 1.0);
        if (s->bv < 0.0)
            s->bv = 0.0;
    }
    d->nbranch = s->rs > 0.0 ? 1 : 0; /* the internal anode node A' */
}

static es_status diode_setup(esk_adev *d)
{
    diode_state *s = (diode_state *)d->state;
    s->kind = JN_DIODE;
    diode_common_setup(d, s, 1e-14, 1.0);
    return ES_OK;
}

static es_status led_setup(esk_adev *d)
{
    diode_state *s = (diode_state *)d->state;
    const char *color = ps(d, "color", "red");
    double vf = 1.8, n = 2.0;
    s->kind = JN_LED;
    if (!strcmp(color, "green"))
        vf = 2.1;
    else if (!strcmp(color, "blue") || !strcmp(color, "white")) {
        vf = 2.9;
        n = 3.0;
    } else if (!strcmp(color, "yellow"))
        vf = 2.0;
    s->if_nom = pd(d, "if_nom", 0.02);
    s->vf = pd(d, "vf", vf);
    n = pd(d, "n", n);
    if (!(s->if_nom > 0.0)) {
        param_error(d, "if_nom", "must be > 0");
        s->if_nom = 0.02;
    }
    /* is fitted so that the junction drops vf at if_nom unless given */
    diode_common_setup(d, s, s->if_nom / (exp(s->vf / (n * VT_27C)) - 1.0), n);
    return ES_OK;
}

static es_status zener_setup(esk_adev *d)
{
    diode_state *s = (diode_state *)d->state;
    double vz = pd(d, "vz", 5.1);
    s->kind = JN_ZENER;
    if (!(vz > 0.0)) {
        param_error(d, "vz", "must be > 0");
        vz = 5.1;
    }
    diode_common_setup(d, s, 1e-14, 1.0);
    if (!s->has_bv) {
        double izt = pd(d, "izt", 5e-3);
        s->has_bv = 1;
        s->bv = vz - (izt > 0.0 ? s->nvt * log(izt / s->is + 1.0) : 0.0);
        if (s->bv < 0.0)
            s->bv = 0.0;
    }
    /* rz as the series resistance when given and rs is not */
    if (!has_param(d, "rs") && has_param(d, "rz")) {
        s->rs = pd(d, "rz", 0.0);
        d->nbranch = s->rs > 0.0 ? 1 : 0;
    }
    return ES_OK;
}

/* the junction between nodes a and k (a = A' when rs > 0) */
static void junction_stamp(esk_adev *d, esk_amatrix *m, diode_state *s)
{
    int a = s->rs > 0.0 ? d->branch[0] : d->node[0], k = d->node[1];
    double vd = esk_am_v(m, a, k), vlim, id, gd;
    if (s->has_bv && vd < -s->bv) {
        /* breakdown: a forward junction in the reverse direction */
        double vr = -(vd + s->bv), vr_lim = pnjlim(vr, s->vr_old, s->nvt, s->vcrit), idr, gdr;
        s->vr_old = vr_lim;
        pn_eval(vr_lim, s->is, s->nvt, &idr, &gdr);
        id = -(idr + s->is * s->bv / s->nvt);
        gd = gdr;
        vlim = -s->bv - vr_lim;
        s->vd_old = vlim;
        if (fabs(vr_lim - vr) > 1e-12)
            esk_am_noncon(m);
    } else {
        vlim = pnjlim(vd, s->vd_old, s->nvt, s->vcrit);
        s->vd_old = vlim;
        if (fabs(vlim - vd) > 1e-12)
            esk_am_noncon(m);
        pn_eval(vlim, s->is, s->nvt, &id, &gd);
        gd += 1e-12;
        s->vr_old = 0.0;
    }
    stamp_nl(m, a, k, gd, id, vlim);
    if (s->rs > 0.0)
        esk_am_stamp_g(m, d->node[0], a, 1.0 / s->rs);
    s->id_trial = id + gd * (vd - vlim);
    s->vd_trial = vd;
}

static void diode_load(esk_adev *d, esk_amatrix *m)
{
    junction_stamp(d, m, (diode_state *)d->state);
}

static void diode_accept(esk_adev *d, es_time_t t)
{
    diode_state *s = (diode_state *)d->state;
    if (s->kind == JN_LED)
        emission_accept(s, t);
    set_pin_currents2(d, s->id_trial);
}

static void diode_observe(esk_adev *d, esk_json *o)
{
    const diode_state *s = (const diode_state *)d->state;
    esk_alloc *a = d->isl->a;
    observe_basic(d, o);
    esk_json_set_number(a, o, "vd", acc_v(d, 0) - acc_v(d, 1));
    esk_json_set_number(a, o, "current", s->id_trial);
    if (s->kind == JN_LED) {
        double lum = s->id_trial / s->if_nom;
        if (lum < 0.0)
            lum = 0.0;
        if (lum > 1.0)
            lum = 1.0;
        esk_json_set_number(a, o, "luminance", lum);
        esk_json_set_bool(a, o, "lit", s->id_trial > 1e-6);
        esk_json_set_number(a, o, "emission_integral", emission_integral(d, s));
    }
}

static const esk_adev_ops diode_ops = {"diode", sizeof(diode_state), diode_setup, diode_load,
                                       NULL,    diode_accept,        NULL,        NULL,
                                       NULL,    diode_observe,       NULL};
static const esk_adev_ops led_ops = {"led", sizeof(diode_state), led_setup, diode_load,
                                     NULL,  diode_accept,        NULL,      NULL,
                                     NULL,  diode_observe,       NULL};
static const esk_adev_ops zener_ops = {"zener", sizeof(diode_state), zener_setup, diode_load,
                                       NULL,    diode_accept,        NULL,        NULL,
                                       NULL,    diode_observe,       NULL};

/* Three independent LED junctions sharing a cathode or anode. Each die
 * stamps the same Shockley/series-resistance equations as the discrete LED. */
typedef struct rgb_state {
    diode_state die[3];
    int branch_slot[3];
    int anode;
} rgb_state;

static es_status rgb_setup(esk_adev *d)
{
    static const char *const keys[] = {"red_vf", "green_vf", "blue_vf"};
    static const double defaults[] = {1.8, 2.1, 2.9};
    static const char *const names[] = {"red", "green", "blue"};
    rgb_state *s = (rgb_state *)d->state;
    int i;
    s->anode = !strcmp(ps(d, "common", "cathode"), "anode");
    d->nbranch = 0;
    for (i = 0; i < 3; i++) {
        esk_adev die = *d;
        char key[32];
        double vf = pd(d, keys[i], defaults[i]), n, nominal, rs;
        snprintf(key, sizeof key, "%s_n", names[i]);
        n = pd(d, key, pd(d, "n", i == 2 ? 3.0 : 2.0));
        snprintf(key, sizeof key, "%s_if_nom", names[i]);
        nominal = pd(d, key, pd(d, "if_nom", 0.02));
        snprintf(key, sizeof key, "%s_rs", names[i]);
        rs = pd(d, key, pd(d, "rs", 0.0));
        if (!isfinite(vf) || !isfinite(n) || !isfinite(nominal) || !isfinite(rs) ||
            !(vf > 0.0) || !(n > 0.0) || !(nominal > 0.0) || rs < 0.0 || vf / (n * VT_27C) > 700) {
            param_error(d, keys[i], "finite positive forward voltage, ideality and current, and nonnegative series resistance required");
            return ES_ESCHEMA;
        }
        s->die[i].kind = JN_LED;
        s->die[i].if_nom = nominal;
        s->die[i].vf = vf;
        diode_common_setup(&die, &s->die[i], nominal / expm1(vf / (n * VT_27C)), n);
        s->die[i].nvt = n * VT_27C;
        s->die[i].rs = rs;
        s->die[i].vcrit = vcrit_of(s->die[i].is, s->die[i].nvt);
        s->branch_slot[i] = rs > 0.0 ? d->nbranch++ : -1;
    }
    return ES_OK;
}

static void rgb_load(esk_adev *d, esk_amatrix *m)
{
    static const int pins[] = {0, 2, 3};
    rgb_state *s = (rgb_state *)d->state;
    int i;
    for (i = 0; i < 3; i++) {
        esk_adev die = *d;
        die.node[0] = d->node[s->anode ? 1 : pins[i]];
        die.node[1] = d->node[s->anode ? pins[i] : 1];
        die.branch[0] = s->branch_slot[i] >= 0 ? d->branch[s->branch_slot[i]] : -1;
        junction_stamp(&die, m, &s->die[i]);
    }
}

static void rgb_accept(esk_adev *d, es_time_t t)
{
    static const int pins[] = {0, 2, 3};
    rgb_state *s = (rgb_state *)d->state;
    int i;
    d->i_last[1] = 0.0;
    for (i = 0; i < 3; i++) {
        double current = s->die[i].id_trial * (s->anode ? -1.0 : 1.0);
        emission_accept(&s->die[i], t);
        d->i_last[pins[i]] = current;
        d->i_last[1] -= current;
    }
}

static void rgb_observe(esk_adev *d, esk_json *out)
{
    static const char *const names[] = {"red", "green", "blue"};
    rgb_state *s = (rgb_state *)d->state;
    double lum[3], peak = 0.0;
    int i, lit = 0;
    char key[32], color[8];
    observe_basic(d, out);
    for (i = 0; i < 3; i++) {
        double current = s->die[i].id_trial;
        lum[i] = fmax(0.0, fmin(1.0, current / s->die[i].if_nom));
        if (lum[i] > peak) peak = lum[i];
        snprintf(key, sizeof key, "%s_current", names[i]);
        esk_json_set_number(d->isl->a, out, key, current);
        snprintf(key, sizeof key, "%s_luminance", names[i]);
        esk_json_set_number(d->isl->a, out, key, lum[i]);
        snprintf(key, sizeof key, "%s_emission_integral", names[i]);
        esk_json_set_number(d->isl->a, out, key, emission_integral(d, &s->die[i]));
        esk_json_set_bool(d->isl->a, out, names[i], current > 1e-6);
        if (current > 1e-6) lit = 1;
    }
    snprintf(color, sizeof color, "#%02x%02x%02x",
             lit ? (unsigned)(255.0 * lum[0] / peak + 0.5) : 0u,
             lit ? (unsigned)(255.0 * lum[1] / peak + 0.5) : 0u,
             lit ? (unsigned)(255.0 * lum[2] / peak + 0.5) : 0u);
    esk_json_set_string(d->isl->a, out, "color", color);
    esk_json_set_number(d->isl->a, out, "luminance", peak);
    esk_json_set_bool(d->isl->a, out, "lit", lit);
}

static const esk_adev_ops rgb_ops = {"rgb_led", sizeof(rgb_state), rgb_setup, rgb_load,
                                    NULL, rgb_accept, NULL, NULL, NULL, rgb_observe, NULL};

/* ---- bipolar transistors (Ebers-Moll transport model with Early effect) ------------- */

typedef struct bjt_state {
    double pol; /* +1 npn, -1 pnp */
    double is, bf, br, nf_vt, nr_vt, vaf, rb, rc, re;
    double vcrit_f, vcrit_r;
    int has_vaf;
    int ib_idx, ic_idx, ie_idx; /* internal nodes (or the pins) */
    double vbe_old, vbc_old;
    double ic_trial, ib_trial, vbe_trial, vbc_trial;
} bjt_state;

static es_status bjt_setup_common(esk_adev *d, double pol)
{
    bjt_state *s = (bjt_state *)d->state;
    int next = 0;
    s->pol = pol;
    s->is = pd(d, "is", 1e-15);
    s->bf = pd(d, "bf", 100.0);
    s->br = pd(d, "br", 1.0);
    s->nf_vt = pd(d, "nf", 1.0) * VT_27C;
    s->nr_vt = pd(d, "nr", 1.0) * VT_27C;
    s->has_vaf = has_param(d, "vaf");
    s->vaf = pd(d, "vaf", 0.0);
    s->rb = pd(d, "rb", 0.0);
    s->rc = pd(d, "rc", 0.0);
    s->re = pd(d, "re", 0.0);
    if (!(s->is > 0.0) || !(s->bf > 0.0) || !(s->br > 0.0)) {
        param_error(d, "is/bf/br", "must be > 0");
        s->is = 1e-15;
        s->bf = 100.0;
        s->br = 1.0;
    }
    if (s->has_vaf && !(s->vaf > 0.0)) {
        param_error(d, "vaf", "must be > 0");
        s->has_vaf = 0;
    }
    s->vcrit_f = vcrit_of(s->is, s->nf_vt);
    s->vcrit_r = vcrit_of(s->is, s->nr_vt);
    s->ic_idx = s->rc > 0.0 ? next++ : -1;
    s->ib_idx = s->rb > 0.0 ? next++ : -1;
    s->ie_idx = s->re > 0.0 ? next++ : -1;
    d->nbranch = next;
    return ES_OK;
}

static es_status npn_setup(esk_adev *d)
{
    return bjt_setup_common(d, 1.0);
}
static es_status pnp_setup(esk_adev *d)
{
    return bjt_setup_common(d, -1.0);
}

static void bjt_load(esk_adev *d, esk_amatrix *m)
{
    bjt_state *s = (bjt_state *)d->state;
    int c = s->ic_idx >= 0 ? d->branch[s->ic_idx] : d->node[0];
    int b = s->ib_idx >= 0 ? d->branch[s->ib_idx] : d->node[1];
    int e = s->ie_idx >= 0 ? d->branch[s->ie_idx] : d->node[2];
    double p = s->pol;
    double vbe = p * esk_am_v(m, b, e), vbc = p * esk_am_v(m, b, c);
    double vbe_l = pnjlim(vbe, s->vbe_old, s->nf_vt, s->vcrit_f);
    double vbc_l = pnjlim(vbc, s->vbc_old, s->nr_vt, s->vcrit_r);
    double If, gif, Ir, gir, qinv = 1.0, dq_dvbc = 0.0;
    double ic, ib, dic_dvbe, dic_dvbc, dib_dvbe, dib_dvbc;
    double Ic, Ib, Ie;
    double dvbe = vbe - vbe_l, dvbc = vbc - vbc_l;
    s->vbe_old = vbe_l;
    s->vbc_old = vbc_l;
    if (fabs(dvbe) > 1e-12 || fabs(dvbc) > 1e-12)
        esk_am_noncon(m);
    pn_eval(vbe_l, s->is, s->nf_vt, &If, &gif);
    pn_eval(vbc_l, s->is, s->nr_vt, &Ir, &gir);
    gif += 1e-12;
    gir += 1e-12;
    if (s->has_vaf) {
        qinv = 1.0 - vbc_l / s->vaf;
        if (qinv < 0.1)
            qinv = 0.1;
        else
            dq_dvbc = -1.0 / s->vaf;
    }
    ic = (If - Ir) * qinv - Ir / s->br;
    ib = If / s->bf + Ir / s->br;
    dic_dvbe = gif * qinv;
    dic_dvbc = -gir * qinv + (If - Ir) * dq_dvbc - gir / s->br;
    dib_dvbe = gif / s->bf;
    dib_dvbc = gir / s->br;
    /* terminal currents into the device, linearised about the limited voltages */
    Ic = p * (ic + dic_dvbe * dvbe + dic_dvbc * dvbc);
    Ib = p * (ib + dib_dvbe * dvbe + dib_dvbc * dvbc);
    Ie = -(Ic + Ib);
    /* d(I)/d(vb), d(I)/d(vc), d(I)/d(ve): vbe = p(vb - ve), vbc = p(vb - vc) */
    esk_am_stamp_j(m, c, b, dic_dvbe + dic_dvbc);
    esk_am_stamp_j(m, c, c, -dic_dvbc);
    esk_am_stamp_j(m, c, e, -dic_dvbe);
    esk_am_stamp_j(m, b, b, dib_dvbe + dib_dvbc);
    esk_am_stamp_j(m, b, c, -dib_dvbc);
    esk_am_stamp_j(m, b, e, -dib_dvbe);
    esk_am_stamp_j(m, e, b, -(dic_dvbe + dic_dvbc + dib_dvbe + dib_dvbc));
    esk_am_stamp_j(m, e, c, dic_dvbc + dib_dvbc);
    esk_am_stamp_j(m, e, e, dic_dvbe + dib_dvbe);
    esk_am_stamp_f(m, c, Ic);
    esk_am_stamp_f(m, b, Ib);
    esk_am_stamp_f(m, e, Ie);
    if (s->ic_idx >= 0)
        esk_am_stamp_g(m, d->node[0], c, 1.0 / s->rc);
    if (s->ib_idx >= 0)
        esk_am_stamp_g(m, d->node[1], b, 1.0 / s->rb);
    if (s->ie_idx >= 0)
        esk_am_stamp_g(m, d->node[2], e, 1.0 / s->re);
    s->ic_trial = Ic;
    s->ib_trial = Ib;
    s->vbe_trial = vbe;
    s->vbc_trial = vbc;
}

static void bjt_accept(esk_adev *d, es_time_t t)
{
    const bjt_state *s = (const bjt_state *)d->state;
    ES_UNUSED(t);
    d->i_last[0] = s->ic_trial;
    d->i_last[1] = s->ib_trial;
    d->i_last[2] = -(s->ic_trial + s->ib_trial);
}

static void bjt_observe(esk_adev *d, esk_json *o)
{
    const bjt_state *s = (const bjt_state *)d->state;
    esk_alloc *a = d->isl->a;
    const char *region;
    double vce = acc_v(d, 0) - acc_v(d, 2);
    observe_basic(d, o);
    if (s->vbe_trial < 0.4)
        region = "cutoff";
    else if (s->vbc_trial > 0.4)
        region = "saturation";
    else
        region = "active";
    esk_json_set_number(a, o, "ic", s->ic_trial);
    esk_json_set_number(a, o, "ib", s->ib_trial);
    esk_json_set_number(a, o, "vce", vce);
    esk_json_set_number(a, o, "vbe", acc_v(d, 1) - acc_v(d, 2));
    esk_json_set_string(a, o, "region", region);
}

static const esk_adev_ops bjt_npn_ops = {"bjt_npn", sizeof(bjt_state), npn_setup, bjt_load,
                                         NULL,      bjt_accept,        NULL,      NULL,
                                         NULL,      bjt_observe,       NULL};
static const esk_adev_ops bjt_pnp_ops = {"bjt_pnp", sizeof(bjt_state), pnp_setup, bjt_load,
                                         NULL,      bjt_accept,        NULL,      NULL,
                                         NULL,      bjt_observe,       NULL};

/* ---- MOSFETs (level 1, Shichman-Hodges, body tied to source) ---------------------- */

typedef struct mos_state {
    double pol; /* +1 nmos, -1 pmos */
    double vto, beta, lambda, rds;
    int has_rds;
    double id_trial, vgs_trial, vds_trial;
    int region;
} mos_state;

static es_status mos_setup_common(esk_adev *d, double pol)
{
    mos_state *s = (mos_state *)d->state;
    double kp = pd(d, "kp", 2e-5), w = pd(d, "w", 1e-4), l = pd(d, "l", 1e-4);
    s->pol = pol;
    s->vto = pd(d, "vto", pol > 0 ? 1.0 : -1.0) * pol; /* magnitude in the device's own polarity */
    s->lambda = pd(d, "lambda", 0.0);
    s->has_rds = has_param(d, "rds");
    s->rds = pd(d, "rds", 0.0);
    if (!(kp > 0.0) || !(w > 0.0) || !(l > 0.0)) {
        param_error(d, "kp/w/l", "must be > 0");
        kp = 2e-5;
        w = l = 1e-4;
    }
    if (s->lambda < 0.0) {
        param_error(d, "lambda", "must be >= 0");
        s->lambda = 0.0;
    }
    s->beta = kp * w / l;
    return ES_OK;
}

static es_status nmos_setup(esk_adev *d)
{
    return mos_setup_common(d, 1.0);
}
static es_status pmos_setup(esk_adev *d)
{
    return mos_setup_common(d, -1.0);
}

static void mos_load(esk_adev *d, esk_amatrix *m)
{
    mos_state *s = (mos_state *)d->state;
    int nd = d->node[0], ng = d->node[1], ns = d->node[2];
    double p = s->pol;
    double vd = p * esk_am_x(m, nd), vg = p * esk_am_x(m, ng), vs = p * esk_am_x(m, ns);
    int swapped = vd < vs;
    double hi = swapped ? vs : vd, lo = swapped ? vd : vs;
    double vgs = vg - lo, vds = hi - lo, vov = vgs - s->vto;
    double ids, gm, gds;
    double Id;
    if (vov <= 0.0) {
        ids = 0.0;
        gm = 0.0;
        gds = 0.0;
        s->region = 0;
    } else if (vds < vov) {
        double lam = 1.0 + s->lambda * vds;
        ids = s->beta * (vov * vds - 0.5 * vds * vds) * lam;
        gm = s->beta * vds * lam;
        gds = s->beta * (vov - vds) * lam + s->beta * (vov * vds - 0.5 * vds * vds) * s->lambda;
        s->region = 1;
    } else {
        double lam = 1.0 + s->lambda * vds;
        ids = 0.5 * s->beta * vov * vov * lam;
        gm = s->beta * vov * lam;
        gds = 0.5 * s->beta * vov * vov * s->lambda;
        s->region = 2;
    }
    gds += 1e-12;
    /* current from the high node to the low node; Jacobian w.r.t. the actual node voltages */
    if (!swapped) {
        esk_am_stamp_j(m, nd, ng, gm);
        esk_am_stamp_j(m, nd, nd, gds);
        esk_am_stamp_j(m, nd, ns, -(gm + gds));
        esk_am_stamp_j(m, ns, ng, -gm);
        esk_am_stamp_j(m, ns, nd, -gds);
        esk_am_stamp_j(m, ns, ns, gm + gds);
        Id = p * ids;
    } else {
        esk_am_stamp_j(m, ns, ng, gm);
        esk_am_stamp_j(m, ns, ns, gds);
        esk_am_stamp_j(m, ns, nd, -(gm + gds));
        esk_am_stamp_j(m, nd, ng, -gm);
        esk_am_stamp_j(m, nd, ns, -gds);
        esk_am_stamp_j(m, nd, nd, gm + gds);
        Id = -p * ids;
    }
    esk_am_stamp_f(m, nd, Id);
    esk_am_stamp_f(m, ns, -Id);
    if (s->has_rds && s->rds > 0.0)
        esk_am_stamp_g(m, nd, ns, 1.0 / s->rds);
    s->id_trial = Id + (s->has_rds && s->rds > 0.0 ? esk_am_v(m, nd, ns) / s->rds : 0.0);
    s->vgs_trial = esk_am_v(m, ng, ns);
    s->vds_trial = esk_am_v(m, nd, ns);
}

static void mos_accept(esk_adev *d, es_time_t t)
{
    const mos_state *s = (const mos_state *)d->state;
    ES_UNUSED(t);
    d->i_last[0] = s->id_trial;
    d->i_last[1] = 0.0;
    d->i_last[2] = -s->id_trial;
}

static void mos_observe(esk_adev *d, esk_json *o)
{
    const mos_state *s = (const mos_state *)d->state;
    esk_alloc *a = d->isl->a;
    static const char *const names[3] = {"cutoff", "linear", "saturation"};
    observe_basic(d, o);
    esk_json_set_number(a, o, "id", s->id_trial);
    esk_json_set_number(a, o, "vgs", s->vgs_trial);
    esk_json_set_number(a, o, "vds", s->vds_trial);
    esk_json_set_string(a, o, "region", names[s->region >= 0 && s->region < 3 ? s->region : 0]);
}

static const esk_adev_ops nmos_ops = {"nmos", sizeof(mos_state), nmos_setup, mos_load,
                                      NULL,   mos_accept,        NULL,       NULL,
                                      NULL,   mos_observe,       NULL};
static const esk_adev_ops pmos_ops = {"pmos", sizeof(mos_state), pmos_setup, mos_load,
                                      NULL,   mos_accept,        NULL,       NULL,
                                      NULL,   mos_observe,       NULL};

/* ---- behavioural op-amp ---------------------------------------------------------------- */

/* With `gbw` the op-amp has its dominant pole (SPEC-004 §7): an internal node
 * y integrates the input stage's drive, as the compensation capacitor does,
 *
 *     dy/dt = SR tanh(wu vd / SR) gate - (y - vmid) / tau,
 *
 * wu = 2 pi gbw and tau = gain / wu, so the DC gain is `gain`, the
 * unity-gain frequency `gbw`, and a large step slews at `slew` (V/us) rather
 * than at the linear pole's rate. The output follows y, clamped at the
 * output limits with corners OPAMP_CORNER wide; `gate` stops the drive
 * pushing y past the limit it is already at, so the amplifier comes out of
 * saturation within tens of nanoseconds instead of winding up. At the
 * operating point y is on the characteristic without `gbw`. */
#define OPAMP_CORNER 0.01 /* volts */

typedef struct opamp_state {
    double gain, rin, rout, vpos, vneg, headroom_high, headroom_low;
    int supply_pins;
    double vout_trial, dvo_trial;
    double arg_old; /* tanh's argument at the last iteration (limiting) */
    /* the single pole, when `gbw` is given */
    int dynamic;
    double wu, tau, slew;
    double y_prev, r_prev, r_prev2, y_trial, r_trial;
    int nhist, trial_first, trial_negligible;
} opamp_state;

static es_status opamp_setup(esk_adev *d)
{
    opamp_state *s = (opamp_state *)d->state;
    double gbw = pd(d, "gbw", 0.0);
    s->headroom_high = pd(d, "headroom_high", 0.0);
    s->headroom_low = pd(d, "headroom_low", 0.0);
    s->gain = pd(d, "gain", 1e5);
    s->rin = pd(d, "rin", 1e12);
    s->rout = pd(d, "rout", 0.0);
    s->vpos = pd(d, "vpos", 15.0);
    s->vneg = pd(d, "vneg", -15.0);
    s->supply_pins = d->nnodes >= 5;
    if (!(s->gain > 0.0) || !(s->rin > 0.0) || s->rout < 0.0) {
        param_error(d, "gain/rin/rout", "gain and rin must be > 0, rout >= 0");
        s->gain = 1e5;
        s->rin = 1e12;
        s->rout = 0.0;
    }
    s->slew = pd(d, "slew", 0.0) * 1e6; /* V/us to V/s; 0: no limit */
    if (gbw < 0.0 || s->slew < 0.0) {
        param_error(d, "gbw/slew", "must be >= 0");
        gbw = 0.0;
        s->slew = 0.0;
    }
    s->dynamic = gbw > 0.0;
    s->wu = 2.0 * 3.14159265358979323846 * gbw;
    s->tau = s->dynamic ? s->gain / s->wu : 0.0;
    d->nbranch = s->dynamic ? 2 : 1;
    return ES_OK;
}

static double opamp_sigmoid(double x)
{
    return x > 40.0 ? 1.0 : x < -40.0 ? 0.0 : 1.0 / (1.0 + exp(-x));
}

static double opamp_softplus(double x)
{
    double u = x / OPAMP_CORNER;
    return u > 40.0 ? x : OPAMP_CORNER * log1p(exp(u));
}

/* the output limits after headroom, and which supply pin gives each: hi and
 * lo index d->node (-1: a fixed vpos/vneg); a collapsed supply (the limits
 * crossed) puts both on the lower pin */
static void opamp_limits(esk_adev *d, esk_amatrix *m, double *vp, double *vn, int *hi, int *lo,
                         int *collapsed)
{
    const opamp_state *s = (const opamp_state *)d->state;
    double vpos = s->vpos, vneg = s->vneg;
    *hi = -1;
    *lo = -1;
    if (s->supply_pins) {
        vpos = esk_am_x(m, d->node[3]);
        vneg = esk_am_x(m, d->node[4]);
        *hi = 3;
        *lo = 4;
    }
    if (vpos < vneg) {
        double t = vpos;
        int ti = *hi;
        vpos = vneg;
        vneg = t;
        *hi = *lo;
        *lo = ti;
    }
    vpos -= s->headroom_high;
    vneg += s->headroom_low;
    *collapsed = vpos < vneg;
    if (*collapsed) {
        vpos = vneg;
        *hi = *lo;
    }
    *vp = vpos;
    *vn = vneg;
}

static void opamp_stamp_inputs(esk_adev *d, esk_amatrix *m)
{
    const opamp_state *s = (const opamp_state *)d->state;
    int b = d->branch[0], no = d->node[2];
    esk_am_stamp_g(m, d->node[0], d->node[1], 1.0 / s->rin);
    esk_am_stamp_j(m, no, b, 1.0);
    esk_am_stamp_f(m, no, esk_am_x(m, b));
}

/* row `row`'s dependence on the output limits: dvp on the pin giving vp, dvn
 * on the one giving vn */
static void opamp_stamp_limits(esk_adev *d, esk_amatrix *m, int row, int hi, int lo, double dvp, double dvn)
{
    if (hi >= 0)
        esk_am_stamp_j(m, row, d->node[hi], dvp);
    if (lo >= 0)
        esk_am_stamp_j(m, row, d->node[lo], dvn);
}

/* the static characteristic: vo(vd) between the limits, and its slope.
 * Newton evaluates it with tanh's argument moved at most OPAMP_ARG_STEP from
 * the last iteration's, linearised from there (SPICE's limiting, as pnjlim
 * does for a junction): deep in saturation tanh's slope underflows, and a
 * follower whose input hangs on its own output (the Uno's L buffer on a
 * floating D13) would otherwise bounce between the rails */
#define OPAMP_ARG_STEP 4.0

static double opamp_static(esk_adev *d, esk_amatrix *m, double *dvo)
{
    opamp_state *s = (opamp_state *)d->state;
    double vd = esk_am_v(m, d->node[0], d->node[1]), vpos, vneg, vamp, vmid, arg, lim, th;
    int hi, lo, collapsed;
    opamp_limits(d, m, &vpos, &vneg, &hi, &lo, &collapsed);
    vamp = 0.5 * (vpos - vneg);
    vmid = 0.5 * (vpos + vneg);
    if (vamp < 1e-6)
        vamp = 1e-6;
    arg = s->gain * vd / vamp;
    if (arg > 40.0)
        arg = 40.0;
    if (arg < -40.0)
        arg = -40.0;
    lim = arg;
    if (lim > s->arg_old + OPAMP_ARG_STEP)
        lim = s->arg_old + OPAMP_ARG_STEP;
    if (lim < s->arg_old - OPAMP_ARG_STEP)
        lim = s->arg_old - OPAMP_ARG_STEP;
    if (lim != arg)
        esk_am_noncon(m);
    s->arg_old = lim;
    th = tanh(lim);
    *dvo = s->gain * (1.0 - th * th);
    /* linearised from the limited point: exact when nothing was limited */
    return vmid + vamp * th + *dvo * (vd - lim * vamp / s->gain);
}

static void opamp_load_static(esk_adev *d, esk_amatrix *m)
{
    opamp_state *s = (opamp_state *)d->state;
    int np = d->node[0], nm = d->node[1], no = d->node[2], b = d->branch[0];
    double ib = esk_am_x(m, b), dvo;
    double vo = opamp_static(d, m, &dvo);
    opamp_stamp_inputs(d, m);
    /* branch: v_out - vo(vd) - rout i_b = 0 */
    esk_am_stamp_j(m, b, no, 1.0);
    esk_am_stamp_j(m, b, np, -dvo);
    esk_am_stamp_j(m, b, nm, dvo);
    esk_am_stamp_j(m, b, b, -s->rout);
    esk_am_stamp_f(m, b, esk_am_x(m, no) - vo - s->rout * ib);
    s->vout_trial = vo;
    s->dvo_trial = dvo;
}

/* a step with the single pole */
static void opamp_load_pole(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    opamp_state *s = (opamp_state *)d->state;
    int np = d->node[0], nm = d->node[1], no = d->node[2], b = d->branch[0], by = d->branch[1];
    double vd = esk_am_v(m, np, nm), ib = esk_am_x(m, b), y = esk_am_x(m, by);
    double vp, vn, drive, ddrive, g, gy, gp = 0.0, gn = 0.0, rate, sn, sp, out, geq, ieq;
    int hi, lo, collapsed;
    opamp_limits(d, m, &vp, &vn, &hi, &lo, &collapsed);
    if (s->slew > 0.0) {
        double th = tanh(s->wu * vd / s->slew);
        drive = s->slew * th;
        ddrive = s->wu * (1.0 - th * th);
    } else {
        drive = s->wu * vd;
        ddrive = s->wu;
    }
    /* the drive stops at the limit it pushes towards */
    if (drive >= 0.0) {
        g = opamp_sigmoid((vp - y) / OPAMP_CORNER);
        gy = -g * (1.0 - g) / OPAMP_CORNER;
        gp = -gy;
    } else {
        g = opamp_sigmoid((y - vn) / OPAMP_CORNER);
        gy = g * (1.0 - g) / OPAMP_CORNER;
        gn = -gy;
    }
    rate = drive * g - (y - 0.5 * (vp + vn)) / s->tau;
    /* y's row: dy/dt - rate = 0 */
    esk_integ_companion(in, y, s->y_prev, s->r_prev, 1.0, &geq, &ieq);
    esk_am_stamp_j(m, by, by, geq - (drive * gy - 1.0 / s->tau));
    esk_am_stamp_j(m, by, np, -ddrive * g);
    esk_am_stamp_j(m, by, nm, ddrive * g);
    opamp_stamp_limits(d, m, by, hi, lo, -(drive * gp + 0.5 / s->tau), -(drive * gn + 0.5 / s->tau));
    esk_am_stamp_f(m, by, ieq - rate);
    s->r_trial = ieq;
    s->trial_first = esk_integ_first_order(in);
    s->trial_negligible = esk_integ_negligible(in);
    s->y_trial = y;
    /* the output: y clamped at the limits, through rout */
    sn = opamp_sigmoid((y - vn) / OPAMP_CORNER);
    sp = opamp_sigmoid((y - vp) / OPAMP_CORNER);
    out = vn + opamp_softplus(y - vn) - opamp_softplus(y - vp);
    if (collapsed) {
        out = vn;
        sn = sp = 0.0;
    }
    opamp_stamp_inputs(d, m);
    esk_am_stamp_j(m, b, no, 1.0);
    esk_am_stamp_j(m, b, by, -(sn - sp));
    opamp_stamp_limits(d, m, b, hi, lo, -sp, collapsed ? -1.0 : -(1.0 - sn));
    esk_am_stamp_j(m, b, b, -s->rout);
    esk_am_stamp_f(m, b, esk_am_x(m, no) - out - s->rout * ib);
    s->vout_trial = out;
}

/* the operating point: the output on the static characteristic, and with the
 * pole y there too, the pole's own equilibrium in the linear range (solved
 * through y and its clamp, a balance of 1e5 to one, Newton would bounce
 * between the rails) */
static void opamp_load(esk_adev *d, esk_amatrix *m)
{
    opamp_state *s = (opamp_state *)d->state;
    int by;
    double dvo, vo;
    opamp_load_static(d, m);
    if (!s->dynamic)
        return;
    by = d->branch[1];
    vo = s->vout_trial;
    dvo = s->dvo_trial;
    esk_am_stamp_j(m, by, by, 1.0);
    esk_am_stamp_j(m, by, d->node[0], -dvo);
    esk_am_stamp_j(m, by, d->node[1], dvo);
    esk_am_stamp_f(m, by, esk_am_x(m, by) - vo);
    s->y_trial = vo;
    s->r_trial = 0.0;
    s->trial_first = 1;
    s->trial_negligible = 0;
}

static void opamp_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    if (((const opamp_state *)d->state)->dynamic)
        opamp_load_pole(d, m, in);
    else
        opamp_load_static(d, m);
}

static void opamp_accept(esk_adev *d, es_time_t t)
{
    opamp_state *s = (opamp_state *)d->state;
    double ib = acc_x(d, d->branch[0]);
    double iin = (acc_v(d, 0) - acc_v(d, 1)) / s->rin;
    ES_UNUSED(t);
    d->i_last[0] = iin;
    d->i_last[1] = -iin;
    d->i_last[2] = ib;
    if (s->supply_pins) {
        /* the output current is drawn from the supply it comes from */
        d->i_last[3] = ib < 0.0 ? -ib : 0.0;
        d->i_last[4] = ib > 0.0 ? -ib : 0.0;
    }
    if (s->dynamic) {
        if (s->trial_first) {
            s->nhist = 1;
            s->r_prev2 = s->r_prev;
        } else if (!(s->trial_negligible && s->nhist >= 1)) {
            if (s->nhist < 2)
                s->nhist++;
            s->r_prev2 = s->r_prev;
        }
        s->r_prev = s->r_trial;
        s->y_prev = s->y_trial;
    }
}

/* the internal node's error, in volts */
static double opamp_lte(esk_adev *d, const esk_integ *in)
{
    const opamp_state *s = (const opamp_state *)d->state;
    return s->dynamic ? reactive_lte(in, s->r_trial, s->r_prev, s->r_prev2, s->nhist) : 0.0;
}

static void opamp_observe(esk_adev *d, esk_json *o)
{
    observe_basic(d, o);
    esk_json_set_number(d->isl->a, o, "vout", acc_v(d, 2));
    esk_json_set_number(d->isl->a, o, "iout", -d->i_last[2]);
}

static const esk_adev_ops opamp_ops = {"opamp",       sizeof(opamp_state), opamp_setup, opamp_load,
                                       opamp_load_tr, opamp_accept,        NULL,        NULL,
                                       opamp_lte,     opamp_observe,       NULL};

/* ---- behavioural regulator (LDO) ----------------------------------------------------- */

typedef struct reg_state {
    double vout, dropout, imax, iq, rlim;
    double vo_trial, iin_trial;
} reg_state;

#define REG_RREV 1e6 /* reverse (sinking) leakage resistance of the output, ohms */

static es_status reg_setup(esk_adev *d)
{
    reg_state *s = (reg_state *)d->state;
    s->vout = pd(d, "vout", 3.3);
    s->dropout = pd(d, "dropout", 0.3);
    s->imax = pd(d, "imax", 1.0);
    s->iq = pd(d, "quiescent", 0.0);
    s->rlim = 1e3;
    if (!(s->vout > 0.0) || s->dropout < 0.0 || !(s->imax > 0.0)) {
        param_error(d, "vout/dropout/imax", "vout and imax must be > 0, dropout >= 0");
        s->vout = 3.3;
        s->dropout = 0.3;
        s->imax = 1.0;
    }
    d->nbranch = 1;
    return ES_OK;
}

static void reg_load(esk_adev *d, esk_amatrix *m)
{
    reg_state *s = (reg_state *)d->state;
    int nin = d->node[0], nout = d->node[1], ng = d->node[2], b = d->branch[0];
    double scale = esk_am_source_scale(m);
    double vin = esk_am_v(m, nin, ng), ib = esk_am_x(m, b);
    double avail = vin - s->dropout, vo, dvo_dvin;
    double iout = -ib, excess = iout - s->imax, rl = 0.0;
    if (avail < 0.0) {
        vo = 0.0;
        dvo_dvin = 0.0;
    } else if (avail < s->vout * scale) {
        vo = avail;
        dvo_dvin = 1.0;
    } else {
        vo = s->vout * scale;
        dvo_dvin = 0.0;
    }
    if (excess > 0.0) {
        vo -= excess * s->rlim; /* a soft current limit */
        rl = s->rlim;
    } else if (iout < 0.0 || (iout <= 0.0 && esk_am_v(m, nout, ng) >= vo)) {
        /* the pass element cannot sink: a reverse current only flows through a
         * large leakage resistance, so a pulled-up output rises above vo. The
         * output above vo with no current yet (the first iteration against a
         * supply that holds it) is this case too: regulating there would set
         * two voltage sources against each other */
        vo -= iout * REG_RREV;
        rl = REG_RREV;
    }
    /* output branch: v_out - v_gnd - vo(vin) + rl * (iout - i_knee) = 0 */
    esk_am_stamp_j(m, nout, b, 1.0);
    esk_am_stamp_j(m, ng, b, -1.0);
    esk_am_stamp_f(m, nout, ib);
    esk_am_stamp_f(m, ng, -ib);
    esk_am_stamp_j(m, b, nout, 1.0);
    esk_am_stamp_j(m, b, ng, -1.0 + dvo_dvin);
    esk_am_stamp_j(m, b, nin, -dvo_dvin);
    esk_am_stamp_j(m, b, b, -rl);
    esk_am_stamp_f(m, b, esk_am_v(m, nout, ng) - vo);
    /* the sourced load current is drawn from IN (never pushed into it) */
    if (iout > 0.0) {
        esk_am_stamp_j(m, nin, b, -1.0);
        esk_am_stamp_f(m, nin, -ib);
        esk_am_stamp_j(m, ng, b, 1.0);
        esk_am_stamp_f(m, ng, ib);
    }
    /* the quiescent current IN -> GND ramps in over the first volt of vin so an
     * unpowered input (held by gmin only) draws nothing */
    if (vin >= 1.0)
        stamp_src_i(m, nin, ng, s->iq);
    else if (vin > 0.0)
        esk_am_stamp_g(m, nin, ng, s->iq * esk_am_source_scale(m));
    s->vo_trial = vo;
    s->iin_trial = (iout > 0.0 ? iout : 0.0) + (vin >= 1.0 ? s->iq : vin > 0.0 ? s->iq * vin : 0.0);
}

static void reg_accept(esk_adev *d, es_time_t t)
{
    const reg_state *s = (const reg_state *)d->state;
    double ib = acc_x(d, d->branch[0]);
    ES_UNUSED(t);
    d->i_last[0] = s->iin_trial;
    d->i_last[1] = ib;
    d->i_last[2] = -(s->iin_trial + ib);
}

static void reg_observe(esk_adev *d, esk_json *o)
{
    observe_basic(d, o);
    esk_json_set_number(d->isl->a, o, "vout", acc_v(d, 1) - acc_v(d, 2));
    esk_json_set_number(d->isl->a, o, "iout", -d->i_last[1]);
}

static const esk_adev_ops regulator_ops = {"regulator", sizeof(reg_state), reg_setup, reg_load,
                                           NULL,        reg_accept,        NULL,      NULL,
                                           NULL,        reg_observe,       NULL};

/* ---- pull: a resistor to an internal level ------------------------------------------------ */

typedef struct pull_state {
    double g, v;
} pull_state;

static es_status pull_setup(esk_adev *d)
{
    pull_state *s = (pull_state *)d->state;
    double r = pd(d, "r", 10e3);
    int level = 1;
    if (d->comp && esk_comp_param_bool(d->comp, "level", &level) != 0)
        level = pd(d, "level", 1.0) != 0.0;
    if (!(r > 0.0)) {
        param_error(d, "r", "must be > 0");
        r = 10e3;
    }
    s->g = 1.0 / r;
    s->v = level ? pd(d, "voltage", 3.3) : 0.0;
    return ES_OK;
}

static void pull_load(esk_adev *d, esk_amatrix *m)
{
    const pull_state *s = (const pull_state *)d->state;
    esk_am_stamp_g(m, d->node[0], 0, s->g);
    stamp_src_i(m, 0, d->node[0], s->g * s->v);
}

static void pull_accept(esk_adev *d, es_time_t t)
{
    const pull_state *s = (const pull_state *)d->state;
    ES_UNUSED(t);
    d->i_last[0] = s->g * (acc_v(d, 0) - s->v);
}

static const esk_adev_ops pull_ops = {"pull", sizeof(pull_state), pull_setup, pull_load,
                                      NULL,   pull_accept,        NULL,       NULL,
                                      NULL,   observe_basic,      NULL};

/* ---- registry ------------------------------------------------------------------------------- */

static const esk_adev_ops *const device_table[] = {
    &resistor_ops,      &capacitor_ops, &inductor_ops, &vsource_ops,   &isource_ops, &switch_ops,
    &potentiometer_ops, &diode_ops,     &led_ops, &rgb_ops,      &zener_ops,     &bjt_npn_ops, &bjt_pnp_ops,
    &nmos_ops,          &pmos_ops,      &opamp_ops,    &regulator_ops, &pull_ops,
    &esk_comparator_ops, &esk_temperature_ops, &esk_timer555_ops,
    &esk_environmental_ops, &esk_photodiode_ops, &esk_optocoupler_ops, &esk_relay_contacts_ops, &esk_spdt_ops, &esk_button4_ops,
    &esk_ptc_fuse_ops, &esk_usb_meter_ops, &instrument_ops, &esk_power_profiler_ops,
};

void esk_adev_on_time(esk_adev *d, es_time_t now)
{
    if (d && d->ops == &esk_ptc_fuse_ops)
        esk_ptc_on_time(d, now);
    else if (d && d->ops == &esk_usb_meter_ops)
        esk_usb_meter_on_time(d, now);
}

const esk_adev_ops *esk_adev_find(const char *name)
{
    size_t i;
    if (!name)
        return NULL;
    for (i = 0; i < ES_COUNTOF(device_table); i++)
        if (!strcmp(device_table[i]->name, name))
            return device_table[i];
    return NULL;
}

const esk_adev_ops *const *esk_adev_table(size_t *n)
{
    if (n)
        *n = ES_COUNTOF(device_table);
    return device_table;
}
