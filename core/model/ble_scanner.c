/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ble_scanner.c -- a Bluetooth LE scanner on the session's air (engine/
 * air.h; docs/engineering/c3-radios.md, step 6), as a phone's scanner app
 * sees the devices around it (Bluetooth Core 5.4, Vol 6, Part B):
 *
 *   - it scans passively, listening `window` ms of every `interval` (30 and
 *     30 by default: a continuous scan) on one advertising channel, the
 *     next one (37, 38, 39 in turn) each interval;
 *   - an advertising PDU heard (ADV_IND, ADV_DIRECT_IND,
 *     ADV_NONCONN_IND, ADV_SCAN_IND) adds or updates its advertiser: its
 *     address and whether it is public or random, the PDU type, its name
 *     (the AD types Complete and Shortened Local Name), an iBeacon's
 *     proximity UUID, major, minor and measured power (Apple's manufacturer
 *     data: company 0x004C, type 0x02, length 0x15), the last RSSI and how
 *     many it heard;
 *   - its outputs list them as a scanner app does.
 *
 * Not yet: active scanning (SCAN_REQ, SCAN_RSP), connections, extended
 * advertising (the secondary channels), the 2M and Coded PHYs, whitelists. */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"

#define MAX_DEV   16
#define LIST_CAP  2048
#define MS_PS     ES_PS_PER_MS

enum { WAKE_HOP = 1, WAKE_CLOSE = 2 };

typedef struct sc_dev {
    int used;
    uint8_t addr[6];
    int random;
    unsigned type;
    char name[32];
    char beacon[80];
    int rssi;
    uint64_t heard;
} sc_dev;

typedef struct sc_state {
    esk_comp *c;
    esk_air_node node;
    int attached;
    int ch; /* 0, 1, 2: channels 37, 38, 39 */
    es_time_t window, interval;
    int listening;
    sc_dev dev[MAX_DEV];
    uint64_t pdus;
    char list[LIST_CAP];
} sc_state;

static const uint32_t CH_MHZ[3] = { 2402u, 2426u, 2480u };

static uint32_t sc_tuned(void *ctx, int kind)
{
    sc_state *s = (sc_state *)ctx;
    return kind == ESK_AIR_BLE && s->listening ? CH_MHZ[s->ch] : 0u;
}

static sc_dev *dev_find(sc_state *s, const uint8_t *addr, int random)
{
    int i;
    sc_dev *free_one = NULL;
    for (i = 0; i < MAX_DEV; i++) {
        if (s->dev[i].used && s->dev[i].random == random && !memcmp(s->dev[i].addr, addr, 6))
            return &s->dev[i];
        if (!s->dev[i].used && !free_one)
            free_one = &s->dev[i];
    }
    if (free_one) {
        memset(free_one, 0, sizeof *free_one);
        free_one->used = 1;
        free_one->random = random;
        memcpy(free_one->addr, addr, 6);
    }
    return free_one;
}

/* an iBeacon as scanner apps show it: the UUID in its usual form, major
 * and minor (big-endian), the measured power at 1 m */
static void beacon_text(char *out, size_t cap, const uint8_t *b)
{
    char *o = out;
    size_t left = cap;
    int i, k;
    for (i = 0; i < 16; i++) {
        k = snprintf(o, left, "%s%02X", i == 4 || i == 6 || i == 8 || i == 10 ? "-" : "", b[i]);
        if (k < 0 || (size_t)k >= left)
            return;
        o += k;
        left -= (size_t)k;
    }
    snprintf(o, left, " major %u minor %u %ddBm@1m", (unsigned)(b[16] << 8 | b[17]), (unsigned)(b[18] << 8 | b[19]),
             (int)(int8_t)b[20]);
}

static int sc_rx(void *ctx, const esk_air_frame *f, int rssi, es_time_t at)
{
    sc_state *s = (sc_state *)ctx;
    const uint8_t *p = f->data;
    unsigned type, len, i;
    sc_dev *d;
    (void)at;
    if (f->len < 8)
        return 0;
    type = p[0] & 15u;
    len = p[1];
    if (len + 2u > f->len || len < 6u || (type != 0 && type != 1 && type != 2 && type != 6))
        return 0;
    s->pdus++;
    /* AdvA is little-endian on the air */
    {
        uint8_t addr[6];
        for (i = 0; i < 6; i++)
            addr[i] = p[2 + 5 - i];
        d = dev_find(s, addr, (p[0] >> 6) & 1);
    }
    if (!d)
        return 0;
    d->type = type;
    d->rssi = rssi;
    d->heard++;
    if (type != 1) /* the AD structures */
        for (i = 8; i + 1u < len + 2u;) {
            unsigned l = p[i], t = p[i + 1];
            if (!l || i + 1u + l > len + 2u)
                break;
            if ((t == 0x09 || (t == 0x08 && !d->name[0])) && l - 1u < sizeof d->name) {
                memcpy(d->name, p + i + 2, l - 1u);
                d->name[l - 1u] = 0;
            }
            if (t == 0xFF && l == 26u && p[i + 2] == 0x4C && p[i + 3] == 0x00 && p[i + 4] == 0x02 && p[i + 5] == 0x15)
                beacon_text(d->beacon, sizeof d->beacon, p + i + 6);
            i += 1u + l;
        }
    return 0;
}

