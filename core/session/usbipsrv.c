/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* usbipsrv.c -- the bench's USB devices on the computer's own USB stack,
 * through USB/IP (ADR-031's last step): the protocol Linux's usbip-host
 * speaks to a computer's vhci-hcd (the kernel's
 * Documentation/usb/usbip_protocol.rst; TCP, big-endian), so a tool that
 * opens the USB device itself -- st-flash and st-info, STM32CubeProgrammer
 * -- finds the NUCLEO's ST-LINK as if it were plugged in:
 *
 *   - OP_REQ_DEVLIST: the devices on the bench whose cable no host part
 *     holds (a NUCLEO's CN1 left free), each with its bus id (1-N), its
 *     identity from its device descriptor and its interfaces' classes
 *     from its configuration descriptor;
 *   - OP_REQ_IMPORT: the device taken by the computer: the session is its
 *     host controller, resets the port and gives it its address, as
 *     usbip-host's host controller has done before the computer sees it;
 *   - USBIP_CMD_SUBMIT: each URB carried out on the device as the bus's
 *     transactions -- a control transfer's SETUP, data and status stages,
 *     a bulk or interrupt transfer's packets of the endpoint's size with
 *     their data toggles -- answered by USBIP_RET_SUBMIT with its status
 *     (0, -EPIPE for a STALL, -ETIME for no answer) and data; a NAK keeps
 *     the URB pending, tried again at each poll; SET_ADDRESS is answered
 *     as done (the address is the session's), SET_CONFIGURATION and
 *     CLEAR_FEATURE reset the toggles they reset on the bus;
 *   - USBIP_CMD_UNLINK: a pending URB cancelled (RET_UNLINK -ECONNRESET),
 *     or already completed (0).
 * One computer at a time imports a device; the connection's end lets it
 * go. Requests are served as the session polls its host ports, while it
 * runs.
 *
 * Inside: a transaction happens at the session's time when it is served,
 * its time on the bus not counted (USB/IP carries URBs, not frames);
 * isochronous URBs are refused (-EINVAL). */
#include "session/session_priv.h"
#include "engine/usb.h"
#include "host/host.h"

#include <string.h>

#define MAX_CLIENTS 4
#define MAX_PENDING 16
#define IN_CAP (64u * 1024u + 64u)

enum { OP_REQ_DEVLIST = 0x8005, OP_REP_DEVLIST = 0x0005, OP_REQ_IMPORT = 0x8003, OP_REP_IMPORT = 0x0003 };
enum { CMD_SUBMIT = 1, CMD_UNLINK = 2, RET_SUBMIT = 3, RET_UNLINK = 4 };
#define E_PIPE (-32)
#define E_TIME (-62)
#define E_INVAL (-22)
#define E_CONNRESET (-104)

typedef struct ui_urb {
    uint32_t seq, ep, dir, len, done;
    int ctrl;
    uint8_t setup[8];
    uint8_t *buf; /* OUT data, or IN data received */
} ui_urb;

typedef struct ui_client {
    esk_host_sock fd;
    uint8_t in[IN_CAP];
    size_t len;
    esk_usb_dev *dev; /* imported, or NULL */
    int addr;
    uint8_t tog_in[16], tog_out[16];
    int mps0;
    ui_urb pend[MAX_PENDING];
    int npend;
} ui_client;

struct esk_usbip_server {
    esk_host_sock listener;
    int port;
    ui_client cl[MAX_CLIENTS];
    uint8_t out[IN_CAP];
};

static void be32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
}
static void be16(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)v;
}
static uint32_t rd32(const uint8_t *b) { return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]; }
static uint32_t rd16(const uint8_t *b) { return (uint32_t)b[0] << 8 | b[1]; }
static uint32_t le16(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8; }

static es_time_t now_of(es_session *s) { return s->eng.sched.now; }

/* the bench's USB devices no host part holds */
int esk_session_usbip_free(es_session *s, const esk_usb_dev *d)
{
    size_t i;
    if (!d->dp || !d->pullup || d->pullup(d->ctx) == ESK_USB_DETACHED)
        return 0;
    for (i = 0; i < s->eng.usb.hosts.len; i++) {
        const esk_usb_host *h = s->eng.usb.hosts.data[i];
        if (h->dp && d->dp->net && h->dp->net == d->dp->net)
            return 0;
    }
    return 1;
}

