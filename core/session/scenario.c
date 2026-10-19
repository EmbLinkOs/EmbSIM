/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* scenario.c -- scenarios and assertions (SPEC-007 §2) and the run report
 * (SPEC-008 §8). */
#include "session/session_priv.h"
#include <math.h>
#include <string.h>
#include "analog/analog.h"
#include "host/host.h"
#include "util/num.h"
#include "util/str.h"

typedef struct assertion {
    const esk_json *def;
    es_time_t t0, t1;   /* at: t0 == t1; between: window; by: [0, t1]; always: whole run */
    int mode;           /* 0 at, 1 between, 2 by, 3 always */
    int done, ok;
    esk_json *observed;
    char reason[160];
} assertion;

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

static double sample_at(esk_probe *p, es_time_t t, int *have)
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
    if (a->mode == 3) {
        const esk_json *forbid = esk_json_get(esk_json_get(def, "diagnostics"), "forbid");
        esk_json *all = esk_diag_to_json(&s->diag);
        size_t i, k;
        for (i = 0; all && i < esk_json_len(all); i++) {
            const char *code = esk_json_get_str(esk_json_at(all, i), "code", "");
            for (k = 0; k < esk_json_len(forbid); k++) {
                if (esk_str_eq(esk_json_str(esk_json_at(forbid, k)), code)) {
                    a->ok = 0;
                    esk_snprintf(a->reason, sizeof a->reason, "diagnostic %s was raised", code);
                }
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
    /* a text expectation on a memory probe: its bytes up to the first NUL */
    if (p->kind == ESK_PROBE_VALUE && p->mem_len &&
        (esk_json_get(expect, "contains") || esk_json_is(esk_json_get(expect, "equals"), ESK_JSON_STRING))) {
        const char *contains = esk_json_get_str(expect, "contains", NULL);
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
        } else if (equals && strcmp(text, equals) != 0) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "\"%.60s\" is not \"%s\"", text, equals);
        }
        return;
    }
    /* a text expectation on a value probe: the output as a string, now (the
     * assertion's time: `at`, or the end of `between`/`by`) */
    if (p->kind == ESK_PROBE_VALUE && p->comp && p->comp->ops->observe &&
        (esk_json_get(expect, "contains") || esk_json_is(esk_json_get(expect, "equals"), ESK_JSON_STRING))) {
        const char *contains = esk_json_get_str(expect, "contains", NULL);
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
        } else if (equals && strcmp(text, equals) != 0) {
            a->ok = 0;
            esk_snprintf(a->reason, sizeof a->reason, "\"%.60s\", expected \"%s\"", text, equals);
        }
        esk_json_free(s->a, out);
        return;
    }
    if (p->kind == ESK_PROBE_SERIAL) {
        const char *contains = esk_json_get_str(expect, "contains", NULL);
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
    int ok_all = 1;
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
    }
    /* run to each assertion's evaluation time in order, then to the end */
    for (;;) {
        es_time_t next = until;
        for (i = 0; i < na; i++)
            if (!as[i].done && as[i].t1 < next)
                next = as[i].t1;
        if (next > s->eng.sched.now) {
            st = esk_session_do_advance(s, next);
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
            esk_json_array_push(s->a, arr, o);
        }
        for (k = 0; k < na; k++)
            esk_json_free(s->a, as[k].observed);
        esk_free(s->a, as, (na ? na : 1) * sizeof *as);
        result = esk_session_report(s, reason, arr);
    }
    if (result)
        esk_json_set_bool(s->a, result, "ok", ok_all && !s->diag.fatal);
    return result;
}
