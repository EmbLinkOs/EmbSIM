/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ws.c -- the WebSocket transport of SPEC-008 §1 (`embsim serve`): a
 * dependency-free HTTP/1.1 server on the loopback interface that serves the
 * workbench assets from a directory (or a built-in console page) and
 * upgrades /ws to a WebSocket (RFC 6455) carrying one JSON-RPC object per
 * text frame. Single-threaded: one session client at a time, with static
 * HTTP requests served between session polls; successive session clients
 * accepted until Ctrl-C or session.close; the transport is polled with a
 * short timeout and the idle hook of protocol.h runs between requests. */
#include <string.h>
#include "host/host.h"
#include "protocol/protocol.h"
#include "protocol/wsutil.h"
#include "session/session_priv.h"
#include "util/str.h"

#define WS_HTTP_HEAD_MAX 16384      /* bytes of request head accepted */
#define WS_HTTP_HEAD_TIMEOUT_MS 10000
#define WS_AUX_HTTP_HEAD_TIMEOUT_MS 250
#define WS_MESSAGE_MAX (16u << 20)  /* bytes per message (CS-17) */
#define WS_SEND_TIMEOUT_MS 5000
#define WS_RECV_CHUNK 4096

typedef struct ws_server {
    esk_alloc *a;
    es_session *s;
    char host[64];          /* the bound host, as given */
    int port;
    const char *ui_dir;     /* NULL: built-in page */
    int ui_has_index;
    esk_host_sock listener; /* static requests can arrive during the active session */
    int http_only;         /* auxiliary connections cannot attach another session */
    esk_host_sock client;   /* ESK_HOST_SOCK_INVALID when none */
    int client_is_ws;       /* the client finished the upgrade */
    int send_failed;        /* the client stopped taking frames */
    int close_requested;    /* session.close was served: stop after this client */
} ws_server;

/* ---- small string helpers ------------------------------------------------ */

static int ascii_lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

static int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

/* a comma-separated token list contains `token` (case-insensitive) */
static int token_list_has(const char *list, const char *token)
{
    size_t tlen = strlen(token);
    while (*list) {
        const char *start, *end;
        while (*list == ' ' || *list == '\t' || *list == ',')
            list++;
        start = list;
        while (*list && *list != ',')
            list++;
        end = list;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
            end--;
        if ((size_t)(end - start) == tlen) {
            size_t i;
            for (i = 0; i < tlen; i++)
                if (ascii_lower((unsigned char)start[i]) != ascii_lower((unsigned char)token[i]))
                    break;
            if (i == tlen)
                return 1;
        }
    }
    return 0;
}

/* ---- sending ------------------------------------------------------------- */

static void send_raw(ws_server *srv, const void *data, size_t len)
{
    if (srv->send_failed || srv->client == ESK_HOST_SOCK_INVALID)
        return;
    if (esk_host_sock_send(srv->client, data, len, WS_SEND_TIMEOUT_MS) < 0)
        srv->send_failed = 1;
}

static void send_frame(ws_server *srv, int opcode, const void *payload, size_t len)
{
    unsigned char hdr[ESK_WS_HEADER_MAX];
    size_t n = esk_ws_encode_header(hdr, 1, opcode, (uint64_t)len);
    send_raw(srv, hdr, n);
    if (len)
        send_raw(srv, payload, len);
}

static void send_close(ws_server *srv, unsigned status)
{
    unsigned char body[2];
    body[0] = (unsigned char)(status >> 8);
    body[1] = (unsigned char)status;
    send_frame(srv, ESK_WS_OP_CLOSE, body, 2);
}

/* The session calls this from inside es_session_rpc or the idle hook. */
static void on_notification(void *ctx, const char *method, const char *params_json, size_t len)
{
    ws_server *srv = (ws_server *)ctx;
    char *text;
    size_t text_len;
    if (!srv->client_is_ws || srv->client == ESK_HOST_SOCK_INVALID)
        return;
    if (esk_protocol_format_notification(srv->a, method, params_json, len, &text, &text_len) != ES_OK)
        return;
    send_frame(srv, ESK_WS_OP_TEXT, text, text_len);
    esk_free(srv->a, text, text_len + 1);
}

