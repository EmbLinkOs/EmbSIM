/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* protocol.h -- what the protocol endpoints (05 §1, SPEC-008 §1) share:
 * the idle hook that lets a run loop interleave with transport polling,
 * and the dispatch step both transports apply to one request text. The
 * endpoints themselves are es_serve_stdio (stdio.c) and es_serve_websocket
 * (ws.c, or the ES_EUNSUPPORTED stub of noserver.c without EMBSIM_SERVER). */
#ifndef ESK_PROTOCOL_H
#define ESK_PROTOCOL_H

#include <stddef.h>
#include "embsim/session.h"
#include "util/alloc.h"

/* ---- the idle hook ------------------------------------------------------- */

/* Both endpoints poll their transport with a short timeout (about 20 ms).
 * Whenever a poll times out with nothing to serve they call the idle hook,
 * so a session run loop (SPEC-002 §8, SPEC-008 §7) can advance virtual time
 * in slices between requests. The hook runs on the serving thread, between
 * requests, never inside es_session_rpc; it may call back into the session.
 * The setting is process-wide (one endpoint runs at a time); NULL clears it. */
typedef void (*es_serve_idle_fn)(void *ctx);
void esk_protocol_set_idle(es_serve_idle_fn fn, void *ctx);
/* Call the hook when one is set (used by the endpoints). */
void esk_protocol_idle(void);
/* The poll timeout the endpoints use, in milliseconds. */
#define ESK_PROTOCOL_POLL_MS 20

/* The port the WebSocket endpoint is bound to while es_serve_websocket
 * runs, or 0 (so a CLI can report an ephemeral port). */
int esk_protocol_listening_port(void);
void esk_protocol_set_listening_port(int port);

/* ---- dispatch ------------------------------------------------------------ */

/* Execute one request text on the session and produce the response text
 * (NUL-terminated; free with esk_free(a, res, res_len + 1)):
 *   - text that is not a JSON object: a -32700 parse error with id null
 *     (SPEC-008 §3), without touching the session;
 *   - method "session.close": the session is called, *close is set, and a
 *     plain {} result is returned when the session has no better answer;
 *   - otherwise the session's own response, or a synthesised error when
 *     es_session_rpc itself fails (-32000 + status, i.e. from -32000 down, es_strerror).
 * A JSON-RPC notification (no id) gets an empty response (*res_len == 0,
 * *res a NUL-terminated empty string). */
es_status esk_protocol_dispatch(es_session *s, esk_alloc *a, const char *req, size_t len, char **res,
                                size_t *res_len, int *close);

/* Format a notification line/frame {"jsonrpc":"2.0","method":...,"params":...}
 * (free with esk_free(a, out, out_len + 1)). `params_json` must be JSON text;
 * NULL or empty becomes {}. */
es_status esk_protocol_format_notification(esk_alloc *a, const char *method, const char *params_json,
                                           size_t params_len, char **out, size_t *out_len);

#endif
