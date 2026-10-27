/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "session/spectrum.h"
#include "util/num.h"
#include "util/sha256.h"
#include "util/str.h"
#include <math.h>
#include <string.h>

static const double PI = 3.14159265358979323846;
static es_status invalid(char *why, size_t cap, const char *message)
{
    esk_snprintf(why, cap, "%s", message); return ES_EINVAL;
}
static int put(es_session *s, esk_json *o, const char *key, esk_json *v)
{
    if (!v) return -1;
    if (esk_json_object_set(s->a, o, key, v)) { esk_json_free(s->a, v); return -1; }
    return 0;
}
static const char *text(const esk_json *o, const char *key)
{
    const esk_json *v = esk_json_get(o, key);
    const char *p = esk_json_str(v);
    return p && strlen(p) == v->u.string.len ? p : NULL;
}
static int time_value(const esk_json *o, const char *key, es_time_t *at)
{
    const char *p = text(o, key);
    return !p || esk_parse_time(p, at);
}
static int ps_value(const esk_json *o, const char *key, es_time_t *at)
{
    const char *p = text(o, key);
    return !p || !*p || strspn(p, "0123456789") != strlen(p) || esk_parse_time(p, at);
}
static esk_json *document(es_session *s, const esk_json *value, char *why, size_t cap)
{
    esk_json *out;
    char *p, hash[72]; size_t len = 0;
    p = esk_json_format(s->a, value, 0, &len);
    if (!p) return NULL;
    if (len > 8388608) { esk_snprintf(why, cap, "Spectrum capture record exceeds the 8 MiB limit."); esk_free(s->a, p, len + 1); return NULL; }
    esk_sha256_text(p, len, hash); out = esk_json_new_object(s->a);
    if (out && (esk_json_set_string(s->a, out, "sha256", hash) || esk_json_set_string(s->a, out, "json", p))) {
        esk_json_free(s->a, out); out = NULL;
    }
    esk_free(s->a, p, len + 1); return out;
}
static esk_json *verified(es_session *s, const esk_json *doc)
{
    const char *p = text(doc, "json"), *expected = text(doc, "sha256");
    char hash[72]; esk_json *value;
    if (!p || !expected || strlen(p) > 8388608 || strlen(expected) != 71) return NULL;
    esk_sha256_text(p, strlen(p), hash);
    if (strcmp(hash, expected)) return NULL;
    value = esk_json_parse(s->a, p, strlen(p), NULL, NULL);
    if (value && value->type != ESK_JSON_OBJECT) { esk_json_free(s->a, value); value = NULL; }
    return value;
}
static int source_ok(es_session *s, const esk_json *source)
{
    esk_json *payload = verified(s, source), *project = NULL;
    const esk_json *catalog, *instances;
    size_t i; int ok = 0;
    if (!payload || esk_json_get_num(payload,"schema_version",0) != 1 || !esk_str_eq(text(payload, "source"), "native") || !esk_str_eq(text(payload, "kind"), "engineering_manifest") ||
        !esk_str_eq(text(payload, "policy"), "electrical")) goto done;
    project = verified(s, esk_json_get(payload, "project"));
    if (!project || esk_json_get_num(project,"schema_version",0) != 1 || !esk_str_eq(text(project, "kind"), "project")) goto done;
    catalog = esk_json_get(payload, "catalog"); instances = esk_json_get(payload, "instances");
    if (!catalog || catalog->type != ESK_JSON_OBJECT || !instances || instances->type != ESK_JSON_ARRAY) goto done;
    for (i = 0; i < esk_json_len(instances); i++) if (esk_str_eq(text(esk_json_at(instances, i), "kind"), "board")) goto done;
    for (i = 0; i < esk_json_len(catalog); i++) {
        esk_json *desc = verified(s, esk_json_at(catalog, i));
        if (!desc) goto done;
        esk_json_free(s->a, desc);
    }
    ok = 1;
done:
    esk_json_free(s->a, project); esk_json_free(s->a, payload); return ok;
}
/* Iterative radix-2 DFT, negative-exponential convention. All inputs are scaled
 * before transform, bounding butterfly intermediates independently of volts. */
static void fft(double *re, double *im, size_t n)
{
    size_t i, j, width;
    for (i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double a = re[i], b = im[i]; re[i] = re[j]; im[i] = im[j]; re[j] = a; im[j] = b; }
    }
    for (width = 2; width <= n; width <<= 1) {
        double angle = -2 * PI / (double)width, cr = cos(angle), ci = sin(angle);
        for (i = 0; i < n; i += width) {
            double wr = 1, wi = 0;
            for (j = 0; j < width / 2; j++) {
                size_t a = i + j, b = a + width / 2;
                double tr = wr * re[b] - wi * im[b], ti = wr * im[b] + wi * re[b], next = wr * cr - wi * ci;
                re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                wi = wr * ci + wi * cr; wr = next;
            }
        }
    }
}

