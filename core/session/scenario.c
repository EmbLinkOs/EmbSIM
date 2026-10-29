/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* scenario.c -- scenarios and assertions (SPEC-007 §2) and the run report
 * (SPEC-008 §8). */
#include "session/session_priv.h"
#include "mcu/mcu.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "analog/analog.h"
#include "host/host.h"
#include "util/num.h"
#include "util/str.h"
#include "util/match.h"
#include "project/loader.h"

#define RTOS_TRACKED 64

/* an rtos assertion's watch over the run (issue #96): each task's blocked
 * stretch, sampled every millisecond, and its stack's low-water mark */
typedef struct rtos_watch {
    struct {
        int64_t tcb;
        char name[24];
        es_time_t since;
        int blocked;
        int64_t sig; /* the wait's identity: a new one is a new stretch */
    } tk[RTOS_TRACKED];
    int ntk, samples;
    es_time_t worst;    /* the longest blocked stretch */
    char worst_task[24];
    int64_t min_stack;  /* the least stack_free_min seen, -1 none */
    char min_task[24];
    char err[160];
} rtos_watch;

typedef struct assertion {
    const esk_json *def;
    es_time_t t0, t1;   /* at: t0 == t1; between: window; by: [0, t1]; always: whole run */
    int mode;           /* 0 at, 1 between, 2 by, 3 always */
    int done, ok;
    esk_json *observed;
    char reason[160];
    rtos_watch *rw;     /* an rtos assertion's */
} assertion;

static int name_listed(const esk_json *list, const char *name)
{
    size_t i;
    for (i = 0; i < esk_json_len(list); i++)
        if (esk_str_eq(esk_json_str(esk_json_at(list, i)), name))
            return 1;
    return 0;
}

static esk_comp *rtos_mcu(es_session *s, const char *board)
{
    size_t i;
    for (i = 0; i < s->boards.len; i++) {
        esk_board_inst *bi = (esk_board_inst *)s->boards.data[i];
        if (bi->mcu && (!board || !strcmp(board, bi->id)))
            return bi->mcu;
    }
    return NULL;
}

/* one sample of the tasks: who is blocked since when, how low each stack
 * has been */
static void rtos_sample(es_session *s, assertion *a)
{
    const esk_json *spec = esk_json_get(a->def, "rtos"), *ignore = esk_json_get(spec, "ignore");
    rtos_watch *w = a->rw;
    esk_comp *mc = rtos_mcu(s, esk_json_get_str(spec, "board", NULL));
    esk_json *r;
    const esk_json *tasks;
    es_time_t now = s->eng.sched.now;
    size_t i;
    int k;
    if (!w || w->err[0])
        return;
    if (!mc) {
        esk_snprintf(w->err, sizeof w->err, "no board %s with an MCU", esk_json_get_str(spec, "board", "(any)"));
        return;
    }
    r = esk_mcu_rtos_json(mc, s->a, w->err, sizeof w->err);
    if (!r)
        return;
    w->err[0] = 0;
    if (!esk_json_get_bool(r, "started", 0)) { /* the scheduler not started yet */
        esk_json_free(s->a, r);
        return;
    }
    w->samples++;
    tasks = esk_json_get(r, "tasks");
    for (i = 0; i < esk_json_len(tasks); i++) {
        const esk_json *t = esk_json_at(tasks, i);
        const char *name = esk_json_get_str(t, "name", "");
        int64_t tcb = esk_json_get_int(t, "tcb", 0);
        int blocked = esk_str_eq(esk_json_get_str(t, "state", ""), "blocked");
        const esk_json *wj = esk_json_get(t, "waits");
        /* a task that woke and blocked again between two samples waits on
         * a new wake tick (or another queue, index or list) */
        int64_t sig = esk_json_get_int(wj, "until_tick", -1) * 31 + esk_json_get_int(wj, "queue", 0) * 7 +
                      esk_json_get_int(wj, "list", 0) * 3 + esk_json_get_int(wj, "index", 0);
        if (esk_json_get(t, "stack_free_min")) {
            int64_t f = esk_json_get_int(t, "stack_free_min", 0);
            if (w->min_stack < 0 || f < w->min_stack) {
                w->min_stack = f;
                esk_snprintf(w->min_task, sizeof w->min_task, "%s", name);
            }
        }
        if (name_listed(ignore, name))
            continue;
        for (k = 0; k < w->ntk && w->tk[k].tcb != tcb; k++)
            ;
        if (k == w->ntk) {
            if (w->ntk == RTOS_TRACKED)
                continue;
            w->ntk++;
            w->tk[k].tcb = tcb;
            w->tk[k].blocked = 0;
        }
        esk_snprintf(w->tk[k].name, sizeof w->tk[k].name, "%s", name);
        if (blocked && (!w->tk[k].blocked || w->tk[k].sig != sig))
            w->tk[k].since = now;
        w->tk[k].blocked = blocked;
        w->tk[k].sig = sig;
        if (blocked && now - w->tk[k].since > w->worst) {
            w->worst = now - w->tk[k].since;
            esk_snprintf(w->worst_task, sizeof w->worst_task, "%s", name);
        }
    }
    esk_json_free(s->a, r);
}

