/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "time/clock.h"
#include <string.h>

static uint64_t gcd(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* floor(k * num / den) without overflow for the ranges clocks use:
 * k < 2^64, num < 2^64, den <= 2^20. Split k = q*den + r. */
static int mul_div(uint64_t k, uint64_t num, uint64_t den, uint64_t *out)
{
    uint64_t q = k / den, r = k % den, hi, lo;
    /* q*num may overflow; r*num fits since r < 2^20 and num < 2^44 in practice; check both */
    if (num && q > UINT64_MAX / num)
        return -1;
    hi = q * num;
    if (num && r > UINT64_MAX / num)
        return -1;
    lo = (r * num) / den;
    if (hi > UINT64_MAX - lo)
        return -1;
    *out = hi + lo;
    return 0;
}

int esk_clock_set_period(esk_clock *c, uint64_t num, uint64_t den, es_time_t now)
{
    uint64_t g;
    if (!den || !num)
        return -1;
    g = gcd(num, den);
    num /= g;
    den /= g;
    if (den > ESK_CLOCK_MAX_DEN)
        return -1;
    c->num = num;
    c->den = den;
    c->origin = now;
    c->count = 0;
    return 0;
}

int esk_clock_set_hz(esk_clock *c, uint64_t hz, es_time_t now)
{
    if (!hz)
        return -1;
    /* period = 1e12 / hz ps = num/den with num = 1e12/g, den = hz/g */
    return esk_clock_set_period(c, ES_PS_PER_S, hz, now);
}

void esk_clock_disable(esk_clock *c) { memset(c, 0, sizeof *c); }
int esk_clock_enabled(const esk_clock *c) { return c->num != 0; }

es_time_t esk_clock_edge(const esk_clock *c, uint64_t k)
{
    uint64_t off;
    if (!c->num)
        return ES_TIME_MAX;
    if (mul_div(k, c->num, c->den, &off))
        return ES_TIME_MAX;
    if (off > ES_TIME_MAX - c->origin)
        return ES_TIME_MAX;
    return c->origin + off;
}

uint64_t esk_clock_edges_until(const esk_clock *c, es_time_t until)
{
    uint64_t span, k, lo, hi;
    if (!c->num || until <= c->origin)
        return 0;
    span = until - c->origin;
    /* k = floor(span * den / num): bound then refine */
    if (span > UINT64_MAX / c->den)
        k = (span / c->num) * c->den; /* approximate for huge spans */
    else
        k = (span * c->den) / c->num;
    /* refine so edge(k) <= until < edge(k+1) */
    lo = k > 2 ? k - 2 : 0;
    hi = k + 2;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo + 1) / 2;
        if (esk_clock_edge(c, mid) <= until)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

es_time_t esk_clock_next(const esk_clock *c, es_time_t after)
{
    uint64_t k;
    if (!c->num)
        return ES_TIME_MAX;
    if (after < c->origin)
        return c->origin;
    k = esk_clock_edges_until(c, after);
    return esk_clock_edge(c, k + 1);
}

es_time_t esk_clock_advance(esk_clock *c, uint64_t n)
{
    es_time_t t;
    if (c->count > UINT64_MAX - n)
        return ES_TIME_MAX;
    t = esk_clock_edge(c, c->count + n);
    if (t != ES_TIME_MAX)
        c->count += n;
    return t;
}

es_time_t esk_clock_time(const esk_clock *c) { return esk_clock_edge(c, c->count); }

int esk_clock_derive(esk_clock *c, const esk_clock *parent, uint64_t mul, uint64_t div, es_time_t now)
{
    uint64_t num, den;
    if (!parent->num || !mul || !div)
        return -1;
    if (parent->num > UINT64_MAX / mul || parent->den > UINT64_MAX / div)
        return -1;
    num = parent->num * mul;
    den = parent->den * div;
    return esk_clock_set_period(c, num, den, now);
}

double esk_clock_hz(const esk_clock *c)
{
    if (!c->num)
        return 0.0;
    return 1e12 * (double)c->den / (double)c->num;
}