/* ---- the bus's transactions ------------------------------------------------------------ */

/* a control transfer: SETUP, the data stage, the status stage; its status
 * and the bytes moved */
static int control(es_session *s, esk_usb_dev *d, int addr, const uint8_t setup[8], uint8_t *data, uint32_t len,
                   uint32_t *done, int mps0)
{
    int r, pid = 1, tries;
    uint32_t got = 0;
    es_time_t t = now_of(s);
    *done = 0;
    for (tries = 0; (r = d->setup(d->ctx, addr, 0, setup, t)) == ESK_USB_NAK && tries < 64; tries++)
        ;
    if (r == ESK_USB_STALL)
        return E_PIPE;
    if (r != ESK_USB_ACK)
        return E_TIME;
    if (setup[0] & 0x80) {
        while (got < len) {
            uint8_t pkt[64];
            uint32_t n = 0;
            int p = 0;
            for (tries = 0; (r = d->in(d->ctx, addr, 0, pkt, sizeof pkt, &n, &p, t)) == ESK_USB_NAK && tries < 64; tries++)
                ;
            if (r == ESK_USB_STALL)
                return E_PIPE;
            if (r != ESK_USB_ACK)
                return E_TIME;
            if ((p != 0) == (pid != 0)) {
                uint32_t k = n < len - got ? n : len - got;
                memcpy(data + got, pkt, k);
                got += k;
                pid ^= 1;
            }
            if (n < (uint32_t)mps0)
                break;
        }
        r = d->out(d->ctx, addr, 0, 1, NULL, 0, t);
    } else {
        while (got < len) {
            uint32_t n = len - got < (uint32_t)mps0 ? len - got : (uint32_t)mps0;
            for (tries = 0; (r = d->out(d->ctx, addr, 0, pid, data + got, n, t)) == ESK_USB_NAK && tries < 64; tries++)
                ;
            if (r == ESK_USB_STALL)
                return E_PIPE;
            if (r != ESK_USB_ACK)
                return E_TIME;
            got += n;
            pid ^= 1;
        }
        {
            uint8_t z[1];
            uint32_t n = 0;
            int p = 0;
            r = d->in(d->ctx, addr, 0, z, 0, &n, &p, t);
        }
    }
    *done = got;
    return r == ESK_USB_STALL ? E_PIPE : r == ESK_USB_ACK ? 0 : E_TIME;
}

/* the device's descriptor (18) and configuration (up to cap) at addr */
static int descriptors(es_session *s, esk_usb_dev *d, int addr, uint8_t *dev18, uint8_t *cfg, uint32_t cap,
                       uint32_t *cfg_len)
{
    uint8_t q[8] = { 0x80, 6, 0, 1, 0, 0, 18, 0 };
    uint32_t n = 0;
    if (control(s, d, addr, q, dev18, 18, &n, 64) || n < 18)
        return -1;
    q[3] = 2;
    q[6] = (uint8_t)cap;
    q[7] = (uint8_t)(cap >> 8);
    if (control(s, d, addr, q, cfg, cap, &n, dev18[7] ? dev18[7] : 64) || n < 9)
        return -1;
    *cfg_len = n < le16(cfg + 2) ? n : le16(cfg + 2);
    return 0;
}

/* the port reset and the address, as the host controller under usbip-host gives them */
static int enumerate(es_session *s, esk_usb_dev *d, int addr)
{
    uint8_t q[8] = { 0, 5, 0, 0, 0, 0, 0, 0 };
    uint32_t n;
    es_time_t t = now_of(s);
    if (d->bus) {
        d->bus(d->ctx, ESK_USB_RESET, 0, t);
        d->bus(d->ctx, ESK_USB_RESET_END, 0, t);
    }
    q[2] = (uint8_t)addr;
    return control(s, d, 0, q, NULL, 0, &n, 8);
}