static void rtos_evaluate(es_session *s, assertion *a)
{
    const esk_json *spec = esk_json_get(a->def, "rtos");
    rtos_watch *w = a->rw;
    es_time_t limit = 0;
    char t1[32], t2[32];
    esk_json *obs = esk_json_new_object(s->a);
    if (!w)
        return;
    rtos_sample(s, a); /* the end's state too */
    if (obs) {
        esk_json_set_integer(s->a, obs, "samples", w->samples);
        esk_json_set_u64_string(s->a, obs, "blocked_max_ps", w->worst);
        if (w->worst_task[0])
            esk_json_set_string(s->a, obs, "blocked_max_task", w->worst_task);
        if (w->min_stack >= 0) {
            esk_json_set_integer(s->a, obs, "stack_free_min", w->min_stack);
            esk_json_set_string(s->a, obs, "stack_free_min_task", w->min_task);
        }
        a->observed = obs;
    }
    if (w->err[0] || !w->samples) {
        a->ok = 0;
        esk_snprintf(a->reason, sizeof a->reason, "%s", w->err[0] ? w->err : "the scheduler never started");
        return;
    }
    if (esk_json_get(spec, "stack_free_min") && w->min_stack >= 0 &&
        w->min_stack < esk_json_get_int(spec, "stack_free_min", 0)) {
        a->ok = 0;
        esk_snprintf(a->reason, sizeof a->reason, "task %s's stack came within %lld bytes of its end (at least %lld wanted)",
                     w->min_task, (long long)w->min_stack, (long long)esk_json_get_int(spec, "stack_free_min", 0));
        return;
    }
    if (esk_json_get(spec, "stack_free_min") && w->min_stack < 0) {
        a->ok = 0;
        esk_snprintf(a->reason, sizeof a->reason,
                     "no stack's high-water mark is known (the kernel fills stacks with 0xA5 only with stack checking, "
                     "the trace facility or uxTaskGetStackHighWaterMark)");
        return;
    }
    if (esk_json_get(spec, "blocked_max") && esk_json_to_time(esk_json_get(spec, "blocked_max"), &limit) == 0 &&
        w->worst > limit) {
        esk_format_time(t1, sizeof t1, w->worst);
        esk_format_time(t2, sizeof t2, limit);
        a->ok = 0;
        esk_snprintf(a->reason, sizeof a->reason, "task %s stayed blocked for %s (at most %s wanted)", w->worst_task, t1, t2);
    }
}

static int expect_value(es_session *s, const esk_json *expect, double v, esk_probe *p, char *why, size_t cap)
{
    const esk_json *j;
    if ((j = esk_json_get(expect, "logic")) != NULL) {
        esk_logic want = esk_logic_from_name(esk_json_str(j));
        esk_logic got = (esk_logic)(int)v;
        if (p && p->kind != ESK_PROBE_DIGITAL) {
            esk_snprintf(why, cap, "logic expectations need a digital probe");
            return 0;
        }
        if (got != want) {
            esk_snprintf(why, cap, "logic %s, expected %s", esk_logic_name(got), esk_logic_name(want));
            return 0;
        }
    }
    if (((j = esk_json_get(expect, "min")) != NULL || (j = esk_json_get(expect, "min_v")) != NULL) && j->type == ESK_JSON_NUMBER && v < j->u.number.value) {
        esk_snprintf(why, cap, "%g below minimum %g", v, j->u.number.value);
        return 0;
    }
    if (((j = esk_json_get(expect, "max")) != NULL || (j = esk_json_get(expect, "max_v")) != NULL) && j->type == ESK_JSON_NUMBER && v > j->u.number.value) {
        esk_snprintf(why, cap, "%g above maximum %g", v, j->u.number.value);
        return 0;
    }
    if ((j = esk_json_get(expect, "equals")) != NULL) {
        double want = j->type == ESK_JSON_NUMBER ? j->u.number.value : j->type == ESK_JSON_BOOL ? (j->u.boolean ? 1.0 : 0.0) : (double)esk_logic_from_name(esk_json_str(j));
        double tol = esk_json_get_num(expect, "tolerance", 1e-9);
        if (fabs(v - want) > tol) {
            esk_snprintf(why, cap, "%g, expected %g", v, want);
            return 0;
        }
    }
    ES_UNUSED(s);
    return 1;
}

static double sample_at(const esk_probe *p, es_time_t t, int *have)
{
    size_t i;
    double v = 0;
    *have = 0;
    for (i = 0; i < p->len; i++) {
        const es_sample_t *smp = &p->ring[(p->head + i) % p->cap];
        if (smp->time_ps <= t) {
            v = smp->value;
            *have = 1;
        } else {
            break;
        }
    }
    return v;
}