/* ---- HTTP ---------------------------------------------------------------- */

typedef struct http_request {
    const char *method, *target, *version;
    const char *host, *origin, *upgrade, *connection, *ws_key, *ws_version, *content_length;
} http_request;

static void send_http(ws_server *srv, int status, const char *reason, const char *content_type, const char *body,
                      size_t body_len)
{
    esk_strbuf b;
    esk_strbuf_init(&b, srv->a);
    esk_strbuf_printf(&b, "HTTP/1.1 %d %s\r\n", status, reason);
    esk_strbuf_printf(&b, "Content-Type: %s\r\n", content_type);
    esk_strbuf_printf(&b, "Content-Length: %lu\r\n", (unsigned long)body_len);
    esk_strbuf_puts(&b, "Cache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n");
    if (!b.oom) {
        send_raw(srv, b.data, b.len);
        if (body_len)
            send_raw(srv, body, body_len);
    }
    esk_strbuf_free(&b);
}

static void send_http_text(ws_server *srv, int status, const char *reason, const char *text)
{
    send_http(srv, status, reason, "text/plain; charset=utf-8", text, strlen(text));
}

/* Read the request head into `head` (up to the blank line); extra bytes
 * after it stay in `head` past *head_len. Returns 0, or -1 on timeout,
 * oversize or disconnect. The idle hook runs while waiting. */
static int read_head(ws_server *srv, esk_strbuf *head, size_t *head_len)
{
    uint64_t deadline = esk_host_monotonic_ms() +
        (srv->http_only ? WS_AUX_HTTP_HEAD_TIMEOUT_MS : WS_HTTP_HEAD_TIMEOUT_MS);
    for (;;) {
        char chunk[WS_RECV_CHUNK];
        long n;
        const char *end = head->data ? strstr(head->data, "\r\n\r\n") : NULL;
        if (end) {
            *head_len = (size_t)(end - head->data) + 4;
            return 0;
        }
        if (head->len >= WS_HTTP_HEAD_MAX || esk_host_monotonic_ms() > deadline)
            return -1;
        n = esk_host_sock_recv(srv->client, chunk, sizeof chunk, ESK_PROTOCOL_POLL_MS);
        if (n == 0) {
            esk_protocol_idle();
            if (esk_host_interrupted() || esk_host_monotonic_ms() > deadline)
                return -1;
            continue;
        }
        if (n < 0)
            return -1;
        if (memchr(chunk, 0, (size_t)n))
            return -1; /* a NUL in a request head: not HTTP */
        if (esk_strbuf_append(head, chunk, (size_t)n))
            return -1;
    }
}

/* Parse the head in place (NUL-terminating lines). Returns 0 or -1. */
static int parse_head(char *head, size_t head_len, http_request *req)
{
    char *line, *next, *sp;
    memset(req, 0, sizeof *req);
    head[head_len - 2] = 0; /* keep one "\r\n" as the terminator of the last line */
    line = head;
    /* request line */
    next = strstr(line, "\r\n");
    if (!next)
        return -1;
    *next = 0;
    next += 2;
    req->method = line;
    sp = strchr(line, ' ');
    if (!sp)
        return -1;
    *sp++ = 0;
    req->target = sp;
    sp = strchr(sp, ' ');
    if (!sp)
        return -1;
    *sp++ = 0;
    req->version = sp;
    if (strncmp(req->version, "HTTP/1.", 7) != 0)
        return -1;
    /* headers */
    for (line = next; *line; line = next) {
        char *colon, *value;
        next = strstr(line, "\r\n");
        if (!next)
            break;
        *next = 0;
        next += 2;
        colon = strchr(line, ':');
        if (!colon)
            return -1;
        *colon = 0;
        value = colon + 1;
        while (*value == ' ' || *value == '\t')
            value++;
        if (str_ieq(line, "host")) req->host = value;
        else if (str_ieq(line, "origin")) req->origin = value;
        else if (str_ieq(line, "upgrade")) req->upgrade = value;
        else if (str_ieq(line, "connection")) req->connection = value;
        else if (str_ieq(line, "sec-websocket-key")) req->ws_key = value;
        else if (str_ieq(line, "sec-websocket-version")) req->ws_version = value;
        else if (str_ieq(line, "content-length")) req->content_length = value;
    }
    return 0;
}

