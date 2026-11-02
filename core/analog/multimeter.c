/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* multimeter.c -- a handheld digital multimeter, the Fluke 87V (ADR-033),
 * from Fluke's "83V and 87V Detailed Specifications" and, for its fuses,
 * Eaton's Bussmann DMM-B data sheet (Technical Data 1047, Dec 2015).
 *
 * Its four jacks are its pins: COM, V (V, ohms, diode), MA (mA/uA) and A.
 * The rotary switch is the input `function`; what the meter puts on its
 * jacks is what the circuit sees:
 *
 *   - volts (vdc, vac, mvdc): 10 MOhm from V to COM;
 *   - ohms: a source behind a resistance from V to COM, per range from the
 *     typical short-circuit current (600 ohm 1 mA, 6 k 100 uA, 60 k 10 uA,
 *     600 k 1 uA, 6 M 1 uA, 50 M 0.5 uA) and the open-circuit bound
 *     (< 7.3 V; 5.5 V on 50 M so its full scale stays under the sheet's
 *     4.5 V); the reading is the resistance that source sees. It ranges
 *     itself one range per display update, up when the reading passes the
 *     range, down below a tenth of it;
 *   - diode: < 3.9 V open, 0.6 mA typical into a short; it reads the
 *     voltage across, OL above 3.000 V;
 *   - the current jacks are shunts to COM in every position (the
 *     burden voltages, typical: uA 100 uV/uA, mA 1.8 mV/mA, A 0.03 V/A),
 *     as the meter's input alert warns: a lead left in A while measuring a
 *     supply's voltage is a short through the 11 A fuse;
 *   - fuses: MA through F1 (DMM-B-44/100, 440 mA 1000 V fast), A through F2
 *     (DMM-B-11A, 11 A 1000 V fast). Each opens when an overload has lasted
 *     its total clearing time, from the data sheet's curves (read off the
 *     log-log plot, about +-10 %): F1 300 s at 1.07 A, 1 s at 1.2 A, 0.1 s
 *     at 1.7 A, 10 ms at 2.1 A; F2 300 s at 22 A, 10 s at 45 A, 1 s at
 *     70 A, 0.1 s at 100 A, 10 ms at 150 A. A varying overload adds its
 *     fractions of the time (dt / t(I)); at or below the rating it carries
 *     forever. A blown fuse opens its jack until it is replaced (input
 *     replace_fuse);
 *   - the display: 6000 counts, 4 updates a second; DC readings average
 *     the update's window (an integrating converter), AC readings are the
 *     window's true RMS of the AC part (the 87V is true-rms, ac-coupled).
 *     Ranges and resolution are the sheet's: V 6.000/60.00/600.0/1000,
 *     mV 600.0, ohms 600.0/6.000k/60.00k/600.0k/6.000M/50.00M, uA
 *     600.0/6000, mA 60.00/400.0 (600 mA readable), A 6.000/10.00 (20 A
 *     readable). OL beyond the top range.
 *
 * Not modelled: the accuracy as an error (readings are exact to their
 * resolution), the input's 100 pF, capacitance, conductance (nS),
 * frequency, duty cycle, temperature, MIN MAX, peak, REL, HOLD, the
 * 4-1/2-digit mode, the bar graph, the backlight and battery; the
 * continuity beeper (its threshold is not in the specifications); the
 * input alert's chirp (a lead's presence in a jack is not known here); in
 * the mA/A position the 87V reads the jack its lead is in, here the
 * function names it (ma_dc or a_dc); the fuse element's cooling between
 * overloads (fractions add up and never decay); overvoltage damage. */
#include "analog/named.h"
#include "util/num.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { J_COM, J_V, J_MA, J_A };
enum { F_OFF, F_VAC, F_VDC, F_MVDC, F_OHM, F_DIODE, F_UADC, F_UAAC, F_MADC, F_MAAC, F_ADC, F_AAC };

static const char *const fn_name[] = { "off", "vac", "vdc", "mvdc", "ohm", "diode",
                                       "ua_dc", "ua_ac", "ma_dc", "ma_ac", "a_dc", "a_ac" };

#define DMM_UPDATE_PS 250000000000ull /* 4 updates a second */
#define DMM_WAKE_UPDATE 0
#define DMM_WAKE_FUSE 1
#define R_VIN 10e6
#define R_UA 100.0  /* 100 uV/uA */
#define R_MA 1.8    /* 1.8 mV/mA */
#define R_A 0.03    /* 0.03 V/A */
#define G_OPEN 1e-12