/* Integrate the recorded sample-and-hold signal, not the number of samples.
 * A retained anchor proves coverage of the left edge; the scheduler proves the
 * right edge. The ring only evicts its oldest records, never an interior span. */
int esk_probe_window(const esk_probe *p, const char *measure, es_time_t from,
                     es_time_t to, es_time_t through, double *out, char *why, size_t cap)
{
    size_t i;
    es_time_t cursor = from;
    double value, mean = 0, rms = 0;
    int have, duty = !strcmp(measure, "duty_cycle");
    if (strcmp(measure, "mean") && strcmp(measure, "rms") && !duty) {
        esk_snprintf(why, cap, "unknown waveform measurement %s", measure);
        return 0;
    }
    if (to <= from || to > through || p->kind == ESK_PROBE_SERIAL ||
        (duty && p->kind != ESK_PROBE_DIGITAL)) {
        esk_snprintf(why, cap, "measurement needs a nonempty completed window and a numeric probe (digital for duty cycle)");
        return 0;
    }
    value = sample_at(p, from, &have);
    if (!have) {
        esk_snprintf(why, cap, "measurement window begins before retained probe history");
        return 0;
    }
    for (i = 0; i <= p->len; i++) {
        const es_sample_t *sample = i < p->len ? &p->ring[(p->head + i) % p->cap] : NULL;
        es_time_t next = sample && sample->time_ps < to ? sample->time_ps : to;
        if (sample && sample->time_ps <= from)
            continue;
        if (next > cursor) {
            double weight = (double)(next - cursor) / (double)(to - from);
            if (!isfinite(value) || (p->kind == ESK_PROBE_DIGITAL && value != 0 && value != 1)) {
                esk_snprintf(why, cap, "measurement contains a nonfinite or unknown/high-impedance interval");
                return 0;
            }
            mean += value * weight;
            rms = hypot(rms, value * sqrt(weight));
            cursor = next;
        }
        if (cursor == to)
            break;
        if (sample)
            value = sample->value;
    }
    *out = !strcmp(measure, "rms") ? rms : mean;
    return isfinite(*out);
}

/* a diagnostic code against a pattern: exact, or a prefix ending in '*' */
static int code_matches(const char *pattern, const char *code)
{
    size_t n;
    if (!pattern || !code)
        return 0;
    n = strlen(pattern);
    if (n && pattern[n - 1] == '*')
        return !strncmp(pattern, code, n - 1);
    return !strcmp(pattern, code);
}

static int code_listed(const esk_json *list, const char *code)
{
    size_t k;
    for (k = 0; k < esk_json_len(list); k++)
        if (code_matches(esk_json_str(esk_json_at(list, k)), code))
            return 1;
    return 0;
}

static int severity_rank(const char *sev)
{
    return !strcmp(sev, "fatal") ? 3 : !strcmp(sev, "error") ? 2 : !strcmp(sev, "warning") ? 1 : 0;
}

/* a serial "matches": the pattern's last match in the text (the latest
 * print), and its group as a number against equals, min/max or a part's
 * output now (equals_output), within tolerance */
static void serial_matches(es_session *s, assertion *a, const esk_json *expect, const char *text)
{
    const char *pattern = esk_json_get_str(expect, "matches", "");
    const char *ref = esk_json_get_str(expect, "equals_output", NULL);
    const esk_json *eq = esk_json_get(expect, "equals");
    double tol = esk_json_get_num(expect, "tolerance", 0), v, want = 0;
    esk_match m;
    char num[64];
    size_t n;
    int r = esk_match_last(pattern, text, strlen(text), &m), have_want = 0;
    if (r < 0) {
        a->ok = 0;
        esk_snprintf(a->reason, sizeof a->reason, "pattern \"%s\" uses what the matcher does not know (alternation, counted repeats, nested groups)", pattern);
        return;
    }
    if (!r) {
        a->ok = 0;
        esk_snprintf(a->reason, sizeof a->reason, "text does not match \"%s\"", pattern);
        return;
    }
    if (!m.has_group || (!ref && !esk_json_is(eq, ESK_JSON_NUMBER) && !esk_json_get(expect, "min") && !esk_json_get(expect, "max")))
        return; /* the match itself is what was expected */
    n = m.group_end - m.group_start < sizeof num - 1 ? m.group_end - m.group_start : sizeof num - 1;
    memcpy(num, text + m.group_start, n);
    num[n] = 0;
    {
        char *end = NULL;
        v = strtod(num, &end);
        if (!n || !end || *end) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "captured \"%s\" is not a number", num);
            return;
        }
    }
    esk_json_free(s->a, a->observed);
    a->observed = esk_json_new_number(s->a, v);
    if (ref) {
        char inst[128], q[64];
        esk_comp *c;
        esk_json *out;
        const esk_json *val;
        if (esk_split_pin_ref(ref, inst, sizeof inst, q, sizeof q) || !(c = esk_engine_comp(&s->eng, inst)) || !c->ops->observe) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "equals_output names %s, which is no part's output", ref);
            return;
        }
        out = esk_json_new_object(s->a);
        if (out)
            c->ops->observe(c, out);
        val = esk_json_get(out, q);
        if (val && val->type == ESK_JSON_NUMBER) {
            want = val->u.number.value;
            have_want = 1;
        }
        esk_json_free(s->a, out);
        if (!have_want) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "%s has no numeric output %s", inst, q);
            return;
        }
    } else if (esk_json_is(eq, ESK_JSON_NUMBER)) {
        want = eq->u.number.value;
        have_want = 1;
    }
    if (have_want && fabs(v - want) > tol) {
        a->ok = 0;
        esk_snprintf(a->reason, sizeof a->reason, "printed %g, expected %g%s%s within %g", v, want, ref ? " from " : "", ref ? ref : "", tol);
    } else if (esk_json_get(expect, "min") && v < esk_json_get_num(expect, "min", 0)) {
        a->ok = 0;
        esk_snprintf(a->reason, sizeof a->reason, "printed %g, below %g", v, esk_json_get_num(expect, "min", 0));
    } else if (esk_json_get(expect, "max") && v > esk_json_get_num(expect, "max", 0)) {
        a->ok = 0;
        esk_snprintf(a->reason, sizeof a->reason, "printed %g, above %g", v, esk_json_get_num(expect, "max", 0));
    }
}