/* usbip_usb_device: 312 bytes */
static size_t device_info(es_session *s, esk_usb_dev *d, int index, uint8_t *o, uint8_t *cfg, uint32_t *cfg_len)
{
    uint8_t dev18[18];
    char path[64];
    int addr = index + 1;
    memset(o, 0, 312);
    if (enumerate(s, d, addr) || descriptors(s, d, addr, dev18, cfg, 512, cfg_len))
        return 0;
    esk_snprintf(path, sizeof path, "/sys/devices/embsim/usb1/1-%d", index + 1);
    memcpy(o, path, strlen(path));
    esk_snprintf((char *)o + 256, 32, "1-%d", index + 1);
    be32(o + 288, 1);                  /* busnum */
    be32(o + 292, (uint32_t)addr);     /* devnum */
    be32(o + 296, 2);                  /* USB_SPEED_FULL */
    be16(o + 300, le16(dev18 + 8));    /* idVendor */
    be16(o + 302, le16(dev18 + 10));   /* idProduct */
    be16(o + 304, le16(dev18 + 12));   /* bcdDevice */
    o[306] = dev18[4];
    o[307] = dev18[5];
    o[308] = dev18[6];
    o[309] = 0; /* bConfigurationValue: not configured yet */
    o[310] = dev18[17];
    o[311] = *cfg_len >= 9 ? cfg[4] : 0;
    return 312;
}

/* ---- requests --------------------------------------------------------------------------- */

static size_t devlist(es_session *s, uint8_t *o)
{
    size_t i, n = 12;
    uint32_t count = 0;
    uint8_t cfg[512];
    be16(o, 0x0111);
    be16(o + 2, OP_REP_DEVLIST);
    be32(o + 4, 0);
    for (i = 0; i < s->eng.usb.devs.len && n + 312 + 4 * 32 < IN_CAP; i++) {
        esk_usb_dev *d = s->eng.usb.devs.data[i];
        uint32_t cl = 0, k;
        if (!esk_session_usbip_free(s, d) || !device_info(s, d, (int)i, o + n, cfg, &cl))
            continue;
        n += 312;
        /* each interface: class, subclass, protocol, padding */
        for (k = 0; k + 9 <= cl; k += cfg[k] ? cfg[k] : cl)
            if (cfg[k + 1] == 4) {
                o[n] = cfg[k + 5];
                o[n + 1] = cfg[k + 6];
                o[n + 2] = cfg[k + 7];
                o[n + 3] = 0;
                n += 4;
            }
        count++;
    }
    be32(o + 8, count);
    return n;
}

static size_t import(es_session *s, ui_client *c, const uint8_t *busid, uint8_t *o)
{
    size_t i;
    uint8_t cfg[512];
    uint32_t cl = 0;
    be16(o, 0x0111);
    be16(o + 2, OP_REP_IMPORT);
    be32(o + 4, 1); /* an error until found */
    for (i = 0; i < s->eng.usb.devs.len; i++) {
        esk_usb_dev *d = s->eng.usb.devs.data[i];
        char want[32];
        esk_snprintf(want, sizeof want, "1-%d", (int)i + 1);
        if (strncmp((const char *)busid, want, 32) || !esk_session_usbip_free(s, d))
            continue;
        if (!device_info(s, d, (int)i, o + 8, cfg, &cl))
            return 8;
        c->dev = d;
        c->addr = (int)i + 1;
        c->mps0 = 64;
        memset(c->tog_in, 0, sizeof c->tog_in);
        memset(c->tog_out, 0, sizeof c->tog_out);
        be32(o + 4, 0);
        return 8 + 312;
    }
    return 8;
}