/* ohms ranges: full scale, short-circuit current, open-circuit voltage */
static const struct { double full, isc, voc; int decimals; const char *unit; double scale; } ohm_range[] = {
    { 600.0, 1e-3, 7.3, 1, "ohm", 1.0 },     { 6000.0, 100e-6, 7.3, 3, "kohm", 1e-3 },
    { 60e3, 10e-6, 7.3, 2, "kohm", 1e-3 },   { 600e3, 1e-6, 7.3, 1, "kohm", 1e-3 },
    { 6e6, 1e-6, 7.3, 3, "Mohm", 1e-6 },     { 50e6, 0.5e-6, 5.5, 2, "Mohm", 1e-6 },
};
#define N_OHM ((int)(sizeof ohm_range / sizeof ohm_range[0]))

/* a range of a measuring function: its top (what it can show), the value one count is */
typedef struct { double top, count; const char *unit; double scale; } drange;

static const drange r_v[] = { { 6.0, 1e-3, "V", 1 }, { 60.0, 1e-2, "V", 1 }, { 600.0, 0.1, "V", 1 }, { 1000.0, 1.0, "V", 1 } };
static const drange r_mv[] = { { 0.6, 1e-4, "mV", 1e3 } };
static const drange r_ua[] = { { 600e-6, 1e-7, "uA", 1e6 }, { 6000e-6, 1e-6, "uA", 1e6 } };
static const drange r_ma[] = { { 60e-3, 1e-5, "mA", 1e3 }, { 600e-3, 1e-4, "mA", 1e3 } };
static const drange r_a[] = { { 6.0, 1e-3, "A", 1 }, { 20.0, 1e-2, "A", 1 } };

/* the fuses' total clearing curves: (amperes, seconds), current rising */
typedef struct { double rating; int n; double i[6], t[6]; } fuse_curve;
static const fuse_curve f1 = { 0.44, 4, { 1.07, 1.2, 1.7, 2.1 }, { 300, 1, 0.1, 0.01 } };
static const fuse_curve f2 = { 11.0, 5, { 22, 45, 70, 100, 150 }, { 300, 10, 1, 0.1, 0.01 } };

typedef struct dmm_state {
    int fn;                      /* the function the matrix holds */
    int ohm_rng;                 /* the ohms range */
    double v, i_ma, i_a;         /* V-COM, and the currents into MA and A, as last accepted */
    double ohm_meas;             /* the resistance the ohms source sees, as last accepted */
    es_time_t t_last;            /* integrated up to here */
    es_time_t win_t;             /* this display window's start */
    double sum, sum2;            /* this window's integral of the measured quantity, and of its square */
    double span;                 /* seconds integrated in it */
    double blow_ma, blow_a;      /* the fuses' used fractions of their clearing time */
    int open_ma, open_a;         /* blown */
    double shown;                /* the display's value (in base units) */
    int ol;                      /* it shows OL */
    char text[24];               /* what it shows */
    const char *unit;
    es_time_t wake_update, wake_fuse;
} dmm_state;

static double node_v(const esk_adev *d, int k) { return esk_island_node_v(d->isl, d->node[k]); }

static int fn_of(const esk_adev *d)
{
    const char *f = d->comp ? esk_json_get_str(d->comp->inputs, "function", NULL) : NULL;
    int i;
    for (i = 0; f && i < (int)(sizeof fn_name / sizeof fn_name[0]); i++)
        if (!strcmp(f, fn_name[i]))
            return i;
    return F_VDC;
}

static int is_ua(int fn) { return fn == F_UADC || fn == F_UAAC; }
static int is_ac(int fn) { return fn == F_VAC || fn == F_UAAC || fn == F_MAAC || fn == F_AAC; }

/* the shunt behind the MA jack: the uA one in the uA functions, else the mA one */
static double r_ma_jack(int fn) { return is_ua(fn) ? R_UA : R_MA; }

/* the total clearing time at |i|: 0 when it never clears */
static double clear_time(const fuse_curve *f, double i)
{
    int k;
    i = fabs(i);
    if (i <= f->rating)
        return 0.0;
    if (i <= f->i[0]) {
        /* between the rating and the first point: along the first segment's slope */
        double slope = log(f->t[1] / f->t[0]) / log(f->i[1] / f->i[0]);
        double t = f->t[0] * pow(i / f->i[0], slope);
        return t > 1e9 ? 1e9 : t;
    }
    for (k = 1; k < f->n; k++)
        if (i <= f->i[k]) {
            double a = log(i / f->i[k - 1]) / log(f->i[k] / f->i[k - 1]);
            return exp(log(f->t[k - 1]) + a * log(f->t[k] / f->t[k - 1]));
        }
    /* beyond the last point: on along the last segment */
    {
        double slope = log(f->t[f->n - 1] / f->t[f->n - 2]) / log(f->i[f->n - 1] / f->i[f->n - 2]);
        return f->t[f->n - 1] * pow(i / f->i[f->n - 1], slope);
    }
}

