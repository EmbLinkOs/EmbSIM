/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* stlinksrv.c -- the bench's ST-LINK probes on the host as ST's
 * stlink-server shares them (docs/engineering/debug-probes.md, ADR-031).
 *
 * stlink-server is the service STM32CubeIDE and STM32CubeProgrammer reach a
 * probe through, a TCP server (port 7184) relaying each USB command to the
 * probe it opened; OpenOCD 0.12 speaks its protocol too (`st-link backend
 * tcp`). The requests and their answers are as OpenOCD's
 * src/jtag/drivers/stlink_usb.c sends and reads them (read, not copied:
 * GPL-2.0):
 *
 *   - GET_SERVER_VERSION: API 2, version 2.1.1;
 *   - REFRESH_DEVICE_LIST, GET_NB_DEV, GET_DEV_INFO (its id, its serial
 *     number, 0483:374B, whether a client has it open);
 *   - OPEN_DEV (a connection id), CLOSE_DEV;
 *   - SEND_USB_CMD: the 16-byte command, its direction and size, a write's
 *     data after it; answered by the status and a read's data; direction
 *     3 (READ_SWO) reads the probe's trace endpoint;
 *   - GET_NB_OF_DEV_CLIENTS.
 * Every answer starts with its status (1: OK) but the version's and the
 * count's. Four clients at a time; a probe opened by one is in use to the
 * others. Requests are served as the session polls its host ports, while it
 * runs, one a client at each poll (a run slice, a millisecond or more of the
 * session's time): as the probe's full-speed USB takes a frame for each
 * command, and so a core a command restarted (a step) runs before the next
 * command looks at it. A paused session holds its clients' requests until it
 * runs again, as a halted host would. */
#include "session/session_priv.h"
#include "host/host.h"
#include "model/registry.h"

#include <string.h>

#define MAX_CLIENTS 4
#define BUF_CAP 10304 /* OpenOCD's 10240-byte buffers, and a header */

enum {
    T_REFRESH = 0x00, T_NB_DEV = 0x01, T_DEV_INFO = 0x02, T_OPEN = 0x03, T_CLOSE = 0x04, T_USB_CMD = 0x05,
    T_VERSION = 0x06, T_NB_CLIENTS = 0x07
};
#define SS_OK 0x00000001u
#define SS_BAD_PARAMETER 0x00001002u
#define SS_OPEN_ERR 0x00001003u
#define SS_CMD_NOT_AVAILABLE 0x00001053u

typedef struct sl_client {
    esk_host_sock fd;
    uint8_t in[BUF_CAP];
    size_t len;
    uint32_t connect_id; /* 0: none open */
    esk_comp *dev;
} sl_client;

struct esk_stlink_server {
    esk_host_sock listener;
    int port;
    uint32_t next_id;
    sl_client cl[MAX_CLIENTS];
    uint8_t out[BUF_CAP];
};

static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

/* the session's probes, in their components' order */
static esk_comp *device(es_session *s, uint32_t index)
{
    size_t i;
    uint32_t k = 0;
    for (i = 0; i < s->eng.comps.len; i++)
        if (esk_stlink_is(s->eng.comps.data[i]) && k++ == index)
            return s->eng.comps.data[i];
    return NULL;
}

static uint32_t devices(es_session *s)
{
    size_t i;
    uint32_t k = 0;
    for (i = 0; i < s->eng.comps.len; i++)
        k += esk_stlink_is(s->eng.comps.data[i]) ? 1u : 0u;
    return k;
}

static int in_use(struct esk_stlink_server *v, const esk_comp *dev)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
        if (v->cl[i].fd != ESK_HOST_SOCK_INVALID && v->cl[i].dev == dev)
            return 1;
    return 0;
}

/* the size of the request at the start of `b`, or 0 while it is incomplete */
static size_t request_size(const uint8_t *b, size_t n)
{
    if (!n)
        return 0;
    switch (b[0]) {
    case T_REFRESH: return n >= 2 ? 2 : 0;
    case T_NB_DEV: return 1;
    case T_DEV_INFO: case T_OPEN: case T_CLOSE: case T_NB_CLIENTS: return n >= 8 ? 8 : 0;
    case T_VERSION: return n >= 4 ? 4 : 0;
    case T_USB_CMD: {
        size_t need;
        if (n < 32)
            return 0;
        need = 32 + (b[24] == 0 ? (size_t)le32(b + 28) : 0);
        if (need > BUF_CAP)
            need = BUF_CAP;
        return n >= need ? need : 0;
    }
    }
    return 1; /* unknown: taken as one byte */
}