/* "name[:port]" names this server: a loopback name and our port. */
static int authority_is_us(const ws_server *srv, const char *authority)
{
    char name[80];
    const char *colon;
    size_t n;
    long port = 0;
    if (!authority)
        return 0;
    colon = strrchr(authority, ':');
    if (colon) {
        const char *p = colon + 1;
        if (!*p)
            return 0;
        while (*p) {
            if (*p < '0' || *p > '9' || port > 65535)
                return 0;
            port = port * 10 + (*p - '0');
            p++;
        }
        n = (size_t)(colon - authority);
    } else {
        port = 80;
        n = strlen(authority);
    }
    if (n == 0 || n >= sizeof name)
        return 0;
    memcpy(name, authority, n);
    name[n] = 0;
    if (port != srv->port)
        return 0;
    return str_ieq(name, "localhost") || !strcmp(name, "127.0.0.1") || !strcmp(name, srv->host);
}

static int origin_is_us(const ws_server *srv, const char *origin)
{
    int i;
    if (!origin)
        return 1; /* non-browser clients send none */
    if (strncmp(origin, "http://", 7) == 0 && authority_is_us(srv, origin + 7))
        return 1;
    /* the origins the host allowed (the desktop shell's webview) */
    for (i = 0; i < srv->s->n_allow_origin; i++)
        if (!strcmp(origin, srv->s->allow_origin[i]))
            return 1;
    return 0;
}

/* ---- static files -------------------------------------------------------- */