static double fuse_use(const fuse_curve *f, double i, double dt)
{
    double t = clear_time(f, i);
    return t > 0.0 ? dt / t : 0.0;
}

/* the ohms (or diode) source: open-circuit voltage and resistance */
static void ohm_source(const dmm_state *s, int fn, double *voc, double *r)
{
    if (fn == F_DIODE) {
        *voc = 3.9;
        *r = 3.9 / 0.6e-3;
    } else {
        *voc = ohm_range[s->ohm_rng].voc;
        *r = ohm_range[s->ohm_rng].voc / ohm_range[s->ohm_rng].isc;
    }
}

/* the measured quantity now, from the accepted solution */
static double measured(const dmm_state *s, int fn)
{
    switch (fn) {
    case F_UADC: case F_UAAC: case F_MADC: case F_MAAC: return s->i_ma;
    case F_ADC: case F_AAC: return s->i_a;
    case F_OHM: return s->ohm_meas;
    default: return s->v;
    }
}

static void fmt_reading(dmm_state *s, double x, double count, double scale, const char *unit)
{
    double shown = floor(x / count + 0.5) * count;
    int dec = 0;
    double c = count * scale;
    while (c < 0.999 && dec < 6) {
        c *= 10.0;
        dec++;
    }
    s->shown = shown;
    s->ol = 0;
    s->unit = unit;
    snprintf(s->text, sizeof s->text, "%.*f", dec, shown * scale);
}

static void show_ol(dmm_state *s, const char *unit)
{
    s->ol = 1;
    s->shown = 0.0;
    s->unit = unit;
    snprintf(s->text, sizeof s->text, "OL");
}

static void show_range(dmm_state *s, const drange *r, int n, double x)
{
    int k;
    for (k = 0; k < n; k++)
        if (fabs(x) < r[k].top + 0.5 * r[k].count) {
            fmt_reading(s, x, r[k].count, r[k].scale, r[k].unit);
            return;
        }
    show_ol(s, r[n - 1].unit);
}

/* the window's reading onto the display, and the ohms range for the next */
static int update_display(esk_adev *d)
{
    dmm_state *s = (dmm_state *)d->state;
    double mean = s->span > 0.0 ? s->sum / s->span : measured(s, s->fn);
    double x = mean;
    int restamp = 0;
    if (is_ac(s->fn) && s->span > 0.0) {
        double var = s->sum2 / s->span - mean * mean;
        x = var > 0.0 ? sqrt(var) : 0.0;
    }
    switch (s->fn) {
    case F_OFF:
        s->text[0] = 0;
        s->unit = "";
        s->ol = 0;
        s->shown = 0.0;
        break;
    case F_VDC: case F_VAC: show_range(s, r_v, 4, x); break;
    case F_MVDC: show_range(s, r_mv, 1, x); break;
    case F_UADC: case F_UAAC: show_range(s, r_ua, 2, x); break;
    case F_MADC: case F_MAAC: show_range(s, r_ma, 2, x); break;
    case F_ADC: case F_AAC: show_range(s, r_a, 2, x); break;
    case F_DIODE:
        if (x > 3.0005)
            show_ol(s, "V");
        else
            fmt_reading(s, x, 1e-3, 1.0, "V");
        break;
    case F_OHM: {
        int r = s->ohm_rng;
        double full = ohm_range[r].full;
        if (x < 0.0 || x > full * 1.0001) {
            show_ol(s, ohm_range[r].unit);
            if (r < N_OHM - 1) {
                s->ohm_rng = r + 1;
                restamp = 1;
            }
        } else {
            fmt_reading(s, x, full / (r == N_OHM - 1 ? 5000.0 : 6000.0), ohm_range[r].scale, ohm_range[r].unit);
            if (r > 0 && x < 0.1 * full) {
                s->ohm_rng = r - 1;
                restamp = 1;
            }
        }
        break;
    }
    }
    s->sum = s->sum2 = s->span = 0.0;
    return restamp;
}

/* time passes to `now`: the measured quantity goes in a straight line from
 * the last accepted solution's to `x1` (the new solution's, or the same
 * when nothing was solved), integrated exactly into the window; the fuses
 * see the last solution's currents */