static void sc_time(esk_comp *c, uint32_t token)
{
    sc_state *s = (sc_state *)c->state;
    if (token == WAKE_CLOSE) { /* the window closes until the next interval */
        s->listening = 0;
        return;
    }
    if (token != WAKE_HOP)
        return;
    s->ch = (s->ch + 1) % 3;
    s->listening = 1;
    if (s->window < s->interval)
        esk_comp_wake_at(c, esk_comp_now(c) + s->window, WAKE_CLOSE);
    esk_comp_wake_at(c, esk_comp_now(c) + s->interval, WAKE_HOP);
}

static es_status sc_create(esk_comp *c)
{
    sc_state *s = (sc_state *)c->state;
    double window = 30, interval = 30;
    s->c = c;
    esk_comp_param_double(c, "window", &window);
    esk_comp_param_double(c, "interval", &interval);
    if (window <= 0 || interval < window) {
        esk_comp_diag(c, ES_SEV_ERROR, "ble_scanner.window", "the scan window is between 0 and the interval");
        return ES_ESCHEMA;
    }
    s->window = (es_time_t)(window * (double)MS_PS);
    s->interval = (es_time_t)(interval * (double)MS_PS);
    s->node.ctx = s;
    s->node.comp = c;
    s->node.kind = ESK_AIR_BLE;
    s->node.tuned = sc_tuned;
    s->node.rx = sc_rx;
    s->node.tx_done = NULL;
    if (esk_air_attach(c->e, &s->node) != ES_OK)
        return ES_ENOMEM;
    s->attached = 1;
    return ES_OK;
}

static void sc_destroy(esk_comp *c)
{
    sc_state *s = (sc_state *)c->state;
    if (s && s->attached)
        esk_air_detach(c->e, &s->node);
    if (s)
        s->attached = 0;
}

static void sc_reset(esk_comp *c)
{
    sc_state *s = (sc_state *)c->state;
    memset(s->dev, 0, sizeof s->dev);
    s->pdus = 0;
    s->ch = 0;
    s->listening = 1;
    if (s->window < s->interval)
        esk_comp_wake_at(c, s->window, WAKE_CLOSE);
    esk_comp_wake_at(c, s->interval, WAKE_HOP);
}

static void sc_observe(esk_comp *c, esk_json *out)
{
    sc_state *s = (sc_state *)c->state;
    size_t n = 0;
    int i, count = 0;
    s->list[0] = 0;
    for (i = 0; i < MAX_DEV; i++) {
        const sc_dev *d = &s->dev[i];
        static const char *const types[] = { "ADV_IND", "ADV_DIRECT_IND", "ADV_NONCONN_IND", "", "", "", "ADV_SCAN_IND" };
        int k;
        if (!d->used)
            continue;
        count++;
        k = snprintf(s->list + n, LIST_CAP - n, "%02x:%02x:%02x:%02x:%02x:%02x (%s) %s %ddBm %s \"%s\"%s%s\n",
                     d->addr[0], d->addr[1], d->addr[2], d->addr[3], d->addr[4], d->addr[5],
                     d->random ? "random" : "public", types[d->type < 7 ? d->type : 3], d->rssi,
                     d->type == 0 || d->type == 1 ? "connectable" : "-", d->name, d->beacon[0] ? " iBeacon " : "",
                     d->beacon);
        if (k < 0 || (size_t)k >= LIST_CAP - n)
            break;
        n += (size_t)k;
    }
    esk_json_set_integer(c->e->a, out, "devices", count);
    esk_json_set_integer(c->e->a, out, "pdus", (int64_t)s->pdus);
    esk_json_set_string(c->e->a, out, "list", s->list);
}

const esk_model_ops esk_ble_scanner_ops = { "ble_scanner", sizeof(sc_state), sc_create, sc_destroy, sc_reset, NULL,
                                            sc_time, NULL, sc_observe, NULL, 0 };
