/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* motor.c -- a permanent-magnet brushed DC motor, with or without a gearbox,
 * as its datasheet's constants describe it (SPEC-004's devices, M7's robot).
 *
 * Electrically the motor is its winding's resistance r and inductance l in
 * series with the back EMF ke w, w the rotor's speed (rad/s): v = r i +
 * l di/dt + ke w across pins "+" and "-", the current flowing from + to -
 * inside. The rotor is a mass of inertia j driven by kt i, held back by
 * viscous damping b w, by the brushes' and bearings' friction tf (Coulomb,
 * smoothed through tanh(w / w_s) so the Newton iteration sees a slope) and
 * by the load: j dw/dt = kt i - b w - tf tanh(w / w_s) - load / (ratio eta),
 * the input `load` being the torque on the output shaft against its
 * positive direction (the gearbox, ratio:1 at efficiency eta, reflects it to
 * the rotor). Positive current turns it positive; driven backwards it is a
 * generator, its EMF pushing current into the circuit.
 *
 * Both equations are part of the island's system: the current and the speed
 * are two extra unknowns, integrated by the island's own method (the
 * winding's flux l i and the rotor's momentum j w through the integrator's
 * companion, their truncation errors in the step control); the speed's row
 * is divided by kt, so its residual is in amperes as the circuit's are. At
 * the operating point the rotor holds its initial speed `w0` (0: at rest,
 * so a motor powered at t = 0 draws its stall current and spins up, as on
 * the bench).
 *
 * Outputs: the current, the rotor's and the output shaft's speed (rad/s and
 * rpm), the output shaft's angle (degrees, from the speed integrated at each
 * accepted step and on to now while the island sleeps at a steady speed),
 * the torque the shaft delivers, and the electrical and mechanical power.
 *
 * Inside: the commutator's ripple (the current's dips as the brushes pass
 * between segments) and the cogging torque are not modelled; the gearbox is
 * an ideal ratio with one efficiency both ways (no backlash, no back-driving
 * limit); the friction is smoothed below w_s; the winding's resistance does
 * not heat. */
#include "analog/named.h"
#include <math.h>

#define MOTOR_PI 3.14159265358979323846

typedef struct motor_state {
    double r, l, ke, kt, j, b, tf, ws, ratio, eta, w0;
    /* history of the winding's flux and of the rotor's momentum, and of
     * their derivatives (the inductor's voltage, the inertial torque) */
    double phi_prev, vl_prev, vl_prev2, h_prev, t_prev, t_prev2;
    double i_trial, w_trial, vl_trial, t_trial;
    int nhist, trial_first, trial_negligible;
    double angle;     /* the output shaft's angle at t_acc, rad */
    double w_acc;     /* the rotor's speed at t_acc, rad/s */
    double i_acc;     /* the current at t_acc, A */
    es_time_t t_acc;  /* the last accepted step's time */
    int have_acc;
} motor_state;

#define MOTOR_LTE_I 1e-3  /* the current's error is held to lte_tol mA */
#define MOTOR_LTE_W 1e-2  /* and the rotor's speed to lte_tol x 10 mrad/s */

static double par(const esk_adev *d, const char *name, double fallback)
{
    double v;
    return d->comp && esk_comp_param_double(d->comp, name, &v) == 0 ? v : fallback;
}

