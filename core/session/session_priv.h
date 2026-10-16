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
    int threshold;      /* a digital probe on an analog net: logic from the voltage, changes only */
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
    /* origins besides its own the WebSocket server accepts (the desktop shell) */
    char *allow_origin[8];
    int n_allow_origin;
    /* project.transaction: the project as edited between begin and commit */
    esk_json *tx_project;
    int tx_id, tx_last, tx_structural;
    ESK_VEC(esk_board_inst *) boards;
    ESK_VEC(esk_probe *) probes;
    uint32_t next_probe_id;
    size_t probe_memory_bytes;
    /* inputs record (SPEC-012 §1 inputs) */
    esk_json *inputs_log;
    uint64_t input_seq;
    int recording;
    char *record_path;
    /* what the run observed while recording (record.c) */
    esk_json *observations;
    int obs_truncated;
    es_time_t record_from; /* record.start's time: its observations begin there */
    uint64_t record_from_seq; /* and the inputs given before it */
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
    /* the firmware analyses on (measure.c): ESK_AN_* bits, on one board or
     * on every board with firmware (NULL) */
    unsigned an_what;
    char *an_board;
    /* statistics */
    uint64_t rpc_count;
    double wall_time_s;
};

/* measure.c: the analyses `what` (ESK_AN_*) on `board`'s MCU, or every
 * board's with firmware (NULL); 0 turns them off. They stay on across a
 * project's reload (esk_session_analyses_apply, after a load) */
es_status esk_session_analyze(es_session *s, const char *board, unsigned what, char *why, size_t cap);
void esk_session_analyses_apply(es_session *s);
/* the boards' coverage (`coverage`) or profile reports as text in format
 * `fmt` (ESK_AN_*), one board's or every board's; ES_ESTATE when it is not on */
es_status esk_session_analysis_text(es_session *s, const char *board, int coverage, int fmt, esk_strbuf *out,
                                    char *why, size_t cap);
/* {BOARD: coverage or profile JSON}; `detail`: every file's line counts,
 * every function's profile */
esk_json *esk_session_analysis_json(es_session *s, const char *board, int coverage, int detail);
/* a run's report gains "coverage" and "profile" summaries when they are on */
void esk_session_analysis_report(es_session *s, esk_json *report);

/* loader.c */
es_status esk_project_load(es_session *s, const esk_json *project, const char *base_dir);
/* edit.c: one project.transaction edit applied to a project's JSON; 0 or
 * -1 with the reason in `why`. `*structural` is set unless it only moves
 * or draws. */
int esk_project_apply_edit(esk_alloc *a, esk_json *project, const esk_json *edit, int *structural, char *why,
                           size_t cap);
/* toolchain.c: the profiles found on this host; a board's firmware built */
es_status esk_toolchain_detect(es_session *s, esk_json **out);
es_status esk_toolchain_build(es_session *s, const char *board_id, const char *profile_id, esk_json **out);
/* debug.c: the debug.* methods; the board whose MCU is stopped (1, and a
 * description of where); every stopped core resumed */
int esk_debug_has(const char *method);
es_status esk_debug_call(es_session *s, const char *method, const esk_json *params, esk_json **result,
                         int *code, char *msg, size_t cap);
int esk_debug_stopped_board(es_session *s, esk_json **info);
void esk_debug_resume_all(es_session *s);
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
/* record.c: recordings (SPEC-012): observations from now on; a core's
 * interrupt entry (the engine's on_irq); a probe's sample; the document
 * (its end observations added); replay's comparison of two observation
 * lists (ok, or the first divergence); a recording as a scenario */
void esk_record_start(es_session *s);
void esk_record_irq(void *ctx, esk_comp *mcu, int core, int cores, int n, es_time_t at);
void esk_record_probe(es_session *s, const esk_probe *p, es_time_t at, double v);
/* each digital probe's level now, as observations at `at` (a recording
 * that starts in a running session begins with them) */
void esk_record_levels(es_session *s, es_time_t at);
esk_json *esk_record_document(es_session *s, const char *project_path, const char *scenario,
                              const esk_json *report, const esk_json *extra_probes);
esk_json *esk_record_compare(es_session *s, const esk_json *want, const esk_json *got);
/* the identities that changed since the recording: [{what, recorded, now}] */
esk_json *esk_record_drift(es_session *s, const esk_json *rec);
/* replay a recording on the session, its project loaded: from reset, its
 * inputs at their times, to its end; the observations compared from its
 * start (from_ps). *result: {ok, compared, divergence?, drift}; ES_ESTATE
 * (and drift) when the project or an image changed and allow_drift is 0 */
es_status esk_record_replay(es_session *s, const esk_json *rec, int allow_drift, esk_json **result);
esk_json *esk_record_to_scenario(esk_alloc *a, const esk_json *rec, const char *name, int with_probes,
                                 int with_serial);

#endif