/* one request answered into v->out: its length */
static size_t serve(es_session *s, struct esk_stlink_server *v, sl_client *c, const uint8_t *b)
{
    uint8_t *o = v->out;
    memset(o, 0, 64);
    switch (b[0]) {
    case T_VERSION:
        put32(o, 2);      /* API v2 */
        put32(o + 4, 2);  /* stlink-server 2.1.1 */
        put32(o + 8, 1);
        put32(o + 12, 1);
        return 16;
    case T_REFRESH:
        put32(o, SS_OK);
        return 4;
    case T_NB_DEV:
        put32(o, devices(s));
        return 4;
    case T_DEV_INFO: {
        esk_comp *d = device(s, b[1]);
        if (!d) {
            put32(o, SS_BAD_PARAMETER);
            return 45;
        }
        put32(o, SS_OK);
        put32(o + 4, (uint32_t)b[1] + 1u);
        strncpy((char *)o + 8, esk_stlink_serial(d), 32);
        o[40] = 0x83;
        o[41] = 0x04;
        o[42] = 0x4B;
        o[43] = 0x37;
        o[44] = (uint8_t)in_use(v, d);
        return 45;
    }
    case T_OPEN: {
        uint32_t id = le32(b + 4);
        esk_comp *d = id ? device(s, id - 1u) : NULL;
        if (!d || in_use(v, d)) {
            put32(o, SS_OPEN_ERR);
            return 8;
        }
        c->dev = d;
        c->connect_id = ++v->next_id;
        put32(o, SS_OK);
        put32(o + 4, c->connect_id);
        return 8;
    }
    case T_CLOSE:
        c->dev = NULL;
        c->connect_id = 0;
        put32(o, SS_OK);
        return 4;
    case T_NB_CLIENTS:
        put32(o, SS_OK);
        put32(o + 4, c->dev ? 1u : 0u);
        return 8;
    case T_USB_CMD: {
        uint32_t size = le32(b + 28), id = le32(b + 4);
        int dir = b[24];
        if (!c->dev || id != c->connect_id) {
            put32(o, SS_BAD_PARAMETER);
            return 4 + (dir ? size : 0);
        }
        if (size > BUF_CAP - 32)
            size = BUF_CAP - 32;
        put32(o, SS_OK);
        if (dir == 0) {
            (void)esk_stlink_command(c->dev, b + 8, b + 32, size, o + 4, 0);
            return 4;
        }
        if (dir == 3) { /* READ_SWO: the probe's trace endpoint */
            memset(o + 4, 0, size);
            (void)esk_stlink_trace(c->dev, o + 4, size);
            return 4 + size;
        }
        memset(o + 4, 0, size);
        (void)esk_stlink_command(c->dev, b + 8, NULL, 0, o + 4, size);
        return 4 + size;
    }
    }
    put32(o, SS_CMD_NOT_AVAILABLE);
    return 4;
}

static void drop(sl_client *c)
{
    esk_host_sock_close(c->fd);
    c->fd = ESK_HOST_SOCK_INVALID;
    c->len = 0;
    c->dev = NULL;
    c->connect_id = 0;
}

es_status esk_session_stlink_serve(es_session *s, const char *host, int port, int *bound)
{
    struct esk_stlink_server *v = s->stlink;
    int i;
    if (v) {
        *bound = v->port;
        return ES_OK;
    }
    v = (struct esk_stlink_server *)esk_calloc(s->a, 1, sizeof *v);
    if (!v)
        return ES_ENOMEM;
    for (i = 0; i < MAX_CLIENTS; i++)
        v->cl[i].fd = ESK_HOST_SOCK_INVALID;
    if (esk_host_sock_init() != ES_OK) {
        esk_free(s->a, v, sizeof *v);
        return ES_EIO;
    }
    if (esk_host_sock_listen(host, port, &v->listener) != ES_OK ||
        esk_host_sock_local_port(v->listener, &v->port) != ES_OK) {
        if (v->listener != ESK_HOST_SOCK_INVALID)
            esk_host_sock_close(v->listener);
        esk_host_sock_shutdown();
        esk_free(s->a, v, sizeof *v);
        return ES_EIO;
    }
    s->stlink = v;
    *bound = v->port;
    return ES_OK;
}

void esk_session_stlink_close(es_session *s)
{
    struct esk_stlink_server *v = s->stlink;
    int i;
    if (!v)
        return;
    for (i = 0; i < MAX_CLIENTS; i++)
        if (v->cl[i].fd != ESK_HOST_SOCK_INVALID)
            drop(&v->cl[i]);
    esk_host_sock_close(v->listener);
    esk_host_sock_shutdown();
    esk_free(s->a, v, sizeof *v);
    s->stlink = NULL;
}

int esk_session_stlink_port(const es_session *s) { return s->stlink ? s->stlink->port : 0; }

void esk_session_stlink_poll(es_session *s)
{
    struct esk_stlink_server *v = s->stlink;
    int i;
    if (!v)
        return;
    while (esk_host_sock_wait(v->listener, 0) == 1) {
        esk_host_sock fd;
        if (esk_host_sock_accept(v->listener, &fd) != ES_OK)
            break;
        for (i = 0; i < MAX_CLIENTS && v->cl[i].fd != ESK_HOST_SOCK_INVALID; i++)
            ;
        if (i == MAX_CLIENTS) {
            esk_host_sock_close(fd); /* no room: refused */
            continue;
        }
        v->cl[i].fd = fd;
        v->cl[i].len = 0;
    }
    for (i = 0; i < MAX_CLIENTS; i++) {
        sl_client *c = &v->cl[i];
        size_t need;
        if (c->fd == ESK_HOST_SOCK_INVALID)
            continue;
        while (c->len < sizeof c->in && esk_host_sock_wait(c->fd, 0) == 1) {
            long got = esk_host_sock_recv(c->fd, c->in + c->len, sizeof c->in - c->len, 0);
            if (got <= 0) {
                if (got < 0)
                    drop(c);
                break;
            }
            c->len += (size_t)got;
        }
        /* one request a poll: the session runs a slice before the next, as
         * the probe's USB takes a frame (1 ms) for each command, and a
         * core a command restarted runs before the next one looks at it */
        if (c->fd != ESK_HOST_SOCK_INVALID && (need = request_size(c->in, c->len)) != 0) {
            size_t n = serve(s, v, c, c->in);
            if (esk_host_sock_send(c->fd, v->out, n, 2000) != (long)n) {
                drop(c);
                continue;
            }
            memmove(c->in, c->in + need, c->len - need);
            c->len -= need;
        }
    }
}