es_status esk_session_spectrum(es_session *s, const esk_json *request, esk_json **result, char *why, size_t cap)
{
    const esk_json *capture = esk_json_get(request, "capture"), *rows = esk_json_get(capture, "samples"), *spec = esk_json_get(capture, "request");
    const esk_json *probes = esk_json_get(spec, "probes"), *diags = esk_json_get(capture, "diagnostics");
    const char *target = text(request, "target"), *window = text(request, "window");
    const esk_json *remove = esk_json_get(request, "remove_mean"), *ok = esk_json_get(capture, "ok"), *failure = esk_json_get(capture, "failure");
    double re[4096], im[4096], scale = 0, mean = 0, sumw = 0, sumw2 = 0;
    es_time_t start, stop, step, end;
    size_t i, j, n, channel = 8;
    esk_json *out = NULL, *bins; es_status st = ES_ENOMEM;
    *result = NULL; why[0] = 0;
    if (!request || request->type != ESK_JSON_OBJECT) return invalid(why, cap, "Expected spectrum options and a complete capture.");
    for (i = 0; i < esk_json_len(request); i++) {
        const char *key = esk_json_at(request, i)->key;
        if (strcmp(key, "capture") && strcmp(key, "target") && strcmp(key, "window") && strcmp(key, "remove_mean")) return invalid(why, cap, "Unknown spectrum option.");
    }
    if (!target || !window || (strcmp(window, "rectangular") && strcmp(window, "hann")) || !remove || remove->type != ESK_JSON_BOOL)
        return invalid(why, cap, "Choose target, window rectangular/hann and explicit boolean remove_mean.");
    if (!capture || !esk_str_eq(text(capture, "kind"), "transient_capture") || esk_json_get_num(capture, "schema_version", 0) != 1 ||
        !esk_str_eq(text(capture, "method"), "reset_uniform_sample") || !ok || ok->type != ESK_JSON_BOOL || !ok->u.boolean ||
        !failure || failure->type != ESK_JSON_NULL || !diags || diags->type != ESK_JSON_ARRAY)
        return invalid(why, cap, "Spectrum requires a complete uniform native capture; partial results are not transformed.");
    for (i = 0; i < esk_json_len(diags); i++) if (esk_str_eq(text(esk_json_at(diags, i), "severity"), "error")) return invalid(why, cap, "Capture contains an error diagnostic.");
    if ((esk_json_get(spec,"kind") && !esk_str_eq(text(spec,"kind"),"transient_request")) ||
        (esk_json_get(spec,"schema_version") && esk_json_get_num(spec,"schema_version",0) != 1)) return invalid(why,cap,"Unsupported captured request format.");
    if (time_value(spec, "start", &start) || time_value(spec, "stop", &stop) || time_value(spec, "step", &step) || start >= stop || !step ||
        stop > UINT64_C(100000000000) || (stop - start) % step || (stop - start) / step > 4096 || ps_value(capture, "ended_at_ps", &end) || end != stop)
        return invalid(why, cap, "Capture times must describe a complete exact uniform grid within 100 ms.");
    n = (size_t)((stop - start) / step);
    if (n < 16 || (n & (n - 1)) || !rows || rows->type != ESK_JSON_ARRAY || esk_json_len(rows) != n + 1)
        return invalid(why, cap, "Use 17–4097 capture samples: one more than a power of two. The final endpoint is excluded from the FFT.");
    if (!probes || probes->type != ESK_JSON_ARRAY || !esk_json_len(probes) || esk_json_len(probes) > 8) return invalid(why, cap, "Capture must have 1–8 ordered voltage targets.");
    for (i = 0; i < esk_json_len(probes); i++) {
        const char *p = esk_json_str(esk_json_at(probes, i));
        if (!p || strlen(p) != esk_json_at(probes, i)->u.string.len || strlen(p) > 191 || (!esk_str_has_prefix(p, "net:") && !esk_str_has_prefix(p, "pin:"))) return invalid(why, cap, "Invalid capture target.");
        if (!strcmp(p, target)) channel = i;
        for (j = 0; j < i; j++) if (esk_str_eq(p, esk_json_str(esk_json_at(probes, j)))) return invalid(why, cap, "Duplicate capture target.");
    }
    if (channel == 8) return invalid(why, cap, "Selected target is not in the captured measurements.");
    for (i = 0; i <= n; i++) {
        const esk_json *row = esk_json_at(rows, i), *measurements = esk_json_get(row, "measurements"); es_time_t at;
        if (ps_value(row, "at_ps", &at) || at != start + (es_time_t)i * step || !measurements || measurements->type != ESK_JSON_ARRAY || esk_json_len(measurements) != esk_json_len(probes)) return invalid(why, cap, "Capture samples do not match their exact grid/target count.");
        for (j = 0; j < esk_json_len(probes); j++) {
            const esk_json *m = esk_json_at(measurements, j), *v = esk_json_get(m, "voltage");
            const char *logic = text(m, "logic");
            if (!esk_str_eq(text(m, "target"), esk_json_str(esk_json_at(probes, j))) || !logic || strlen(logic) != 1 || !strchr("01XZ", *logic) || !v ||
                (v->type != ESK_JSON_NULL && (v->type != ESK_JSON_NUMBER || !isfinite(v->u.number.value)))) return invalid(why, cap, "Malformed measurement, logic state or nonfinite voltage.");
            if (j == channel) {
                if (v->type != ESK_JSON_NUMBER) return invalid(why, cap, "Selected trace has unavailable voltages; gaps are not interpolated for FFT.");
                if (i < n) { re[i] = v->u.number.value; im[i] = 0; if (fabs(re[i]) > scale) scale = fabs(re[i]); }
            }
        }
    }
    if (!esk_str_eq(text(esk_json_get(capture,"source"),"kind"),"engineering_manifest") || esk_json_get_num(esk_json_get(capture,"source"),"schema_version",0) != 1 || !source_ok(s, esk_json_get(capture, "source"))) return invalid(why, cap, "Capture source/project/model hashes or native electrical context do not verify.");
    if (!scale) scale = 1;
    for (i = 0; i < n; i++) { re[i] /= scale; mean += re[i] / (double)n; }
    for (i = 0; i < n; i++) {
        double w = !strcmp(window, "hann") ? .5 - .5 * cos(2 * PI * (double)i / (double)n) : 1;
        re[i] = (re[i] - (remove->u.boolean ? mean : 0)) * w; sumw += w; sumw2 += w*w;
    }
    fft(re, im, n);
    out = esk_json_new_object(s->a); if (!out) goto done;
    if (put(s, out, "capture", document(s, capture, why, cap))) { if (*why) st = ES_ELIMIT; goto done; }
    if (put(s, out, "bins", esk_json_new_array(s->a)) ||
        esk_json_set_integer(s->a, out, "schema_version", 1) || esk_json_set_string(s->a, out, "kind", "frequency_spectrum") ||
        esk_json_set_string(s->a, out, "method", "radix2_coherent_amplitude") ||
        esk_json_set_string(s->a, out, "record_convention", "half_open_drop_endpoint") || esk_json_set_string(s->a, out, "phase_reference", "first_sample_cosine") || esk_json_set_string(s->a, out, "target", target) ||
        esk_json_set_string(s->a, out, "window", window) || esk_json_set_bool(s->a, out, "remove_mean", remove->u.boolean) ||
        esk_json_set_integer(s->a, out, "used_samples", (int64_t)n) || esk_json_set_number(s->a, out, "sample_rate_hz", 1e12 / (double)step) ||
        esk_json_set_number(s->a, out, "bin_width_hz", 1e12 / (double)step / (double)n) || esk_json_set_number(s->a, out, "nyquist_hz", 5e11 / (double)step) ||
        esk_json_set_number(s->a, out, "mean_v", mean*scale) || esk_json_set_number(s->a, out, "coherent_gain", sumw/(double)n) ||
        esk_json_set_number(s->a, out, "enbw_bins", (double)n*sumw2/(sumw*sumw))) goto done;
    bins = esk_json_get_mut(out, "bins");
    for (i = 0; i <= n/2; i++) {
        double factor = (i == 0 || i == n/2 ? 1 : 2) / sumw;
        double rv = (re[i]*factor)*scale, iv = (im[i]*factor)*scale, peak = hypot(rv, iv), rms = peak * (i == 0 || i == n/2 ? 1 : .7071067811865475244);
        esk_json *bin;
        if (!isfinite(rv) || !isfinite(iv) || !isfinite(peak) || !isfinite(rms) || !isfinite(mean*scale)) { st = invalid(why, cap, "Spectrum exceeds representable voltage range."); goto done; }
        bin = esk_json_new_object(s->a); if (!bin) goto done;
        if (esk_json_array_push(s->a, bins, bin)) { esk_json_free(s->a, bin); goto done; }
        if (esk_json_set_integer(s->a, bin, "index", (int64_t)i) || esk_json_set_number(s->a, bin, "frequency_hz", (double)i*1e12/(double)step/(double)n) ||
            esk_json_set_number(s->a, bin, "real_v", rv) || esk_json_set_number(s->a, bin, "imag_v", iv) || esk_json_set_number(s->a, bin, "peak_v", peak) ||
            esk_json_set_number(s->a, bin, "rms_v", rms) || put(s, bin, "dbv_rms", rms > 0 ? esk_json_new_number(s->a, 20*log10(rms)) : esk_json_new_null(s->a)) ||
            put(s, bin, "phase_deg", peak > 0 ? esk_json_new_number(s->a, atan2(iv,rv)*180/PI) : esk_json_new_null(s->a))) goto done;
    }
    st = ES_OK;
done:
    if (st == ES_OK) *result = out; else esk_json_free(s->a, out); return st;
}