static void evaluate(es_session *s, assertion *a)
{
    const esk_json *def = a->def;
    const char *pname = esk_json_get_str(def, "probe", NULL);
    const esk_json *expect = esk_json_get(def, "expect");
    esk_probe *p = pname ? esk_session_probe_find(s, pname) : NULL;
    char why[160];
    a->done = 1;
    a->ok = 1;
    why[0] = 0;
    if (esk_json_get(def, "measure") && a->mode != 1) {
        a->ok = 0;
        esk_snprintf(a->reason, sizeof a->reason, "measure requires a between window");
        return;
    }
    if (a->mode == 3 && esk_json_get(def, "rtos")) {
        rtos_evaluate(s, a);
        return;
    }
    if (a->mode == 3) {
        const esk_json *dg = esk_json_get(def, "diagnostics");
        const esk_json *forbid = esk_json_get(dg, "forbid"), *allow = esk_json_get(dg, "allow"), *require = esk_json_get(dg, "require");
        const char *worst = esk_json_get_str(dg, "forbid_severity", NULL);
        int floor = worst ? severity_rank(worst) : 99;
        esk_json *all = esk_diag_to_json(&s->diag);
        size_t i, k;
        for (i = 0; a->ok && all && i < esk_json_len(all); i++) {
            const esk_json *d = esk_json_at(all, i);
            const char *code = esk_json_get_str(d, "code", ""), *sev = esk_json_get_str(d, "severity", "info");
            if (code_listed(forbid, code)) {
                a->ok = 0;
                esk_snprintf(a->reason, sizeof a->reason, "diagnostic %s was raised: %s", code, esk_json_get_str(d, "message", ""));
            } else if (severity_rank(sev) >= floor && !code_listed(allow, code)) {
                a->ok = 0;
                esk_snprintf(a->reason, sizeof a->reason, "%s %s was raised: %s", sev, code, esk_json_get_str(d, "message", ""));
            }
        }
        for (k = 0; a->ok && k < esk_json_len(require); k++) {
            const char *want = esk_json_str(esk_json_at(require, k));
            int seen = 0;
            for (i = 0; all && !seen && i < esk_json_len(all); i++)
                seen = code_matches(want, esk_json_get_str(esk_json_at(all, i), "code", ""));
            if (!seen) {
                a->ok = 0;
                esk_snprintf(a->reason, sizeof a->reason, "no diagnostic %s was raised", want ? want : "?");
            }
        }
        esk_json_free(s->a, all);
        return;
    }
    if (!p) {
        a->ok = 0;
        esk_snprintf(a->reason, sizeof a->reason, "unknown probe %s", pname ? pname : "(none)");
        return;
    }
    if (esk_json_get(def, "measure")) {
        const char *measure = esk_json_get_str(def, "measure", "");
        double value = 0;
        es_time_t measured_to = a->t1;
        size_t k;
        int numeric = a->mode == 1 && expect && expect->type == ESK_JSON_OBJECT && esk_json_len(expect) > 0, bound = 0;
        for (k = 0; numeric && k < esk_json_len(expect); k++) {
            const esk_json *v = esk_json_at(expect, k);
            int allowed = !strcmp(v->key, "min") || !strcmp(v->key, "max") ||
                          !strcmp(v->key, "equals") || !strcmp(v->key, "tolerance");
            numeric &= allowed && v->type == ESK_JSON_NUMBER && isfinite(v->u.number.value);
            if (strcmp(v->key, "tolerance")) bound = 1;
            if (!strcmp(v->key, "tolerance") && v->type == ESK_JSON_NUMBER && v->u.number.value < 0) numeric = 0;
        }
        if (!numeric || !bound) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "measure requires between and numeric min/max/equals expectations");
            return;
        }
        /* Scheduling clamps assertion evaluation to until; measurement must
         * still reject a requested window that extends beyond the run. */
        esk_json_to_time(esk_json_at(esk_json_get(def, "between"), 1), &measured_to);
        a->ok = esk_probe_window(p, measure, a->t0, measured_to, s->eng.sched.now, &value, why, sizeof why);
        if (a->ok) {
            a->observed = esk_json_new_number(s->a, value);
            a->ok = expect_value(s, expect, value, NULL, why, sizeof why);
        }
        if (!a->ok) esk_snprintf(a->reason, sizeof a->reason, "%s", why[0] ? why : "invalid waveform measurement");
        return;
    }
    /* a text expectation on a memory probe: its bytes up to the first NUL */
    if (p->kind == ESK_PROBE_VALUE && p->mem_len &&
        (esk_json_get(expect, "contains") || esk_json_get(expect, "not_contains") ||
         esk_json_is(esk_json_get(expect, "equals"), ESK_JSON_STRING))) {
        const char *contains = esk_json_get_str(expect, "contains", NULL);
        const char *lacks = esk_json_get_str(expect, "not_contains", NULL);
        const char *equals = esk_json_get_str(expect, "equals", NULL);
        char text[257];
        size_t n = esk_session_probe_bytes(p, (unsigned char *)text, sizeof text - 1), k;
        for (k = 0; k < n && text[k]; k++)
            ;
        text[k] = 0;
        a->observed = esk_json_new_string(s->a, text);
        if (contains && !strstr(text, contains)) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "\"%.60s\" does not contain \"%s\"", text, contains);
        } else if (lacks && strstr(text, lacks)) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "\"%.60s\" contains \"%s\"", text, lacks);
        } else if (equals && strcmp(text, equals) != 0) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "\"%.60s\" is not \"%s\"", text, equals);
        }
        return;
    }
    /* a text expectation on a value probe: the output as a string, now (the
     * assertion's time: `at`, or the end of `between`/`by`) */
    if (p->kind == ESK_PROBE_VALUE && p->comp && p->comp->ops->observe &&
        (esk_json_get(expect, "contains") || esk_json_get(expect, "not_contains") ||
         esk_json_is(esk_json_get(expect, "equals"), ESK_JSON_STRING))) {
        const char *contains = esk_json_get_str(expect, "contains", NULL);
        const char *lacks = esk_json_get_str(expect, "not_contains", NULL);
        const char *equals = esk_json_get_str(expect, "equals", NULL);
        esk_json *out = esk_json_new_object(s->a);
        const esk_json *q;
        char num[48];
        const char *text = "";
        if (out) {
            p->comp->ops->observe(p->comp, out);
            q = esk_json_get(out, p->quantity ? p->quantity : "i");
            if (q && q->type == ESK_JSON_STRING)
                text = q->u.string.data;
            else if (q && q->type == ESK_JSON_NUMBER) {
                esk_snprintf(num, sizeof num, "%g", q->u.number.value);
                text = num;
            } else if (q && q->type == ESK_JSON_BOOL)
                text = q->u.boolean ? "true" : "false";
        }
        a->observed = esk_json_new_string(s->a, text);
        if (contains && !strstr(text, contains)) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "\"%.60s\" does not contain \"%s\"", text, contains);
        } else if (lacks && strstr(text, lacks)) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "\"%.60s\" contains \"%s\"", text, lacks);
        } else if (equals && strcmp(text, equals) != 0) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "\"%.60s\", expected \"%s\"", text, equals);
        }
        esk_json_free(s->a, out);
        return;
    }
    if (p->kind == ESK_PROBE_SERIAL) {
        const char *contains = esk_json_get_str(expect, "contains", NULL);
        const char *lacks = esk_json_get_str(expect, "not_contains", NULL);
        esk_json *out = esk_json_new_object(s->a);
        const char *text = "";
        if (out && p->comp->ops->observe) {
            p->comp->ops->observe(p->comp, out);
            text = esk_json_get_str(out, p->quantity ? p->quantity : "rx_text", "");
        }
        a->observed = esk_json_new_string(s->a, text);
        if (contains && !strstr(text, contains)) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "text does not contain \"%s\"", contains);
        } else if (lacks && strstr(text, lacks)) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "text contains \"%s\"", lacks);
        } else if (esk_json_get_str(expect, "matches", NULL)) {
            serial_matches(s, a, expect, text);
        }
        esk_json_free(s->a, out);
        return;
    }
    if (a->mode == 0) {
        int have;
        double v = sample_at(p, a->t0, &have);
        if (!have)
            v = p->last;
        a->observed = esk_json_new_number(s->a, v);
        a->ok = expect_value(s, expect, v, p, why, sizeof why);
    } else {
        /* between / by: samples in the window plus the value at the window start */
        size_t i;
        int have, any_ok = 0, all_ok = 1, edges = 0, analog = p->kind != ESK_PROBE_DIGITAL;
        double first = sample_at(p, a->t0, &have), last_v = first, v_last = first;
        const esk_json *edges_j = esk_json_get(expect, "edges");
        const esk_json *freq_j = esk_json_get(expect, "frequency_hz");
        double lo = first, hi = first, mid = 0, band = 0;
        int level = 0;
        if (analog && (edges_j || freq_j)) {
            /* threshold at mid-range with 10 % hysteresis before counting edges */
            for (i = 0; i < p->len; i++) {
                const es_sample_t *smp = &p->ring[(p->head + i) % p->cap];
                if (smp->time_ps < a->t0 || smp->time_ps > a->t1)
                    continue;
                if (smp->value < lo) lo = smp->value;
                if (smp->value > hi) hi = smp->value;
            }
            mid = (lo + hi) / 2;
            band = (hi - lo) * 0.1;
            level = first > mid;
        }
        if (have) {
            int ok = expect_value(s, expect, first, p, why, sizeof why);
            any_ok |= ok;
            all_ok &= ok;
        }
        for (i = 0; i < p->len; i++) {
            const es_sample_t *smp = &p->ring[(p->head + i) % p->cap];
            if (smp->time_ps <= a->t0 || smp->time_ps > a->t1)
                continue;
            {
                int ok = expect_value(s, expect, smp->value, p, why, sizeof why);
                any_ok |= ok;
                all_ok &= ok;
            }
            if (analog && (edges_j || freq_j)) {
                if (level && smp->value < mid - band) { level = 0; edges++; }
                else if (!level && smp->value > mid + band) { level = 1; edges++; }
            } else if (smp->value != v_last) {
                edges++;
            }
            v_last = smp->value;
            last_v = smp->value;
        }
        a->observed = esk_json_new_number(s->a, last_v);
        if (edges_j && edges_j->type == ESK_JSON_NUMBER) {
            a->ok = edges == (int)edges_j->u.number.value;
            if (!a->ok) esk_snprintf(why, sizeof why, "%d edges, expected %d", edges, (int)edges_j->u.number.value);
            esk_json_free(s->a, a->observed);
            a->observed = esk_json_new_integer(s->a, edges);
        } else if (freq_j && freq_j->type == ESK_JSON_NUMBER) {
            double span = (double)(a->t1 - a->t0) / 1e12;
            double f = span > 0 ? (double)edges / 2.0 / span : 0.0;
            double tol = esk_json_get_num(expect, "tolerance", 0.05);
            a->ok = fabs(f - freq_j->u.number.value) <= tol * freq_j->u.number.value;
            if (!a->ok) esk_snprintf(why, sizeof why, "%g Hz, expected %g Hz", f, freq_j->u.number.value);
            esk_json_free(s->a, a->observed);
            a->observed = esk_json_new_number(s->a, f);
        } else {
            a->ok = a->mode == 1 ? all_ok : any_ok;
        }
    }
    if (!a->ok)
        esk_snprintf(a->reason, sizeof a->reason, "%s", why[0] ? why : "expectation not met");
}

