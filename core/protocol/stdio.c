/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* stdio.c -- the stdio transport of SPEC-008 §1 (`embsim sidecar`): one
 * JSON-RPC object per line on stdin, one response per line on stdout,
 * notifications interleaved as lines of their own. The host layer does the
 * actual reading and writing (CS-4, CS-5). */
#include <string.h>
#include "host/host.h"
#include "protocol/protocol.h"
#include "util/str.h"

typedef struct stdio_endpoint {
    esk_alloc *a;
    es_status io_status; /* the first write failure, if any */
} stdio_endpoint;

static void write_line(stdio_endpoint *ep, const char *text, size_t len)
{
    es_status st;
    if (ep->io_status != ES_OK)
        return;
    st = esk_host_stdout_write(text, len);
    if (st == ES_OK)
        st = esk_host_stdout_write("\n", 1);
    if (st != ES_OK)
        ep->io_status = st;
}

/* The session calls this (from inside es_session_rpc or the idle hook);
 * the notification goes out before the pending response (SPEC-008 §7). */
static void on_notification(void *ctx, const char *method, const char *params_json, size_t len)
{
    stdio_endpoint *ep = (stdio_endpoint *)ctx;
    char *text;
    size_t text_len;
    if (esk_protocol_format_notification(ep->a, method, params_json, len, &text, &text_len) != ES_OK)
        return;
    write_line(ep, text, text_len);
    esk_free(ep->a, text, text_len + 1);
}

static int line_is_blank(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r')
            return 0;
    return 1;
}

es_status es_serve_stdio(es_session *s)
{
    esk_alloc alloc;
    stdio_endpoint ep;
    esk_strbuf line;
    es_status st = ES_OK;
    if (!s)
        return ES_EINVAL;
    esk_alloc_init(&alloc, NULL);
    ep.a = &alloc;
    ep.io_status = ES_OK;
    esk_strbuf_init(&line, &alloc);
    esk_host_stdio_init();
    st = es_session_set_notification_handler(s, on_notification, &ep);
    if (st != ES_OK)
        return st;
    for (;;) {
        char *res = NULL;
        size_t res_len = 0;
        int r, close = 0;
        /* poll so a run loop can advance while the client is quiet */
        r = esk_host_stdin_ready(ESK_PROTOCOL_POLL_MS);
        if (r == 0) {
            esk_protocol_idle();
            continue;
        }
        r = esk_host_stdin_readline(&line);
        if (r < 0) {
            st = ES_EIO;
            break;
        }
        if (r == 0)
            break; /* end of input */
        if (line_is_blank(line.data ? line.data : "", line.len))
            continue;
        st = esk_protocol_dispatch(s, &alloc, line.data, line.len, &res, &res_len, &close);
        if (st != ES_OK)
            break;
        if (res_len)
            write_line(&ep, res, res_len);
        esk_free(&alloc, res, res_len + 1);
        if (ep.io_status != ES_OK) {
            st = ep.io_status;
            break;
        }
        if (close)
            break;
    }
    es_session_set_notification_handler(s, NULL, NULL);
    esk_strbuf_free(&line);
    return st;
}
