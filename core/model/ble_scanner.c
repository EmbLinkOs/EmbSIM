/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ble_scanner.c -- a Bluetooth LE scanner on the session's air (engine/
 * air.h; docs/engineering/c3-radios.md, step 6), as a phone's scanner app
 * sees the devices around it (Bluetooth Core 5.4, Vol 6, Part B):
 *
 *   - it scans, listening `window` ms of every `interval` (30 and 30 by
 *     default: a continuous scan) on one advertising channel, the next one
 *     (37, 38, 39 in turn) each interval;
 *   - actively, as phones scan (`active`, true by default): T_IFS after a
 *     scannable PDU (ADV_IND, ADV_SCAN_IND) it sends a SCAN_REQ from its
 *     random static `address` and takes the SCAN_RESP that follows, whose
 *     AD structures add to the advertiser's; the requests follow the
 *     specification's backoff (4.4.3.2: a request when a count drawn from
 *     1 to an upper limit runs out, the limit doubled after two failures
 *     in a row, halved after two successes, between 1 and 256);
 *   - an advertising PDU heard (ADV_IND, ADV_DIRECT_IND,
 *     ADV_NONCONN_IND, ADV_SCAN_IND) adds or updates its advertiser: its
 *     address and whether it is public or random, the PDU type, its name
 *     (the AD types Complete and Shortened Local Name), an iBeacon's
 *     proximity UUID, major, minor and measured power (Apple's manufacturer
 *     data: company 0x004C, type 0x02, length 0x15), the last RSSI and how
 *     many it heard;
 *   - its outputs list them as a scanner app does.
 *
 * Not yet: connections, extended advertising (the secondary channels), the
 * 2M and Coded PHYs, whitelists. */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"

#define MAX_DEV   16
#define LIST_CAP  2048
#define MS_PS     ES_PS_PER_MS
#define T_IFS_PS  (150u * ES_PS_PER_US)
#define TX_DBM    0.0

enum { WAKE_HOP = 1, WAKE_CLOSE = 2 };

typedef struct sc_dev {
    int used;
    uint8_t addr[6];
    int random;
    unsigned type;
    char name[32];
    char beacon[80];
    int rssi;
    uint64_t heard, responses;
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
    /* active scanning */
    int active;
    uint8_t me[6];
    uint8_t req_to[7]; /* the request outstanding: AdvA and its TxAdd */
    es_time_t req_until;
    unsigned upper, count, ok_run, fail_run;
    uint32_t rng;
    uint64_t requests;
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

/* the backoff's result for the request outstanding (4.4.3.2) */
static void backoff(sc_state *s, int ok)
{
    s->req_until = 0;
    if (ok) {
        s->fail_run = 0;
        if (++s->ok_run == 2) {
            s->ok_run = 0;
            if (s->upper > 1u)
                s->upper /= 2u;
        }
    } else {
        s->ok_run = 0;
        if (++s->fail_run == 2) {
            s->fail_run = 0;
            if (s->upper < 256u)
                s->upper *= 2u;
        }
    }
    s->rng = s->rng * 1103515245u + 12345u;
    s->count = 1u + (s->rng >> 16) % s->upper;
}

/* a SCAN_REQ to the advertiser of `adv`, T_IFS after it ends at `end` */
static void scan_req(sc_state *s, const uint8_t *adv, uint32_t mhz, es_time_t end)
{
    uint8_t p[14];
    esk_air_frame f;
    unsigned i;
    p[0] = (uint8_t)(0x03u | 1u << 6 | (adv[0] & 0x40u) << 1); /* random ScanA; RxAdd as the AdvA's TxAdd */
    p[1] = 12;
    for (i = 0; i < 6; i++)
        p[2 + i] = s->me[5 - i];
    memcpy(p + 8, adv + 2, 6);
    f.data = p;
    f.len = sizeof p;
    f.mhz = mhz;
    f.dbm = TX_DBM;
    f.rate_kbps = 1000u;
    f.kind = ESK_AIR_BLE;
    if (esk_air_send(s->c->e, &s->node, &f, end + T_IFS_PS, esk_air_ble_airtime(sizeof p, 1000u), 0u) != ES_OK)
        return;
    s->requests++;
    memcpy(s->req_to, adv + 2, 6);
    s->req_to[6] = (uint8_t)((adv[0] >> 6) & 1u);
    /* the response starts T_IFS after the request */
    s->req_until = end + 2u * T_IFS_PS + esk_air_ble_airtime(sizeof p, 1000u) + 40u * ES_PS_PER_US;
}

static int sc_rx(void *ctx, const esk_air_frame *f, int rssi, es_time_t at)
{
    sc_state *s = (sc_state *)ctx;
    const uint8_t *p = f->data;
    unsigned type, len, i;
    sc_dev *d;
    if (f->len < 8)
        return 0;
    type = p[0] & 15u;
    len = p[1];
    if (len + 2u > f->len || len < 6u || (type != 0 && type != 1 && type != 2 && type != 4 && type != 6))
        return 0;
    if (type == 4u) { /* a SCAN_RSP: to the request outstanding only */
        if (!s->req_until || at > s->req_until + esk_air_ble_airtime(f->len, 1000u) ||
            memcmp(p + 2, s->req_to, 6) != 0 || ((p[0] >> 6) & 1u) != s->req_to[6])
            return 0;
        backoff(s, 1);
    } else if (s->req_until && at > s->req_until) /* no response came */
        backoff(s, 0);
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
    if (type == 4u)
        d->responses++;
    else
        d->type = type;
    d->rssi = rssi;
    d->heard++;
    if (s->active && !s->req_until && (type == 0u || type == 6u) && --s->count == 0u)
        scan_req(s, p, f->mhz, at);
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
    s->active = 1;
    esk_comp_param_bool(c, "active", &s->active);
    {
        unsigned v[6];
        int i;
        if (sscanf(esk_comp_param_str(c, "address", "f2:4b:9c:01:23:45"), "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1],
                   &v[2], &v[3], &v[4], &v[5]) != 6 ||
            (v[0] & 0xC0u) != 0xC0u) {
            esk_comp_diag(c, ES_SEV_ERROR, "ble_scanner.address", "a random static address: its top two bits are 1");
            return ES_ESCHEMA;
        }
        for (i = 0; i < 6; i++)
            s->me[i] = (uint8_t)v[i];
    }
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
    s->requests = 0;
    s->req_until = 0;
    s->upper = s->count = 1u;
    s->ok_run = s->fail_run = 0;
    s->rng = 0x2545F491u;
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
    esk_json_set_integer(c->e->a, out, "requests", (int64_t)s->requests);
    esk_json_set_string(c->e->a, out, "list", s->list);
}

const esk_model_ops esk_ble_scanner_ops = { "ble_scanner", sizeof(sc_state), sc_create, sc_destroy, sc_reset, NULL,
                                            sc_time, NULL, sc_observe, NULL, 0 };
