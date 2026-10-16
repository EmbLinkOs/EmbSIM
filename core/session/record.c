/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* record.c -- recordings (SPEC-012 §1): what a run observed, the document
 * that keeps it with the run's identities and inputs, the comparison replay
 * makes (§2), and a recording turned into a scenario (§3).
 *
 * Observations, as they happen while recording:
 *   - irq: a core enters an exception or interrupt (the board, the core
 *     when its part has several, the core's number for it);
 *   - probe: a digital probe's level changes (0, 1, Z, X), its first level
 *     included;
 * and at the end:
 *   - serial: each serial probe's text;
 *   - diagnostic: each warning, error or fatal diagnostic's code;
 *   - end: the run's end.
 * They are kept in time order (ties in the order they came). Past
 * ESK_REC_MAX observations the rest are not kept, and the document says
 * so (`truncated`). */
#include <stdio.h>
#include <string.h>

#include "host/host.h"
#include "session/session_priv.h"
#include "util/num.h"
#include "util/sha256.h"
#include "util/str.h"

#define ESK_REC_MAX 200000

void esk_record_start(es_session *s)
{
    s->record_from = 0;
    esk_json_free(s->a, s->observations);
    s->observations = esk_json_new_array(s->a);
    s->obs_truncated = 0;
    s->recording = 1;
}

static esk_json *obs(es_session *s, const char *kind, es_time_t at)
{
    esk_json *o;
    if (!s->recording || !s->observations)
        return NULL;
    if (esk_json_len(s->observations) >= ESK_REC_MAX) {
        s->obs_truncated = 1;
        return NULL;
    }
    o = esk_json_new_object(s->a);
    if (!o)
        return NULL;
    esk_json_set_u64_string(s->a, o, "at_ps", at);
    esk_json_set_string(s->a, o, "kind", kind);
    if (esk_json_array_push(s->a, s->observations, o)) {
        esk_json_free(s->a, o);
        return NULL;
    }
    return o;
}

static const char *board_of(es_session *s, esk_comp *mcu)
{
    size_t i;
    for (i = 0; i < s->boards.len; i++)
        if (s->boards.data[i]->mcu == mcu)
            return s->boards.data[i]->id;
    return mcu->id;
}

void esk_record_irq(void *ctx, esk_comp *mcu, int core, int cores, int n, es_time_t at)
{
    es_session *s = (es_session *)ctx;
    esk_json *o = obs(s, "irq", at);
    if (!o)
        return;
    esk_json_set_string(s->a, o, "board", board_of(s, mcu));
    if (cores > 1)
        esk_json_set_integer(s->a, o, "core", core);
    esk_json_set_integer(s->a, o, "n", n);
}

void esk_record_probe(es_session *s, const esk_probe *p, es_time_t at, double v)
{
    static const char *const lv[4] = { "0", "1", "Z", "X" };
    esk_json *o;
    int k = (int)v;
    if (p->kind != ESK_PROBE_DIGITAL || (p->has_last && p->last == v))
        return;
    o = obs(s, "probe", at);
    if (!o)
        return;
    esk_json_set_string(s->a, o, "probe", p->name);
    esk_json_set_string(s->a, o, "value", lv[k >= 0 && k < 4 ? k : 3]);
}

/* ---- the document --------------------------------------------------------- */

static void hash_file(es_session *s, const char *path, char out[72])
{
    char *data = NULL;
    size_t len = 0;
    if (path && esk_host_read_file(s->a, path, &data, &len) == ES_OK) {
        esk_sha256_text(data, len, out);
        esk_free(s->a, data, len + 1);
    } else {
        esk_sha256_text("", 0, out); /* no file: the empty one's */
    }
}

static const char *platform(void)
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "posix";
#endif
}

static const char *build(void)
{
#if defined(__clang__)
    return "clang";
#elif defined(__TINYC__)
    return "tcc";
#elif defined(__GNUC__)
    return "gcc";
#elif defined(_MSC_VER)
    return "msvc";
#else
    return "cc";
#endif
}

