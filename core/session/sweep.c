/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "session/sweep.h"
#include "session/circuit_analysis.h"
#include "project/variants.h"
#include "project/manifest.h"
#include "project/loader.h"
#include "util/num.h"
#include "util/str.h"
#include <math.h>
#include <string.h>

static es_status reject(char *why, size_t cap, const char *message)
{
    esk_snprintf(why, cap, "%s", message);
    return ES_EINVAL;
}
static int put(es_session *s, esk_json *o, const char *key, esk_json *v)
{
    if (!v) return -1;
    if (esk_json_object_set(s->a, o, key, v)) { esk_json_free(s->a, v); return -1; }
    return 0;
}

es_status esk_session_sweep(es_session *s, const esk_json *request, esk_json **result, char *why, size_t cap)
{
    const char *target = esk_json_get_str(request, "target", ""), *parameter = esk_json_get_str(request, "parameter", "");
    const esk_json *values = esk_json_get(request, "values"), *probes = esk_json_get(request, "probes");
    const esk_json *definition;
    esk_comp *source;
    esk_json *out = NULL, *rows = NULL, *manifest = NULL;
    es_time_t sample_at;
    es_status st = ES_OK;
    size_t i, j;
    *result = NULL; why[0] = 0;
    if (!s->loaded) return ES_ESTATE;
    if (s->eng.policy != ESK_POLICY_ELECTRICAL || s->boards.len)
        return reject(why, cap, "Parameter sweeps require a board-free electrical circuit; firmware and functional/timed-policy sweeps are unsupported.");
    if (s->tx_project || s->running) return reject(why, cap, "Pause the circuit and finish or abort the open transaction before analysis.");
    if (s->eng.comps.len > 256 || s->eng.nets.len > 1024) return reject(why, cap, "Sweep limit: 256 compiled components and 1024 nets.");
    if (!request || request->type != ESK_JSON_OBJECT) return reject(why, cap, "Expected a sweep request object.");
    for (i = 0; i < esk_json_len(request); i++) {
        const char *key = esk_json_at(request, i)->key;
        if (strcmp(key, "target") && strcmp(key, "parameter") && strcmp(key, "values") && strcmp(key, "probes") && strcmp(key, "sample_at") && strcmp(key, "schema_version") && strcmp(key, "kind"))
            return reject(why, cap, "Unknown sweep request field.");
    }
    if ((esk_json_get(request, "kind") && !esk_str_eq(esk_json_get_str(request, "kind", ""), "parameter_sweep_request")) ||
        (esk_json_get(request, "schema_version") && esk_json_get_num(request, "schema_version", 0) != 1))
        return reject(why, cap, "Unsupported sweep request format.");
    if (!values || values->type != ESK_JSON_ARRAY || !esk_json_len(values) || esk_json_len(values) > 64 ||
        !probes || probes->type != ESK_JSON_ARRAY || !esk_json_len(probes) || esk_json_len(probes) > 8)
        return reject(why, cap, "Supply 1–64 parameter values and 1–8 unique net:/pin: voltage targets.");
    if (!esk_json_str(esk_json_get(request, "sample_at")) || esk_json_to_time(esk_json_get(request, "sample_at"), &sample_at) || sample_at > UINT64_C(1000000000) ||
        sample_at > UINT64_C(10000000000) / esk_json_len(values))
        return reject(why, cap, "Sample time must be 0–1 ms after reset; total sweep time cannot exceed 10 ms.");
    source = esk_engine_comp(&s->eng, target);
    definition = esk_json_get(esk_json_get(source ? source->desc : NULL, "params"), parameter);
    if (!source || !esk_str_eq(esk_json_get_str(definition, "type", ""), "number") || !strcmp(source->ops->name, "unpopulated"))
        return reject(why, cap, "Choose a fitted project-owned component with a declared numeric parameter.");
    for (i = 0; i < esk_json_len(values); i++) {
        double v;
        if (esk_json_to_double(esk_json_at(values, i), &v) || !isfinite(v)) return reject(why, cap, "Sweep values must be finite numbers or SI strings.");
    }
    for (j = 0; j < esk_json_len(probes); j++) {
        const char *p = esk_json_str(esk_json_at(probes, j));
        if (!p || !esk_circuit_measurement(s, p)) return reject(why, cap, "Each measurement must name an existing net:NAME or pin:INSTANCE.PIN.");
        for (i = 0; i < j; i++) if (esk_str_eq(p, esk_json_str(esk_json_at(probes, i)))) return reject(why, cap, "Duplicate sweep measurement target.");
    }
    st = esk_project_manifest(s, &manifest);
    if (st != ES_OK) return st;
    out = esk_json_new_object(s->a);
    if (!out) { esk_json_free(s->a, manifest); return ES_ENOMEM; }
    if (put(s, out, "source", manifest) || put(s, out, "request", esk_json_clone(s->a, request)) ||
        put(s, out, "points", esk_json_new_array(s->a))) { st = ES_ENOMEM; goto done; }
    rows = esk_json_get_mut(out, "points");
    if (esk_json_set_integer(s->a, out, "schema_version", 1) || esk_json_set_string(s->a, out, "kind", "parameter_sweep") ||
        esk_json_set_string(s->a, out, "method", "independent_reset_sample") || esk_json_set_bool(s->a, out, "ok", 1)) { st = ES_ENOMEM; goto done; }
    for (i = 0; i < esk_json_len(values); i++) {
        es_session_config_t cfg;
        es_session *temporary = NULL;
        esk_json *overrides = NULL, *effective = NULL, *change, *params, *row = NULL, *samples;
        es_status checked;
        char at[32], validation[200];
        memset(&cfg, 0, sizeof cfg); cfg.catalog = s->catalog; cfg.policy = "electrical";
        overrides = esk_json_new_object(s->a);
        if (!overrides || put(s, overrides, target, esk_json_new_object(s->a))) { esk_json_free(s->a, overrides); st = ES_ENOMEM; goto done; }
        change = esk_json_get_mut(overrides, target);
        if (put(s, change, "params", esk_json_new_object(s->a))) { esk_json_free(s->a, overrides); st = ES_ENOMEM; goto done; }
        params = esk_json_get_mut(change, "params");
        if (put(s, params, parameter, esk_json_clone(s->a, esk_json_at(values, i)))) { esk_json_free(s->a, overrides); st = ES_ENOMEM; goto done; }
        /* Validate concrete project ownership before materializing. Catalog package
         * internals have compiled IDs too but are not editable assembly targets. */
        if (put(s, change, "populated", esk_json_new_bool(s->a, 1))) { esk_json_free(s->a, overrides); st = ES_ENOMEM; goto done; }
        {
            esk_json *test = esk_json_clone(s->a, s->project), *variants;
            if (!test) { esk_json_free(s->a, overrides); st = ES_ENOMEM; goto done; }
            esk_json_remove(s->a, test, "variant_origin");
            esk_json_remove(s->a, test, "variants");
            if (put(s, test, "variants", esk_json_new_object(s->a))) { esk_json_free(s->a, test); esk_json_free(s->a, overrides); st = ES_ENOMEM; goto done; }
            variants = esk_json_get_mut(test, "variants");
            if (put(s, variants, "analysis", esk_json_new_object(s->a)) || put(s, esk_json_get_mut(variants, "analysis"), "overrides", esk_json_clone(s->a, overrides))) { esk_json_free(s->a, test); esk_json_free(s->a, overrides); st = ES_ENOMEM; goto done; }
            checked = esk_project_validate_variants(test, validation, sizeof validation) ? ES_EINVAL : ES_OK;
            esk_json_free(s->a, test);
        }
        if (checked != ES_OK) { esk_json_free(s->a, overrides); st = reject(why, cap, validation); goto done; }
        st = esk_project_override(s, overrides, &effective);
        esk_json_free(s->a, overrides);
        if (st != ES_OK) goto done;
        st = es_session_create(&cfg, &temporary);
        if (st != ES_OK) { esk_json_free(s->a, effective); goto done; }
        checked = esk_project_load(temporary, effective, s->base_dir);
        if (checked == ES_OK) checked = esk_session_do_reset(temporary);
        if (checked == ES_OK) checked = esk_session_do_advance(temporary, sample_at);
        row = esk_json_new_object(s->a);
        if (!row) { esk_json_free(s->a, effective); es_session_destroy(temporary); st = ES_ENOMEM; goto done; }
        if (esk_json_array_push(s->a, rows, row)) { esk_json_free(s->a, row); esk_json_free(s->a, effective); es_session_destroy(temporary); st = ES_ENOMEM; goto done; }
        esk_snprintf(at, sizeof at, "%llu", (unsigned long long)temporary->eng.sched.now);
        if (esk_diag_count(&temporary->diag, ES_SEV_ERROR)) checked = ES_ESTATE;
        if (esk_json_set_bool(s->a, row, "ok", checked == ES_OK) || esk_json_set_string(s->a, row, "at_ps", at) ||
            put(s, row, "requested", esk_json_clone(s->a, esk_json_at(values, i))) ||
            put(s, row, "measurements", esk_json_new_array(s->a))) st = ES_ENOMEM;
        {
            esk_json *diags = esk_diag_to_json(&temporary->diag);
            if (put(s, row, "diagnostics", esk_json_clone(s->a, diags))) st = ES_ENOMEM;
            esk_json_free(temporary->a, diags);
        }
        if (checked != ES_OK) esk_json_set_bool(s->a, out, "ok", 0);
        source = esk_engine_comp(&temporary->eng, target);
        if (put(s, row, "resolved", source && checked == ES_OK ? esk_json_clone(s->a, esk_json_get(source->params, parameter)) : esk_json_new_null(s->a))) st = ES_ENOMEM;
        samples = esk_json_get_mut(row, "measurements");
        for (j = 0; st == ES_OK && j < esk_json_len(probes); j++) {
            const char *p = esk_json_str(esk_json_at(probes, j));
            esk_net *net = checked == ES_OK ? esk_circuit_measurement(temporary, p) : NULL;
            esk_json *sample = esk_json_new_object(s->a);
            if (!sample) { st = ES_ENOMEM; break; }
            if (esk_json_array_push(s->a, samples, sample)) { esk_json_free(s->a, sample); st = ES_ENOMEM; break; }
            if (esk_json_set_string(s->a, sample, "target", p) || esk_json_set_string(s->a, sample, "logic", net ? net->value == ESK_L0 ? "0" : net->value == ESK_L1 ? "1" : net->value == ESK_LZ ? "Z" : "X" : "X") ||
                put(s, sample, "voltage", net && (net->island || net->is_ground) && isfinite(net->voltage) ? esk_json_new_number(s->a, net->voltage) : esk_json_new_null(s->a))) st = ES_ENOMEM;
        }
        esk_json_free(s->a, effective); es_session_destroy(temporary);
        if (st != ES_OK) goto done;
    }
done:
    if (st == ES_OK) *result = out; else esk_json_free(s->a, out);
    return st;
}
