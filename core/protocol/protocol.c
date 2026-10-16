/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* protocol.c -- the shared state and steps of the protocol endpoints
 * (protocol.h): the idle hook, the listening port, request dispatch with
 * the transport-level errors of SPEC-008 §3, and notification formatting. */
#include "protocol/protocol.h"
#include <string.h>
#include "util/json.h"
#include "util/str.h"

/* The one static state struct of this module (SPEC-001 §10): set by the
 * embedder before serving, read by the endpoints between requests. */
static struct {
    es_serve_idle_fn idle_fn;
    void *idle_ctx;
    int listening_port;
} g_protocol;

void esk_protocol_set_idle(es_serve_idle_fn fn, void *ctx)
{
    g_protocol.idle_fn = fn;
    g_protocol.idle_ctx = ctx;
}

void esk_protocol_idle(void)
{
    if (g_protocol.idle_fn)
        g_protocol.idle_fn(g_protocol.idle_ctx);
}

int esk_protocol_listening_port(void) { return g_protocol.listening_port; }
void esk_protocol_set_listening_port(int port) { g_protocol.listening_port = port; }

/* ---- dispatch ------------------------------------------------------------ */

/* {"jsonrpc":"2.0","id":<id or null>,"error":{"code":..,"message":..}} */
static es_status error_response(esk_alloc *a, const esk_json *id, int code, const char *message, char **res,
                                size_t *res_len)
{
    esk_strbuf b;
    esk_strbuf_init(&b, a);
    esk_strbuf_puts(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
    if (id)
        esk_json_write(&b, id, 0);
    else
        esk_strbuf_puts(&b, "null");
    esk_strbuf_printf(&b, ",\"error\":{\"code\":%d,\"message\":", code);
    esk_json_write_string(&b, message, strlen(message));
    esk_strbuf_puts(&b, "}}");
    if (b.oom) {
        esk_strbuf_free(&b);
        return ES_ENOMEM;
    }
    *res = esk_strbuf_detach(&b, res_len);
    return *res ? ES_OK : ES_ENOMEM;
}

static es_status empty_result(esk_alloc *a, const esk_json *id, char **res, size_t *res_len)
{
    esk_strbuf b;
    esk_strbuf_init(&b, a);
    esk_strbuf_puts(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
    if (id)
        esk_json_write(&b, id, 0);
    else
        esk_strbuf_puts(&b, "null");
    esk_strbuf_puts(&b, ",\"result\":{}}");
    if (b.oom) {
        esk_strbuf_free(&b);
        return ES_ENOMEM;
    }
    *res = esk_strbuf_detach(&b, res_len);
    return *res ? ES_OK : ES_ENOMEM;
}

static int response_is_error(esk_alloc *a, const char *res, size_t len)
{
    int line;
    const char *err;
    esk_json *j = esk_json_parse(a, res, len, &line, &err);
    int is_err = !j || esk_json_get(j, "error") != NULL;
    esk_json_free(a, j);
    return is_err;
}

es_status esk_protocol_dispatch(es_session *s, esk_alloc *a, const char *req, size_t len, char **res,
                                size_t *res_len, int *close)
{
    esk_json *j;
    const esk_json *id;
    const char *method;
    int line = 0, is_close, is_notification;
    const char *err = NULL;
    char *out = NULL;
    size_t out_len = 0;
    es_status st;

    if (!s || !a || !req || !res || !res_len || !close)
        return ES_EINVAL;
    *close = 0;
    j = esk_json_parse(a, req, len, &line, &err);
    if (!j)
        return error_response(a, NULL, -32700, "Parse error", res, res_len);
    if (j->type != ESK_JSON_OBJECT) {
        esk_json_free(a, j);
        return error_response(a, NULL, -32600, "Invalid Request", res, res_len);
    }
    id = esk_json_get(j, "id");
    method = esk_json_get_str(j, "method", NULL);
    is_close = method && !strcmp(method, "session.close");
    is_notification = id == NULL;

    st = es_session_rpc(s, req, len, &out, &out_len);
    if (is_notification) {
        if (out)
            es_free(out);
        esk_json_free(a, j);
        *res = esk_strdup(a, "");
        *res_len = 0;
        return *res ? ES_OK : ES_ENOMEM;
    }
    if (st != ES_OK || !out) {
        int code = st != ES_OK ? -32000 + st : -32603; /* statuses are negative: -32000 down */
        const char *msg = st != ES_OK ? es_strerror(st) : "Internal error";
        if (out)
            es_free(out);
        if (is_close) {
            *close = 1;
            st = empty_result(a, id, res, res_len);
        } else {
            st = error_response(a, id, code, msg ? msg : "error", res, res_len);
        }
        esk_json_free(a, j);
        return st;
    }
    if (is_close) {
        *close = 1;
        if (response_is_error(a, out, out_len)) {
            /* the transport owns session.close: a session without the
             * method still gets a clean close */
            es_free(out);
            st = empty_result(a, id, res, res_len);
            esk_json_free(a, j);
            return st;
        }
    }
    *res = esk_strndup(a, out, out_len);
    *res_len = out_len;
    es_free(out);
    esk_json_free(a, j);
    return *res ? ES_OK : ES_ENOMEM;
}

es_status esk_protocol_format_notification(esk_alloc *a, const char *method, const char *params_json,
                                           size_t params_len, char **out, size_t *out_len)
{
    esk_strbuf b;
    if (!a || !method || !out || !out_len)
        return ES_EINVAL;
    esk_strbuf_init(&b, a);
    esk_strbuf_puts(&b, "{\"jsonrpc\":\"2.0\",\"method\":");
    esk_json_write_string(&b, method, strlen(method));
    esk_strbuf_puts(&b, ",\"params\":");
    if (params_json && params_len)
        esk_strbuf_append(&b, params_json, params_len);
    else
        esk_strbuf_puts(&b, "{}");
    esk_strbuf_putc(&b, '}');
    if (b.oom) {
        esk_strbuf_free(&b);
        return ES_ENOMEM;
    }
    *out = esk_strbuf_detach(&b, out_len);
    return *out ? ES_OK : ES_ENOMEM;
}
