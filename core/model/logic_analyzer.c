/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* logic_analyzer.c -- an 8-channel USB logic analyzer, the Saleae Logic 8
 * (ADR-033, docs/engineering/bench-instruments.md).
 *
 * Its leads load the circuit through their pin models (component.json):
 * 1 MOhm to ground in parallel with 10 pF, high above 1.2 V and low below
 * 0.6 V (the Logic 8's fixed threshold). This model is the sampler behind
 * them:
 *
 *   - it samples the eight inputs at `sample_rate` (100, 50, 40, 25, 20,
 *     10, 8, 5, 4, 2 or 1 MS/s; another value is taken down to the next
 *     of these), its sample clock running from reset. An edge is seen at
 *     the first sample instant at or after it; a change undone before that
 *     instant is not seen at all, which is what a sampler misses. It does
 *     not wake at each sample: each change is placed on the sample grid as
 *     it comes, and a pending change is committed once a later sample
 *     instant is reached, so the record is the sampled one, exactly;
 *   - only the channels in `channels` (a bit mask, all eight by default)
 *     are recorded, the others read 0. The USB link limits the rate by the
 *     channels on (the data sheet's footnote 1): 100 MS/s on up to 3,
 *     50 MS/s on up to 6, 40 MS/s on up to 7, 25 MS/s on all 8; asked for
 *     more, it samples at the fastest the channels on allow, as the Logic 2
 *     software offers no more;
 *   - the `run` input starts a capture (true) or stops it (false). Without
 *     a trigger (`trigger_channel` -1) it records from the start for
 *     `duration` seconds. With one, it waits for that channel's edge
 *     (`trigger_edge`: rising, falling or both), keeping `pretrigger`
 *     (a share of the duration) before it, and records the rest after;
 *   - the capture is held as transitions (the sample index and the eight
 *     inputs), read with esk_logic_analyzer_read (the instrument.read
 *     method) since a sequence number, as the workbench follows it live;
 *     waiting for the trigger, only the pre-trigger window is held, its
 *     first record moving on (`first`, the sequence number it now has, and
 *     its sample moved to the window's start), so a reader starts over when
 *     `first` changes.
 *     The computer's memory is the Logic 8's (it streams): here at most
 *     LA_MAX transitions, after which the capture ends as full.
 *
 * Not modelled: the analog inputs (10 bits, up to 10 MS/s); the leads'
 * inductance; inputs past the absolute maximum. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"

#define LA_CH 8
#define LA_MAX ((size_t)1 << 21) /* transitions held */
#define LA_WAKE_END 1u

enum { LA_IDLE, LA_ARMED, LA_CAPTURING, LA_DONE };

typedef struct la_edge {
    uint64_t k;     /* the sample index (time k * period from reset) */
    uint8_t bits;   /* the eight inputs at that sample */
} la_edge;

typedef struct la_state {
    esk_pin *in[LA_CH];
    es_time_t period;      /* ps per sample */
    double rate;           /* S/s */
    unsigned mask;         /* enabled channels */
    int trig_ch;           /* -1: none */
    int trig_edge;         /* 1 rising, -1 falling, 0 both */
    double pre;            /* share of the duration before the trigger */
    uint64_t dur_samples;  /* the capture's length in samples */
    int mode;
    int full;
    uint64_t start_k, trig_k, end_k;
    la_edge *edges;
    size_t n, cap;
    uint64_t seq0;         /* the sequence number of edges[0] (edges dropped before the trigger count on) */
    int pend_valid;
    la_edge pend;
    uint8_t last;          /* the inputs at the last committed edge */
    int warned;            /* the rate limit said once */
} la_state;

static const double RATES[] = { 100e6, 50e6, 40e6, 25e6, 20e6, 10e6, 8e6, 5e6, 4e6, 2e6, 1e6 };

static int popcount8(unsigned v)
{
    int n = 0;
    for (; v; v &= v - 1)
        n++;
    return n;
}

static uint8_t read_inputs(const la_state *s)
{
    unsigned bits = 0;
    int i;
    for (i = 0; i < LA_CH; i++)
        if (s->in[i] && esk_pin_level(s->in[i]) == ESK_L1)
            bits |= 1u << i;
    return (uint8_t)(bits & s->mask);
}

/* the first sample instant at or after `t` */
static uint64_t sample_at(const la_state *s, es_time_t t)
{
    return (uint64_t)((t + s->period - 1) / s->period);
}

static int push(la_state *s, uint64_t k, uint8_t bits)
{
    if (s->n == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 1024;
        la_edge *e;
        if (cap > LA_MAX)
            cap = LA_MAX;
        if (s->n == cap)
            return -1;
        e = (la_edge *)realloc(s->edges, cap * sizeof *e);
        if (!e)
            return -1;
        s->edges = e;
        s->cap = cap;
    }
    s->edges[s->n].k = k;
    s->edges[s->n].bits = bits;
    s->n++;
    return 0;
}

/* drop what is older than `from`, keeping the inputs as they were then as the first record */
static void keep_from(la_state *s, uint64_t from)
{
    size_t i = 0;
    uint8_t then;
    if (!s->n || s->edges[0].k >= from)
        return;
    while (i + 1 < s->n && s->edges[i + 1].k <= from)
        i++;
    then = s->edges[i].bits;
    /* edges[i] becomes the window's first record, at `from` */
    s->seq0 += i;
    memmove(s->edges, s->edges + i, (s->n - i) * sizeof *s->edges);
    s->n -= i;
    s->edges[0].k = from;
    s->edges[0].bits = then;
}

static void finish(esk_comp *c)
{
    la_state *s = (la_state *)c->state;
    s->mode = LA_DONE;
    esk_comp_wake_cancel(c, LA_WAKE_END);
}

static void begin_post(esk_comp *c, uint64_t k)
{
    la_state *s = (la_state *)c->state;
    uint64_t post = s->dur_samples - (uint64_t)((double)s->dur_samples * s->pre);
    s->mode = LA_CAPTURING;
    s->end_k = k + post;
    esk_comp_wake_at(c, (es_time_t)(s->end_k * s->period), LA_WAKE_END);
}

/* a sample's inputs, recorded when they differ from the last; the trigger looked for */
static void commit(esk_comp *c, uint64_t k, uint8_t bits)
{
    la_state *s = (la_state *)c->state;
    uint8_t old = s->last;
    if (s->mode != LA_ARMED && s->mode != LA_CAPTURING)
        return;
    if (s->mode == LA_CAPTURING && k > s->end_k)
        return;
    if (bits == old)
        return;
    if (push(s, k, bits)) {
        s->full = 1;
        finish(c);
        return;
    }
    s->last = bits;
    if (s->mode == LA_ARMED) {
        int was = (old >> s->trig_ch) & 1, now = (bits >> s->trig_ch) & 1;
        if (was != now && (s->trig_edge == 0 || (s->trig_edge > 0) == (now == 1))) {
            uint64_t pre = (uint64_t)((double)s->dur_samples * s->pre);
            uint64_t from = k > pre ? (k - pre > s->start_k ? k - pre : s->start_k) : s->start_k;
            s->trig_k = k;
            keep_from(s, from);
            s->start_k = from; /* the capture is the window it keeps: it starts there */
            begin_post(c, k);
        } else if (s->n > 4096) {
            /* waiting: only the pre-trigger window need be kept */
            uint64_t pre = (uint64_t)((double)s->dur_samples * s->pre) + 1;
            if (k > pre)
                keep_from(s, k - pre > s->start_k ? k - pre : s->start_k);
        }
    }
}

/* a pending change whose sample instant is now behind `k` is committed */
static void settle(esk_comp *c, uint64_t k)
{
    la_state *s = (la_state *)c->state;
    if (s->pend_valid && s->pend.k < k) {
        s->pend_valid = 0;
        commit(c, s->pend.k, s->pend.bits);
    }
}

static void la_in(esk_comp *c, esk_pin *p)
{
    la_state *s = (la_state *)c->state;
    uint64_t k = sample_at(s, esk_comp_now(c));
    ES_UNUSED(p);
    settle(c, k);
    /* the sample at k sees the inputs as they are after every change up to its instant */
    s->pend.k = k;
    s->pend.bits = read_inputs(s);
    s->pend_valid = 1;
}

static void la_time(esk_comp *c, uint32_t token)
{
    la_state *s = (la_state *)c->state;
    if (token != LA_WAKE_END || s->mode != LA_CAPTURING)
        return;
    /* the last sample: what is pending at or before it counts */
    if (s->pend_valid && s->pend.k <= s->end_k) {
        s->pend_valid = 0;
        commit(c, s->pend.k, s->pend.bits);
    }
    s->mode = LA_DONE;
}

static void configure(esk_comp *c)
{
    la_state *s = (la_state *)c->state;
    double rate = 10e6, dur = 1.0, pre = 0.1;
    int64_t mask = 0xff, ch = -1;
    const char *edge;
    size_t i;
    esk_comp_param_double(c, "sample_rate", &rate);
    esk_comp_param_double(c, "duration", &dur);
    esk_comp_param_double(c, "pretrigger", &pre);
    esk_comp_param_int(c, "channels", &mask);
    esk_comp_param_int(c, "trigger_channel", &ch);
    edge = esk_comp_param_str(c, "trigger_edge", "rising");
    s->mask = (unsigned)mask & 0xffu;
    {
        /* the fastest rate the channels on allow (data sheet, footnote 1) */
        int on = popcount8(s->mask);
        double most = on <= 3 ? 100e6 : on <= 6 ? 50e6 : on == 7 ? 40e6 : 25e6;
        if (rate > most * 1.0001) {
            if (!s->warned)
                esk_comp_diag(c, ES_SEV_WARNING, "LA_RATE", "%g MS/s is not offered with %d channels on; capturing at %g MS/s", rate / 1e6, on, most / 1e6);
            s->warned = 1;
            rate = most;
        }
    }
    for (i = 0; i < ES_COUNTOF(RATES); i++)
        if (rate >= RATES[i] * 0.999)
            break;
    s->rate = i < ES_COUNTOF(RATES) ? RATES[i] : RATES[ES_COUNTOF(RATES) - 1];
    s->period = (es_time_t)(1e12 / s->rate + 0.5);
    if (dur <= 0)
        dur = 1.0;
    s->dur_samples = (uint64_t)(dur * s->rate + 0.5);
    s->pre = pre < 0 ? 0 : pre > 1 ? 1 : pre;
    s->trig_ch = ch >= 0 && ch < LA_CH && ((s->mask >> ch) & 1) ? (int)ch : -1;
    s->trig_edge = !strcmp(edge, "falling") ? -1 : !strcmp(edge, "both") ? 0 : 1;
}

static void stop_capture(esk_comp *c)
{
    la_state *s = (la_state *)c->state;
    if (s->mode == LA_CAPTURING || s->mode == LA_ARMED) {
        settle(c, sample_at(s, esk_comp_now(c)) + 1);
        s->end_k = sample_at(s, esk_comp_now(c));
        finish(c);
    }
}

static void start_capture(esk_comp *c)
{
    la_state *s = (la_state *)c->state;
    configure(c);
    esk_comp_wake_cancel(c, LA_WAKE_END);
    s->n = 0;
    s->seq0 = 0;
    s->full = 0;
    s->pend_valid = 0;
    s->start_k = sample_at(s, esk_comp_now(c));
    s->trig_k = 0;
    s->last = read_inputs(s);
    if (push(s, s->start_k, s->last)) {
        s->full = 1;
        s->mode = LA_DONE;
        return;
    }
    if (s->trig_ch >= 0) {
        s->mode = LA_ARMED;
    } else {
        s->mode = LA_CAPTURING;
        s->trig_k = s->start_k;
        s->end_k = s->start_k + s->dur_samples;
        esk_comp_wake_at(c, (es_time_t)(s->end_k * s->period), LA_WAKE_END);
    }
}

static es_status la_create(esk_comp *c)
{
    la_state *s = (la_state *)c->state;
    int i;
    char name[4];
    for (i = 0; i < LA_CH; i++) {
        name[0] = 'D';
        name[1] = (char)('0' + i);
        name[2] = 0;
        s->in[i] = esk_comp_pin(c, name);
        if (s->in[i])
            esk_pin_watch(s->in[i], 1);
    }
    configure(c);
    return ES_OK;
}

static void la_destroy(esk_comp *c)
{
    la_state *s = (la_state *)c->state;
    free(s->edges);
    s->edges = NULL;
    s->n = s->cap = 0;
}

static void la_reset(esk_comp *c)
{
    la_state *s = (la_state *)c->state;
    s->mode = LA_IDLE;
    s->n = 0;
    s->seq0 = 0;
    s->full = 0;
    s->pend_valid = 0;
    configure(c);
    if (esk_comp_input_bool(c, "run", 0))
        start_capture(c);
}

static es_status la_set(esk_comp *c, const char *input, const esk_json *value)
{
    if (strcmp(input, "run"))
        return ES_EINVAL;
    if (value && ((value->type == ESK_JSON_BOOL && value->u.boolean) || (value->type == ESK_JSON_NUMBER && value->u.number.value != 0)))
        start_capture(c);
    else
        stop_capture(c);
    return ES_OK;
}

static const char *mode_name(int m)
{
    return m == LA_ARMED ? "armed" : m == LA_CAPTURING ? "capturing" : m == LA_DONE ? "done" : "idle";
}

static void la_observe(esk_comp *c, esk_json *out)
{
    la_state *s = (la_state *)c->state;
    esk_alloc *a = c->e->a;
    if (s->mode == LA_CAPTURING || s->mode == LA_ARMED)
        settle(c, sample_at(s, esk_comp_now(c)));
    esk_json_set_string(a, out, "state", mode_name(s->mode));
    esk_json_set_number(a, out, "rate", s->rate);
    esk_json_set_integer(a, out, "channels_on", (int64_t)s->mask);
    esk_json_set_integer(a, out, "transitions", (int64_t)s->n); /* what the capture holds now */
    esk_json_set_bool(a, out, "full", s->full);
    esk_json_set_integer(a, out, "inputs", (int64_t)read_inputs(s));
}

es_status esk_logic_analyzer_read(esk_comp *c, uint64_t since, size_t max, esk_json *out)
{
    la_state *s;
    esk_alloc *a = c->e->a;
    esk_json *list;
    size_t i, from;
    char num[32];
    if (!c->ops || strcmp(c->ops->name, "logic_analyzer"))
        return ES_EINVAL;
    s = (la_state *)c->state;
    if (s->mode == LA_CAPTURING || s->mode == LA_ARMED)
        settle(c, sample_at(s, esk_comp_now(c)));
    list = esk_json_new_array(a);
    if (!list)
        return ES_ENOMEM;
    from = since > s->seq0 ? (size_t)(since - s->seq0) : 0;
    if (!max)
        max = 65536;
    for (i = from; i < s->n && i - from < max; i++) {
        esk_json *e = esk_json_new_array(a);
        if (!e)
            break;
        /* sample indices exceed a double's integers only after weeks at 100 MS/s */
        esk_json_array_push(a, e, esk_json_new_number(a, (double)(s->edges[i].k - s->start_k)));
        esk_json_array_push(a, e, esk_json_new_number(a, s->edges[i].bits));
        esk_json_array_push(a, list, e);
    }
    esk_json_set_string(a, out, "state", mode_name(s->mode));
    esk_json_set_number(a, out, "sample_rate", s->rate);
    snprintf(num, sizeof num, "%llu", (unsigned long long)(s->start_k * s->period));
    esk_json_set_string(a, out, "start_ps", num);
    snprintf(num, sizeof num, "%llu", (unsigned long long)s->period);
    esk_json_set_string(a, out, "period_ps", num);
    esk_json_set_number(a, out, "trigger", s->trig_ch >= 0 && s->mode != LA_ARMED ? (double)(s->trig_k - s->start_k) : -1);
    esk_json_set_number(a, out, "end", s->mode == LA_DONE || s->mode == LA_CAPTURING ? (double)(s->end_k - s->start_k) : -1);
    esk_json_set_integer(a, out, "channels", (int64_t)s->mask);
    esk_json_set_integer(a, out, "first", (int64_t)s->seq0);
    esk_json_set_integer(a, out, "since", (int64_t)(s->seq0 + from));
    esk_json_set_integer(a, out, "next", (int64_t)(s->seq0 + i));
    esk_json_set_bool(a, out, "full", s->full);
    esk_json_object_set(a, out, "transitions", list);
    return ES_OK;
}

const esk_model_ops esk_logic_analyzer_ops = { "logic_analyzer", sizeof(la_state), la_create, la_destroy, la_reset, la_in,
                                               la_time, la_set, la_observe, NULL, 0 };