static double load_rotor(const esk_adev *d)
{
    const motor_state *s = (const motor_state *)d->state;
    double t = d->comp ? esk_comp_input_double(d->comp, "load", 0.0) : 0.0;
    return t / (s->ratio * s->eta);
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

static es_status motor_setup(esk_adev *d)
{
    motor_state *s = (motor_state *)d->state;
    s->r = par(d, "r", 3.75);
    s->l = par(d, "l", 1e-3);
    s->ke = par(d, "ke", 1e-3);
    s->kt = par(d, "kt", s->ke);
    s->j = par(d, "j", 1e-9);
    s->b = par(d, "b", 0.0);
    s->tf = par(d, "tf", 0.0);
    s->ws = par(d, "w_s", 1.0);
    s->ratio = par(d, "ratio", 1.0);
    s->eta = par(d, "eta", 1.0);
    s->w0 = par(d, "w0", 0.0);
    if (!(s->r > 0.0))
        s->r = 1.0;
    if (!(s->l > 0.0))
        s->l = 1e-6;
    if (!(s->ke > 0.0))
        s->ke = 1e-3;
    if (!(s->kt > 0.0))
        s->kt = s->ke;
    if (!(s->j > 0.0))
        s->j = 1e-9;
    if (s->b < 0.0)
        s->b = 0.0;
    if (s->tf < 0.0)
        s->tf = 0.0;
    if (!(s->ws > 0.0))
        s->ws = 1.0;
    if (!(s->ratio > 0.0))
        s->ratio = 1.0;
    s->eta = clampd(s->eta, 0.01, 1.0);
    d->nbranch = 2; /* the current, the rotor's speed */
    return ES_OK;
}

/* the friction and damping torque at speed w, and its slope */
static void drag(const motor_state *s, double w, double *t, double *dt)
{
    double x = clampd(w / s->ws, -40.0, 40.0), th = tanh(x);
    *t = s->b * w + s->tf * th;
    *dt = s->b + s->tf / s->ws * (1.0 - th * th);
}

/* KCL: the current leaves "+" into the motor and returns at "-" */
static void stamp_kcl(esk_amatrix *m, int n1, int n2, int bi, double i)
{
    esk_am_stamp_j(m, n1, bi, 1.0);
    esk_am_stamp_j(m, n2, bi, -1.0);
    esk_am_stamp_f(m, n1, i);
    esk_am_stamp_f(m, n2, -i);
}

static void motor_load_dc(esk_adev *d, esk_amatrix *m)
{
    motor_state *s = (motor_state *)d->state;
    int n1 = d->node[0], n2 = d->node[1], bi = d->branch[0], bw = d->branch[1];
    double i = esk_am_x(m, bi), w = esk_am_x(m, bw);
    stamp_kcl(m, n1, n2, bi, i);
    /* the winding: v - r i - ke w = 0 (the inductance a short at DC) */
    esk_am_stamp_j(m, bi, n1, 1.0);
    esk_am_stamp_j(m, bi, n2, -1.0);
    esk_am_stamp_j(m, bi, bi, -s->r);
    esk_am_stamp_j(m, bi, bw, -s->ke);
    esk_am_stamp_f(m, bi, esk_am_v(m, n1, n2) - s->r * i - s->ke * w);
    /* the rotor holds its initial speed */
    esk_am_stamp_j(m, bw, bw, 1.0);
    esk_am_stamp_f(m, bw, w - s->w0);
    s->i_trial = i;
    s->w_trial = w;
    s->vl_trial = 0.0;
    s->t_trial = 0.0;
    s->trial_first = 1;
    s->trial_negligible = 0;
}

static void motor_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    motor_state *s = (motor_state *)d->state;
    int n1 = d->node[0], n2 = d->node[1], bi = d->branch[0], bw = d->branch[1];
    double i = esk_am_x(m, bi), w = esk_am_x(m, bw);
    double req, vl, geq, tin, td, dtd;
    esk_integ_companion(in, s->l * i, s->phi_prev, s->vl_prev, s->l, &req, &vl);
    esk_integ_companion(in, s->j * w, s->h_prev, s->t_prev, s->j, &geq, &tin);
    stamp_kcl(m, n1, n2, bi, i);
    /* the winding: v - r i - l di/dt - ke w = 0 */
    esk_am_stamp_j(m, bi, n1, 1.0);
    esk_am_stamp_j(m, bi, n2, -1.0);
    esk_am_stamp_j(m, bi, bi, -(s->r + req));
    esk_am_stamp_j(m, bi, bw, -s->ke);
    esk_am_stamp_f(m, bi, esk_am_v(m, n1, n2) - s->r * i - vl - s->ke * w);
    /* the rotor, divided by kt: (j dw/dt + drag + load) / kt - i = 0 */
    drag(s, w, &td, &dtd);
    esk_am_stamp_j(m, bw, bw, (geq + dtd) / s->kt);
    esk_am_stamp_j(m, bw, bi, -1.0);
    esk_am_stamp_f(m, bw, (tin + td + load_rotor(d)) / s->kt - i);
    s->i_trial = i;
    s->w_trial = w;
    s->vl_trial = vl;
    s->t_trial = tin;
    s->trial_first = esk_integ_first_order(in);
    s->trial_negligible = esk_integ_negligible(in);
}

