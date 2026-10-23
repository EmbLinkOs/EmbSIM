/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* web_server.c -- a web server on the internet beyond the bench's routers
 * (engine/inet.h; docs/engineering/c3-radios.md, step 4), as a board's
 * HTTP client finds it: by its DNS `name` at its public `address`, `latency`
 * ms away from the internet's core.
 *
 *   - TCP as a server (RFC 9293) on `port`: a SYN answered by SYN-ACK with
 *     its MSS (1460) and window (65535); the client's MSS and window
 *     followed; the request taken in order and acknowledged, anything out
 *     of order answered with the expected sequence number; the response in
 *     segments of the MSS as the window allows; then FIN; the client's FIN
 *     acknowledged; what is unacknowledged sent again after 1 s, doubling,
 *     five times, then the connection given up; TIME-WAIT for 2 s; a
 *     segment for no connection (another port, an old one) answered by RST;
 *   - HTTP/1.0 and 1.1 (RFC 9112): GET and HEAD of `path` (and of its
 *     index.html when it is a directory, as a server's directory index)
 *     answered 200 OK with `content_type` and `body` (Content-Length,
 *     Connection: close: the server closes after each response), another
 *     path 404 Not Found, another method 501 Not Implemented;
 *   - an ICMP echo request answered (RFC 792);
 *   - its outputs: the requests served and a log of them.
 *
 * Where it differs inside: one machine answers at once, with no time to
 * serve a request. Not yet: HTTPS (TLS), persistent connections and
 * pipelining, chunked uploads, more than 8 connections at a time, UDP
 * (answered by nothing, not by ICMP port unreachable), IPv6. */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"

#define MAX_CONN   8
#define REQ_CAP    2048
#define RESP_CAP   16384
#define BODY_CAP   12288
#define LOG_CAP    4096
#define OUR_MSS    1460u
#define OUR_WND    65535u
#define RTO_PS     (1000u * ES_PS_PER_MS)
#define TIMEWAIT_PS (2000u * ES_PS_PER_MS)
#define TRIES      5

enum { F_FIN = 1, F_SYN = 2, F_RST = 4, F_PSH = 8, F_ACK = 16 };
enum { C_FREE, C_SYN_RCVD, C_ESTAB, C_FIN_WAIT, C_TIME_WAIT };

typedef struct ws_conn {
    int state;
    uint8_t ip[4];
    unsigned port;
    uint32_t iss, snd_una, snd_nxt, rcv_nxt;
    uint32_t peer_wnd, peer_mss;
    int peer_fin, responded, fin_sent, tries;
    unsigned gen;
    es_time_t rto;
    uint32_t req_len, resp_len;
    char req[REQ_CAP];
    char resp[RESP_CAP];
} ws_conn;

typedef struct ws_state {
    esk_comp *c;
    esk_inet_node node;
    int attached;
    char name[64], path[128], content_type[96];
    char body[BODY_CAP];
    unsigned port;
    uint32_t next_iss;
    unsigned ip_id;
    uint64_t requests, pings;
    ws_conn conn[MAX_CONN];
    char log[LOG_CAP];
    size_t log_len;
} ws_state;

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v >> 16);
    put16(p + 2, v & 0xFFFFu);
}

static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t get32(const uint8_t *p) { return get16(p) << 16 | get16(p + 2); }

