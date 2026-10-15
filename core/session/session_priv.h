/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* session_priv.h -- the session (SPEC-001 §6, SPEC-008): a project loaded
 * and under control; probes; the input log; run state. */
#ifndef ESK_SESSION_PRIV_H
#define ESK_SESSION_PRIV_H

#include "embsim/session.h"
#include "engine/engine.h"
#include "model/registry.h"
#include "project/catalog.h"
#include "util/diag.h"

typedef enum esk_probe_kind { ESK_PROBE_DIGITAL, ESK_PROBE_ANALOG, ESK_PROBE_VALUE, ESK_PROBE_SERIAL } esk_probe_kind;

typedef struct esk_probe {
    uint32_t id;
    char *name;         /* user id or generated */
    char *target;       /* "net:X", "pin:I.P", "device:I.q", "output:I.name", "serial:I" */
    esk_probe_kind kind;
    esk_net *net;
    esk_comp *comp;
    char *quantity;     /* device quantity or output name */
    double rate_hz;     /* analog resampling; 0 = at changes/steps */
    es_time_t period_ps;
    es_sample_t *ring;
    size_t cap, len, head;
    double last;
    int has_last;
    uint64_t dropped;
    int subscribed;     /* waveform stream active */
    uint32_t stream_seq;
    es_time_t next_sample;
    esk_event_handle sample_event;
} esk_probe;

typedef struct esk_board_inst {
    char *id;
    char *board_id;
    const esk_catalog_item *item;
    esk_json *firmware;  /* copy of the project's firmware object, or NULL */
    esk_comp *mcu;       /* the placeholder MCU component */
} esk_board_inst;

struct es_session {
    esk_alloc alloc;
    esk_alloc *a;
    esk_diag diag;
    esk_engine eng;
    esk_registry reg;
    es_catalog *catalog;
    int owns_catalog;
    char *catalog_dir;
    /* project */
    esk_json *project;      /* the canonical project JSON as loaded (with resolved layout) */
    char *project_path;
    char *base_dir;
    char *name;
    int loaded;
    int policy_forced;      /* the session config fixed the policy; the project cannot change it */
    ESK_VEC(esk_board_inst *) boards;
    ESK_VEC(esk_probe *) probes;
    uint32_t next_probe_id;
    size_t probe_memory_bytes;
    /* inputs record (SPEC-012 §1 inputs) */
    esk_json *inputs_log;
    uint64_t input_seq;
    int recording;
    char *record_path;
    /* run state */
    int running;
    double speed;
    uint64_t last_host_ms;
    es_time_t run_until;     /* ES_TIME_MAX when unbounded */
    es_time_t run_slice_ps;
    es_time_t core_quantum_ps;
    /* clients */
    es_notification_fn notify;
    void *notify_ctx;
    double observe_rate_hz;
    int observe_subscribed;
    uint64_t last_observe_ms;
    char *client_name;
    int opened;
    /* statistics */
    uint64_t rpc_count;
    double wall_time_s;
};

/* loader.c */
es_status esk_project_load(es_session *s, const esk_json *project, const char *base_dir);
void esk_project_unload(es_session *s);
/* session.c helpers used by rpc.c and scenario.c */
es_status esk_session_apply_input(es_session *s, const char *target, const char *input, const esk_json *value,
                                  es_time_t at, es_time_t *applied_at, uint64_t *seq);
esk_json *esk_session_snapshot(es_session *s, const esk_json *include);
esk_json *esk_session_topology(es_session *s);
es_status esk_session_probe_add(es_session *s, const char *name, const char *target, const char *kind, double rate_hz, esk_probe **out);
void esk_session_probe_remove(es_session *s, uint32_t id);
esk_probe *esk_session_probe_find(es_session *s, const char *name_or_id);
esk_json *esk_session_probe_samples(es_session *s, esk_probe *p, es_time_t from, es_time_t to, size_t max);
void esk_session_notify(es_session *s, const char *method, esk_json *params); /* takes ownership */
es_status esk_session_run_slice(es_session *s);  /* one pacing slice while running */
void esk_session_emit_observations(es_session *s, int force);
es_status esk_session_do_reset(es_session *s);
es_status esk_session_do_advance(es_session *s, es_time_t until);
/* rpc.c */
es_status esk_rpc_dispatch(es_session *s, const esk_json *req, esk_json **response);
/* scenario.c */
esk_json *esk_scenario_run(es_session *s, const esk_json *scenario, const char *base_dir, es_time_t until_override);
esk_json *esk_session_report(es_session *s, const char *ended_reason, esk_json *assertions);
/* probes sampled from net changes */
void esk_session_on_net_change(void *ctx, esk_net *net);

#endif
