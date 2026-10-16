/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "time/sched.h"
#include <string.h>

es_status esk_sched_init(esk_sched *s, esk_alloc *a, size_t max_events, uint32_t max_delta_cycles)
{
    memset(s, 0, sizeof *s);
    s->a = a;
    s->max = max_events ? max_events : 1048576;
    s->max_delta_cycles = max_delta_cycles ? max_delta_cycles : 10000;
    return ES_OK;
}

void esk_sched_free(esk_sched *s)
{
    esk_free(s->a, s->heap, s->cap * sizeof *s->heap);
    esk_free(s->a, s->gens, s->gens_cap * sizeof *s->gens);
    esk_free(s->a, s->free_ids, s->free_cap * sizeof *s->free_ids);
    s->heap = NULL;
    s->gens = NULL;
    s->free_ids = NULL;
    s->cap = s->len = s->gens_cap = s->ngens = s->free_cap = s->nfree = 0;
}

void esk_sched_reset(esk_sched *s)
{
    size_t i;
    s->stop_requested = 0;
    s->len = 0;
    s->now = 0;
    s->seq = 0;
    s->delta_cycles = 0;
    s->delta_overflow = 0;
    s->last_signal_time = ES_TIME_MAX;
    s->processed = 0;
    /* invalidate all handles */
    for (i = 0; i < s->ngens; i++)
        s->gens[i]++;
    s->nfree = 0;
    for (i = 0; i < s->ngens; i++)
        s->free_ids[s->nfree++] = (uint32_t)i;
}

static int less(const esk_event *x, const esk_event *y)
{
    if (x->time != y->time)
        return x->time < y->time;
    if (x->cls != y->cls)
        return x->cls < y->cls;
    return x->seq < y->seq;
}

static void sift_up(esk_sched *s, size_t i)
{
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (!less(&s->heap[i], &s->heap[p]))
            break;
        {
            esk_event t = s->heap[i];
            s->heap[i] = s->heap[p];
            s->heap[p] = t;
        }
        i = p;
    }
}

static void sift_down(esk_sched *s, size_t i)
{
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, m = i;
        if (l < s->len && less(&s->heap[l], &s->heap[m]))
            m = l;
        if (r < s->len && less(&s->heap[r], &s->heap[m]))
            m = r;
        if (m == i)
            break;
        {
            esk_event t = s->heap[i];
            s->heap[i] = s->heap[m];
            s->heap[m] = t;
        }
        i = m;
    }
}

static int alloc_id(esk_sched *s, uint32_t *id, uint32_t *gen)
{
    if (s->nfree) {
        *id = s->free_ids[--s->nfree];
    } else {
        if (s->ngens == s->gens_cap) {
            size_t ncap = s->gens_cap ? s->gens_cap * 2 : 64;
            uint32_t *g = (uint32_t *)esk_realloc(s->a, s->gens, s->gens_cap * sizeof *g, ncap * sizeof *g);
            uint32_t *f;
            if (!g)
                return -1;
            s->gens = g;
            f = (uint32_t *)esk_realloc(s->a, s->free_ids, s->free_cap * sizeof *f, ncap * sizeof *f);
            if (!f)
                return -1;
            s->free_ids = f;
            s->gens_cap = s->free_cap = ncap;
        }
        s->gens[s->ngens] = 1;
        *id = (uint32_t)s->ngens++;
    }
    *gen = s->gens[*id];
    return 0;
}

static void release_id(esk_sched *s, uint32_t id)
{
    s->gens[id]++;
    if (s->nfree < s->free_cap)
        s->free_ids[s->nfree++] = id;
}

es_status esk_sched_at(esk_sched *s, es_time_t t, esk_event_class cls,
                       void (*fn)(void *ctx, void *arg, es_time_t now), void *ctx, void *arg,
                       esk_event_handle *handle)
{
    esk_event *e;
    uint32_t id, gen;
    if (!fn)
        return ES_EINVAL;
    if (t < s->now)
        return ES_ETIME;
    if (s->len >= s->max)
        return ES_ELIMIT;
    if (s->len == s->cap) {
        size_t ncap = s->cap ? s->cap * 2 : 256;
        esk_event *h = (esk_event *)esk_realloc(s->a, s->heap, s->cap * sizeof *h, ncap * sizeof *h);
        if (!h)
            return ES_ENOMEM;
        s->heap = h;
        s->cap = ncap;
    }
    if (alloc_id(s, &id, &gen))
        return ES_ENOMEM;
    e = &s->heap[s->len++];
    e->time = t;
    e->seq = ++s->seq;
    e->cls = (uint8_t)cls;
    e->cancelled = 0;
    e->id = id;
    e->gen = gen;
    e->fn = fn;
    e->ctx = ctx;
    e->arg = arg;
    sift_up(s, s->len - 1);
    if (handle)
        *handle = ((esk_event_handle)gen << 32) | (esk_event_handle)(id + 1);
    return ES_OK;
}

void esk_sched_cancel(esk_sched *s, esk_event_handle h)
{
    uint32_t id, gen;
    size_t i;
    if (!h)
        return;
    id = (uint32_t)(h & 0xffffffffu) - 1;
    gen = (uint32_t)(h >> 32);
    if (id >= s->ngens || s->gens[id] != gen)
        return;
    /* linear scan: cancellation is rare relative to scheduling */
    for (i = 0; i < s->len; i++) {
        if (s->heap[i].id == id && s->heap[i].gen == gen) {
            s->heap[i].cancelled = 1;
            return;
        }
    }
}

es_time_t esk_sched_next_time(const esk_sched *s) { return s->len ? s->heap[0].time : ES_TIME_MAX; }

es_time_t esk_sched_next_time_excluding(const esk_sched *s, esk_event_class cls)
{
    es_time_t best = ES_TIME_MAX;
    size_t i;
    for (i = 0; i < s->len; i++)
        if (s->heap[i].cls != (uint8_t)cls && !s->heap[i].cancelled && s->heap[i].time < best)
            best = s->heap[i].time;
    return best;
}

int esk_sched_run_one(esk_sched *s, es_time_t until)
{
    esk_event e;
    if (!s->len || s->heap[0].time > until)
        return 0;
    e = s->heap[0];
    s->heap[0] = s->heap[--s->len];
    if (s->len)
        sift_down(s, 0);
    release_id(s, e.id);
    if (e.cancelled)
        return 1;
    s->now = e.time;
    if (e.cls == ESK_EV_SIGNAL) {
        if (s->last_signal_time == e.time) {
            if (++s->delta_cycles > s->max_delta_cycles) {
                s->delta_overflow = 1;
                return -1;
            }
        } else {
            s->last_signal_time = e.time;
            s->delta_cycles = 0;
        }
    }
    s->processed++;
    e.fn(e.ctx, e.arg, e.time);
    return 1;
}

es_status esk_sched_advance(esk_sched *s, es_time_t until)
{
    int r;
    if (until < s->now)
        return ES_ETIME;
    while ((r = esk_sched_run_one(s, until)) == 1)
        if (s->stop_requested)
            return ES_OK; /* now stays at the stopping event's time */
    if (r < 0)
        return ES_ELIMIT;
    s->now = until;
    return ES_OK;
}

size_t esk_sched_pending(const esk_sched *s) { return s->len; }