/* a before b in sequence space */
static int seq_lt(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

static void note(ws_state *s, const char *line)
{
    size_t n = strlen(line);
    if (s->log_len + n + 2 >= LOG_CAP)
        return;
    memcpy(s->log + s->log_len, line, n);
    s->log_len += n;
    s->log[s->log_len++] = '\n';
    s->log[s->log_len] = 0;
}

/* a TCP segment to `ip`:`port` */
static void tcp_out(ws_state *s, const uint8_t ip[4], unsigned port, uint32_t seq, uint32_t ack, unsigned flags,
                    const uint8_t *data, uint32_t len)
{
    uint8_t seg[ESK_INET_MTU], p[ESK_INET_MTU];
    uint32_t hl = (flags & F_SYN) ? 24u : 20u;
    if (hl + len + 20u > ESK_INET_MTU)
        return;
    memset(seg, 0, hl);
    put16(seg, s->port);
    put16(seg + 2, port);
    put32(seg + 4, seq);
    put32(seg + 8, ack);
    seg[12] = (uint8_t)((hl / 4u) << 4);
    seg[13] = (uint8_t)flags;
    put16(seg + 14, OUR_WND);
    if (flags & F_SYN) { /* the MSS option */
        seg[20] = 2;
        seg[21] = 4;
        put16(seg + 22, OUR_MSS);
    }
    if (len)
        memcpy(seg + hl, data, len);
    (void)esk_inet_send(s->c->e, &s->node, p,
                        esk_inet_packet(p, s->node.ip, ip, 6, 64, s->ip_id++, seg, hl + len), esk_comp_now(s->c));
}

static void arm(ws_state *s, ws_conn *k, es_time_t after)
{
    k->gen++;
    esk_comp_wake_at(s->c, esk_comp_now(s->c) + after, (uint32_t)(k - s->conn) | (k->gen & 0xFFFFFFu) << 4);
}

/* the response, as much as the window lets out, then FIN */
static void push(ws_state *s, ws_conn *k)
{
    uint32_t base = k->iss + 1u, mss = k->peer_mss < OUR_MSS ? k->peer_mss : OUR_MSS;
    int sent = 0;
    while (k->snd_nxt - base < k->resp_len) {
        uint32_t off = k->snd_nxt - base, n = k->resp_len - off, out = k->snd_nxt - k->snd_una;
        if (out >= k->peer_wnd)
            break;
        if (n > mss)
            n = mss;
        if (n > k->peer_wnd - out)
            n = k->peer_wnd - out;
        tcp_out(s, k->ip, k->port, k->snd_nxt, k->rcv_nxt, F_ACK | (off + n == k->resp_len ? F_PSH : 0u),
                (const uint8_t *)k->resp + off, n);
        k->snd_nxt += n;
        sent = 1;
    }
    if (k->snd_nxt - base == k->resp_len && !k->fin_sent) {
        tcp_out(s, k->ip, k->port, k->snd_nxt, k->rcv_nxt, F_FIN | F_ACK, NULL, 0);
        k->snd_nxt++;
        k->fin_sent = 1;
        k->state = C_FIN_WAIT;
        sent = 1;
    }
    if (sent) {
        k->tries = 0;
        k->rto = RTO_PS;
        arm(s, k, k->rto);
    }
}

/* `target` (origin or absolute form) names the resource at `path`, or its
 * directory index */
static int is_ours(const char *target, const char *path)
{
    size_t n = strlen(path);
    if (!strncmp(target, "http://", 7)) {
        target = strchr(target + 7, '/');
        if (!target)
            target = "/";
    }
    return !strcmp(target, path) ||
           (n && path[n - 1] == '/' && !strncmp(target, path, n) && !strcmp(target + n, "index.html"));
}

/* the request, whole: the response, its status by method and path */
static void respond(ws_state *s, ws_conn *k)
{
    char method[16] = "", target[160] = "", version[16] = "", host[96] = "", line[384];
    const char *h, *reason = "OK", *ct = s->content_type, *b = s->body;
    int status = 200, head;
    size_t blen;
    k->req[k->req_len] = 0;
    if (sscanf(k->req, "%15s %159s %15s", method, target, version) != 3 || strncmp(version, "HTTP/1.", 7)) {
        status = 400;
        reason = "Bad Request";
    }
    h = strstr(k->req, "\r\nHost:");
    if (!h)
        h = strstr(k->req, "\r\nhost:");
    if (h)
        sscanf(h + 7, " %95[^\r\n]", host);
    head = !strcmp(method, "HEAD");
    if (status == 200 && strcmp(method, "GET") && !head) {
        status = 501;
        reason = "Not Implemented";
    } else if (status == 200 && !is_ours(target, s->path)) {
        status = 404;
        reason = "Not Found";
    }
    if (status != 200) {
        ct = "text/plain";
        b = status == 404 ? "Not Found\n" : status == 501 ? "Not Implemented\n" : "Bad Request\n";
    }
    blen = strlen(b);
    k->resp_len = (uint32_t)snprintf(k->resp, RESP_CAP,
                                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nConnection: "
                                     "close\r\n\r\n",
                                     status, reason, ct, (unsigned long)blen);
    if (!head && k->resp_len + blen < RESP_CAP) {
        memcpy(k->resp + k->resp_len, b, blen);
        k->resp_len += (uint32_t)blen;
    }
    k->responded = 1;
    s->requests++;
    snprintf(line, sizeof line, "%s %s %s from %u.%u.%u.%u:%u (Host: %s): %d", method, target, version, k->ip[0],
             k->ip[1], k->ip[2], k->ip[3], k->port, host[0] ? host : "-", status);
    note(s, line);
}

static ws_conn *conn_find(ws_state *s, const uint8_t ip[4], unsigned port)
{
    int i;
    for (i = 0; i < MAX_CONN; i++)
        if (s->conn[i].state != C_FREE && s->conn[i].port == port && !memcmp(s->conn[i].ip, ip, 4))
            return &s->conn[i];
    return NULL;
}

/* the client's MSS option, or 536 (RFC 9293 3.7.1) */
static uint32_t mss_of(const uint8_t *t, uint32_t hl)
{
    uint32_t i = 20;
    while (i < hl) {
        if (t[i] == 0)
            break;
        if (t[i] == 1) {
            i++;
            continue;
        }
        if (i + 1 >= hl || t[i + 1] < 2)
            break;
        if (t[i] == 2 && t[i + 1] == 4 && i + 4 <= hl)
            return get16(t + i + 2);
        i += t[i + 1];
    }
    return 536;
}

static void tcp_in(ws_state *s, const uint8_t *ip, const uint8_t *t, uint32_t len)
{
    uint32_t hl = (uint32_t)(t[12] >> 4) * 4u, seq, ack, dlen;
    unsigned sport, dport, flags;
    ws_conn *k;
    if (len < 20 || hl < 20 || hl > len)
        return;
    sport = get16(t);
    dport = get16(t + 2);
    seq = get32(t + 4);
    ack = get32(t + 8);
    flags = t[13];
    dlen = len - hl;
    k = conn_find(s, ip, sport);
    if (flags & F_RST) {
        if (k)
            k->state = C_FREE;
        return;
    }
    if (!k) {
        int i;
        if (dport != s->port || !(flags & F_SYN) || (flags & F_ACK)) {
            /* no connection: RST (RFC 9293 3.10.7.1) */
            if (flags & F_ACK)
                tcp_out(s, ip, sport, ack, 0, F_RST, NULL, 0);
            else
                tcp_out(s, ip, sport, 0, seq + dlen + ((flags & F_SYN) ? 1u : 0u) + ((flags & F_FIN) ? 1u : 0u),
                        F_RST | F_ACK, NULL, 0);
            return;
        }
        for (i = 0; i < MAX_CONN && s->conn[i].state != C_FREE; i++)
            ;
        if (i == MAX_CONN)
            return; /* the backlog is full: the client tries again */
        k = &s->conn[i];
        memset(k, 0, sizeof *k);
        k->state = C_SYN_RCVD;
        memcpy(k->ip, ip, 4);
        k->port = sport;
        k->iss = s->next_iss;
        s->next_iss += 0x01000193u;
        k->snd_una = k->iss;
        k->snd_nxt = k->iss + 1u;
        k->rcv_nxt = seq + 1u;
        k->peer_mss = mss_of(t, hl);
        k->peer_wnd = get16(t + 14);
        tcp_out(s, ip, sport, k->iss, k->rcv_nxt, F_SYN | F_ACK, NULL, 0);
        k->rto = RTO_PS;
        arm(s, k, k->rto);
        return;
    }
    if (flags & F_SYN) { /* its SYN again: our SYN-ACK was lost */
        if (k->state == C_SYN_RCVD)
            tcp_out(s, ip, sport, k->iss, k->rcv_nxt, F_SYN | F_ACK, NULL, 0);
        return;
    }
    if (flags & F_ACK) {
        if (seq_lt(k->snd_una, ack) && !seq_lt(k->snd_nxt, ack)) {
            k->snd_una = ack;
            k->tries = 0;
            k->rto = RTO_PS;
        }
        if (k->state == C_SYN_RCVD && ack == k->iss + 1u)
            k->state = C_ESTAB;
        k->peer_wnd = get16(t + 14);
    }
    if (k->state == C_SYN_RCVD)
        return;
    if (dlen || (flags & F_FIN)) {
        if (seq == k->rcv_nxt) {
            uint32_t room = REQ_CAP - 1u - k->req_len, take = dlen < room ? dlen : room;
            memcpy(k->req + k->req_len, t + hl, take);
            k->req_len += take;
            k->rcv_nxt += dlen;
            if (flags & F_FIN) {
                k->rcv_nxt++;
                k->peer_fin = 1;
            }
        }
        if (!k->responded) {
            k->req[k->req_len] = 0;
            if (strstr(k->req, "\r\n\r\n") || k->peer_fin || k->req_len >= REQ_CAP - 1u)
                respond(s, k);
        }
        /* acknowledged on its own, or with the response's first segment */
        if (!(k->responded && k->snd_nxt == k->iss + 1u))
            tcp_out(s, ip, sport, k->snd_nxt, k->rcv_nxt, F_ACK, NULL, 0);
    }
    if (k->responded)
        push(s, k);
    if (k->fin_sent && k->snd_una == k->snd_nxt && k->peer_fin && k->state != C_TIME_WAIT) {
        k->state = C_TIME_WAIT;
        arm(s, k, TIMEWAIT_PS);
    }
}

static void icmp_in(ws_state *s, const uint8_t *ip, const uint8_t *m, uint32_t len)
{
    uint8_t r[ESK_INET_MTU], p[ESK_INET_MTU];
    if (len < 8 || m[0] != 8 || len > sizeof r - 20u)
        return;
    memcpy(r, m, len);
    r[0] = 0; /* echo reply */
    s->pings++;
    (void)esk_inet_send(s->c->e, &s->node, p, esk_inet_packet(p, s->node.ip, ip, 1, 64, s->ip_id++, r, len),
                        esk_comp_now(s->c));
}

static void ws_rx(void *ctx, const uint8_t *p, uint32_t len, es_time_t at)
{
    ws_state *s = (ws_state *)ctx;
    uint32_t hl = (uint32_t)(p[0] & 15u) * 4u, tot = get16(p + 2);
    (void)at;
    if (hl < 20 || tot < hl || tot > len || esk_inet_csum(p, hl, 0) != 0)
        return;
    if (p[9] == 6)
        tcp_in(s, p + 12, p + hl, tot - hl);
    else if (p[9] == 1)
        icmp_in(s, p + 12, p + hl, tot - hl);
}

static void ws_time(esk_comp *c, uint32_t token)
{
    ws_state *s = (ws_state *)c->state;
    ws_conn *k = &s->conn[token & 15u];
    if ((token & 15u) >= MAX_CONN || (k->gen & 0xFFFFFFu) != token >> 4 || k->state == C_FREE)
        return;
    if (k->state == C_TIME_WAIT) {
        k->state = C_FREE;
        return;
    }
    if (k->snd_una == k->snd_nxt)
        return; /* all acknowledged */
    if (++k->tries > TRIES) {
        k->state = C_FREE; /* given up */
        tcp_out(s, k->ip, k->port, k->snd_nxt, k->rcv_nxt, F_RST | F_ACK, NULL, 0);
        return;
    }
    if (k->state == C_SYN_RCVD)
        tcp_out(s, k->ip, k->port, k->iss, k->rcv_nxt, F_SYN | F_ACK, NULL, 0);
    else { /* go back to the first unacknowledged byte */
        k->snd_nxt = k->snd_una;
        if (k->fin_sent && k->snd_nxt - (k->iss + 1u) <= k->resp_len)
            k->fin_sent = 0;
        push(s, k);
    }
    k->rto *= 2u;
    arm(s, k, k->rto);
}

static int parse_ip(const char *t, uint8_t ip[4])
{
    unsigned v[4];
    int i;
    if (sscanf(t, "%u.%u.%u.%u", &v[0], &v[1], &v[2], &v[3]) != 4)
        return 0;
    for (i = 0; i < 4; i++) {
        if (v[i] > 255u)
            return 0;
        ip[i] = (uint8_t)v[i];
    }
    return 1;
}

static const char DEFAULT_BODY[] = "<!doctype html>\n<html>\n<head><title>Example Domain</title></head>\n"
                                   "<body>\n<h1>Example Domain</h1>\n<p>This domain is for use in documentation "
                                   "examples.</p>\n</body>\n</html>\n";

static es_status ws_create(esk_comp *c)
{
    ws_state *s = (ws_state *)c->state;
    double latency = 10, port = 80;
    const char *body;
    s->c = c;
    snprintf(s->name, sizeof s->name, "%s", esk_comp_param_str(c, "name", "example.com"));
    snprintf(s->path, sizeof s->path, "%s", esk_comp_param_str(c, "path", "/"));
    snprintf(s->content_type, sizeof s->content_type, "%s",
             esk_comp_param_str(c, "content_type", "text/html; charset=UTF-8"));
    body = esk_comp_param_str(c, "body", DEFAULT_BODY);
    if (strlen(body) >= sizeof s->body) {
        esk_comp_diag(c, ES_SEV_ERROR, "web_server.body", "the body is at most %d bytes", (int)sizeof s->body - 1);
        return ES_ESCHEMA;
    }
    snprintf(s->body, sizeof s->body, "%s", body);
    esk_comp_param_double(c, "latency", &latency);
    esk_comp_param_double(c, "port", &port);
    if (!parse_ip(esk_comp_param_str(c, "address", "93.184.215.14"), s->node.ip) || latency < 0 || latency > 2000 ||
        port < 1 || port > 65535) {
        esk_comp_diag(c, ES_SEV_ERROR, "web_server.params",
                      "an IPv4 address, a latency of 0 to 2000 ms and a port of 1 to 65535");
        return ES_ESCHEMA;
    }
    s->port = (unsigned)port;
    s->node.ctx = s;
    s->node.name = s->name[0] ? s->name : NULL;
    s->node.latency = (es_time_t)(latency * (double)ES_PS_PER_MS);
    s->node.rx = ws_rx;
    if (esk_inet_attach(c->e, &s->node) != ES_OK)
        return ES_ENOMEM;
    s->attached = 1;
    return ES_OK;
}

static void ws_destroy(esk_comp *c)
{
    ws_state *s = (ws_state *)c->state;
    if (s && s->attached)
        esk_inet_detach(c->e, &s->node);
    if (s)
        s->attached = 0;
}

static void ws_reset(esk_comp *c)
{
    ws_state *s = (ws_state *)c->state;
    memset(s->conn, 0, sizeof s->conn);
    s->requests = s->pings = 0;
    s->log[0] = 0;
    s->log_len = 0;
    s->ip_id = 1;
    s->next_iss = 0x5A3C9E01u; /* the same every run, so that a run replays */
}

static void ws_observe(esk_comp *c, esk_json *out)
{
    ws_state *s = (ws_state *)c->state;
    int i, open = 0;
    for (i = 0; i < MAX_CONN; i++)
        open += s->conn[i].state != C_FREE;
    esk_json_set_integer(c->e->a, out, "requests", (int64_t)s->requests);
    esk_json_set_integer(c->e->a, out, "connections", open);
    esk_json_set_integer(c->e->a, out, "pings", (int64_t)s->pings);
    esk_json_set_string(c->e->a, out, "log", s->log);
}

const esk_model_ops esk_web_server_ops = { "web_server", sizeof(ws_state), ws_create, ws_destroy, ws_reset, NULL,
                                           ws_time, NULL, ws_observe, NULL, 0 };