/* a stable sort of the observations by time */
static void sort_obs(esk_json *a)
{
    size_t n = esk_json_len(a), i, j;
    esk_json **it = a && a->type == ESK_JSON_ARRAY ? a->u.array.items : NULL;
    for (i = 1; it && i < n; i++) {
        esk_json *x = it[i];
        es_time_t tx = 0, tj = 0;
        (void)esk_json_to_time(esk_json_get(x, "at_ps"), &tx);
        for (j = i; j > 0; j--) {
            (void)esk_json_to_time(esk_json_get(it[j - 1], "at_ps"), &tj);
            if (tj <= tx)
                break;
            it[j] = it[j - 1];
        }
        it[j] = x;
    }
}

static const char *end_reason(const char *r)
{
    static const char *const known[] = { "until", "stopped", "fatal", "assertion", "breakpoint", "idle" };
    size_t i;
    for (i = 0; r && i < sizeof known / sizeof known[0]; i++)
        if (!strcmp(r, known[i]))
            return r;
    return "stopped";
}

/* the end-of-run observations: the serial probes' text, the diagnostics,
 * the end */
static void finish(es_session *s, es_time_t end)
{
    size_t i;
    const esk_json *diags = NULL;
    esk_json *dj = esk_diag_to_json(&s->diag);
    for (i = 0; i < s->probes.len; i++) {
        esk_probe *p = s->probes.data[i];
        if (p->kind == ESK_PROBE_SERIAL && p->comp && p->comp->ops->observe) {
            esk_json *out = esk_json_new_object(s->a), *o;
            const char *text;
            if (!out)
                continue;
            p->comp->ops->observe(p->comp, out);
            text = esk_json_get_str(out, p->quantity ? p->quantity : "rx_text", "");
            o = obs(s, "serial", end);
            if (o) {
                esk_json_set_string(s->a, o, "probe", p->name);
                esk_json_set_string(s->a, o, "bytes", text);
            }
            esk_json_free(s->a, out);
        }
    }
    diags = dj;
    for (i = 0; i < esk_json_len(diags); i++) {
        const esk_json *d = esk_json_at(diags, i);
        const char *sev = esk_json_get_str(d, "severity", "");
        es_time_t at = 0;
        esk_json *o;
        if (strcmp(sev, "warning") && strcmp(sev, "error") && strcmp(sev, "fatal"))
            continue;
        (void)esk_json_to_time(esk_json_get(d, "time_ps"), &at);
        o = obs(s, "diagnostic", at);
        if (o)
            esk_json_set_string(s->a, o, "code", esk_json_get_str(d, "code", ""));
    }
    esk_json_free(s->a, dj);
    (void)obs(s, "end", end);
    sort_obs(s->observations);
}

