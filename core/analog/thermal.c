/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* thermal.c -- a part's junction and package temperatures (ADR-034): two
 * thermal nodes, the junction (heat capacity cj) and the solder point or
 * case (cs), the junction joined to it through rjs and it to the room
 * through rja - rjs, as a datasheet's thermal resistances give them.
 *
 * With the power held constant over an interval the two temperatures have a
 * closed form (the 2x2 system's matrix exponential), so an interval of any
 * length costs the same: a quiet circuit can sleep for minutes while a part
 * warms. esk_thermal2_cross finds when the junction would pass a limit
 * under constant power, so the part can be woken exactly then. */
#include "analog/named.h"
#include <math.h>

void esk_thermal2_init(esk_thermal2 *th, double ta, double rjs, double rja, double cj, double cs)
{
    th->rjs = rjs > 1e-6 ? rjs : 1e-6;
    th->rsa = rja - rjs > 1e-6 ? rja - rjs : 1e-6;
    th->cj = cj > 1e-12 ? cj : 1e-12;
    th->cs = cs > 1e-12 ? cs : 1e-12;
    th->tj = th->ts = ta;
}

/* the temperatures `dt` seconds on from (tj, ts) under constant p and ta */
static void solve(const esk_thermal2 *th, double p, double ta, double dt, double *tj, double *ts)
{
    double a = 1.0 / (th->rjs * th->cj), c = 1.0 / (th->rjs * th->cs), e = 1.0 / (th->rsa * th->cs);
    double tr = -(a + c + e), det = a * e, disc = sqrt(tr * tr - 4.0 * det);
    double l1 = 0.5 * (tr + disc), l2 = 0.5 * (tr - disc);
    double ss = ta + p * th->rsa, sj = ss + p * th->rjs; /* the steady state */
    double y1 = th->tj - sj, y2 = th->ts - ss, e1 = exp(l1 * dt), e2 = exp(l2 * dt);
    /* exp(At) = (e1 (A - l2 I) - e2 (A - l1 I)) / (l1 - l2), A = [[-a, a], [c, -(c+e)]] */
    double k = 1.0 / (l1 - l2);
    double m11 = k * (e1 * (-a - l2) - e2 * (-a - l1)), m12 = k * a * (e1 - e2);
    double m21 = k * c * (e1 - e2), m22 = k * (e1 * (-(c + e) - l2) - e2 * (-(c + e) - l1));
    *tj = sj + m11 * y1 + m12 * y2;
    *ts = ss + m21 * y1 + m22 * y2;
}

void esk_thermal2_advance(esk_thermal2 *th, double p, double ta, double dt)
{
    double tj, ts;
    if (dt <= 0.0)
        return;
    solve(th, p, ta, dt, &tj, &ts);
    th->tj = tj;
    th->ts = ts;
}

double esk_thermal2_steady(const esk_thermal2 *th, double p, double ta) { return ta + p * (th->rsa + th->rjs); }

double esk_thermal2_cross(const esk_thermal2 *th, double p, double ta, double limit)
{
    double fast = th->rjs * th->cj, lo = 0.0, hi, tj, ts;
    int k;
    if (th->tj >= limit)
        return 0.0;
    if (esk_thermal2_steady(th, p, ta) <= limit)
        return -1.0;
    /* out geometrically until it is past, then halve the bracket */
    hi = fast * 0.01;
    for (k = 0; k < 80; k++) {
        solve(th, p, ta, hi, &tj, &ts);
        if (tj >= limit)
            break;
        lo = hi;
        hi *= 2.0;
    }
    if (k == 80)
        return -1.0;
    for (k = 0; k < 60 && hi - lo > 1e-9 * (1.0 + hi); k++) {
        double mid = 0.5 * (lo + hi);
        solve(th, p, ta, mid, &tj, &ts);
        if (tj >= limit)
            hi = mid;
        else
            lo = mid;
    }
    return hi;
}
