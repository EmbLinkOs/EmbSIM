/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* diag.h -- diagnostics (SPEC-001 §7): delivered to the session's handler
 * and kept in a bounded log for reports. */
#ifndef ESK_DIAG_H
#define ESK_DIAG_H

#include "embsim/types.h"
#include "util/alloc.h"
#include "util/json.h"

typedef struct esk_diag_record {
    es_severity_t severity;
    es_time_t time_ps;
    char code[48];
    char subject_kind[16];
    char subject[96];
    char message[240];
    char file[128];
    int line;
    int phase; /* ESK_DIAG_LOAD or ESK_DIAG_RUN */
} esk_diag_record;

/* the phase a diagnostic was raised in: a run's reset forgets the run's own,
 * not the project's load (netlist checks, missing images) */
enum { ESK_DIAG_LOAD = 0, ESK_DIAG_RUN = 1 };

typedef struct esk_diag {
    esk_alloc *a;
    es_diagnostic_fn fn;
    void *ctx;
    esk_diag_record *log; /* ring */
    size_t cap, len, head;
    size_t counts[4];
    int fatal; /* a fatal diagnostic was raised since the last reset */
    char fatal_code[48];
    int phase; /* what new diagnostics are raised in */
} esk_diag;

es_status esk_diag_init(esk_diag *d, esk_alloc *a, size_t log_capacity);
void esk_diag_free(esk_diag *d);
/* forget the run's diagnostics, keeping the load's */
void esk_diag_reset(esk_diag *d);
/* forget every diagnostic (a new project is loaded) */
void esk_diag_clear(esk_diag *d);
/* Raise a diagnostic. subject_kind/subject/file may be NULL. */
void esk_diag_raise(esk_diag *d, es_severity_t sev, const char *code, es_time_t t,
                    const char *subject_kind, const char *subject, const char *file, int line,
                    const char *fmt, ...);
/* the log as a JSON array (newest last) */
esk_json *esk_diag_to_json(const esk_diag *d);
size_t esk_diag_count(const esk_diag *d, es_severity_t sev);

#endif