esk_json *esk_record_document(es_session *s, const char *project_path, const char *scenario,
                              const esk_json *report, const esk_json *extra_probes)
{
    esk_json *rec = esk_json_new_object(s->a), *eng, *proj, *imgs, *end, *stats, *ins;
    const esk_json *ended = esk_json_get(report, "ended"), *rstats = esk_json_get(report, "statistics");
    char h[72];
    es_time_t at = s->eng.sched.now;
    size_t i;
    if (!rec)
        return NULL;
    if (s->recording && s->observations)
        finish(s, at);
    esk_json_set_integer(s->a, rec, "schema_version", 1);
    esk_json_set_string(s->a, rec, "kind", "recording");
    eng = esk_json_new_object(s->a);
    if (eng) {
        esk_json_set_string(s->a, eng, "version", es_version().string);
        esk_json_set_integer(s->a, eng, "abi", es_abi_version());
        esk_json_set_string(s->a, eng, "platform", platform());
        esk_json_set_string(s->a, eng, "build", build());
        esk_json_object_set(s->a, rec, "engine", eng);
    }
    proj = esk_json_new_object(s->a);
    if (proj) {
        hash_file(s, s->project_path, h);
        esk_json_set_string(s->a, proj, "path", project_path ? project_path : s->project_path ? s->project_path : "");
        esk_json_set_string(s->a, proj, "hash", h);
        if (s->name)
            esk_json_set_string(s->a, proj, "name", s->name);
        esk_json_object_set(s->a, rec, "project", proj);
    }
    imgs = esk_json_new_array(s->a);
    for (i = 0; imgs && i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        const char *img = bi->firmware ? esk_json_get_str(bi->firmware, "image", NULL) : NULL;
        esk_json *o;
        char *full;
        if (!img)
            continue;
        full = esk_host_path_join(s->a, s->base_dir ? s->base_dir : ".", img);
        hash_file(s, full, h);
        esk_strfree(s->a, full);
        o = esk_json_new_object(s->a);
        if (!o)
            continue;
        esk_json_set_string(s->a, o, "board", bi->id);
        esk_json_set_string(s->a, o, "path", img);
        esk_json_set_string(s->a, o, "hash", h);
        esk_json_array_push(s->a, imgs, o);
    }
    if (imgs)
        esk_json_object_set(s->a, rec, "images", imgs);
    if (scenario)
        esk_json_set_string(s->a, rec, "scenario", scenario);
    if (s->record_from)
        esk_json_set_u64_string(s->a, rec, "from_ps", s->record_from);
    esk_json_set_string(s->a, rec, "policy", esk_json_get_str(report, "policy", "electrical"));
    if (extra_probes && esk_json_len(extra_probes))
        esk_json_object_set(s->a, rec, "probes", esk_json_clone(s->a, extra_probes));
    ins = esk_json_clone(s->a, s->inputs_log);
    esk_json_object_set(s->a, rec, "inputs", ins ? ins : esk_json_new_array(s->a));
    esk_json_object_set(s->a, rec, "observations",
                        s->observations ? esk_json_clone(s->a, s->observations) : esk_json_new_array(s->a));
    if (s->obs_truncated)
        esk_json_set_bool(s->a, rec, "truncated", 1);
    end = esk_json_new_object(s->a);
    if (end) {
        esk_json_set_u64_string(s->a, end, "at_ps", at);
        esk_json_set_string(s->a, end, "reason", end_reason(esk_json_get_str(ended, "reason", "until")));
        stats = esk_json_new_object(s->a);
        if (stats) {
            esk_json_set_integer(s->a, stats, "events", esk_json_get_int(rstats, "events", 0));
            esk_json_set_integer(s->a, stats, "analog_steps", esk_json_get_int(rstats, "analog_steps", 0));
            esk_json_set_number(s->a, stats, "wall_time_s", esk_json_get_num(rstats, "wall_time_s", 0));
            esk_json_object_set(s->a, end, "statistics", stats);
        }
        esk_json_object_set(s->a, rec, "end", end);
    }
    return rec;
}

/* ---- replay's comparison ------------------------------------------------------- */

static int same(esk_alloc *al, const esk_json *a, const esk_json *b)
{
    static const char *const keys[] = { "at_ps", "kind", "board", "core", "n", "probe", "value", "code", "bytes" };
    size_t i;
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        const esk_json *x = esk_json_get(a, keys[i]), *y = esk_json_get(b, keys[i]);
        char *tx, *ty;
        size_t lx = 0, ly = 0;
        int eq;
        if (!x && !y)
            continue;
        if (!x || !y)
            return 0;
        tx = esk_json_format(al, x, 0, &lx);
        ty = esk_json_format(al, y, 0, &ly);
        eq = tx && ty && lx == ly && !memcmp(tx, ty, lx);
        if (tx)
            esk_free(al, tx, lx + 1);
        if (ty)
            esk_free(al, ty, ly + 1);
        if (!eq)
            return 0;
    }
    return 1;
}