esk_json *esk_session_report(es_session *s, const char *ended_reason, esk_json *assertions)
{
    esk_json *root = esk_json_new_object(s->a);
    esk_json *proj = esk_json_new_object(s->a), *ended = esk_json_new_object(s->a), *stats = esk_json_new_object(s->a);
    size_t i;
    if (!root || !proj || !ended || !stats) {
        esk_json_free(s->a, root); esk_json_free(s->a, proj); esk_json_free(s->a, ended); esk_json_free(s->a, stats);
        esk_json_free(s->a, assertions);
        return NULL;
    }
    esk_session_perf_check(s);
    esk_json_set_string(s->a, root, "engine", es_version().string);
    esk_json_set_string(s->a, root, "protocol", "1.0");
    esk_json_set_string(s->a, proj, "name", s->name ? s->name : "");
    esk_json_set_string(s->a, proj, "path", s->project_path ? s->project_path : "");
    esk_json_object_set(s->a, root, "project", proj);
    esk_json_set_string(s->a, root, "policy", s->eng.policy == ESK_POLICY_FUNCTIONAL ? "functional" : s->eng.policy == ESK_POLICY_TIMED ? "timed" : "electrical");
    esk_json_set_string(s->a, ended, "reason", ended_reason);
    esk_json_set_u64_string(s->a, ended, "now_ps", s->eng.sched.now);
    esk_json_object_set(s->a, root, "ended", ended);
    esk_json_object_set(s->a, root, "assertions", assertions ? assertions : esk_json_new_array(s->a));
    esk_json_object_set(s->a, root, "diagnostics", esk_diag_to_json(&s->diag));
    esk_json_set_integer(s->a, stats, "events", (int64_t)s->eng.sched.processed);
    esk_json_set_integer(s->a, stats, "signal_events", (int64_t)s->eng.signal_events);
    {
        uint64_t steps = 0, rej = 0, iters = 0;
        for (i = 0; i < s->eng.islands.len; i++) {
            steps += s->eng.islands.data[i]->steps;
            rej += s->eng.islands.data[i]->rejections;
            iters += s->eng.islands.data[i]->newton_iters;
        }
        esk_json_set_integer(s->a, stats, "analog_steps", (int64_t)steps);
        esk_json_set_integer(s->a, stats, "analog_rejections", (int64_t)rej);
        esk_json_set_integer(s->a, stats, "newton_iterations", (int64_t)iters);
    }
    esk_json_set_integer(s->a, stats, "inputs", (int64_t)esk_json_len(s->inputs_log));
    esk_json_set_number(s->a, stats, "wall_time_s", s->wall_time_s);
    esk_json_object_set(s->a, root, "statistics", stats);
    {
        esk_json *inc = esk_json_new_array(s->a);
        if (inc) {
            esk_json_array_push(s->a, inc, esk_json_new_string(s->a, "nets"));
            esk_json_array_push(s->a, inc, esk_json_new_string(s->a, "outputs"));
            esk_json_array_push(s->a, inc, esk_json_new_string(s->a, "probes"));
            esk_json_object_set(s->a, root, "snapshot", esk_session_snapshot(s, inc));
            esk_json_free(s->a, inc);
        }
    }
    {
        /* each board's serial text, by board and UART, beside its console's
         * name: no need to know the MCU's designator on the board */
        esk_json *serial = esk_json_new_object(s->a);
        for (i = 0; serial && i < s->boards.len; i++) {
            esk_board_inst *bi = s->boards.data[i];
            if (bi->mcu)
                esk_json_object_set(s->a, serial, bi->id, esk_mcu_serial_json(bi->mcu, s->a));
        }
        if (serial)
            esk_json_object_set(s->a, root, "serial", serial);
    }
    esk_session_analysis_report(s, root);
    return root;
}