static int advance(esk_adev *d, es_time_t now, double x1)
{
    dmm_state *s = (dmm_state *)d->state;
    int changed = 0;
    es_time_t t0 = s->t_last;
    double x0 = measured(s, s->fn), whole = (double)(now - t0);
    while (s->t_last < now) {
        es_time_t end = s->win_t + DMM_UPDATE_PS, to = now < end ? now : end;
        double dt = (double)(to - s->t_last) * 1e-12;
        double a = x0 + (x1 - x0) * (double)(s->t_last - t0) / whole;
        double b = x0 + (x1 - x0) * (double)(to - t0) / whole;
        s->sum += 0.5 * (a + b) * dt;
        s->sum2 += (a * a + a * b + b * b) / 3.0 * dt;
        s->span += dt;
        if (!s->open_ma) {
            s->blow_ma += fuse_use(&f1, s->i_ma, dt);
            if (s->blow_ma >= 1.0) {
                s->open_ma = 1;
                s->i_ma = 0.0;
                changed = 1;
                esk_comp_diag(d->comp, ES_SEV_WARNING, "instrument.fuse_blown",
                              "%s: the mA/uA input's 440 mA fuse (F1) has blown: replace it", d->comp->id);
            }
        }
        if (!s->open_a) {
            s->blow_a += fuse_use(&f2, s->i_a, dt);
            if (s->blow_a >= 1.0) {
                s->open_a = 1;
                s->i_a = 0.0;
                changed = 1;
                esk_comp_diag(d->comp, ES_SEV_WARNING, "instrument.fuse_blown",
                              "%s: the A input's 11 A fuse (F2) has blown: replace it", d->comp->id);
            }
        }
        s->t_last = to;
        if (to == end) {
            changed |= update_display(d);
            s->win_t = end;
        }
    }
    return changed;
}

static void schedule(esk_adev *d)
{
    dmm_state *s = (dmm_state *)d->state;
    es_time_t now = d->comp->e->sched.now, t = s->win_t + DMM_UPDATE_PS;
    double left = 0.0;
    if (!s->wake_update || s->wake_update > t || s->wake_update <= now) {
        if (esk_comp_wake_at(d->comp, t, DMM_WAKE_UPDATE) == ES_OK)
            s->wake_update = t;
    }
    /* the time a constant overload takes to clear what is left of a fuse */
    if (!s->open_ma && clear_time(&f1, s->i_ma) > 0.0)
        left = (1.0 - s->blow_ma) * clear_time(&f1, s->i_ma);
    if (!s->open_a && clear_time(&f2, s->i_a) > 0.0) {
        double l2 = (1.0 - s->blow_a) * clear_time(&f2, s->i_a);
        if (left == 0.0 || l2 < left)
            left = l2;
    }
    if (left > 0.0) {
        es_time_t tf = now + (es_time_t)(left * 1e12) + 1;
        if (tf < t && (!s->wake_fuse || s->wake_fuse <= now || s->wake_fuse > tf) &&
            esk_comp_wake_at(d->comp, tf, DMM_WAKE_FUSE) == ES_OK)
            s->wake_fuse = tf;
    }
}

static es_status dmm_setup(esk_adev *d)
{
    dmm_state *s = (dmm_state *)d->state;
    if (d->nnodes != 4)
        return ES_EINVAL;
    memset(s, 0, sizeof *s);
    s->fn = fn_of(d);
    s->ohm_rng = N_OHM - 1;
    s->t_last = s->win_t = d->comp ? d->comp->e->sched.now : 0;
    s->unit = "";
    return ES_OK;
}

static void dmm_load(esk_adev *d, esk_amatrix *m)
{
    dmm_state *s = (dmm_state *)d->state;
    int com = d->node[J_COM], v = d->node[J_V];
    int fn = fn_of(d);
    s->fn = fn;
    /* the current jacks: shunts through their fuses, in every position */
    esk_am_stamp_g(m, d->node[J_MA], com, s->open_ma ? G_OPEN : 1.0 / r_ma_jack(fn));
    esk_am_stamp_g(m, d->node[J_A], com, s->open_a ? G_OPEN : 1.0 / R_A);
    if (fn == F_OHM || fn == F_DIODE) {
        double voc, r;
        ohm_source(s, fn, &voc, &r);
        esk_am_stamp_g(m, v, com, 1.0 / r);
        esk_am_stamp_i(m, com, v, voc / r * esk_am_source_scale(m));
    } else if (fn == F_OFF) {
        esk_am_stamp_g(m, v, com, G_OPEN);
    } else {
        esk_am_stamp_g(m, v, com, 1.0 / R_VIN);
    }
}

