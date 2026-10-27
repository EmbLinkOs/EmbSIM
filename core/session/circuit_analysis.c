/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "session/circuit_analysis.h"
#include "project/loader.h"
#include "project/manifest.h"
#include "util/num.h"
#include "util/str.h"
#include <math.h>
#include <string.h>

esk_net *esk_circuit_measurement(es_session *s, const char *target)
{
    if (esk_str_has_prefix(target, "net:")) return esk_engine_net(&s->eng, target + 4);
    if (esk_str_has_prefix(target, "pin:")) {
        esk_pin *pin = esk_project_pin(s, target + 4);
        return pin ? pin->net : NULL;
    }
    return NULL;
}
static es_status invalid(char *why, size_t cap, const char *text)
{
    esk_snprintf(why, cap, "%s", text);
    return ES_EINVAL;
}
static int put(es_session *s, esk_json *object, const char *key, esk_json *value)
{
    if (!value) return -1;
    if (esk_json_object_set(s->a, object, key, value)) { esk_json_free(s->a, value); return -1; }
    return 0;
}
static int time_field(const esk_json *request, const char *key, es_time_t *value)
{
    const esk_json *field = esk_json_get(request, key);
    return !esk_json_str(field) || esk_json_to_time(field, value);
}

es_status esk_session_transient(es_session *s, const esk_json *request, esk_json **result, char *why, size_t cap)
{
    es_time_t start, stop, step, at;
    const esk_json *probes = esk_json_get(request, "probes");
    es_session_config_t cfg;
    es_session *temporary = NULL;
    esk_json *out = NULL, *source = NULL, *rows;
    es_status st, checked;
    char end_text[32];
    size_t i, j;
    *result = NULL; why[0] = 0;
    if (!s->loaded) return ES_ESTATE;
    if (s->eng.policy != ESK_POLICY_ELECTRICAL || s->boards.len)
        return invalid(why, cap, "Transient capture requires a board-free electrical circuit; firmware and functional/timed policy are unsupported.");
    if (s->running || s->tx_project) return invalid(why, cap, "Pause the source and finish or abort the open transaction before capture.");
    if (s->eng.comps.len > 256 || s->eng.nets.len > 1024) return invalid(why, cap, "Capture limit: 256 compiled components and 1024 nets.");
    if (!request || request->type != ESK_JSON_OBJECT) return invalid(why, cap, "Expected a transient capture request.");
    for (i = 0; i < esk_json_len(request); i++) {
        const char *key = esk_json_at(request, i)->key;
        if (strcmp(key, "start") && strcmp(key, "stop") && strcmp(key, "step") && strcmp(key, "probes") && strcmp(key, "schema_version") && strcmp(key, "kind"))
            return invalid(why, cap, "Unknown transient capture field.");
    }
    if ((esk_json_get(request, "kind") && !esk_str_eq(esk_json_get_str(request, "kind", ""), "transient_request")) ||
        (esk_json_get(request, "schema_version") && esk_json_get_num(request, "schema_version", 0) != 1)) return invalid(why, cap, "Unsupported transient request format.");
    if (time_field(request, "start", &start) || time_field(request, "stop", &stop) || time_field(request, "step", &step) ||
        start >= stop || !step || stop > UINT64_C(100000000000) || (stop - start) % step || (stop - start) / step > 4096)
        return invalid(why, cap, "Use string times: 0 <= start < stop <= 100 ms, positive step dividing the interval exactly, and 2–4097 samples.");
    if (!probes || probes->type != ESK_JSON_ARRAY || !esk_json_len(probes) || esk_json_len(probes) > 8)
        return invalid(why, cap, "Supply 1–8 unique net:/pin: voltage measurements.");
    for (i = 0; i < esk_json_len(probes); i++) {
        const char *target = esk_json_str(esk_json_at(probes, i));
        if (!target || strlen(target) > 191 || !esk_circuit_measurement(s, target)) return invalid(why, cap, "Each measurement must name an existing net:NAME or pin:INSTANCE.PIN.");
        for (j = 0; j < i; j++) if (esk_str_eq(target, esk_json_str(esk_json_at(probes, j)))) return invalid(why, cap, "Duplicate transient measurement target.");
    }
    st = esk_project_manifest(s, &source);
    if (st != ES_OK) return st;
    out = esk_json_new_object(s->a);
    if (!out) { esk_json_free(s->a, source); return ES_ENOMEM; }
    if (put(s, out, "source", source) || put(s, out, "request", esk_json_clone(s->a, request)) || put(s, out, "samples", esk_json_new_array(s->a)) ||
        esk_json_set_integer(s->a, out, "schema_version", 1) || esk_json_set_string(s->a, out, "kind", "transient_capture") ||
        esk_json_set_string(s->a, out, "method", "reset_uniform_sample")) { st = ES_ENOMEM; goto done; }
    memset(&cfg, 0, sizeof cfg); cfg.catalog = s->catalog; cfg.policy = "electrical";
    st = es_session_create(&cfg, &temporary);
    if (st != ES_OK) goto done;
    checked = esk_project_load(temporary, s->project, s->base_dir);
    if (checked == ES_OK) checked = esk_session_do_reset(temporary);
    rows = esk_json_get_mut(out, "samples");
    for (at = start; checked == ES_OK; at += step) {
        esk_json *row, *measurements;
        char text[32];
        checked = esk_session_do_advance(temporary, at);
        if (checked == ES_OK && esk_diag_count(&temporary->diag, ES_SEV_ERROR)) checked = ES_ESTATE;
        if (checked != ES_OK) break;
        if (temporary->eng.sched.now != at) { checked = ES_ESTATE; break; }
        row = esk_json_new_object(s->a);
        if (!row) { st = ES_ENOMEM; goto done; }
        if (esk_json_array_push(s->a, rows, row)) { esk_json_free(s->a, row); st = ES_ENOMEM; goto done; }
        esk_snprintf(text, sizeof text, "%llu", (unsigned long long)at);
        if (esk_json_set_string(s->a, row, "at_ps", text) || put(s, row, "measurements", esk_json_new_array(s->a))) { st = ES_ENOMEM; goto done; }
        measurements = esk_json_get_mut(row, "measurements");
        for (i = 0; i < esk_json_len(probes); i++) {
            const char *target = esk_json_str(esk_json_at(probes, i));
            esk_net *net = esk_circuit_measurement(temporary, target);
            esk_json *m = esk_json_new_object(s->a);
            if (!m) { st = ES_ENOMEM; goto done; }
            if (esk_json_array_push(s->a, measurements, m)) { esk_json_free(s->a, m); st = ES_ENOMEM; goto done; }
            if (esk_json_set_string(s->a, m, "target", target) || esk_json_set_string(s->a, m, "logic", net ? net->value == ESK_L0 ? "0" : net->value == ESK_L1 ? "1" : net->value == ESK_LZ ? "Z" : "X" : "X") ||
                put(s, m, "voltage", net && (net->island || net->is_ground) && isfinite(net->voltage) ? esk_json_new_number(s->a, net->voltage) : esk_json_new_null(s->a))) { st = ES_ENOMEM; goto done; }
        }
        if (at == stop) break;
    }
    esk_snprintf(end_text, sizeof end_text, "%llu", (unsigned long long)temporary->eng.sched.now);
    if (esk_json_set_bool(s->a, out, "ok", checked == ES_OK) || esk_json_set_string(s->a, out, "ended_at_ps", end_text) ||
        put(s, out, "failure", checked == ES_OK ? esk_json_new_null(s->a) : esk_json_new_string(s->a, es_strerror(checked)))) { st = ES_ENOMEM; goto done; }
    {
        esk_json *diagnostics = esk_diag_to_json(&temporary->diag);
        int failed = put(s, out, "diagnostics", esk_json_clone(s->a, diagnostics));
        esk_json_free(temporary->a, diagnostics);
        if (failed) { st = ES_ENOMEM; goto done; }
    }
done:
    if (temporary) es_session_destroy(temporary);
    if (st == ES_OK) *result = out; else esk_json_free(s->a, out);
    return st;
}
