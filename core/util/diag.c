/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "util/diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "util/str.h"

es_status esk_diag_init(esk_diag *d, esk_alloc *a, size_t log_capacity)
{
    memset(d, 0, sizeof *d);
    d->a = a;
    d->cap = log_capacity ? log_capacity : 256;
    d->log = (esk_diag_record *)esk_calloc(a, d->cap, sizeof *d->log);
    return d->log ? ES_OK : ES_ENOMEM;
}

void esk_diag_free(esk_diag *d)
{
    esk_free(d->a, d->log, d->cap * sizeof *d->log);
    d->log = NULL;
}

void esk_diag_clear(esk_diag *d)
{
    d->len = d->head = 0;
    memset(d->counts, 0, sizeof d->counts);
    d->fatal = 0;
    d->fatal_code[0] = 0;
}

void esk_diag_reset(esk_diag *d)
{
    size_t i, kept = 0;
    memset(d->counts, 0, sizeof d->counts);
    d->fatal = 0;
    d->fatal_code[0] = 0;
    for (i = 0; i < d->len; i++) {
        const esk_diag_record *r = &d->log[(d->head + i) % d->cap];
        if (r->phase != ESK_DIAG_LOAD)
            continue;
        if (kept != i)
            d->log[(d->head + kept) % d->cap] = *r;
        if ((unsigned)r->severity < 4)
            d->counts[r->severity]++;
        kept++;
    }
    d->len = kept;
}

static void copy(char *dst, size_t cap, const char *src)
{
    if (!src)
        src = "";
    esk_snprintf(dst, cap, "%s", src);
}

void esk_diag_raise(esk_diag *d, es_severity_t sev, const char *code, es_time_t t,
                    const char *subject_kind, const char *subject, const char *file, int line,
                    const char *fmt, ...)
{
    esk_diag_record *r;
    va_list ap;
    es_diagnostic_t out;
    size_t idx = (d->head + d->len) % d->cap;
    if (d->len == d->cap)
        d->head = (d->head + 1) % d->cap;
    else
        d->len++;
    r = &d->log[idx];
    memset(r, 0, sizeof *r);
    r->severity = sev;
    r->time_ps = t;
    copy(r->code, sizeof r->code, code);
    copy(r->subject_kind, sizeof r->subject_kind, subject_kind);
    copy(r->subject, sizeof r->subject, subject);
    copy(r->file, sizeof r->file, file);
    r->line = line;
    r->phase = d->phase;
    va_start(ap, fmt);
    vsnprintf(r->message, sizeof r->message, fmt, ap);
    va_end(ap);
    if ((unsigned)sev < 4)
        d->counts[sev]++;
    if (sev == ES_SEV_FATAL && !d->fatal) {
        d->fatal = 1;
        copy(d->fatal_code, sizeof d->fatal_code, code);
    }
    if (d->fn) {
        memset(&out, 0, sizeof out);
        out.severity = sev;
        out.code = r->code;
        out.message = r->message;
        out.time_ps = t;
        out.subject_kind = subject_kind ? r->subject_kind : NULL;
        out.subject = subject ? r->subject : NULL;
        out.file = file ? r->file : NULL;
        out.line = line;
        d->fn(d->ctx, &out);
    }
}

static const char *sevname(es_severity_t s)
{
    switch (s) {
    case ES_SEV_INFO: return "info";
    case ES_SEV_WARNING: return "warning";
    case ES_SEV_ERROR: return "error";
    default: return "fatal";
    }
}

esk_json *esk_diag_to_json(const esk_diag *d)
{
    esk_json *arr = esk_json_new_array(d->a);
    size_t i;
    if (!arr)
        return NULL;
    for (i = 0; i < d->len; i++) {
        const esk_diag_record *r = &d->log[(d->head + i) % d->cap];
        esk_json *o = esk_json_new_object(d->a);
        if (!o)
            break;
        esk_json_set_string(d->a, o, "severity", sevname(r->severity));
        esk_json_set_string(d->a, o, "code", r->code);
        esk_json_set_string(d->a, o, "message", r->message);
        esk_json_set_u64_string(d->a, o, "time_ps", r->time_ps);
        esk_json_set_string(d->a, o, "phase", r->phase == ESK_DIAG_LOAD ? "load" : "run");
        if (r->subject_kind[0]) {
            esk_json *s = esk_json_new_object(d->a);
            if (s) {
                esk_json_set_string(d->a, s, "kind", r->subject_kind);
                esk_json_set_string(d->a, s, "name", r->subject);
                esk_json_object_set(d->a, o, "subject", s);
            }
        }
        if (r->file[0]) {
            esk_json_set_string(d->a, o, "file", r->file);
            esk_json_set_integer(d->a, o, "line", r->line);
        }
        esk_json_array_push(d->a, arr, o);
    }
    return arr;
}

size_t esk_diag_count(const esk_diag *d, es_severity_t sev) { return (unsigned)sev < 4 ? d->counts[sev] : 0; }