esk_json *esk_record_compare(es_session *s, const esk_json *want, const esk_json *got)
{
    esk_json *r = esk_json_new_object(s->a);
    size_t n = esk_json_len(want), m = esk_json_len(got), i, k = n < m ? n : m;
    if (!r)
        return NULL;
    for (i = 0; i < k; i++)
        if (!same(s->a, esk_json_at(want, i), esk_json_at(got, i)))
            break;
    esk_json_set_integer(s->a, r, "compared", (int64_t)(i < k ? i : k));
    if (i == k && n == m) {
        esk_json_set_bool(s->a, r, "ok", 1);
        return r;
    }
    esk_json_set_bool(s->a, r, "ok", 0);
    {
        esk_json *d = esk_json_new_object(s->a);
        if (d) {
            esk_json_set_integer(s->a, d, "index", (int64_t)i);
            esk_json_object_set(s->a, d, "expected",
                                i < n ? esk_json_clone(s->a, esk_json_at(want, i)) : esk_json_new_null(s->a));
            esk_json_object_set(s->a, d, "observed",
                                i < m ? esk_json_clone(s->a, esk_json_at(got, i)) : esk_json_new_null(s->a));
            esk_json_object_set(s->a, r, "divergence", d);
        }
    }
    return r;
}

/* ---- a recording as a scenario ------------------------------------------------- */

static esk_json *time_value(esk_alloc *a, const char *ps)
{
    char t[48];
    esk_snprintf(t, sizeof t, "%sps", ps ? ps : "0");
    return esk_json_new_string(a, t);
}

esk_json *esk_record_to_scenario(esk_alloc *a, const esk_json *rec, const char *name, int with_probes,
                                 int with_serial)
{
    esk_json *sc = esk_json_new_object(a), *ins = esk_json_new_array(a), *as = esk_json_new_array(a);
    const esk_json *inputs = esk_json_get(rec, "inputs"), *obsv = esk_json_get(rec, "observations");
    const char *end = esk_json_get_str(esk_json_get(rec, "end"), "at_ps", "0");
    size_t i, j;
    if (!sc || !ins || !as) {
        esk_json_free(a, sc);
        esk_json_free(a, ins);
        esk_json_free(a, as);
        return NULL;
    }
    esk_json_set_integer(a, sc, "schema_version", 1);
    esk_json_set_string(a, sc, "kind", "scenario");
    esk_json_set_string(a, sc, "name", name ? name : "recorded");
    esk_json_set_string(a, sc, "description", "Made from a recording by `embsim record to-scenario`: its inputs at their times, and assertions on what it observed.");
    esk_json_object_set(a, sc, "until", time_value(a, end));
    esk_json_set_string(a, sc, "policy", esk_json_get_str(rec, "policy", "electrical"));
    for (i = 0; i < esk_json_len(inputs); i++) {
        const esk_json *in = esk_json_at(inputs, i);
        esk_json *o = esk_json_new_object(a);
        if (!o)
            continue;
        esk_json_object_set(a, o, "at", time_value(a, esk_json_get_str(in, "at_ps", "0")));
        esk_json_set_string(a, o, "target", esk_json_get_str(in, "target", ""));
        esk_json_set_string(a, o, "input", esk_json_get_str(in, "input", ""));
        esk_json_object_set(a, o, "value", esk_json_clone(a, esk_json_get(in, "value")));
        esk_json_array_push(a, ins, o);
    }
    esk_json_object_set(a, sc, "inputs", ins);
    /* each digital probe's level at the end and its edges over the run; each
     * serial probe's text */
    for (i = 0; i < esk_json_len(obsv); i++) {
        const esk_json *o = esk_json_at(obsv, i);
        const char *kind = esk_json_get_str(o, "kind", ""), *probe = esk_json_get_str(o, "probe", NULL);
        int seen = 0, edges = 0;
        const char *last = NULL;
        esk_json *x, *ex;
        char id[96];
        if (!probe || !((with_probes && !strcmp(kind, "probe")) || (with_serial && !strcmp(kind, "serial"))))
            continue;
        for (j = 0; j < i; j++)
            if (!strcmp(esk_json_get_str(esk_json_at(obsv, j), "kind", ""), kind) &&
                esk_str_eq(esk_json_get_str(esk_json_at(obsv, j), "probe", NULL), probe))
                seen = 1;
        if (seen)
            continue; /* one assertion set per probe */
        if (!strcmp(kind, "probe")) {
            for (j = i; j < esk_json_len(obsv); j++) {
                const esk_json *q = esk_json_at(obsv, j);
                if (!strcmp(esk_json_get_str(q, "kind", ""), "probe") && esk_str_eq(esk_json_get_str(q, "probe", NULL), probe)) {
                    if (last)
                        edges++;
                    last = esk_json_get_str(q, "value", "X");
                }
            }
            x = esk_json_new_object(a);
            ex = esk_json_new_object(a);
            if (x && ex) {
                esk_snprintf(id, sizeof id, "%s-level", probe);
                esk_json_set_string(a, x, "id", id);
                esk_json_object_set(a, x, "at", time_value(a, end));
                esk_json_set_string(a, x, "probe", probe);
                esk_json_set_string(a, ex, "logic", last ? last : "X");
                esk_json_object_set(a, x, "expect", ex);
                esk_json_array_push(a, as, x);
            } else {
                esk_json_free(a, x);
                esk_json_free(a, ex);
            }
            x = esk_json_new_object(a);
            ex = esk_json_new_object(a);
            if (x && ex) {
                esk_json *between = esk_json_new_array(a);
                esk_snprintf(id, sizeof id, "%s-edges", probe);
                esk_json_set_string(a, x, "id", id);
                if (between) {
                    esk_json_array_push(a, between, esk_json_new_string(a, "0ps"));
                    esk_json_array_push(a, between, time_value(a, end));
                    esk_json_object_set(a, x, "between", between);
                }
                esk_json_set_string(a, x, "probe", probe);
                esk_json_set_integer(a, ex, "edges", edges);
                esk_json_object_set(a, x, "expect", ex);
                esk_json_array_push(a, as, x);
            } else {
                esk_json_free(a, x);
                esk_json_free(a, ex);
            }
        } else {
            const char *bytes = esk_json_get_str(o, "bytes", "");
            if (!*bytes)
                continue;
            x = esk_json_new_object(a);
            ex = esk_json_new_object(a);
            if (x && ex) {
                esk_snprintf(id, sizeof id, "%s-text", probe);
                esk_json_set_string(a, x, "id", id);
                esk_json_object_set(a, x, "at", time_value(a, end));
                esk_json_set_string(a, x, "probe", probe);
                esk_json_set_string(a, ex, "contains", bytes);
                esk_json_object_set(a, x, "expect", ex);
                esk_json_array_push(a, as, x);
            } else {
                esk_json_free(a, x);
                esk_json_free(a, ex);
            }
        }
    }
    esk_json_object_set(a, sc, "assertions", as);
    return sc;
}