static void dmm_load_tr(esk_adev *d, esk_amatrix *m, const esk_integ *in)
{
    ES_UNUSED(in);
    dmm_load(d, m);
}

static void dmm_accept(esk_adev *d, es_time_t t)
{
    dmm_state *s = (dmm_state *)d->state;
    int fn = fn_of(d), changed;
    double vc, x1;
    {
        /* the new solution's value of the quantity the window measures */
        dmm_state n = *s;
        double nvc = node_v(d, J_COM);
        n.v = node_v(d, J_V) - nvc;
        n.i_ma = s->open_ma ? 0.0 : (node_v(d, J_MA) - nvc) / r_ma_jack(s->fn);
        n.i_a = s->open_a ? 0.0 : (node_v(d, J_A) - nvc) / R_A;
        if (s->fn == F_OHM) {
            double voc, r, i;
            ohm_source(s, s->fn, &voc, &r);
            i = (voc - n.v) / r;
            n.ohm_meas = i > 1e-15 ? n.v / i : -1.0;
        }
        x1 = measured(&n, s->fn);
    }
    changed = advance(d, t, x1);
    if (fn != s->fn) {
        /* the switch turned: a new window */
        s->fn = fn;
        s->sum = s->sum2 = s->span = 0.0;
    }
    vc = node_v(d, J_COM);
    s->v = node_v(d, J_V) - vc;
    s->i_ma = s->open_ma ? 0.0 : (node_v(d, J_MA) - vc) / r_ma_jack(fn);
    s->i_a = s->open_a ? 0.0 : (node_v(d, J_A) - vc) / R_A;
    if (fn == F_OHM || fn == F_DIODE) {
        double voc, r, i;
        ohm_source(s, fn, &voc, &r);
        i = (voc - s->v) / r; /* out of V into the circuit */
        s->ohm_meas = i > 1e-15 ? s->v / i : -1.0;
        d->i_last[J_V] = -i;
    } else {
        d->i_last[J_V] = fn == F_OFF ? 0.0 : s->v / R_VIN;
    }
    d->i_last[J_MA] = s->i_ma;
    d->i_last[J_A] = s->i_a;
    d->i_last[J_COM] = -(d->i_last[J_V] + s->i_ma + s->i_a);
    if (changed && d->comp)
        esk_island_touch(d->isl, t);
    if (d->comp)
        schedule(d);
}

void esk_multimeter_on_time(esk_adev *d, es_time_t now)
{
    dmm_state *s = (dmm_state *)d->state;
    s->wake_update = s->wake_fuse = 0;
    if (advance(d, now, measured(s, s->fn)))
        esk_island_touch(d->isl, now);
    schedule(d);
}

void esk_multimeter_on_input(esk_adev *d, const char *input, es_time_t now)
{
    dmm_state *s = (dmm_state *)d->state;
    (void)advance(d, now, measured(s, s->fn));
    if (!strcmp(input, "replace_fuse") && d->comp && esk_comp_input_bool(d->comp, "replace_fuse", 0)) {
        s->open_ma = s->open_a = 0;
        s->blow_ma = s->blow_a = 0.0;
    }
    if (!strcmp(input, "function")) {
        s->sum = s->sum2 = s->span = 0.0;
        s->win_t = now; /* the display starts a new window as the switch turns */
        s->ohm_rng = N_OHM - 1;
    }
    esk_island_touch(d->isl, now);
}

static void dmm_observe(esk_adev *d, esk_json *out)
{
    dmm_state *s = (dmm_state *)d->state;
    esk_alloc *a = d->isl->a;
    esk_json_set_string(a, out, "position", fn_name[s->fn]);
    esk_json_set_string(a, out, "display", s->text);
    esk_json_set_string(a, out, "unit", s->unit ? s->unit : "");
    esk_json_set_bool(a, out, "overload", s->ol);
    esk_json_set_number(a, out, "value", s->shown);
    esk_json_set_string(a, out, "fuse_ma", s->open_ma ? "blown" : "ok");
    esk_json_set_string(a, out, "fuse_a", s->open_a ? "blown" : "ok");
    esk_json_set_number(a, out, "i_ma", s->i_ma);
    esk_json_set_number(a, out, "i_a", s->i_a);
}

const esk_adev_ops esk_multimeter_ops = { "multimeter", sizeof(dmm_state), dmm_setup, dmm_load, dmm_load_tr,
                                          dmm_accept, NULL, NULL, NULL, dmm_observe, NULL };