/* a URB tried on the device: 1 when it completed (status in *st), 0 when it waits on a NAK */
static int attempt(es_session *s, ui_client *c, ui_urb *u, int *st)
{
    esk_usb_dev *d = c->dev;
    es_time_t t = now_of(s);
    int r;
    if (!d || !esk_session_usbip_free(s, d)) {
        *st = E_TIME;
        return 1;
    }
    if (u->ctrl) {
        uint8_t *q = u->setup;
        if (q[0] == 0 && q[1] == 5) { /* SET_ADDRESS: the session's address stands */
            *st = 0;
            return 1;
        }
        *st = control(s, d, c->addr, q, u->buf, u->len, &u->done, c->mps0);
        if (!*st && q[0] == 0 && (q[1] == 9 || q[1] == 11)) { /* SET_CONFIGURATION, SET_INTERFACE */
            memset(c->tog_in, 0, sizeof c->tog_in);
            memset(c->tog_out, 0, sizeof c->tog_out);
        } else if (!*st && q[0] == 2 && q[1] == 1) { /* CLEAR_FEATURE(ENDPOINT_HALT) */
            uint32_t e = q[4] & 15u;
            if (q[4] & 0x80)
                c->tog_in[e] = 0;
            else
                c->tog_out[e] = 0;
        }
        return 1;
    }
    for (;;) {
        if (u->dir) {
            uint8_t pkt[64];
            uint32_t n = 0;
            int p = 0;
            if (u->done >= u->len) {
                *st = 0;
                return 1;
            }
            r = d->in(d->ctx, c->addr, (int)u->ep, pkt, sizeof pkt, &n, &p, t);
            if (r == ESK_USB_NAK)
                return 0;
            if (r != ESK_USB_ACK) {
                *st = r == ESK_USB_STALL ? E_PIPE : E_TIME;
                return 1;
            }
            if ((p != 0) == (c->tog_in[u->ep] != 0)) {
                uint32_t k = n < u->len - u->done ? n : u->len - u->done;
                memcpy(u->buf + u->done, pkt, k);
                u->done += k;
                c->tog_in[u->ep] ^= 1;
            }
            if (n < 64) { /* a short packet ends it */
                *st = 0;
                return 1;
            }
        } else {
            uint32_t n = u->len - u->done < 64u ? u->len - u->done : 64u;
            r = d->out(d->ctx, c->addr, (int)u->ep, c->tog_out[u->ep], u->buf + u->done, n, t);
            if (r == ESK_USB_NAK)
                return 0;
            if (r != ESK_USB_ACK) {
                *st = r == ESK_USB_STALL ? E_PIPE : E_TIME;
                return 1;
            }
            c->tog_out[u->ep] ^= 1;
            u->done += n;
            if (u->done >= u->len) {
                *st = 0;
                return 1;
            }
        }
    }
}

static int reply(ui_client *c, uint8_t *o, ui_urb *u, int st)
{
    size_t n = 48 + (u->dir ? u->done : 0);
    memset(o, 0, 48);
    be32(o, RET_SUBMIT);
    be32(o + 4, u->seq);
    be32(o + 20, (uint32_t)st);
    be32(o + 24, u->done);
    if (u->dir && u->done)
        memcpy(o + 48, u->buf, u->done);
    return esk_host_sock_send(c->fd, o, n, 2000) == (long)n;
}

static void urb_free(es_session *s, ui_urb *u)
{
    if (u->buf)
        esk_free(s->a, u->buf, u->len ? u->len : 1);
    u->buf = NULL;
}

/* the size of the request at the start of b, or 0 while incomplete */
static size_t request_size(const ui_client *c, const uint8_t *b, size_t n)
{
    if (!c->dev) {
        if (n < 8)
            return 0;
        if (rd16(b + 2) == OP_REQ_IMPORT)
            return n >= 40 ? 40 : 0;
        return 8;
    }
    if (n < 48)
        return 0;
    if (rd32(b) == CMD_SUBMIT && rd32(b + 12) == 0) { /* OUT: its data follows */
        size_t need = 48 + rd32(b + 24);
        return need <= IN_CAP && n >= need ? need : need > IN_CAP ? 48 : 0;
    }
    return 48;
}

static void drop(es_session *s, ui_client *c)
{
    int i;
    esk_host_sock_close(c->fd);
    for (i = 0; i < c->npend; i++)
        urb_free(s, &c->pend[i]);
    c->npend = 0;
    c->fd = ESK_HOST_SOCK_INVALID;
    c->len = 0;
    c->dev = NULL;
}

