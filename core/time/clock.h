/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* clock.h -- rational clocks (SPEC-002 §2): period num/den picoseconds, an
 * origin and an edge count; edge k is at origin + floor(k*num/den). */
#ifndef ESK_CLOCK_H
#define ESK_CLOCK_H

#include "embsim/types.h"

typedef struct esk_clock {
    uint64_t num, den; /* period in ps as a reduced rational; num == 0: disabled */
    es_time_t origin;
    uint64_t count;    /* edges elapsed since origin */
} esk_clock;

#define ESK_CLOCK_MAX_DEN (UINT64_C(1) << 20)

/* period from a frequency in Hz: 1e12/hz as a rational; -1 if not representable */
int esk_clock_set_hz(esk_clock *c, uint64_t hz, es_time_t now);
/* period as an explicit rational (ps); reduced; -1 if den == 0 or too large */
int esk_clock_set_period(esk_clock *c, uint64_t num, uint64_t den, es_time_t now);
void esk_clock_disable(esk_clock *c);
int esk_clock_enabled(const esk_clock *c);
/* time of edge k (k from 0), or ES_TIME_MAX on overflow */
es_time_t esk_clock_edge(const esk_clock *c, uint64_t k);
/* the first edge strictly after `after`, or ES_TIME_MAX */
es_time_t esk_clock_next(const esk_clock *c, es_time_t after);
/* advance by n edges; returns the time of the new edge count, or ES_TIME_MAX */
es_time_t esk_clock_advance(esk_clock *c, uint64_t n);
/* the time of the current edge count */
es_time_t esk_clock_time(const esk_clock *c);
/* how many whole edges fit in (now, until]; 0 if none */
uint64_t esk_clock_edges_until(const esk_clock *c, es_time_t until);
/* derived: parent period * mul / div (e.g. a prescaler: mul = prescaler, div = 1) */
int esk_clock_derive(esk_clock *c, const esk_clock *parent, uint64_t mul, uint64_t div, es_time_t now);
/* frequency in Hz as a double (for display) */
double esk_clock_hz(const esk_clock *c);

#endif