/* ---- replay's identities ------------------------------------------------------- */

static void drift_add(es_session *s, esk_json *out, const char *what, const char *was, const char *now)
{
    esk_json *o = esk_json_new_object(s->a);
    if (!o)
        return;
    esk_json_set_string(s->a, o, "what", what);
    esk_json_set_string(s->a, o, "recorded", was ? was : "");
    esk_json_set_string(s->a, o, "now", now ? now : "");
    esk_json_array_push(s->a, out, o);
}

esk_json *esk_record_drift(es_session *s, const esk_json *rec)
{
    esk_json *out = esk_json_new_array(s->a);
    const esk_json *imgs = esk_json_get(rec, "images");
    char h[72];
    size_t i, k;
    if (!out)
        return NULL;
    hash_file(s, s->project_path, h);
    if (!esk_str_eq(h, esk_json_get_str(esk_json_get(rec, "project"), "hash", "")))
        drift_add(s, out, "project", esk_json_get_str(esk_json_get(rec, "project"), "hash", ""), h);
    for (i = 0; i < esk_json_len(imgs); i++) {
        const esk_json *im = esk_json_at(imgs, i);
        const char *board = esk_json_get_str(im, "board", "");
        for (k = 0; k < s->boards.len; k++) {
            esk_board_inst *bi = s->boards.data[k];
            const char *img = bi->firmware ? esk_json_get_str(bi->firmware, "image", NULL) : NULL;
            char *full, what[96];
            if (!esk_str_eq(bi->id, board) || !img)
                continue;
            full = esk_host_path_join(s->a, s->base_dir ? s->base_dir : ".", img);
            hash_file(s, full, h);
            esk_strfree(s->a, full);
            if (!esk_str_eq(h, esk_json_get_str(im, "hash", ""))) {
                esk_snprintf(what, sizeof what, "image of %s", board);
                drift_add(s, out, what, esk_json_get_str(im, "hash", ""), h);
            }
        }
    }
    return out;
}

