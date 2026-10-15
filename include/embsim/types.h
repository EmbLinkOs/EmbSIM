/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* types.h -- status codes, time, versions, diagnostics and samples
 * (SPEC-001 §3, §7; SPEC-002 §1). */
#ifndef EMBSIM_TYPES_H
#define EMBSIM_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include "embsim/compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int es_status;
enum {
    ES_OK = 0,
    ES_EINVAL = -1,
    ES_ENOMEM = -2,
    ES_ELIMIT = -3,
    ES_ETIME = -4,
    ES_ENOTFOUND = -5,
    ES_EPARSE = -6,
    ES_ESCHEMA = -7,
    ES_EUNSUPPORTED = -8,
    ES_ECONVERGE = -9,
    ES_ESTATE = -10,
    ES_EIO = -11,
    ES_EPROTO = -12,
    ES_EINTERNAL = -13
};

/* Virtual time: picoseconds since reset. ES_TIME_MAX is the horizon. */
typedef uint64_t es_time_t;
#define ES_TIME_MAX UINT64_MAX
#define ES_PS_PER_NS UINT64_C(1000)
#define ES_PS_PER_US UINT64_C(1000000)
#define ES_PS_PER_MS UINT64_C(1000000000)
#define ES_PS_PER_S UINT64_C(1000000000000)

typedef struct es_version_t {
    int major, minor, patch;
    const char *string;
} es_version_t;

typedef enum es_severity_t {
    ES_SEV_INFO = 0,
    ES_SEV_WARNING = 1,
    ES_SEV_ERROR = 2,
    ES_SEV_FATAL = 3
} es_severity_t;

/* A diagnostic record (SPEC-001 §7). Strings are valid during the callback. */
typedef struct es_diagnostic_t {
    es_severity_t severity;
    const char *code;      /* stable identifier, e.g. "net.contention" */
    const char *message;
    es_time_t time_ps;
    const char *subject_kind; /* "net", "pin", "component", "file", ... or NULL */
    const char *subject;      /* its name, or NULL */
    const char *file;         /* for description errors, or NULL */
    int line;
} es_diagnostic_t;

typedef void (*es_diagnostic_fn)(void *ctx, const es_diagnostic_t *d);
/* A protocol notification: method name and JSON params text. */
typedef void (*es_notification_fn)(void *ctx, const char *method, const char *params_json, size_t len);

typedef struct es_sample_t {
    es_time_t time_ps;
    double value; /* analog: volts/amps; digital: 0, 1, 2 (Z), 3 (X); value probes: the value */
} es_sample_t;

typedef struct es_allocator_t {
    void *(*alloc)(void *ctx, size_t size);
    void *(*realloc)(void *ctx, void *p, size_t size);
    void (*free)(void *ctx, void *p);
    void *ctx;
} es_allocator_t;

#ifdef __cplusplus
}
#endif
#endif
