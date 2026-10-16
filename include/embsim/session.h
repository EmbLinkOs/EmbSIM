/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* session.h -- a project loaded and under control (SPEC-001 §6). The
 * session protocol (SPEC-008) is the functional surface; es_session_rpc runs
 * any of its methods in process. */
#ifndef EMBSIM_SESSION_H
#define EMBSIM_SESSION_H

#include "embsim/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct es_session es_session;
typedef struct es_catalog es_catalog;

typedef struct es_session_config_t {
    const es_allocator_t *allocator; /* NULL: malloc/free */
    es_catalog *catalog;             /* NULL: the session creates one with the bundled catalog */
    const char *policy;              /* "functional" | "timed" | "electrical"; NULL: "electrical" */
    uint32_t max_nets;               /* 0: defaults of SPEC-002/003 */
    uint32_t max_components;
    uint32_t max_pending_events;
    uint32_t max_delta_cycles;
    es_time_t core_quantum_ps;
    es_time_t crossing_resolution_ps;
    size_t probe_memory_bytes;
} es_session_config_t;

ES_API es_status es_session_create(const es_session_config_t *cfg, es_session **out);
ES_API void es_session_destroy(es_session *s);
ES_API es_status es_session_load_file(es_session *s, const char *path);
ES_API es_status es_session_load_json(es_session *s, const char *json, size_t len, const char *base_dir);
ES_API es_status es_session_reset(es_session *s);
ES_API es_status es_session_advance(es_session *s, es_time_t until);
ES_API es_status es_session_step(es_session *s);
ES_API es_time_t es_session_now(const es_session *s);
ES_API es_status es_session_rpc(es_session *s, const char *req, size_t len, char **res, size_t *res_len);
ES_API es_status es_session_set_diagnostic_handler(es_session *s, es_diagnostic_fn fn, void *ctx);
ES_API es_status es_session_set_notification_handler(es_session *s, es_notification_fn fn, void *ctx);
ES_API es_status es_session_probe_read(es_session *s, uint32_t probe, es_sample_t *buf, size_t cap, size_t *n);

ES_API es_status es_catalog_create(es_catalog **out);
ES_API void es_catalog_destroy(es_catalog *c);
ES_API es_status es_catalog_add_library(es_catalog *c, const char *path);
ES_API es_status es_catalog_validate(es_catalog *c, const char *path, char **report_json);

ES_API es_version_t es_version(void);
ES_API int es_abi_version(void);
ES_API const char *es_strerror(es_status st);
ES_API void es_free(void *p);

/* Protocol endpoints (05 §1): a stdio sidecar loop and a loopback WebSocket
 * server that also serves the workbench assets from `ui_dir` (NULL: none).
 * Both return when the client disconnects or the host interrupts them. */
ES_API es_status es_serve_stdio(es_session *s);
ES_API es_status es_serve_websocket(es_session *s, const char *host, int port, const char *ui_dir, int open_browser);
/* Let pages from `origin` (exactly, e.g. "tauri://localhost") use the
 * WebSocket endpoint besides the server's own pages: the desktop shell's
 * webview. At most 8; call before es_serve_websocket. */
ES_API es_status es_serve_allow_origin(es_session *s, const char *origin);
/* Serve the GDB remote protocol for a board's MCU (NULL: the first with
 * firmware) on host:port (port 0: any); `listening` is told the port before
 * the server waits for its one connection. Returns when the debugger
 * detaches, kills the target or the firmware ends. */
ES_API es_status es_serve_gdb(es_session *s, const char *board, const char *host, int port,
                              void (*listening)(void *ctx, int port), void *ctx);

#ifdef __cplusplus
}
#endif
#endif