esk_json *esk_scenario_run(es_session *s, const esk_json *scenario, const char *base_dir, es_time_t until_override)
{
    const esk_json *inputs, *asserts;
    assertion *as = NULL;
    size_t na, i, k;
    es_time_t until = 50 * ES_PS_PER_MS;
    esk_json *result;
    es_status st;
    uint64_t t_start = esk_host_monotonic_ms();
    const char *reason = "until";
    int ok_all = 1, sample_rtos = 0;
    ES_UNUSED(base_dir);
    if (!s->loaded)
        return NULL;
    if (esk_json_get_int(scenario, "schema_version", 1) != 1)
        return NULL;
    if (esk_json_to_time(esk_json_get(scenario, "until"), &until) != 0 && !until_override)
        until = 50 * ES_PS_PER_MS;
    if (until_override)
        until = until_override;
    {
        const char *pol = esk_json_get_str(scenario, "policy", NULL);
        if (pol && !s->policy_forced) {
            esk_policy np = !strcmp(pol, "functional") ? ESK_POLICY_FUNCTIONAL : !strcmp(pol, "timed") ? ESK_POLICY_TIMED : ESK_POLICY_ELECTRICAL;
            if (np != s->eng.policy) {
                esk_json *proj = esk_json_clone(s->a, s->project);
                char *base = esk_strdup(s->a, s->base_dir);
                s->eng.policy = np;
                if (proj && esk_project_load(s, proj, base) != ES_OK) {
                    esk_json_free(s->a, proj);
                    esk_strfree(s->a, base);
                    return esk_session_report(s, "load_failed", NULL);
                }
                esk_json_free(s->a, proj);
                esk_strfree(s->a, base);
            }
        }
    }
    st = esk_session_do_reset(s);
    if (st != ES_OK)
        return esk_session_report(s, "reset_failed", NULL);
    /* inputs */
    inputs = esk_json_get(scenario, "inputs");
    for (i = 0; i < esk_json_len(inputs); i++) {
        const esk_json *in = esk_json_at(inputs, i);
        es_time_t at = 0;
        esk_json_to_time(esk_json_get(in, "at"), &at);
        esk_session_apply_input(s, esk_json_get_str(in, "target", NULL), esk_json_get_str(in, "input", NULL), esk_json_get(in, "value"), at, NULL, NULL);
    }
    /* assertions */
    asserts = esk_json_get(scenario, "assertions");
    na = esk_json_len(asserts);
    as = (assertion *)esk_calloc(s->a, na ? na : 1, sizeof *as);
    if (!as)
        return NULL;
    for (i = 0; i < na; i++) {
        const esk_json *d = esk_json_at(asserts, i);
        as[i].def = d;
        if (esk_json_get(d, "at")) {
            as[i].mode = 0;
            esk_json_to_time(esk_json_get(d, "at"), &as[i].t0);
            as[i].t1 = as[i].t0;
        } else if (esk_json_get(d, "between")) {
            as[i].mode = 1;
            esk_json_to_time(esk_json_at(esk_json_get(d, "between"), 0), &as[i].t0);
            esk_json_to_time(esk_json_at(esk_json_get(d, "between"), 1), &as[i].t1);
        } else if (esk_json_get(d, "by")) {
            as[i].mode = 2;
            esk_json_to_time(esk_json_get(d, "by"), &as[i].t1);
        } else {
            as[i].mode = 3;
            as[i].t1 = until;
        }
        if (as[i].t1 > until)
            as[i].t1 = until;
        if (as[i].mode == 3 && esk_json_get(d, "rtos")) {
            as[i].rw = (rtos_watch *)esk_calloc(s->a, 1, sizeof *as[i].rw);
            if (as[i].rw)
                as[i].rw->min_stack = -1;
            sample_rtos = 1;
        }
    }
    /* run to each assertion's evaluation time in order, then to the end */
    for (;;) {
        es_time_t next = until;
        for (i = 0; i < na; i++)
            if (!as[i].done && as[i].t1 < next)
                next = as[i].t1;
        if (next > s->eng.sched.now) {
            if (s->scenario_follow || sample_rtos) {
                /* a millisecond at a time, for whoever follows the run and
                 * for the RTOS watches' samples */
                st = ES_OK;
                while (st == ES_OK && !s->diag.fatal && s->eng.sched.now < next) {
                    es_time_t step = next - s->eng.sched.now > ES_PS_PER_MS ? s->eng.sched.now + ES_PS_PER_MS : next;
                    st = esk_session_do_advance(s, step);
                    if (s->scenario_follow)
                        s->scenario_follow(s, s->scenario_follow_ctx);
                    for (i = 0; sample_rtos && i < na; i++)
                        if (as[i].rw && !as[i].done)
                            rtos_sample(s, &as[i]);
                }
            } else {
                st = esk_session_do_advance(s, next);
            }
            if (st != ES_OK || s->diag.fatal) {
                reason = s->diag.fatal ? s->diag.fatal_code : es_strerror(st);
                break;
            }
        }
        for (i = 0; i < na; i++)
            if (!as[i].done && as[i].t1 <= s->eng.sched.now)
                evaluate(s, &as[i]);
        if (s->eng.sched.now >= until)
            break;
    }
    /* anything not evaluated (a run that stopped early) */
    for (i = 0; i < na; i++)
        if (!as[i].done)
            evaluate(s, &as[i]);
    s->wall_time_s = (double)(esk_host_monotonic_ms() - t_start) / 1000.0;
    {
        esk_json *arr = esk_json_new_array(s->a);
        for (k = 0; arr && k < na; k++) {
            esk_json *o = esk_json_new_object(s->a);
            if (!o) break;
            esk_json_set_string(s->a, o, "id", esk_json_get_str(as[k].def, "id", ""));
            esk_json_set_bool(s->a, o, "ok", as[k].ok);
            if (!as[k].ok) {
                ok_all = 0;
                esk_json_set_string(s->a, o, "reason", as[k].reason);
            }
            esk_json_object_set(s->a, o, "observed", as[k].observed ? as[k].observed : esk_json_new_null(s->a));
            as[k].observed = NULL;
            if (esk_json_get(as[k].def, "expect"))
                esk_json_object_set(s->a, o, "expected", esk_json_clone(s->a, esk_json_get(as[k].def, "expect")));
            esk_json_set_u64_string(s->a, o, "at_ps", as[k].t1);
            if (esk_json_get(as[k].def, "measure")) {
                const esk_json *window = esk_json_get(as[k].def, "between");
                esk_json *times = esk_json_new_object(s->a);
                es_time_t t0 = 0, t1 = 0;
                esk_json_to_time(esk_json_at(window, 0), &t0);
                esk_json_to_time(esk_json_at(window, 1), &t1);
                esk_json_object_set(s->a, o, "measure", esk_json_clone(s->a, esk_json_get(as[k].def, "measure")));
                esk_json_set_string(s->a, o, "unit", esk_str_eq(esk_json_get_str(as[k].def, "measure", ""), "duty_cycle") ? "" :
                                    esk_probe_unit(esk_session_probe_find(s, esk_json_get_str(as[k].def, "probe", ""))));
                esk_json_set_u64_string(s->a, times, "from", t0);
                esk_json_set_u64_string(s->a, times, "to", t1);
                esk_json_object_set(s->a, o, "window_ps", times);
            }
            esk_json_array_push(s->a, arr, o);
        }
        for (k = 0; k < na; k++) {
            esk_json_free(s->a, as[k].observed);
            if (as[k].rw)
                esk_free(s->a, as[k].rw, sizeof *as[k].rw);
        }
        esk_free(s->a, as, (na ? na : 1) * sizeof *as);
        result = esk_session_report(s, reason, arr);
    }
    if (result)
        esk_json_set_bool(s->a, result, "ok", ok_all && !s->diag.fatal);
    return result;
}