static const char *mime_for(const char *path)
{
    static const struct {
        const char *ext, *mime;
    } table[] = {
        {".html", "text/html; charset=utf-8"},   {".htm", "text/html; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"}, {".mjs", "text/javascript; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},     {".json", "application/json; charset=utf-8"},
        {".map", "application/json; charset=utf-8"}, {".svg", "image/svg+xml"},
        {".png", "image/png"},                   {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},                 {".gif", "image/gif"},
        {".ico", "image/x-icon"},                {".webp", "image/webp"},
        {".wasm", "application/wasm"},           {".woff", "font/woff"},
        {".woff2", "font/woff2"},                {".ttf", "font/ttf"},
        {".txt", "text/plain; charset=utf-8"},   {".md", "text/markdown; charset=utf-8"},
        {".webmanifest", "application/manifest+json"}, {".lua", "text/x-lua; charset=utf-8"},
    };
    const char *dot = strrchr(path, '.');
    size_t i;
    if (dot)
        for (i = 0; i < ES_COUNTOF(table); i++)
            if (str_ieq(dot, table[i].ext))
                return table[i].mime;
    return "application/octet-stream";
}

/* The dev page used when no workbench assets are available: a JSON console
 * over /ws. A line that is not an object is read as `method [params]`. */
static const char BUILTIN_INDEX[] =
    "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>EmbSIM</title>\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "<style>body{font:14px/1.45 ui-monospace,Menlo,Consolas,monospace;margin:1em;background:#15171a;color:#d8dde3}"
    "h1{font-size:1.2em;margin:0 0 .3em}p{color:#8b949e;margin:.2em 0 .8em}"
    "#log{white-space:pre-wrap;word-break:break-all;border:1px solid #333a42;padding:.5em;height:62vh;overflow:auto;background:#0f1114}"
    "form{display:flex;gap:.5em;margin-top:.6em}input{flex:1;font:inherit;background:#1d2126;color:#e6edf3;border:1px solid #333a42;padding:.35em .5em}"
    "button{font:inherit;background:#2b5fb0;color:#fff;border:0;padding:.35em .9em}"
    ".n{color:#79b8ff}.e{color:#ff7b72}.r{color:#7ee787}.q{color:#8b949e}</style></head>\n"
    "<body><h1>EmbSIM dev console</h1><p>No workbench assets are installed; this page speaks JSON-RPC to <code>/ws</code>. "
    "Type a JSON object, or <code>method params</code> (e.g. <code>control.advance {\"by\":\"10ms\"}</code>).</p>\n"
    "<div id=\"log\"></div><form id=\"f\"><input id=\"m\" autocomplete=\"off\" autofocus placeholder=\"session.info {}\"><button>Send</button></form>\n"
    "<script>\n"
    "var log=document.getElementById('log'),m=document.getElementById('m'),id=1;\n"
    "function put(c,t){var d=document.createElement('div');d.className=c;d.textContent=t;log.appendChild(d);log.scrollTop=log.scrollHeight;}\n"
    "var ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');\n"
    "ws.onopen=function(){put('n','# connected');send(JSON.stringify({jsonrpc:'2.0',id:id++,method:'session.open',params:{client:'devpage/0.1',protocol:'1.0',streams:'base64'}}));};\n"
    "ws.onmessage=function(e){var j;try{j=JSON.parse(e.data);}catch(x){put('e',e.data);return;}put(j.method?'n':(j.error?'e':'r'),e.data);};\n"
    "ws.onclose=function(){put('e','# disconnected');};\n"
    "function send(t){put('q','> '+t);ws.send(t);}\n"
    "document.getElementById('f').onsubmit=function(ev){ev.preventDefault();var t=m.value.trim();if(!t)return;\n"
    "if(t[0]!=='{'){var sp=t.indexOf(' ');var meth=sp<0?t:t.slice(0,sp);var p={};try{if(sp>=0)p=JSON.parse(t.slice(sp+1));}catch(x){put('e','# bad params');return;}\n"
    "t=JSON.stringify({jsonrpc:'2.0',id:id++,method:meth,params:p});}\n"
    "send(t);m.value='';};\n"
    "</script></body></html>\n";

static void serve_static(ws_server *srv, const char *target)
{
    char path[1024];
    size_t n = 0;
    const char *q = strchr(target, '?');
    char *full, *data;
    size_t len;
    es_status st;
    /* the path part only; never anything with ".." or a backslash */
    n = q ? (size_t)(q - target) : strlen(target);
    if (n == 0 || n >= sizeof path || target[0] != '/' || memchr(target, '\\', n) || strstr(target, "..")) {
        send_http_text(srv, 404, "Not Found", "not found\n");
        return;
    }
    memcpy(path, target, n);
    path[n] = 0;
    if (!strcmp(path, "/"))
        esk_snprintf(path, sizeof path, "%s", "/index.html");
    if (!srv->ui_dir || !srv->ui_has_index) {
        if (!strcmp(path, "/index.html"))
            send_http(srv, 200, "OK", "text/html; charset=utf-8", BUILTIN_INDEX, sizeof BUILTIN_INDEX - 1);
        else
            send_http_text(srv, 404, "Not Found", "not found\n");
        return;
    }
    full = esk_host_path_join(srv->a, srv->ui_dir, path + 1);
    if (!full) {
        send_http_text(srv, 500, "Internal Server Error", "out of memory\n");
        return;
    }
    if (esk_host_is_dir(full)) {
        esk_strfree(srv->a, full);
        send_http_text(srv, 404, "Not Found", "not found\n");
        return;
    }
    st = esk_host_read_file(srv->a, full, &data, &len);
    esk_strfree(srv->a, full);
    if (st != ES_OK) {
        send_http_text(srv, 404, "Not Found", "not found\n");
        return;
    }
    send_http(srv, 200, "OK", mime_for(path), data, len);
    esk_free(srv->a, data, len + 1);
}

/* ---- WebSocket ----------------------------------------------------------- */

static void consume(esk_strbuf *b, size_t n)
{
    if (n >= b->len) {
        esk_strbuf_clear(b);
        if (b->data)
            b->data[0] = 0;
        return;
    }
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
    b->data[b->len] = 0;
}

/* One complete text message: a request. Returns 1 when the client must be
 * closed afterwards (session.close). */
static int handle_text(ws_server *srv, const char *text, size_t len)
{
    char *res = NULL;
    size_t res_len = 0;
    int close = 0;
    if (esk_protocol_dispatch(srv->s, srv->a, text, len, &res, &res_len, &close) != ES_OK)
        return 1;
    if (res_len)
        send_frame(srv, ESK_WS_OP_TEXT, res, res_len);
    esk_free(srv->a, res, res_len + 1);
    if (close)
        srv->close_requested = 1;
    return close;
}

static void serve_pending_http(ws_server *srv);

static void ws_loop(ws_server *srv, esk_strbuf *in)
{
    esk_strbuf msg; /* fragments of the message in progress */
    int msg_opcode = -1;
    esk_strbuf_init(&msg, srv->a);
    for (;;) {
        char chunk[WS_RECV_CHUNK];
        long n;
        int done = 0;
        /* every complete frame in the buffer */
        while (!done && in->len >= 2) {
            esk_ws_frame f;
            size_t total;
            unsigned char *payload;
            int r = esk_ws_decode_header((const unsigned char *)in->data, in->len, &f);
            if (r == 1)
                break;
            if (r < 0 || !f.masked) { /* clients must mask (RFC 6455 §5.1) */
                send_close(srv, 1002);
                done = 1;
                break;
            }
            if (f.payload_len > WS_MESSAGE_MAX) {
                send_close(srv, 1009);
                done = 1;
                break;
            }
            total = f.header_len + (size_t)f.payload_len;
            if (in->len < total)
                break;
            payload = (unsigned char *)in->data + f.header_len;
            esk_ws_unmask(&f, payload, (size_t)f.payload_len);
            switch (f.opcode) {
            case ESK_WS_OP_TEXT:
            case ESK_WS_OP_BINARY:
                if (msg_opcode != -1) {
                    send_close(srv, 1002); /* a new message inside a fragmented one */
                    done = 1;
                } else if (f.fin) {
                    if (f.opcode == ESK_WS_OP_TEXT && handle_text(srv, (const char *)payload, (size_t)f.payload_len))
                        done = 1;
                    /* binary frames (sample streams) are ignored in M0 */
                } else {
                    msg_opcode = f.opcode;
                    esk_strbuf_clear(&msg);
                    esk_strbuf_append(&msg, (const char *)payload, (size_t)f.payload_len);
                }
                break;
            case ESK_WS_OP_CONTINUATION:
                if (msg_opcode == -1) {
                    send_close(srv, 1002);
                    done = 1;
                    break;
                }
                esk_strbuf_append(&msg, (const char *)payload, (size_t)f.payload_len);
                if (msg.oom || msg.len > WS_MESSAGE_MAX) {
                    send_close(srv, 1009);
                    done = 1;
                    break;
                }
                if (f.fin) {
                    int op = msg_opcode;
                    msg_opcode = -1;
                    if (op == ESK_WS_OP_TEXT && handle_text(srv, msg.data, msg.len))
                        done = 1;
                    esk_strbuf_clear(&msg);
                }
                break;
            case ESK_WS_OP_CLOSE: {
                unsigned status = 1000;
                if (f.payload_len >= 2)
                    status = ((unsigned)payload[0] << 8) | payload[1];
                send_close(srv, status >= 1000 && status < 5000 ? status : 1000);
                done = 1;
                break;
            }
            case ESK_WS_OP_PING:
                send_frame(srv, ESK_WS_OP_PONG, payload, (size_t)f.payload_len);
                break;
            default: /* pong */
                break;
            }
            consume(in, total);
            if (srv->close_requested && !done) {
                send_close(srv, 1001);
                done = 1;
            }
        }
        if (done || srv->send_failed)
            break;
        if (esk_host_interrupted()) {
            send_close(srv, 1001);
            break;
        }
        serve_pending_http(srv);
        n = esk_host_sock_recv(srv->client, chunk, sizeof chunk, ESK_PROTOCOL_POLL_MS);
        if (n == 0) {
            esk_protocol_idle();
            continue;
        }
        if (n < 0)
            break; /* the client went away */
        if (esk_strbuf_append(in, chunk, (size_t)n))
            break;
    }
    esk_strbuf_free(&msg);
}

static void upgrade(ws_server *srv, const http_request *req, esk_strbuf *in)
{
    char accept[ESK_WS_ACCEPT_LEN + 1];
    esk_strbuf b;
    if (!req->ws_key || !req->ws_version || strcmp(req->ws_version, "13") != 0 || !req->connection ||
        !token_list_has(req->connection, "upgrade") || esk_ws_accept_key(req->ws_key, strlen(req->ws_key), accept)) {
        send_http_text(srv, 400, "Bad Request", "bad websocket handshake\n");
        return;
    }
    esk_strbuf_init(&b, srv->a);
    esk_strbuf_printf(&b, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                          "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    if (b.oom) {
        esk_strbuf_free(&b);
        return;
    }
    send_raw(srv, b.data, b.len);
    esk_strbuf_free(&b);
    if (srv->send_failed)
        return;
    srv->client_is_ws = 1;
    ws_loop(srv, in);
    srv->client_is_ws = 0;
}

/* One accepted connection: a request head, then a static response or the
 * WebSocket session. The socket is closed by the caller. */
static void serve_client(ws_server *srv)
{
    esk_strbuf in;
    size_t head_len = 0;
    http_request req;
    esk_strbuf_init(&in, srv->a);
    if (read_head(srv, &in, &head_len) != 0) {
        if (in.len)
            send_http_text(srv, 400, "Bad Request", "bad request\n");
        esk_strbuf_free(&in);
        return;
    }
    if (parse_head(in.data, head_len, &req) != 0) {
        send_http_text(srv, 400, "Bad Request", "bad request\n");
        esk_strbuf_free(&in);
        return;
    }
    if (!authority_is_us(srv, req.host)) {
        send_http_text(srv, 403, "Forbidden", "this server only answers loopback requests\n");
    } else if (!strcmp(req.target, "/ws") && req.upgrade && str_ieq(req.upgrade, "websocket")) {
        if (!origin_is_us(srv, req.origin)) {
            send_http_text(srv, 403, "Forbidden", "origin not allowed\n");
        } else if (strcmp(req.method, "GET") != 0) {
            send_http_text(srv, 405, "Method Not Allowed", "method not allowed\n");
        } else if (srv->http_only) {
            send_http_text(srv, 409, "Conflict", "a session client is already attached\n");
        } else {
            /* bytes after the head belong to the WebSocket stream */
            consume(&in, head_len);
            upgrade(srv, &req, &in);
        }
    } else if (!strcmp(req.method, "GET") || !strcmp(req.method, "HEAD")) {
        if (req.content_length && strcmp(req.content_length, "0") != 0)
            send_http_text(srv, 400, "Bad Request", "unexpected body\n");
        else
            serve_static(srv, req.target);
    } else {
        send_http_text(srv, 405, "Method Not Allowed", "method not allowed\n");
    }
    esk_strbuf_free(&in);
}

/* A reload requests its document before releasing the old WebSocket. Serve
 * one pending static request per poll, without replacing the session client or
 * notification handler. Auxiliary request heads have a short bounded wait. */
static void serve_pending_http(ws_server *srv)
{
    ws_server http = {0};
    esk_host_sock client = ESK_HOST_SOCK_INVALID;
    if (esk_host_sock_wait(srv->listener, 0) != 1)
        return;
    if (esk_host_sock_accept(srv->listener, &client) != ES_OK)
        return;
    http = *srv;
    http.client = client;
    http.client_is_ws = 0;
    http.http_only = 1;
    http.send_failed = 0;
    serve_client(&http);
    esk_host_sock_close(client);
}

/* ---- entry --------------------------------------------------------------- */

es_status es_serve_websocket(es_session *s, const char *host, int port, const char *ui_dir, int open_browser)
{
    esk_alloc alloc;
    ws_server srv;
    esk_host_sock listener = ESK_HOST_SOCK_INVALID;
    es_status st;
    char url[128], line[192];
    size_t url_len, line_len;

    if (!s || port < 0 || port > 65535)
        return ES_EINVAL;
    if (!host || !*host)
        host = "127.0.0.1";
    memset(&srv, 0, sizeof srv);
    esk_alloc_init(&alloc, NULL);
    srv.a = &alloc;
    srv.s = s;
    srv.ui_dir = ui_dir;
    srv.client = ESK_HOST_SOCK_INVALID;
    esk_snprintf(srv.host, sizeof srv.host, "%s", host);
    if (ui_dir) {
        char *index = esk_host_path_join(&alloc, ui_dir, "index.html");
        srv.ui_has_index = index && esk_host_file_exists(index);
        esk_strfree(&alloc, index);
    }

    st = esk_host_sock_init();
    if (st != ES_OK)
        return st;
    st = esk_host_sock_listen(host, port, &listener);
    if (st != ES_OK) {
        esk_host_sock_shutdown();
        return st;
    }
    srv.listener = listener;
    st = esk_host_sock_local_port(listener, &srv.port);
    if (st != ES_OK) {
        esk_host_sock_close(listener);
        esk_host_sock_shutdown();
        return st;
    }
    st = es_session_set_notification_handler(s, on_notification, &srv);
    if (st != ES_OK) {
        esk_host_sock_close(listener);
        esk_host_sock_shutdown();
        return st;
    }
    esk_protocol_set_listening_port(srv.port);
    esk_host_interrupt_install();

    /* announce the URL: the notification path (for an attached client, if
     * the embedder forwards it), the port accessor, and one line on stdout */
    url_len = esk_snprintf(url, sizeof url, "http://%s:%d/", srv.host, srv.port);
    {
        char params[160];
        size_t plen = esk_snprintf(params, sizeof params, "{\"url\":\"%s\",\"port\":%d}", url, srv.port);
        on_notification(&srv, "server.listening", params, plen);
    }
    line_len = esk_snprintf(line, sizeof line, "EmbSIM listening on %s (Ctrl-C stops)\n", url);
    esk_host_stdout_write(line, line_len);
    ES_UNUSED(url_len);
    if (open_browser)
        esk_host_open_browser(url); /* best effort */

    st = ES_OK;
    while (!esk_host_interrupted() && !srv.close_requested) {
        int w = esk_host_sock_wait(listener, ESK_PROTOCOL_POLL_MS);
        if (w == 0) {
            esk_protocol_idle();
            continue;
        }
        if (w < 0) {
            st = ES_EIO;
            break;
        }
        if (esk_host_sock_accept(listener, &srv.client) != ES_OK) {
            srv.client = ESK_HOST_SOCK_INVALID;
            continue; /* the peer vanished between poll and accept */
        }
        srv.send_failed = 0;
        serve_client(&srv);
        esk_host_sock_close(srv.client);
        srv.client = ESK_HOST_SOCK_INVALID;
    }

    es_session_set_notification_handler(s, NULL, NULL);
    esk_protocol_set_listening_port(0);
    esk_host_sock_close(listener);
    esk_host_sock_shutdown();
    return st;
}
