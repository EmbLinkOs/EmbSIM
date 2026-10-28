/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Linear AC MNA, represented as [G -B; B G]. No C99 complex dependency
 * (MSVC/TinyCC), nonlinear linearization, artificial shunts or live mutation. */
#include "analog/analog.h"
#include "util/str.h"
#include <float.h>
#include <math.h>
#include <string.h>

static void entry(double *a, int n, int row, int col, double re, double im)
{
    int size = 2 * n;
    if (!row || !col)
        return;
    row--;
    col--;
    a[(size_t)row * (size_t)size + (size_t)col] += re;
    a[(size_t)row * (size_t)size + (size_t)(col + n)] -= im;
    a[(size_t)(row + n) * (size_t)size + (size_t)col] += im;
    a[(size_t)(row + n) * (size_t)size + (size_t)(col + n)] += re;
}
static void admittance(double *a, int n, int p, int q, double re, double im)
{
    entry(a, n, p, p, re, im);
    entry(a, n, q, q, re, im);
    entry(a, n, p, q, -re, -im);
    entry(a, n, q, p, -re, -im);
}
static void rhs(double *b, int p, int q, double value)
{
    if (p)
        b[p - 1] -= value;
    if (q)
        b[q - 1] += value;
}
static int solve(double *a, double *b, int n)
{
    int i, j, k, p;
    for (i = 0; i < n; i++) {
        double scale = 0.0;
        for (j = 0; j < n; j++) {
            double v = a[(size_t)i * (size_t)n + (size_t)j];
            if (!isfinite(v))
                return -1;
            if (fabs(v) > scale)
                scale = fabs(v);
        }
        if (!(scale > 0.0) || !isfinite(b[i]))
            return -1;
        for (j = 0; j < n; j++)
            a[(size_t)i * (size_t)n + (size_t)j] /= scale;
        b[i] /= scale;
    }
    for (k = 0; k < n; k++) {
        double largest = 0.0;
        p = k;
        for (i = k; i < n; i++) {
            double v = fabs(a[(size_t)i * (size_t)n + (size_t)k]);
            if (v > largest) {
                largest = v;
                p = i;
            }
        }
        if (!(largest > 1e-14) || !isfinite(largest))
            return -1;
        if (p != k) {
            double t = b[p];
            b[p] = b[k];
            b[k] = t;
            for (j = k; j < n; j++) {
                t = a[(size_t)p * (size_t)n + (size_t)j];
                a[(size_t)p * (size_t)n + (size_t)j] = a[(size_t)k * (size_t)n + (size_t)j];
                a[(size_t)k * (size_t)n + (size_t)j] = t;
            }
        }
        for (i = k + 1; i < n; i++) {
            double f = a[(size_t)i * (size_t)n + (size_t)k] / a[(size_t)k * (size_t)n + (size_t)k];
            for (j = k + 1; j < n; j++)
                a[(size_t)i * (size_t)n + (size_t)j] -= f * a[(size_t)k * (size_t)n + (size_t)j];
            b[i] -= f * b[k];
        }
    }
    for (i = n - 1; i >= 0; i--) {
        for (j = i + 1; j < n; j++)
            b[i] -= a[(size_t)i * (size_t)n + (size_t)j] * b[j];
        b[i] /= a[(size_t)i * (size_t)n + (size_t)i];
        if (!isfinite(b[i]))
            return -1;
    }
    return 0;
}
es_status esk_island_ac(const esk_island *isl, const esk_comp *source, double frequency,
                        double **real, double **imag, char *why, size_t cap)
{
    size_t i, nodes = isl->nets.len, count;
    int n = (int)nodes - 1, branch, m, r, c;
    double *a = NULL, *b = NULL, *original = NULL, *excitation = NULL;
    double omega = 6.28318530717958647692 * frequency;
    es_status status = ES_EINVAL;
    *real = NULL;
    *imag = NULL;
    for (i = 0; i < isl->devs.len; i++) {
        const esk_adev *d = isl->devs.data[i];
        esk_ac_element e;
        if (esk_adev_ac_element(d, &e) != ES_OK) {
            esk_snprintf(why, cap, "Unsupported AC device %s (%s).",
                         d->comp ? d->comp->id : "bridge", d->ops->name);
            return ES_EINVAL;
        }
        if (e.kind == ESK_AC_V || e.kind == ESK_AC_L)
            n++;
    }
    if (n <= 0 || n > 128 || !isfinite(omega) || !(omega > 0.0)) {
        esk_snprintf(
            why, cap,
            "AC island limit: 1–128 complex unknowns and finite positive angular frequency.");
        return ES_EINVAL;
    }
    m = 2 * n;
    count = (size_t)m * (size_t)m;
    a = esk_calloc(isl->a, count, sizeof(double));
    b = esk_calloc(isl->a, (size_t)m, sizeof(double));
    original = esk_calloc(isl->a, count, sizeof(double));
    excitation = esk_calloc(isl->a, (size_t)m, sizeof(double));
    if (!a || !b || !original || !excitation) {
        status = ES_ENOMEM;
        goto done;
    }
    branch = (int)nodes;
    for (i = 0; i < isl->devs.len; i++) {
        const esk_adev *d = isl->devs.data[i];
        esk_ac_element e;
        int p = d->node[0], q = d->node[1];
        double re = 0.0, im = 0.0;
        if (esk_adev_ac_element(d, &e) != ES_OK)
            goto done;
        switch (e.kind) {
        case ESK_AC_R:
            re = e.value;
            break;
        case ESK_AC_C: {
            double x = 1.0 / (omega * e.value), z = hypot(e.resistance, x);
            re = (e.resistance / z) / z;
            im = (x / z) / z;
            break;
        }
        case ESK_AC_INPUT:
            re = 1.0 / e.resistance;
            im = omega * e.value;
            break;
        case ESK_AC_V:
        case ESK_AC_L:
            entry(a, n, p, branch, 1.0, 0.0);
            entry(a, n, q, branch, -1.0, 0.0);
            entry(a, n, branch, p, 1.0, 0.0);
            entry(a, n, branch, q, -1.0, 0.0);
            entry(a, n, branch, branch, -e.resistance, e.kind == ESK_AC_L ? -omega * e.value : 0.0);
            if (e.kind == ESK_AC_V && d->comp == source)
                b[branch - 1] = 1.0;
            branch++;
            continue;
        case ESK_AC_I:
            if (d->comp == source)
                rhs(b, p, q, 1.0);
            continue;
        }
        if (!isfinite(re) || !isfinite(im))
            goto done;
        admittance(a, n, p, q, re, im);
    }
    memcpy(original, a, count * sizeof(double));
    memcpy(excitation, b, (size_t)m * sizeof(double));
    if (solve(a, b, m))
        goto done;
    /* Normwise backward error on each unscaled equation. Use the whole
     * solution norm so a zero-valued constrained unknown can retain ordinary
     * roundoff without giving a spurious 100% relative error. */
    {
        double solution_norm = 0.0;
        for (r = 0; r < m; r++)
            if (fabs(b[r]) > solution_norm)
                solution_norm = fabs(b[r]);
        for (r = 0; r < m; r++) {
            double residual = -excitation[r], row_norm = 0.0, scale;
            for (c = 0; c < m; c++) {
                double coefficient = original[(size_t)r * (size_t)m + (size_t)c];
                residual += coefficient * b[c];
                row_norm += fabs(coefficient);
            }
            scale = fabs(excitation[r]) + row_norm * solution_norm;
            if (!isfinite(residual) || !isfinite(scale) ||
                fabs(residual) > 1e-10 * fmax(scale, DBL_MIN))
                goto done;
        }
    }
    *real = esk_calloc(isl->a, nodes, sizeof(double));
    *imag = esk_calloc(isl->a, nodes, sizeof(double));
    if (!*real || !*imag) {
        status = ES_ENOMEM;
        goto done;
    }
    for (i = 1; i < nodes; i++) {
        (*real)[i] = b[i - 1];
        (*imag)[i] = b[i - 1 + (size_t)n];
    }
    status = ES_OK;
done:
    esk_free(isl->a, a, count * sizeof(double));
    esk_free(isl->a, original, count * sizeof(double));
    esk_free(isl->a, b, (size_t)m * sizeof(double));
    esk_free(isl->a, excitation, (size_t)m * sizeof(double));
    if (status != ES_OK) {
        esk_free(isl->a, *real, nodes * sizeof(double));
        esk_free(isl->a, *imag, nodes * sizeof(double));
        *real = NULL;
        *imag = NULL;
        if (!*why)
            esk_snprintf(why, cap,
                         "AC matrix is singular, ill-conditioned or numerically unrepresentable; "
                         "no artificial shunt was added.");
    }
    return status;
}
