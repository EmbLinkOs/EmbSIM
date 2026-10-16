/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* sched.h -- the scheduler (SPEC-002 §3 to §5): a binary heap of events
 * keyed by (time, class, seq), delta-cycle accounting, cancellation by
 * generation. */
#ifndef ESK_SCHED_H
#define ESK_SCHED_H

#include "embsim/types.h"
#include "util/alloc.h"

typedef enum esk_event_class {
    ESK_EV_EXTERNAL = 0,
    ESK_EV_CORE = 1,
    ESK_EV_PERIPHERAL = 2,
    ESK_EV_SIGNAL = 3,
    ESK_EV_ANALOG = 4,
    ESK_EV_PROBE = 5
} esk_event_class;

typedef struct esk_event {
    es_time_t time;
    uint64_t seq;
    uint32_t id;        /* handle for cancellation: index+1 into the token table */
    uint32_t gen;
    uint8_t cls;
    uint8_t cancelled;
    void (*fn)(void *ctx, void *arg, es_time_t now);
    void *ctx;
    void *arg;
} esk_event;

typedef uint64_t esk_event_handle; /* (gen << 32) | id, 0 = none */

typedef struct esk_sched {
    esk_alloc *a;
    esk_event *heap;
    size_t len, cap, max;
    uint64_t seq;
    es_time_t now;
    uint32_t delta_cycles;      /* SIGNAL events processed at `now` after the first */
    uint32_t max_delta_cycles;
    es_time_t last_signal_time;
    int delta_overflow;
    /* handle table: gen per slot so stale handles never cancel a reused slot */
    uint32_t *gens;
    size_t ngens, gens_cap;
    uint32_t *free_ids;
    size_t nfree, free_cap;
    uint64_t processed;
} esk_sched;

es_status esk_sched_init(esk_sched *s, esk_alloc *a, size_t max_events, uint32_t max_delta_cycles);
void esk_sched_free(esk_sched *s);
void esk_sched_reset(esk_sched *s);
/* schedule at `t` (>= now, or == now for a delta event); ES_ETIME in the past, ES_ELIMIT when full */
es_status esk_sched_at(esk_sched *s, es_time_t t, esk_event_class cls,
                       void (*fn)(void *ctx, void *arg, es_time_t now), void *ctx, void *arg,
                       esk_event_handle *handle);
/* cancel a pending event; a stale or zero handle is ignored */
void esk_sched_cancel(esk_sched *s, esk_event_handle h);
/* time of the next pending event, or ES_TIME_MAX */
es_time_t esk_sched_next_time(const esk_sched *s);
/* time of the next pending event of any class but `cls` (an island clips its step to this; other islands' steps cannot change its sources) */
es_time_t esk_sched_next_time_excluding(const esk_sched *s, esk_event_class cls);
/* pop and run the next event if its time <= until; returns 1 if one ran, 0 if none,
 * -1 if the delta-cycle bound was exceeded (delta_overflow is set and the event dropped) */
int esk_sched_run_one(esk_sched *s, es_time_t until);
/* run everything up to and including `until`, then set now = until;
 * returns ES_OK, or ES_ELIMIT on delta overflow (now stays at the overflow time) */
es_status esk_sched_advance(esk_sched *s, es_time_t until);
size_t esk_sched_pending(const esk_sched *s);

#endif