/* ---- replay -------------------------------------------------------------------- */

static esk_json *from_on(es_session *s, const esk_json *obsv, es_time_t from)
{
    esk_json *out = esk_json_new_array(s->a);
    size_t i;
    for (i = 0; out && i < esk_json_len(obsv); i++) {
        const esk_json *o = esk_json_at(obsv, i);
        es_time_t at = 0;
        (void)esk_json_to_time(esk_json_get(o, "at_ps"), &at);
        if (at >= from)
            esk_json_array_push(s->a, out, esk_json_clone(s->a, o));
    }
    return out;
}

es_status esk_record_replay(es_session *s, const esk_json *rec, int allow_drift, esk_json **result)
{
    const esk_json *inputs = esk_json_get(rec, "inputs"), *extra = esk_json_get(rec, "probes");
    esk_json *drift, *report, *doc, *cmp, *got;
    es_time_t end = 0, from = 0;
    size_t i;
    uint64_t t0;
    *result = NULL;
    if (!s->loaded || esk_json_to_time(esk_json_get(esk_json_get(rec, "end"), "at_ps"), &end))
        return ES_EINVAL;
    (void)esk_json_to_time(esk_json_get(rec, "from_ps"), &from);
    drift = esk_record_drift(s, rec);
    if (!drift)
        return ES_ENOMEM;
    if (esk_json_len(drift) && !allow_drift) {
        *result = esk_json_new_object(s->a);
        if (!*result) {
            esk_json_free(s->a, drift);
            return ES_ENOMEM;
        }
        esk_json_set_bool(s->a, *result, "ok", 0);
        esk_json_object_set(s->a, *result, "drift", drift);
        return ES_ESTATE;
    }
    for (i = 0; i < esk_json_len(extra); i++)
        (void)esk_session_probe_add(s, NULL, esk_json_str(esk_json_at(extra, i)), NULL, 0, NULL);
    esk_record_start(s);
    (void)esk_session_do_reset(s); /* from reset, as the recorded run */
    for (i = 0; i < esk_json_len(inputs); i++) {
        const esk_json *in = esk_json_at(inputs, i);
        es_time_t at = 0;
        (void)esk_json_to_time(esk_json_get(in, "at_ps"), &at);
        (void)esk_session_apply_input(s, esk_json_get_str(in, "target", ""), esk_json_get_str(in, "input", ""),
                                      esk_json_get(in, "value"), at, NULL, NULL);
    }
    t0 = esk_host_monotonic_ms();
    (void)esk_session_do_advance(s, end);
    s->wall_time_s = (double)(esk_host_monotonic_ms() - t0) / 1000.0;
    report = esk_session_report(s, esk_json_get_str(esk_json_get(rec, "end"), "reason", "until"), NULL);
    doc = report ? esk_record_document(s, NULL, NULL, report, NULL) : NULL;
    s->recording = 0;
    got = doc ? from_on(s, esk_json_get(doc, "observations"), from) : NULL;
    cmp = got ? esk_record_compare(s, esk_json_get(rec, "observations"), got) : NULL;
    esk_json_free(s->a, got);
    esk_json_free(s->a, doc);
    esk_json_free(s->a, report);
    if (!cmp) {
        esk_json_free(s->a, drift);
        return ES_ENOMEM;
    }
    esk_json_object_set(s->a, cmp, "drift", drift);
    *result = cmp;
    return ES_OK;
}
