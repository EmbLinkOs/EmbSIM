/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* wifi_scanner.c -- a phone's Wi-Fi analyzer on the session's air (engine/
 * air.h; docs/engineering/c3-radios.md, step 7), as a phone's Wi-Fi list
 * and analyzer apps see the networks around it (IEEE 802.11-2020, 11.1.4):
 *
 *   - it scans as a phone does: every `period` s (10 by default, as a
 *     phone scans while its Wi-Fi settings are open) it goes through
 *     channels 1 to `channels` (13, as in Europe and most of the world),
 *     `dwell` ms on each (40 by default);
 *   - actively (`active`, true by default): DIFS after it tunes to a
 *     channel it sends a probe request for any SSID from its randomised,
 *     locally administered `address` (Android and iOS randomise it); the
 *     access points there answer with probe responses, which it
 *     acknowledges; passively it hears their beacons only;
 *   - each beacon and probe response heard adds or updates its network:
 *     the BSSID, the SSID (a hidden network's is empty), the channel its
 *     DS Parameter Set names, the security the capability's Privacy bit,
 *     the RSN element's AKM suites and WPA's vendor element give (Open,
 *     WEP, WPA, WPA2, WPA3, WPA2/WPA3 and -Enterprise), the last RSSI;
 *   - its outputs list the networks heard in the scan going on or the last
 *     one finished, the strongest first, as the apps do.
 *
 * Not yet: the 5 and 6 GHz bands, DFS's passive channels, 802.11k/v
 * neighbour reports, scanning while connected (it is no station). */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"

#define MAX_NET   24
#define LIST_CAP  3072
#define DIFS_PS   (50u * ES_PS_PER_US)
#define TX_DBM    15.0 /* a phone's 2.4 GHz transmit power */
#define RATE_1M   1000u

enum { WAKE_DWELL = 1, WAKE_SCAN = 2, WAKE_PROBE = 3 };

typedef struct ws_net {
    int used;
    uint8_t bssid[6];
    char ssid[33];
    int channel;
    char security[24];
    int rssi;
    uint64_t heard;
    uint32_t scan; /* the scan it was last heard in */
} ws_net;

typedef struct ws_state {
    esk_comp *c;
    esk_air_node node;
    int attached;
    int channels, active;
    es_time_t dwell, period;
    uint8_t me[6];
    int ch; /* the channel tuned to, 0 between scans */
    uint32_t scan; /* the scan going on (from 1), or the last one */
    uint64_t scans, probes;
    unsigned seq;
    ws_net net[MAX_NET];
    char list[LIST_CAP];
} ws_state;

static uint32_t ws_tuned(void *ctx, int kind)
{
    ws_state *s = (ws_state *)ctx;
    return kind == ESK_AIR_WIFI && s->ch ? esk_air_wifi_mhz(s->ch) : 0u;
}

/* an element in the IEs at `p` (n bytes), or NULL; `len` its length */
static const uint8_t *ie_find(const uint8_t *p, uint32_t n, unsigned id, unsigned *len)
{
    uint32_t i = 0;
    while (i + 2u <= n && i + 2u + p[i + 1] <= n) {
        if (p[i] == id) {
            *len = p[i + 1];
            return p + i + 2;
        }
        i += 2u + p[i + 1];
    }
    return NULL;
}

/* WPA's vendor element (Microsoft's OUI, type 1), or NULL */
static const uint8_t *wpa_find(const uint8_t *p, uint32_t n)
{
    uint32_t i = 0;
    while (i + 2u <= n && i + 2u + p[i + 1] <= n) {
        if (p[i] == 221 && p[i + 1] >= 4 && p[i + 2] == 0x00 && p[i + 3] == 0x50 && p[i + 4] == 0xF2 && p[i + 5] == 1)
            return p + i + 2;
        i += 2u + p[i + 1];
    }
    return NULL;
}

/* the AKM suites of an RSN element's body (version, group cipher, pairwise
 * suites, then the AKM suites): PSK (2), SAE (8), 802.1X (1) as bits 0-2 */
static unsigned rsn_akms(const uint8_t *b, unsigned len)
{
    unsigned i, n, at, akm = 0;
    if (len < 8u)
        return 0;
    n = (unsigned)(b[6] | b[7] << 8);
    at = 8u + 4u * n;
    if (at + 2u > len)
        return 0;
    n = (unsigned)(b[at] | b[at + 1] << 8);
    at += 2u;
    for (i = 0; i < n && at + 4u <= len; i++, at += 4u) {
        if (b[at] != 0x00 || b[at + 1] != 0x0F || b[at + 2] != 0xAC)
            continue;
        if (b[at + 3] == 2 || b[at + 3] == 6)
            akm |= 1u;
        else if (b[at + 3] == 8)
            akm |= 2u;
        else if (b[at + 3] == 1 || b[at + 3] == 5)
            akm |= 4u;
    }
    return akm;
}

static void security_of(char *out, size_t cap, unsigned capinfo, const uint8_t *ies, uint32_t n)
{
    unsigned len = 0, akm;
    const uint8_t *rsn = ie_find(ies, n, 48, &len);
    if (rsn) {
        akm = rsn_akms(rsn, len);
        snprintf(out, cap, "%s", akm & 4u ? "WPA2-Enterprise" : (akm & 3u) == 3u ? "WPA2/WPA3" : akm & 2u ? "WPA3" : "WPA2");
    } else if (wpa_find(ies, n)) {
        snprintf(out, cap, "WPA");
    } else {
        snprintf(out, cap, "%s", capinfo & 0x0010u ? "WEP" : "Open");
    }
}

static ws_net *net_find(ws_state *s, const uint8_t *bssid)
{
    int i;
    ws_net *free_one = NULL, *oldest = NULL;
    for (i = 0; i < MAX_NET; i++) {
        if (s->net[i].used && !memcmp(s->net[i].bssid, bssid, 6))
            return &s->net[i];
        if (!s->net[i].used && !free_one)
            free_one = &s->net[i];
        if (s->net[i].used && (!oldest || s->net[i].scan < oldest->scan))
            oldest = &s->net[i];
    }
    if (!free_one && oldest && oldest->scan + 1u < s->scan)
        free_one = oldest; /* one gone for a scan makes room */
    if (free_one) {
        memset(free_one, 0, sizeof *free_one);
        free_one->used = 1;
        memcpy(free_one->bssid, bssid, 6);
    }
    return free_one;
}

static int ws_rx(void *ctx, const esk_air_frame *fr, int rssi, es_time_t at)
{
    ws_state *s = (ws_state *)ctx;
    const uint8_t *f = fr->data;
    unsigned subtype, len = 0, capinfo;
    const uint8_t *ie;
    ws_net *n;
    (void)at;
    if (fr->len < 36u || (f[0] & 0x0Cu) != 0)
        return 0; /* not a management frame with a body */
    subtype = (f[0] >> 4) & 15u;
    if (subtype != 8u && subtype != 5u)
        return 0;
    if (subtype == 5u && memcmp(f + 4, s->me, 6))
        return 0; /* another's probe response */
    if (!(n = net_find(s, f + 16)))
        return subtype == 5u;
    capinfo = (unsigned)(f[34] | f[35] << 8);
    ie = ie_find(f + 36, fr->len - 36u, 0, &len);
    if (ie && len <= 32u) {
        memcpy(n->ssid, ie, len);
        n->ssid[len] = 0;
    }
    ie = ie_find(f + 36, fr->len - 36u, 3, &len);
    n->channel = ie && len == 1u ? ie[0] : s->ch;
    security_of(n->security, sizeof n->security, capinfo, f + 36, fr->len - 36u);
    n->rssi = rssi;
    n->heard++;
    n->scan = s->scan;
    return subtype == 5u; /* a probe response to it: its ACK */
}

static void probe_req(ws_state *s, es_time_t at)
{
    uint8_t f[48];
    uint32_t n = 0;
    esk_air_frame fr;
    static const uint8_t rates[] = { 1, 8, 0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18, 0x24 };
    static const uint8_t ext[] = { 50, 4, 0x30, 0x48, 0x60, 0x6c };
    f[n++] = 0x40; /* management, probe request */
    f[n++] = 0;
    f[n++] = 0;
    f[n++] = 0;
    memset(f + n, 0xFF, 6); /* DA: broadcast */
    n += 6;
    memcpy(f + n, s->me, 6);
    n += 6;
    memset(f + n, 0xFF, 6); /* BSSID: wildcard */
    n += 6;
    f[n++] = (uint8_t)((s->seq & 0xFu) << 4);
    f[n++] = (uint8_t)((s->seq >> 4) & 0xFFu);
    s->seq = (s->seq + 1u) & 0xFFFu;
    f[n++] = 0; /* SSID: any */
    f[n++] = 0;
    memcpy(f + n, rates, sizeof rates);
    n += (uint32_t)sizeof rates;
    memcpy(f + n, ext, sizeof ext);
    n += (uint32_t)sizeof ext;
    fr.data = f;
    fr.len = n;
    fr.mhz = esk_air_wifi_mhz(s->ch);
    fr.dbm = TX_DBM;
    fr.rate_kbps = RATE_1M;
    fr.kind = ESK_AIR_WIFI;
    if (esk_air_send(s->c->e, &s->node, &fr, at, esk_air_wifi_airtime(n, RATE_1M), 0u) == ES_OK)
        s->probes++;
}

static void tune(ws_state *s, int ch)
{
    es_time_t now = esk_comp_now(s->c);
    s->ch = ch;
    if (s->active)
        esk_comp_wake_at(s->c, now + DIFS_PS, WAKE_PROBE);
    esk_comp_wake_at(s->c, now + s->dwell, WAKE_DWELL);
}

static void ws_time(esk_comp *c, uint32_t token)
{
    ws_state *s = (ws_state *)c->state;
    switch (token) {
    case WAKE_SCAN: /* a scan begins */
        s->scan++;
        tune(s, 1);
        esk_comp_wake_at(c, esk_comp_now(c) + s->period, WAKE_SCAN);
        break;
    case WAKE_PROBE:
        if (s->ch)
            probe_req(s, esk_comp_now(c));
        break;
    case WAKE_DWELL:
        if (s->ch && s->ch < s->channels) {
            tune(s, s->ch + 1);
        } else if (s->ch) {
            s->ch = 0; /* the scan is done */
            s->scans++;
        }
        break;
    default:
        break;
    }
}

static es_status ws_create(esk_comp *c)
{
    ws_state *s = (ws_state *)c->state;
    double channels = 13, dwell = 40, period = 10;
    unsigned v[6];
    int i;
    s->c = c;
    esk_comp_param_double(c, "channels", &channels);
    esk_comp_param_double(c, "dwell", &dwell);
    esk_comp_param_double(c, "period", &period);
    s->active = 1;
    esk_comp_param_bool(c, "active", &s->active);
    if (sscanf(esk_comp_param_str(c, "address", "da:a1:19:6e:2c:01"), "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2],
               &v[3], &v[4], &v[5]) != 6 ||
        (v[0] & 1u)) {
        esk_comp_diag(c, ES_SEV_ERROR, "wifi_scanner.address", "an individual MAC address: its first octet is even");
        return ES_ESCHEMA;
    }
    for (i = 0; i < 6; i++)
        s->me[i] = (uint8_t)v[i];
    if (channels < 1 || channels > 14 || dwell <= 0 || period * 1000.0 < dwell * channels) {
        esk_comp_diag(c, ES_SEV_ERROR, "wifi_scanner.period", "channels 1 to 14, and a period that holds every dwell");
        return ES_ESCHEMA;
    }
    s->channels = (int)channels;
    s->dwell = (es_time_t)(dwell * (double)ES_PS_PER_MS);
    s->period = (es_time_t)(period * 1000.0 * (double)ES_PS_PER_MS);
    s->node.ctx = s;
    s->node.comp = c;
    s->node.kind = ESK_AIR_WIFI;
    s->node.tuned = ws_tuned;
    s->node.rx = ws_rx;
    s->node.tx_done = NULL;
    if (esk_air_attach(c->e, &s->node) != ES_OK)
        return ES_ENOMEM;
    s->attached = 1;
    return ES_OK;
}

static void ws_destroy(esk_comp *c)
{
    ws_state *s = (ws_state *)c->state;
    if (s && s->attached)
        esk_air_detach(c->e, &s->node);
    if (s)
        s->attached = 0;
}

static void ws_reset(esk_comp *c)
{
    ws_state *s = (ws_state *)c->state;
    memset(s->net, 0, sizeof s->net);
    s->ch = 0;
    s->scan = 0;
    s->scans = s->probes = 0;
    s->seq = 0;
    esk_comp_wake_at(c, 0, WAKE_SCAN);
}

static void ws_observe(esk_comp *c, esk_json *out)
{
    ws_state *s = (ws_state *)c->state;
    const ws_net *order[MAX_NET];
    size_t n = 0;
    int i, j, count = 0;
    for (i = 0; i < MAX_NET; i++) {
        const ws_net *w = &s->net[i];
        if (!w->used || w->scan + 1u < s->scan) /* gone for a whole scan */
            continue;
        /* the strongest first */
        for (j = count; j > 0 && order[j - 1]->rssi < w->rssi; j--)
            order[j] = order[j - 1];
        order[j] = w;
        count++;
    }
    s->list[0] = 0;
    for (i = 0; i < count; i++) {
        const ws_net *w = order[i];
        int k = snprintf(s->list + n, LIST_CAP - n, "%02x:%02x:%02x:%02x:%02x:%02x ch %d %ddBm %s \"%s\"%s\n",
                         w->bssid[0], w->bssid[1], w->bssid[2], w->bssid[3], w->bssid[4], w->bssid[5], w->channel,
                         w->rssi, w->security, w->ssid, w->ssid[0] ? "" : " hidden");
        if (k < 0 || (size_t)k >= LIST_CAP - n)
            break;
        n += (size_t)k;
    }
    esk_json_set_integer(c->e->a, out, "networks", count);
    esk_json_set_integer(c->e->a, out, "scans", (int64_t)s->scans);
    esk_json_set_integer(c->e->a, out, "probes", (int64_t)s->probes);
    esk_json_set_integer(c->e->a, out, "channel", s->ch);
    esk_json_set_string(c->e->a, out, "list", s->list);
}

const esk_model_ops esk_wifi_scanner_ops = { "wifi_scanner", sizeof(ws_state), ws_create, ws_destroy, ws_reset, NULL,
                                             ws_time, NULL, ws_observe, NULL, 0 };
