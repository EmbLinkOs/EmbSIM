/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* hbridge.c -- one H-bridge of an integrated motor driver (DRV8833,
 * TB6612FNG, L293D's pairs), as its datasheet describes it (SPEC-004's
 * devices, M7's robot).
 *
 * Terminals: the logic inputs IN1, IN2, PWM and EN, the outputs OUT1 and
 * OUT2, the motor supply VM, the logic ground GND, the low-side return SEN
 * (the sense resistor's top, or GND), the open-drain FAULT output and the
 * logic supply VCC (used with vcc_ref, else left open). Each
 * output is a half-bridge: a high-side FET from VM (ron_hs) and a low-side
 * FET to SEN (ron_ls), each with its body diode (OUT to VM, SEN to OUT), so
 * a motor's current recirculates through the diodes while both FETs are off
 * (fast decay) and its back EMF, driven, pumps current back into VM. An
 * output's high level is therefore VM less the drop of the current it
 * carries, as on the bench: a sagging battery slows the motor.
 *
 * The inputs are read as voltages to GND through Schmitt thresholds (the
 * datasheet's VIL and VIH, the hysteresis vhys about their mean), each with
 * its pulldown (rpd; EN's rpd_en and, when en_clamp > 0, its clamping
 * Zener). The logic is the datasheet's, by `mode`:
 *   in_in      (DRV8833) IN1 IN2: 00 coast, 10 forward, 01 reverse, 11 brake;
 *   in_in_pwm  (TB6612FNG) IN1 IN2 PWM: 00 off (coast), 10/01 with PWM high
 *              forward/reverse, PWM low or 11 short brake;
 *   half       (L293D) each output follows its input while EN is high,
 *              both off while low.
 * With `sleep` 1, EN is nSLEEP: low sleeps the bridge (outputs off, the
 * logic reset, iq_sleep drawn) and t_wake passes after it rises before the
 * outputs follow; otherwise EN is a logic input of the table above
 * (TB6612FNG's STBY: low turns both outputs off).
 *
 * Timing: an input change is taken after it holds t_deg (the deglitch
 * filter; shorter pulses are lost) and reaches the outputs t_prop after it
 * began; an output passing between high and low is off for t_dead (the
 * break-before-make; t_dead_lh for low to high when it differs). VM below
 * uvlo (falling; uvlo + uvlo_hys rising) disables the bridge and resets
 * its logic.
 *
 * With vcc_ref (TB6612FNG), the logic has its own supply VCC: the input
 * thresholds are the fractions vil_frac and vih_frac of it (vhys about
 * their mean, in volts), the lockout watches VCC instead of VM, and the
 * quiescent currents are drawn from VCC.
 *
 * Current control (vtrip > 0): while the bridge drives, SEN above vtrip
 * (I x Rsense) switches it to slow decay (both low sides on) until the
 * next edge of the f_chop clock; the comparator is then ignored for
 * t_blank. Overcurrent (iocp > 0): a FET carrying more than iocp for
 * t_ocp_deg disables the bridge and pulls FAULT low for t_ocp, then it
 * retries.
 *
 * Inside: the comparator, the overcurrent detector and the Schmitt inputs
 * are sampled at the island's accepted steps, which stop at every timer of
 * this model (deglitch, delay, dead time, wake, chop clock, retry) but not
 * at an input's crossing between them (an MCU's edge reaches the island as
 * a step of its own). The FETs switch instantly (no rise or fall time, no
 * gate charge), each a resistance set from VM between its datasheet's
 * points (ron at vm_hi, ron_low at vm_lo); the FETs' analog current
 * limiting during the overcurrent deglitch, the die's temperature (thermal
 * shutdown) and the charge pump's and regulator's pins are not modelled. */
#include "analog/named.h"
#include <math.h>
#include <string.h>

enum { HB_IN1, HB_IN2, HB_PWM, HB_EN, HB_OUT1, HB_OUT2, HB_VM, HB_GND, HB_SEN, HB_FAULT, HB_VCC, HB_NPINS };
enum { HB_Z, HB_H, HB_L };
enum { HB_MODE_IN_IN, HB_MODE_IN_IN_PWM, HB_MODE_HALF };

#define HB_Q 8          /* commands in flight through the propagation delay */
#define HB_NIN 4        /* IN1, IN2, PWM, EN */
#define HB_VT 0.025852  /* kT/q at 27 C */

typedef struct hb_cmd {
    es_time_t at;
    int out[2];
} hb_cmd;

typedef struct hb_state {
    int mode, sleep, vcc_ref;
    double vil_frac, vih_frac, hys_v[HB_NIN];
    double ron_hs_hi, ron_ls_hi, ron_hs_lo, ron_ls_lo, vm_lo, vm_hi, ron_hs, ron_ls, goff;
    double bd_is, bd_nvt, bd_vcrit;
    double rpd, rpd_en, vt_rise[HB_NIN], vt_fall[HB_NIN];
    double en_clamp, z_is, z_nvt, z_vcrit;
    double iq, iq_sleep, uvlo, uvlo_hys, vtrip, iocp, fault_r;
    es_time_t t_deg, t_prop, t_dead, t_dead_lh, t_wake, t_blank, t_chop, t_ocp_deg, t_ocp;
    /* the inputs: as seen at the last accepted step, and as taken */
    int raw[HB_NIN], filt[HB_NIN];
    int pend[HB_NIN];           /* raw differs from filt: taken at deg_at */
    es_time_t deg_at[HB_NIN];
    hb_cmd q[HB_Q];
    int qn;
    int out[2];                 /* what the logic commands after the delay */
    int want[2];                /* after sleep, faults and chopping */
    int eff[2];                 /* on the FETs (after the dead time) */
    es_time_t dead_until[2];
    int awake, waking;
    es_time_t wake_at;
    int uvlo_on, chop, over, fault, powered;
    es_time_t chop_end, blank_end, over_since, fault_end;
    double vlim[4], vz_old;     /* junction limiting: the body diodes and EN's clamp */
} hb_state;

static double par(const esk_adev *d, const char *name, double fallback)
{
    double v;
    return d->comp && esk_comp_param_double(d->comp, name, &v) == 0 ? v : fallback;
}

/* a boolean parameter (true/false, or a number) */
static int par_b(const esk_adev *d, const char *name, int fallback)
{
    int b;
    double v;
    if (d->comp && esk_comp_param_bool(d->comp, name, &b) == 0)
        return b != 0;
    if (d->comp && esk_comp_param_double(d->comp, name, &v) == 0)
        return v != 0.0;
    return fallback;
}

static es_time_t par_t(const esk_adev *d, const char *name, double fallback_s)
{
    double s = par(d, name, fallback_s);
    return s > 0.0 && s < 1e6 ? (es_time_t)(s * 1e12 + 0.5) : 0;
}

static double node_v(const esk_adev *d, int k)
{
    return esk_island_node_v(d->isl, d->node[k]);
}

static void bad_param(esk_adev *d, const char *name, const char *why)
{
    if (d->comp)
        esk_comp_diag(d->comp, ES_SEV_ERROR, "analog.param_out_of_range", "%s: %s", name, why);
}

static es_status hb_setup(esk_adev *d)
{
    hb_state *s = (hb_state *)d->state;
    const char *mode = d->comp ? esk_comp_param_str(d->comp, "mode", "in_in") : "in_in";
    double vil = par(d, "vil", 0.7), vih = par(d, "vih", 2.0), hys = par(d, "vhys", 0.4);
    double en_vil = par(d, "en_vil", vil), en_vih = par(d, "en_vih", vih), en_hys = par(d, "en_vhys", hys);
    double bd_vf = par(d, "bd_vf", 0.8), bd_if = par(d, "bd_if", 1.0);
    int k;
    if (d->nnodes < HB_NPINS) {
        bad_param(d, "pins", "an h_bridge has eleven terminals");
        return ES_EINVAL;
    }
    s->mode = !strcmp(mode, "in_in_pwm") ? HB_MODE_IN_IN_PWM : !strcmp(mode, "half") ? HB_MODE_HALF : HB_MODE_IN_IN;
    if (strcmp(mode, "in_in") && strcmp(mode, "in_in_pwm") && strcmp(mode, "half"))
        bad_param(d, "mode", "in_in, in_in_pwm or half");
    s->sleep = par_b(d, "sleep", s->mode == HB_MODE_IN_IN);
    s->vcc_ref = par_b(d, "vcc_ref", 0);
    s->vil_frac = par(d, "vil_frac", 0.3);
    s->vih_frac = par(d, "vih_frac", 0.7);
    s->ron_hs_hi = par(d, "ron_hs", 0.2);
    s->ron_ls_hi = par(d, "ron_ls", 0.16);
    s->ron_hs_lo = par(d, "ron_hs_low", s->ron_hs_hi);
    s->ron_ls_lo = par(d, "ron_ls_low", s->ron_ls_hi);
    s->vm_hi = par(d, "vm_hi", 5.0);
    s->vm_lo = par(d, "vm_lo", 2.7);
    s->goff = 1.0 / par(d, "roff", 1e8);
    s->bd_nvt = par(d, "bd_n", 1.2) * HB_VT;
    s->rpd = par(d, "rpd", 150e3);
    s->rpd_en = par(d, "rpd_en", 500e3);
    s->en_clamp = par(d, "en_clamp", 0.0);
    s->iq = par(d, "iq", 1.7e-3);
    s->iq_sleep = par(d, "iq_sleep", 1.6e-6);
    s->uvlo = par(d, "uvlo", 2.6);
    s->uvlo_hys = par(d, "uvlo_hys", 0.09);
    s->vtrip = par(d, "vtrip", 0.0);
    s->iocp = par(d, "iocp", 0.0);
    s->fault_r = par(d, "fault_r", 100.0);
    s->t_deg = par_t(d, "t_deg", 450e-9);
    s->t_prop = par_t(d, "t_prop", 1.1e-6);
    s->t_dead = par_t(d, "t_dead", 450e-9);
    s->t_dead_lh = par_t(d, "t_dead_lh", (double)s->t_dead * 1e-12);
    s->t_wake = par_t(d, "t_wake", s->sleep ? 1e-3 : 0.0);
    s->t_blank = par_t(d, "t_blank", 3.75e-6);
    s->t_chop = par_t(d, "t_chop", 1.0 / par(d, "f_chop", 50e3));
    s->t_ocp_deg = par_t(d, "t_ocp_deg", 4e-6);
    s->t_ocp = par_t(d, "t_ocp", 1.35e-3);
    if (!(s->ron_hs_hi > 0.0) || !(s->ron_ls_hi > 0.0) || !(s->ron_hs_lo > 0.0) || !(s->ron_ls_lo > 0.0) ||
        !(s->vm_hi > s->vm_lo) || !(s->goff > 0.0) || !(s->bd_nvt > 0.0) || !(bd_vf > 0.0) || !(bd_if > 0.0) ||
        s->rpd < 0.0 || s->rpd_en < 0.0 || s->iq < 0.0 || s->iq_sleep < 0.0 || s->en_clamp < 0.0 ||
        s->vtrip < 0.0 || s->iocp < 0.0 || !(s->fault_r > 0.0) || !(vih > vil) || !(en_vih > en_vil) ||
        hys < 0.0 || en_hys < 0.0 || !(s->vih_frac > s->vil_frac) || s->vil_frac < 0.0 || s->vih_frac > 1.0 || s->t_prop < s->t_deg || !s->t_chop) {
        bad_param(d, "h_bridge", "resistances, VM points, junction and thresholds out of range");
        return ES_EINVAL;
    }
    s->ron_hs = s->ron_hs_hi;
    s->ron_ls = s->ron_ls_hi;
    s->bd_is = bd_if / (exp(bd_vf / s->bd_nvt) - 1.0);
    s->bd_vcrit = esk_vcrit(s->bd_is, s->bd_nvt);
    for (k = 0; k < HB_NIN; k++) {
        double lo = k == HB_EN ? en_vil : vil, hi = k == HB_EN ? en_vih : vih, h = k == HB_EN ? en_hys : hys;
        double mid = 0.5 * (lo + hi);
        s->vt_rise[k] = mid + 0.5 * h;
        s->vt_fall[k] = mid - 0.5 * h;
        s->hys_v[k] = h;
    }
    /* the clamp: 100 uA at en_clamp, n = 1.5 */
    s->z_nvt = 1.5 * HB_VT;
    s->z_is = 1e-14;
    s->z_vcrit = esk_vcrit(s->z_is, s->z_nvt);
    s->out[0] = s->out[1] = s->want[0] = s->want[1] = s->eff[0] = s->eff[1] = HB_Z;
    return ES_OK;
}

/* the clamp's junction voltage for a pin voltage v: 100 uA at en_clamp */
static double hb_zx(const hb_state *s, double v)
{
    return v - s->en_clamp + s->z_nvt * log(1e-4 / s->z_is);
}

/* conductance or nonlinear current from pin a to pin b (into a, out of b) */
static void stamp_nl(esk_amatrix *m, int na, int nb, double g, double i, double vl)
{
    esk_am_stamp_g(m, na, nb, g);
    esk_am_stamp_i(m, na, nb, i - g * vl);
}

static void hb_diode(esk_adev *d, esk_amatrix *m, int a, int c, double *vold, double is, double nvt, double vcrit, double xofs)
{
    double v = esk_am_v(m, d->node[a], d->node[c]), x = v + xofs, xl = esk_pnjlim(x, *vold, nvt, vcrit), i, g;
    if (fabs(xl - x) > 1e-12)
        esk_am_noncon(m);
    *vold = xl;
    esk_pn_eval(xl, is, nvt, &i, &g);
    stamp_nl(m, d->node[a], d->node[c], g + 1e-12, i, xl - xofs);
}

/* the supply's quiescent current, fading out below 0.5 V so an unpowered
 * VM is not pulled negative */
static void hb_iq(double iq, double v, double *i, double *g)
{
    if (v <= 0.0) {
        *i = 0.0;
        *g = 0.0;
    } else {
        double e = exp(-v / 0.5);
        *i = iq * (1.0 - e);
        *g = iq * e / 0.5;
    }
}

static void hb_load(esk_adev *d, esk_amatrix *m)
{
    hb_state *s = (hb_state *)d->state;
    int vm = d->node[HB_VM], gnd = d->node[HB_GND], sen = d->node[HB_SEN], k;
    double i, g, v;
    for (k = 0; k < 2; k++) {
        int o = d->node[HB_OUT1 + k];
        if (s->eff[k] == HB_H)
            esk_am_stamp_g(m, vm, o, 1.0 / s->ron_hs);
        else if (s->eff[k] == HB_L)
            esk_am_stamp_g(m, o, sen, 1.0 / s->ron_ls);
        esk_am_stamp_g(m, vm, o, s->goff);
        esk_am_stamp_g(m, o, sen, s->goff);
        hb_diode(d, m, HB_OUT1 + k, HB_VM, &s->vlim[2 * k], s->bd_is, s->bd_nvt, s->bd_vcrit, 0.0);
        hb_diode(d, m, HB_SEN, HB_OUT1 + k, &s->vlim[2 * k + 1], s->bd_is, s->bd_nvt, s->bd_vcrit, 0.0);
    }
    for (k = 0; k < HB_NIN; k++) {
        double r = k == HB_EN ? s->rpd_en : s->rpd;
        if (r > 0.0)
            esk_am_stamp_g(m, d->node[k], gnd, 1.0 / r);
    }
    if (s->en_clamp > 0.0)
        hb_diode(d, m, HB_EN, HB_GND, &s->vz_old, s->z_is, s->z_nvt, s->z_vcrit, hb_zx(s, 0.0));
    {
        int sup = s->vcc_ref ? d->node[HB_VCC] : vm;
        v = esk_am_v(m, sup, gnd);
        hb_iq(s->awake || !s->sleep ? s->iq : s->iq_sleep, v, &i, &g);
        stamp_nl(m, sup, gnd, g + 1e-12, i, v);
    }
    esk_am_stamp_g(m, d->node[HB_FAULT], gnd, s->fault ? 1.0 / s->fault_r : 1e-9);
}

static void hb_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    (void)in;
    hb_load(d, m);
}

/* the table: the outputs the taken inputs command */
static void hb_table(const hb_state *s, const int *in, int *o)
{
    int a = in[HB_IN1], b = in[HB_IN2];
    switch (s->mode) {
    case HB_MODE_HALF:
        if (!in[HB_EN]) {
            o[0] = o[1] = HB_Z;
        } else {
            o[0] = a ? HB_H : HB_L;
            o[1] = b ? HB_H : HB_L;
        }
        return;
    case HB_MODE_IN_IN_PWM:
        if (!s->sleep && !in[HB_EN]) { /* standby */
            o[0] = o[1] = HB_Z;
        } else if (!a && !b) {
            o[0] = o[1] = HB_Z;
        } else if (a && b) {
            o[0] = o[1] = HB_L;
        } else if (!in[HB_PWM]) {
            o[0] = o[1] = HB_L;
        } else {
            o[0] = a ? HB_H : HB_L;
            o[1] = a ? HB_L : HB_H;
        }
        return;
    default:
        if (!s->sleep && !in[HB_EN]) {
            o[0] = o[1] = HB_Z;
        } else if (!a && !b) {
            o[0] = o[1] = HB_Z;
        } else if (a && b) {
            o[0] = o[1] = HB_L;
        } else {
            o[0] = a ? HB_H : HB_L;
            o[1] = a ? HB_L : HB_H;
        }
        return;
    }
}

static int hb_driving(const int *o)
{
    return (o[0] == HB_H && o[1] == HB_L) || (o[0] == HB_L && o[1] == HB_H);
}

/* the logic is reset (sleep, UVLO): nothing in flight, no fault, no chop */
static void hb_logic_reset(hb_state *s)
{
    s->qn = 0;
    s->out[0] = s->out[1] = HB_Z;
    s->chop = s->over = s->fault = 0;
}

/* the FET currents at the accepted solution: high side k from VM, low side k to SEN */
static double hb_fet_i(const esk_adev *d, const hb_state *s, int k)
{
    double vo = node_v(d, HB_OUT1 + k);
    if (s->eff[k] == HB_H)
        return (node_v(d, HB_VM) - vo) / s->ron_hs;
    if (s->eff[k] == HB_L)
        return (vo - node_v(d, HB_SEN)) / s->ron_ls;
    return 0.0;
}

static void hb_currents(esk_adev *d)
{
    hb_state *s = (hb_state *)d->state;
    double *il = d->i_last, i, g;
    double vgnd = node_v(d, HB_GND), vm = node_v(d, HB_VM), vsen = node_v(d, HB_SEN);
    int k;
    for (k = 0; k < HB_NPINS; k++)
        il[k] = 0.0;
#define FLOW(a, b, cur) do { double c_ = (cur); il[a] += c_; il[b] -= c_; } while (0)
    for (k = 0; k < 2; k++) {
        int o = HB_OUT1 + k;
        double vo = node_v(d, o);
        if (s->eff[k] == HB_H)
            FLOW(HB_VM, o, (vm - vo) / s->ron_hs);
        else if (s->eff[k] == HB_L)
            FLOW(o, HB_SEN, (vo - vsen) / s->ron_ls);
        FLOW(HB_VM, o, (vm - vo) * s->goff);
        FLOW(o, HB_SEN, (vo - vsen) * s->goff);
        esk_pn_eval(vo - vm, s->bd_is, s->bd_nvt, &i, &g);
        FLOW(o, HB_VM, i);
        esk_pn_eval(vsen - vo, s->bd_is, s->bd_nvt, &i, &g);
        FLOW(HB_SEN, o, i);
    }
    for (k = 0; k < HB_NIN; k++) {
        double r = k == HB_EN ? s->rpd_en : s->rpd;
        if (r > 0.0)
            FLOW(k, HB_GND, (node_v(d, k) - vgnd) / r);
    }
    if (s->en_clamp > 0.0) {
        esk_pn_eval(hb_zx(s, node_v(d, HB_EN) - vgnd), s->z_is, s->z_nvt, &i, &g);
        FLOW(HB_EN, HB_GND, i);
    }
    {
        int sup = s->vcc_ref ? HB_VCC : HB_VM;
        hb_iq(s->awake || !s->sleep ? s->iq : s->iq_sleep, node_v(d, sup) - vgnd, &i, &g);
        FLOW(sup, HB_GND, i);
    }
    FLOW(HB_FAULT, HB_GND, (node_v(d, HB_FAULT) - vgnd) * (s->fault ? 1.0 / s->fault_r : 1e-9));
#undef FLOW
}

static void hb_accept(esk_adev *d, es_time_t t)
{
    hb_state *s = (hb_state *)d->state;
    double vgnd = node_v(d, HB_GND), vm = node_v(d, HB_VM) - vgnd;
    double vcc = node_v(d, HB_VCC) - vgnd, vsup = s->vcc_ref ? vcc : vm;
    int before[2];
    int k, taken = 0;
    before[0] = s->eff[0];
    before[1] = s->eff[1];
    /* the FETs' resistances follow VM between the datasheet's points */
    {
        double x = (vm - s->vm_lo) / (s->vm_hi - s->vm_lo);
        x = x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : x;
        s->ron_hs = s->ron_hs_lo + (s->ron_hs_hi - s->ron_hs_lo) * x;
        s->ron_ls = s->ron_ls_lo + (s->ron_ls_hi - s->ron_ls_lo) * x;
    }
    /* undervoltage lockout: below it everything is off and reset */
    if (!s->powered || s->uvlo_on) {
        if (vsup > s->uvlo + s->uvlo_hys) {
            s->powered = 1;
            s->uvlo_on = 0;
            hb_logic_reset(s);
            for (k = 0; k < HB_NIN; k++)
                s->pend[k] = 0;
            s->awake = 0;
            s->waking = 0;
            for (k = 0; k < HB_NIN; k++)
                s->filt[k] = s->raw[k] = 0;
        }
    } else if (vsup < s->uvlo) {
        s->uvlo_on = 1;
        s->awake = s->waking = 0;
        hb_logic_reset(s);
    }
    /* the inputs: Schmitt triggers, then the deglitch filter */
    for (k = 0; k < HB_NIN; k++) {
        double v = node_v(d, k) - vgnd;
        int r = s->raw[k];
        double rise = s->vt_rise[k], fall = s->vt_fall[k];
        if (s->vcc_ref) {
            double mid = 0.5 * (s->vil_frac + s->vih_frac) * vcc;
            rise = mid + 0.5 * s->hys_v[k];
            fall = mid - 0.5 * s->hys_v[k];
        }
        if (!r && v > rise)
            r = 1;
        else if (r && v < fall)
            r = 0;
        if (r != s->raw[k]) {
            s->raw[k] = r;
            s->pend[k] = r != s->filt[k];
            s->deg_at[k] = t + s->t_deg;
        }
    }
    if (s->powered && !s->uvlo_on) {
        es_time_t when = 0;
        for (k = 0; k < HB_NIN; k++)
            if (s->pend[k] && s->deg_at[k] <= t) {
                if (s->deg_at[k] > when)
                    when = s->deg_at[k];
                s->filt[k] = s->raw[k];
                s->pend[k] = 0;
                taken = 1;
                if (k == HB_EN && s->sleep) {
                    if (s->filt[k]) {
                        s->waking = 1;
                        s->wake_at = when + s->t_wake;
                    } else {
                        s->awake = s->waking = 0;
                        hb_logic_reset(s);
                    }
                }
            }
        if (!s->sleep)
            s->awake = 1;
        else if (!s->awake && !s->waking && s->filt[HB_EN]) {
            /* powered up (or out of UVLO) with nSLEEP high: it wakes */
            s->waking = 1;
            s->wake_at = t + s->t_wake;
        }
        if (s->waking && t >= s->wake_at) {
            s->waking = 0;
            s->awake = 1;
            s->qn = 0;
            hb_table(s, s->filt, s->out);
            taken = 0;
        }
        if (taken && s->awake) {
            hb_cmd c;
            hb_table(s, s->filt, c.out);
            c.at = when + (s->t_prop - s->t_deg);
            if (s->qn == HB_Q) {
                memmove(&s->q[0], &s->q[1], (HB_Q - 1) * sizeof s->q[0]);
                s->qn--;
            }
            s->q[s->qn++] = c;
        }
        while (s->qn && s->q[0].at <= t) {
            s->out[0] = s->q[0].out[0];
            s->out[1] = s->q[0].out[1];
            memmove(&s->q[0], &s->q[1], (size_t)(s->qn - 1) * sizeof s->q[0]);
            s->qn--;
        }
    }
    /* overcurrent: a FET over iocp for t_ocp_deg disables the bridge for t_ocp */
    if (s->fault && t >= s->fault_end)
        s->fault = 0;
    if (s->iocp > 0.0 && !s->fault) {
        int over = fabs(hb_fet_i(d, s, 0)) > s->iocp || fabs(hb_fet_i(d, s, 1)) > s->iocp;
        if (!over)
            s->over = 0;
        else if (!s->over) {
            s->over = 1;
            s->over_since = t;
        }
        if (s->over && t >= s->over_since + s->t_ocp_deg) {
            s->over = 0;
            s->fault = 1;
            s->fault_end = t + s->t_ocp;
        }
    }
    /* current chopping: slow decay to the next clock edge, then blanking */
    if (s->chop && t >= s->chop_end) {
        s->chop = 0;
        s->blank_end = t + s->t_blank;
    }
    {
        int active = s->awake && !s->uvlo_on && s->powered && !s->fault;
        int w[2];
        if (!active || !hb_driving(s->out))
            s->chop = 0;
        if (active && !s->chop && s->vtrip > 0.0 && hb_driving(s->eff) && t >= s->blank_end &&
            node_v(d, HB_SEN) - vgnd > s->vtrip) {
            s->chop = 1;
            s->chop_end = (t / s->t_chop + 1) * s->t_chop;
        }
        if (!active)
            w[0] = w[1] = HB_Z;
        else if (s->chop)
            w[0] = w[1] = HB_L;
        else {
            w[0] = s->out[0];
            w[1] = s->out[1];
        }
        if (!hb_driving(s->want) && hb_driving(w))
            s->blank_end = t + s->t_blank;
        for (k = 0; k < 2; k++) {
            if (s->want[k] == HB_H && w[k] == HB_L)
                s->dead_until[k] = t + s->t_dead;
            else if (s->want[k] == HB_L && w[k] == HB_H)
                s->dead_until[k] = t + s->t_dead_lh;
            s->want[k] = w[k];
            s->eff[k] = t < s->dead_until[k] ? HB_Z : w[k];
        }
    }
    if (s->eff[0] != before[0] || s->eff[1] != before[1])
        esk_island_touch(d->isl, t);
    hb_currents(d);
}

static void hb_min(es_time_t *best, es_time_t at, es_time_t after)
{
    if (at > after && at < *best)
        *best = at;
}

static int hb_next_breakpoint(esk_adev *d, es_time_t after, es_time_t *at)
{
    const hb_state *s = (const hb_state *)d->state;
    es_time_t best = ES_TIME_MAX;
    int k;
    for (k = 0; k < HB_NIN; k++)
        if (s->pend[k])
            hb_min(&best, s->deg_at[k], after);
    if (s->qn)
        hb_min(&best, s->q[0].at, after);
    if (s->waking)
        hb_min(&best, s->wake_at, after);
    for (k = 0; k < 2; k++)
        hb_min(&best, s->dead_until[k], after);
    if (s->chop)
        hb_min(&best, s->chop_end, after);
    if (s->fault)
        hb_min(&best, s->fault_end, after);
    if (s->over)
        hb_min(&best, s->over_since + s->t_ocp_deg, after);
    if (best == ES_TIME_MAX)
        return 0;
    *at = best;
    return 1;
}

static const char *hb_half_name(int o)
{
    return o == HB_H ? "high" : o == HB_L ? "low" : "off";
}

static void hb_observe(esk_adev *d, esk_json *o)
{
    const hb_state *s = (const hb_state *)d->state;
    esk_alloc *a = d->isl->a;
    const char *state;
    if (s->eff[0] == HB_H && s->eff[1] == HB_L)
        state = "forward";
    else if (s->eff[0] == HB_L && s->eff[1] == HB_H)
        state = "reverse";
    else if (s->eff[0] == HB_L && s->eff[1] == HB_L)
        state = "brake";
    else if (s->eff[0] == HB_Z && s->eff[1] == HB_Z)
        state = "coast";
    else
        state = "half";
    esk_json_set_string(a, o, "state", state);
    esk_json_set_string(a, o, "out1", hb_half_name(s->eff[0]));
    esk_json_set_string(a, o, "out2", hb_half_name(s->eff[1]));
    /* the load current, out of OUT1 (into the motor) */
    esk_json_set_number(a, o, "current", -d->i_last[HB_OUT1]);
    esk_json_set_number(a, o, "supply_current", d->i_last[HB_VM]);
    if (s->vcc_ref)
        esk_json_set_number(a, o, "logic_current", d->i_last[HB_VCC]);
    esk_json_set_number(a, o, "vout", node_v(d, HB_OUT1) - node_v(d, HB_OUT2));
    esk_json_set_bool(a, o, "fault", s->fault != 0);
    esk_json_set_bool(a, o, "asleep", s->sleep && !s->awake);
    esk_json_set_bool(a, o, "chopping", s->chop != 0);
    esk_json_set_bool(a, o, "undervoltage", s->uvlo_on || !s->powered);
    {
        double p = 0.0;
        int k;
        for (k = 0; k < HB_NPINS; k++)
            p += node_v(d, k) * d->i_last[k];
        esk_json_set_number(a, o, "power", p);
    }
}

const esk_adev_ops esk_h_bridge_ops = {"h_bridge", sizeof(hb_state), hb_setup, hb_load,
                                       hb_load_tr, hb_accept,        NULL,     hb_next_breakpoint,
                                       NULL,       hb_observe,       NULL};