static int serve(es_session *s, struct esk_usbip_server *v, ui_client *c, const uint8_t *b)
{
    size_t n;
    if (!c->dev) {
        if (rd16(b + 2) == OP_REQ_DEVLIST)
            n = devlist(s, v->out);
        else if (rd16(b + 2) == OP_REQ_IMPORT)
            n = import(s, c, b + 8, v->out);
        else
            return 0;
        return esk_host_sock_send(c->fd, v->out, n, 2000) == (long)n;
    }
    if (rd32(b) == CMD_UNLINK) {
        uint32_t seq = rd32(b + 20);
        int i, st = 0;
        for (i = 0; i < c->npend; i++)
            if (c->pend[i].seq == seq) {
                urb_free(s, &c->pend[i]);
                c->pend[i] = c->pend[--c->npend];
                st = E_CONNRESET;
                break;
            }
        memset(v->out, 0, 48);
        be32(v->out, RET_UNLINK);
        be32(v->out + 4, rd32(b + 4));
        be32(v->out + 20, (uint32_t)st);
        return esk_host_sock_send(c->fd, v->out, 48, 2000) == 48;
    }
    if (rd32(b) == CMD_SUBMIT) {
        ui_urb u;
        int st = 0;
        memset(&u, 0, sizeof u);
        u.seq = rd32(b + 4);
        u.dir = rd32(b + 12);
        u.ep = rd32(b + 16) & 15u;
        u.len = rd32(b + 24);
        u.ctrl = u.ep == 0;
        memcpy(u.setup, b + 40, 8);
        if (rd32(b + 32) || u.len > IN_CAP - 64) { /* isochronous packets, or too long */
            u.done = 0;
            return reply(c, v->out, &u, E_INVAL);
        }
        u.buf = (uint8_t *)esk_malloc(s->a, u.len ? u.len : 1);
        if (!u.buf)
            return 0;
        if (!u.dir && u.len)
            memcpy(u.buf, b + 48, u.len);
        if (attempt(s, c, &u, &st)) {
            int ok = reply(c, v->out, &u, st);
            urb_free(s, &u);
            return ok;
        }
        if (c->npend == MAX_PENDING) {
            int ok = reply(c, v->out, &u, E_TIME);
            urb_free(s, &u);
            return ok;
        }
        c->pend[c->npend++] = u; /* waits on a NAK */
        return 1;
    }
    return 1;
}

es_status esk_session_usbip_serve(es_session *s, const char *host, int port, int *bound)
{
    struct esk_usbip_server *v = s->usbip;
    int i;
    if (v) {
        *bound = v->port;
        return ES_OK;
    }
    v = (struct esk_usbip_server *)esk_calloc(s->a, 1, sizeof *v);
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
    s->usbip = v;
    *bound = v->port;
    return ES_OK;
}

void esk_session_usbip_close(es_session *s)
{
    struct esk_usbip_server *v = s->usbip;
    int i;
    if (!v)
        return;
    for (i = 0; i < MAX_CLIENTS; i++)
        if (v->cl[i].fd != ESK_HOST_SOCK_INVALID)
            drop(s, &v->cl[i]);
    esk_host_sock_close(v->listener);
    esk_host_sock_shutdown();
    esk_free(s->a, v, sizeof *v);
    s->usbip = NULL;
}

void esk_session_usbip_poll(es_session *s)
{
    struct esk_usbip_server *v = s->usbip;
    int i, k;
    if (!v)
        return;
    while (esk_host_sock_wait(v->listener, 0) == 1) {
        esk_host_sock fd;
        if (esk_host_sock_accept(v->listener, &fd) != ES_OK)
            break;
        for (i = 0; i < MAX_CLIENTS && v->cl[i].fd != ESK_HOST_SOCK_INVALID; i++)
            ;
        if (i == MAX_CLIENTS) {
            esk_host_sock_close(fd);
            continue;
        }
        memset(&v->cl[i], 0, sizeof v->cl[i]);
        v->cl[i].fd = fd;
    }
    for (i = 0; i < MAX_CLIENTS; i++) {
        ui_client *c = &v->cl[i];
        size_t need;
        if (c->fd == ESK_HOST_SOCK_INVALID)
            continue;
        /* the URBs waiting on a NAK, tried again */
        for (k = 0; k < c->npend;) {
            int st = 0;
            if (attempt(s, c, &c->pend[k], &st)) {
                int ok = reply(c, v->out, &c->pend[k], st);
                urb_free(s, &c->pend[k]);
                c->pend[k] = c->pend[--c->npend];
                if (!ok) {
                    drop(s, c);
                    break;
                }
            } else {
                k++;
            }
        }
        if (c->fd == ESK_HOST_SOCK_INVALID)
            continue;
        while (c->len < sizeof c->in && esk_host_sock_wait(c->fd, 0) == 1) {
            long got = esk_host_sock_recv(c->fd, c->in + c->len, sizeof c->in - c->len, 0);
            if (got <= 0) {
                drop(s, c);
                break;
            }
            c->len += (size_t)got;
        }
        while (c->fd != ESK_HOST_SOCK_INVALID && (need = request_size(c, c->in, c->len)) != 0) {
            if (!serve(s, v, c, c->in)) {
                drop(s, c);
                break;
            }
            memmove(c->in, c->in + need, c->len - need);
            c->len -= need;
        }
    }
}

int esk_session_usbip_port(const es_session *s) { return s->usbip ? s->usbip->port : 0; }
