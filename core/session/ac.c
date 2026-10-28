/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "session/ac.h"
#include "analog/analog.h"
#include "project/loader.h"
#include "project/manifest.h"
#include "session/circuit_analysis.h"
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
    if (!v)
        return -1;
    if (esk_json_object_set(s->a, o, key, v)) {
        esk_json_free(s->a, v);
        return -1;
    }
    return 0;
}
es_status esk_session_ac(es_session *s, const esk_json *request, esk_json **result, char *why,
                         size_t cap)
{
    const esk_json *frequencies = esk_json_get(request, "frequencies_hz"),
                   *probes = esk_json_get(request, "probes");
    const char *source_id = esk_json_get_str(request, "source", ""),
               *reference_id = esk_json_get_str(request, "reference", "");
    es_session *temporary = NULL;
    es_session_config_t cfg;
    esk_json *out = NULL, *manifest = NULL, *rows;
    esk_comp *source;
    esk_net *reference;
    esk_ac_element excitation;
    double previous = 0.0;
    size_t i, j, k;
    es_status status;
    *result = NULL;
    why[0] = 0;
    if (!s->loaded)
        return ES_ESTATE;
    if (s->running || s->tx_project)
        return reject(why, cap,
                      "Pause the source and finish or abort the transaction before AC analysis.");
    if (s->eng.policy != ESK_POLICY_ELECTRICAL || s->boards.len)
        return reject(why, cap, "AC analysis requires a board-free electrical circuit.");
    if (s->eng.comps.len > 256 || s->eng.nets.len > 1024)
        return reject(why, cap, "AC limit: 256 compiled components and 1024 nets.");
    if (!request || request->type != ESK_JSON_OBJECT)
        return reject(why, cap, "Expected an AC sweep request.");
    for (i = 0; i < esk_json_len(request); i++) {
        const char *key = esk_json_at(request, i)->key;
        if (strcmp(key, "source") && strcmp(key, "reference") && strcmp(key, "frequencies_hz") &&
            strcmp(key, "probes") && strcmp(key, "schema_version") && strcmp(key, "kind"))
            return reject(why, cap, "Unknown AC request field.");
    }
    if ((esk_json_get(request, "kind") &&
         strcmp(esk_json_get_str(request, "kind", ""), "ac_sweep_request")) ||
        (esk_json_get(request, "schema_version") &&
         esk_json_get_num(request, "schema_version", 0) != 1))
        return reject(why, cap, "Unsupported AC request version or kind.");
    if (!frequencies || frequencies->type != ESK_JSON_ARRAY || !esk_json_len(frequencies) ||
        esk_json_len(frequencies) > 256 || !probes || probes->type != ESK_JSON_ARRAY ||
        !esk_json_len(probes) || esk_json_len(probes) > 8)
        return reject(why, cap,
                      "Supply 1–256 ascending frequencies and 1–8 unique voltage targets.");
    for (i = 0; i < esk_json_len(frequencies); i++) {
        const esk_json *v = esk_json_at(frequencies, i);
        double f = v->type == ESK_JSON_NUMBER ? v->u.number.value : 0.0;
        if (v->type != ESK_JSON_NUMBER || !isfinite(f) || f < 1e-3 || f > 1e12 || f <= previous)
            return reject(
                why, cap,
                "Frequencies must be strictly ascending finite numbers between 1 mHz and 1 THz.");
        previous = f;
    }
    /* Reuse the native immutable project and catalog on a fresh engine. Never
     * read dynamic live inputs or write the source solver/history. */
    memset(&cfg, 0, sizeof cfg);
    cfg.catalog = s->catalog;
    cfg.policy = "electrical";
    status = es_session_create(&cfg, &temporary);
    if (status != ES_OK)
        return status;
    status = esk_project_load(temporary, s->project, s->base_dir);
    if (status == ES_OK)
        status = esk_session_do_reset(temporary);
    if (status != ES_OK || esk_diag_count(&temporary->diag, ES_SEV_ERROR)) {
        status = reject(why, cap, "AC isolated native project reset failed.");
        goto done;
    }
    for (i = 0; i < temporary->eng.comps.len; i++) {
        esk_comp *c = temporary->eng.comps.data[i];
        esk_ac_element e;
        int marker = !strcmp(c->ops->name, "none");
        for (k = 0; marker && k < c->npins; k++)
            if (!c->pins[k].model.power_ground)
                marker = 0;
        if ((!c->adev && !marker && strcmp(c->ops->name, "composite") && strcmp(c->ops->name, "unpopulated")) ||
            (c->adev && esk_adev_ac_element(c->adev, &e) != ES_OK)) {
            esk_snprintf(why, cap,
                         "Unsupported AC component %s (%s); nonlinear, digital, scripted and "
                         "firmware models are not linearized.",
                         c->id, c->ops->name);
            status = ES_EINVAL;
            goto done;
        }
    }
    {
        double work = 0.0;
        for (i = 0; i < temporary->eng.islands.len; i++) {
            esk_island *isl = temporary->eng.islands.data[i];
            double unknowns = (double)isl->nets.len - 1.0;
            for (j = 0; j < isl->devs.len; j++) {
                esk_ac_element e;
                if (esk_adev_ac_element(isl->devs.data[j], &e) != ES_OK) {
                    status = reject(why, cap, "Unsupported AC bridge or element.");
                    goto done;
                }
                if (e.kind == ESK_AC_L || e.kind == ESK_AC_V)
                    unknowns += 1.0;
            }
            if (unknowns > 128.0) {
                status = reject(why, cap, "AC island limit: 128 complex unknowns.");
                goto done;
            }
            work += 8.0 * unknowns * unknowns * unknowns;
        }
        if (work * (double)esk_json_len(frequencies) > 268435456.0) {
            status = reject(why, cap,
                            "AC solve budget exceeded; reduce frequency points or circuit size.");
            goto done;
        }
    }
    source = esk_engine_comp(&temporary->eng, source_id);
    reference = esk_circuit_measurement(temporary, reference_id);
    if (!source || !source->adev || esk_adev_ac_element(source->adev, &excitation) != ES_OK ||
        (excitation.kind != ESK_AC_V && excitation.kind != ESK_AC_I)) {
        status = reject(why, cap, "Choose a fitted independent native voltage or current source.");
        goto done;
    }
    if (!reference || (!reference->island && !reference->is_ground)) {
        status = reject(why, cap, "Reference must be an analog net:/pin: voltage target.");
        goto done;
    }
    for (i = 0; i < esk_json_len(probes); i++) {
        const char *target = esk_json_str(esk_json_at(probes, i));
        esk_net *net = target ? esk_circuit_measurement(temporary, target) : NULL;
        if (!target || strlen(target) > 191 || !net || (!net->island && !net->is_ground)) {
            status =
                reject(why, cap, "Each AC output must name an analog net:/pin: voltage target.");
            goto done;
        }
        for (j = 0; j < i; j++)
            if (!strcmp(target, esk_json_str(esk_json_at(probes, j)))) {
                status = reject(why, cap, "Duplicate AC output target.");
                goto done;
            }
    }
    status = esk_project_manifest(s, &manifest);
    if (status != ES_OK)
        goto done;
    out = esk_json_new_object(s->a);
    if (!out) {
        esk_json_free(s->a, manifest);
        status = ES_ENOMEM;
        goto done;
    }
    if (put(s, out, "source", manifest) || put(s, out, "request", esk_json_clone(s->a, request)) ||
        put(s, out, "points", esk_json_new_array(s->a)) ||
        esk_json_set_integer(s->a, out, "schema_version", 1) ||
        esk_json_set_string(s->a, out, "kind", "ac_sweep") ||
        esk_json_set_string(s->a, out, "method", "linear_phasor_mna") ||
        esk_json_set_string(s->a, out, "unit", excitation.kind == ESK_AC_V ? "V/V" : "ohm") ||
        esk_json_set_string(s->a, out, "excitation", "unit_real_source_phasor") ||
        esk_json_set_bool(s->a, out, "ok", 1)) {
        status = ES_ENOMEM;
        goto done;
    }
    rows = esk_json_get_mut(out, "points");
    for (i = 0; i < esk_json_len(frequencies); i++) {
        double f = esk_json_at(frequencies, i)->u.number.value, reference_re = 0, reference_im = 0;
        esk_json *row = esk_json_new_object(s->a), *values;
        if (!row) {
            status = ES_ENOMEM;
            goto done;
        }
        if (esk_json_array_push(s->a, rows, row)) {
            esk_json_free(s->a, row);
            status = ES_ENOMEM;
            goto done;
        }
        if (esk_json_set_number(s->a, row, "frequency_hz", f) ||
            put(s, row, "responses", esk_json_new_array(s->a))) {
            status = ES_ENOMEM;
            goto done;
        }
        values = esk_json_get_mut(row, "responses");
        /* Store solved phasors by native net index, independent of island IDs. */
        {
            size_t count = temporary->eng.nets.len;
            double *re = esk_calloc(s->a, count, sizeof(double)),
                   *im = esk_calloc(s->a, count, sizeof(double));
            if (!re || !im) {
                esk_free(s->a, re, count * sizeof(double));
                esk_free(s->a, im, count * sizeof(double));
                status = ES_ENOMEM;
                goto done;
            }
            for (j = 0; j < temporary->eng.islands.len; j++) {
                esk_island *isl = temporary->eng.islands.data[j];
                double *ir = NULL, *ii = NULL;
                status = esk_island_ac(isl, source, f, &ir, &ii, why, cap);
                if (status == ES_OK)
                    for (k = 0; k < isl->nets.len; k++)
                        if (isl->nets.data[k]) {
                            uint32_t index = isl->nets.data[k]->index;
                            re[index] = ir[k];
                            im[index] = ii[k];
                        }
                esk_free(isl->a, ir, isl->nets.len * sizeof(double));
                esk_free(isl->a, ii, isl->nets.len * sizeof(double));
                if (status != ES_OK)
                    break;
            }
            if (status == ES_OK) {
                reference_re = re[reference->index];
                reference_im = im[reference->index];
                for (j = 0; j < esk_json_len(probes); j++) {
                    const char *target = esk_json_str(esk_json_at(probes, j));
                    esk_net *net = esk_circuit_measurement(temporary, target);
                    double vr = re[net->index] - reference_re, vi = im[net->index] - reference_im,
                           magnitude = hypot(vr, vi);
                    esk_json *v = esk_json_new_object(s->a);
                    if (!v) {
                        status = ES_ENOMEM;
                        break;
                    }
                    if (esk_json_array_push(s->a, values, v)) {
                        esk_json_free(s->a, v);
                        status = ES_ENOMEM;
                        break;
                    }
                    if (!isfinite(magnitude)) {
                        status = reject(why, cap, "Unrepresentable AC response.");
                        break;
                    }
                    if (esk_json_set_string(s->a, v, "target", target) ||
                        esk_json_set_number(s->a, v, "real", vr) ||
                        esk_json_set_number(s->a, v, "imag", vi) ||
                        esk_json_set_number(s->a, v, "magnitude", magnitude) ||
                        put(s, v, "db",
                            magnitude > 0 ? esk_json_new_number(s->a, 20 * log10(magnitude))
                                          : esk_json_new_null(s->a)) ||
                        put(s, v, "phase_deg",
                            magnitude > 0
                                ? esk_json_new_number(s->a, atan2(vi, vr) * 57.29577951308232)
                                : esk_json_new_null(s->a))) {
                        status = ES_ENOMEM;
                        break;
                    }
                }
            }
            esk_free(s->a, re, count * sizeof(double));
            esk_free(s->a, im, count * sizeof(double));
            if (status != ES_OK)
                goto done;
        }
    }
done:
    if (temporary)
        es_session_destroy(temporary);
    if (status == ES_OK)
        *result = out;
    else
        esk_json_free(s->a, out);
    return status;
}