static void motor_accept(esk_adev *d, es_time_t t)
{
    motor_state *s = (motor_state *)d->state;
    if (s->trial_first) {
        s->nhist = 1;
        s->vl_prev2 = s->vl_prev;
        s->t_prev2 = s->t_prev;
    } else if (s->trial_negligible && s->nhist >= 1) {
        /* replace the newest point: the spacing to the one before stays usable */
    } else {
        if (s->nhist < 2)
            s->nhist++;
        s->vl_prev2 = s->vl_prev;
        s->t_prev2 = s->t_prev;
    }
    s->vl_prev = s->vl_trial;
    s->t_prev = s->t_trial;
    s->phi_prev = s->l * s->i_trial;
    s->h_prev = s->j * s->w_trial;
    if (s->have_acc && t > s->t_acc)
        s->angle += 0.5 * (s->w_acc + s->w_trial) * ((double)(t - s->t_acc) * 1e-12) / s->ratio;
    s->w_acc = s->w_trial;
    s->i_acc = s->i_trial;
    s->t_acc = t;
    s->have_acc = 1;
    d->i_last[0] = s->i_trial;
    d->i_last[1] = -s->i_trial;
}

/* a reactive state's truncation error from its derivative's history:
 * trapezoidal h^3/12 |x'''|, backward Euler h^2/2 |x''| */
static double hist_lte(const esk_integ *in, double now, double prev, double prev2, int nhist)
{
    double h = esk_integ_h(in), hp = esk_integ_h_prev(in);
    if (!(h > 0.0) || nhist < 1)
        return 0.0;
    if (esk_integ_first_order(in) || nhist < 2 || !(hp > 0.0))
        return 0.5 * h * fabs(now - prev);
    {
        double d1 = (now - prev) / h, d0 = (prev - prev2) / hp;
        return h * h * h / 12.0 * fabs(2.0 * (d1 - d0) / (h + hp));
    }
}

static double motor_lte(esk_adev *d, const esk_integ *in)
{
    const motor_state *s = (const motor_state *)d->state;
    double ei = hist_lte(in, s->vl_trial, s->vl_prev, s->vl_prev2, s->nhist) / (s->l * MOTOR_LTE_I);
    double ew = hist_lte(in, s->t_trial, s->t_prev, s->t_prev2, s->nhist) / (s->j * MOTOR_LTE_W);
    return ei > ew ? ei : ew;
}

static void motor_observe(esk_adev *d, esk_json *out)
{
    const motor_state *s = (const motor_state *)d->state;
    esk_alloc *a = d->isl->a;
    double w = s->w_acc, i = s->i_acc, angle = s->angle, td, dtd, v, wout, tout;
    es_time_t now = esk_engine_now(d->isl->e);
    if (s->have_acc && now > s->t_acc) /* asleep at a steady speed: on to now */
        angle += w * ((double)(now - s->t_acc) * 1e-12) / s->ratio;
    drag(s, w, &td, &dtd);
    v = esk_island_node_v(d->isl, d->node[0]) - esk_island_node_v(d->isl, d->node[1]);
    wout = w / s->ratio;
    /* the torque the output shaft delivers at a steady speed: the rotor's
     * net torque through the gearbox */
    tout = (s->kt * i - td) * s->ratio * (s->kt * i - td >= 0.0 ? s->eta : 1.0 / s->eta);
    esk_json_set_number(a, out, "current", i);
    esk_json_set_number(a, out, "voltage", v);
    esk_json_set_number(a, out, "rotor_speed", w);
    esk_json_set_number(a, out, "speed", wout);
    esk_json_set_number(a, out, "rpm", wout * 60.0 / (2.0 * MOTOR_PI));
    esk_json_set_number(a, out, "angle", angle * 180.0 / MOTOR_PI);
    esk_json_set_number(a, out, "torque", tout);
    esk_json_set_number(a, out, "power", v * i);
    esk_json_set_number(a, out, "mechanical_power", tout * wout);
}

void esk_motor_on_input(esk_adev *d, es_time_t now)
{
    esk_island_touch(d->isl, now);
}

const esk_adev_ops esk_dc_motor_ops = { "dc_motor", sizeof(motor_state), motor_setup, motor_load_dc,
                                        motor_load_tr, motor_accept, NULL, NULL, motor_lte, motor_observe,
                                        NULL };
