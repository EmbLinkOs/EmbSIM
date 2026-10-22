/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* wifi_ap.c -- a Wi-Fi access point on the session's air (engine/air.h;
 * docs/engineering/c3-radios.md, step 4), an 802.11b/g one as a home router
 * is seen from a station (IEEE 802.11-2020):
 *
 *   - it sends a beacon every beacon interval (100 TU, 102.4 ms, by
 *     default) at 1 Mbit/s: its timestamp (the TSF, microseconds since the
 *     session began), the interval, its capabilities (an ESS; privacy when
 *     it has a password), its SSID, rates (1, 2, 5.5 and 11 Mbit/s basic,
 *     6 to 54 Mbit/s), channel, TIM (DTIM period 1), ERP and, with a
 *     password, the RSN element of WPA2-Personal (CCMP, PSK);
 *   - it answers a probe request for its SSID or any SSID with a probe
 *     response, as a beacon without the TIM;
 *   - it authenticates a station (open system) and associates it (an
 *     association ID from 1), and forgets it on a deauthentication or
 *     disassociation;
 *   - a unicast frame for it is answered by an ACK (the air's).
 *
 * Not yet: the data path (WPA2's 4-way handshake, the network behind it:
 * DHCP, ARP, ICMP, DNS, TCP and UDP); 802.11n (HT capabilities and
 * operation); WPA3 (SAE); power save buffering (the TIM is always empty);
 * hidden SSIDs; retransmissions; contention (it sends when the air is
 * free of its own frames). */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"

#define AP_MAX_STA   8
#define AP_FRAME     512
#define TU_PS        UINT64_C(1024000000) /* a time unit, 1024 us */
#define SIFS_PS      UINT64_C(10000000)
#define DIFS_PS      UINT64_C(50000000) /* 802.11b: SIFS + 2 slots of 20 us */
#define RATE_1M      1000u
#define LOG_CAP      4096

enum { WAKE_BEACON = 1 };

typedef struct ap_sta {
    int used, assoc;
    uint8_t mac[6];
    unsigned aid;
} ap_sta;

typedef struct ap_state {
    esk_comp *c;
    esk_air_node node;
    int attached;
    char ssid[33];
    size_t ssid_len;
    int secure;
    int channel;
    uint32_t mhz;
    double dbm;
    unsigned interval_tu;
    uint8_t bssid[6];
    unsigned seq;
    es_time_t free_at; /* its last frame's end */
    ap_sta sta[AP_MAX_STA];
    uint64_t beacons, rx_frames, tx_frames, probes;
    char log[LOG_CAP];
    size_t log_len;
} ap_state;

/* ---- frames ------------------------------------------------------------------ */

static void note(ap_state *s, const uint8_t *mac, const char *what)
{
    char line[96];
    int k = snprintf(line, sizeof line, "%02x:%02x:%02x:%02x:%02x:%02x %s", mac[0], mac[1], mac[2], mac[3], mac[4],
                     mac[5], what);
    if (k <= 0)
        return;
    if ((size_t)k >= sizeof line)
        k = (int)sizeof line - 1;
    if (s->log_len + (size_t)k + 2 >= LOG_CAP)
        s->log_len = 0; /* the oldest lines go */
    memcpy(s->log + s->log_len, line, (size_t)k);
    s->log_len += (size_t)k;
    s->log[s->log_len++] = '\n';
    s->log[s->log_len] = 0;
}

static void ap_send(ap_state *s, const uint8_t *f, uint32_t len, es_time_t at)
{
    esk_engine *e = s->c->e;
    esk_air_frame fr;
    es_time_t air;
    if (!s->attached)
        return;
    if (at < s->free_at)
        at = s->free_at;
    fr.data = f;
    fr.len = len;
    fr.mhz = s->mhz;
    fr.dbm = s->dbm;
    fr.rate_kbps = RATE_1M;
    fr.kind = ESK_AIR_WIFI;
    air = esk_air_wifi_airtime(len, RATE_1M);
    if (esk_air_send(e, &s->node, &fr, at, air, 0) == ES_OK) {
        s->free_at = at + air;
        s->tx_frames++;
    }
}

/* a management frame's header: frame control, duration, the addresses, the
 * sequence number */
static uint32_t mgmt_header(ap_state *s, uint8_t *f, unsigned subtype, const uint8_t *da)
{
    f[0] = (uint8_t)(subtype << 4);
    f[1] = 0;
    f[2] = f[3] = 0;
    memcpy(f + 4, da, 6);
    memcpy(f + 10, s->bssid, 6);
    memcpy(f + 16, s->bssid, 6);
    f[22] = (uint8_t)((s->seq & 0xFu) << 4);
    f[23] = (uint8_t)((s->seq >> 4) & 0xFFu);
    s->seq = (s->seq + 1u) & 0xFFFu;
    return 24;
}

static uint32_t put_rates(uint8_t *f, uint32_t n)
{
    static const uint8_t rates[] = { 1, 8, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };
    static const uint8_t ext[] = { 50, 4, 0x30, 0x48, 0x60, 0x6c };
    memcpy(f + n, rates, sizeof rates);
    n += (uint32_t)sizeof rates;
    memcpy(f + n, ext, sizeof ext);
    return n + (uint32_t)sizeof ext;
}

/* a beacon's or probe response's body */
static uint32_t bss_body(ap_state *s, uint8_t *f, uint32_t n, int beacon)
{
    static const uint8_t rsn[] = { 48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4,
                              1, 0, 0x00, 0x0f, 0xac, 2, 0, 0 };
    uint64_t tsf = s->c->e->sched.now / UINT64_C(1000000);
    unsigned cap = 0x0001u | 0x0020u | 0x0400u; /* ESS, short preamble, short slot */
    int i;
    if (s->secure)
        cap |= 0x0010u; /* privacy */
    for (i = 0; i < 8; i++)
        f[n++] = (uint8_t)(tsf >> (8 * i));
    f[n++] = (uint8_t)s->interval_tu;
    f[n++] = (uint8_t)(s->interval_tu >> 8);
    f[n++] = (uint8_t)cap;
    f[n++] = (uint8_t)(cap >> 8);
    f[n++] = 0; /* the SSID */
    f[n++] = (uint8_t)s->ssid_len;
    memcpy(f + n, s->ssid, s->ssid_len);
    n += (uint32_t)s->ssid_len;
    n = put_rates(f, n);
    f[n++] = 3; /* DS parameter set: the channel */
    f[n++] = 1;
    f[n++] = (uint8_t)s->channel;
    if (beacon) { /* TIM: DTIM count 0, period 1, nothing buffered */
        static const uint8_t tim[] = { 5, 4, 0, 1, 0, 0 };
        memcpy(f + n, tim, sizeof tim);
        n += (uint32_t)sizeof tim;
    }
    f[n++] = 42; /* ERP: no protection */
    f[n++] = 1;
    f[n++] = 0;
    if (s->secure) {
        memcpy(f + n, rsn, sizeof rsn);
        n += (uint32_t)sizeof rsn;
    }
    return n;
}

static void beacon(ap_state *s)
{
    static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t f[AP_FRAME];
    uint32_t n = mgmt_header(s, f, 8, bcast);
    n = bss_body(s, f, n, 1);
    ap_send(s, f, n, s->c->e->sched.now);
    s->beacons++;
}

static ap_sta *sta_find(ap_state *s, const uint8_t *mac, int add)
{
    int i;
    ap_sta *free_one = NULL;
    for (i = 0; i < AP_MAX_STA; i++) {
        if (s->sta[i].used && !memcmp(s->sta[i].mac, mac, 6))
            return &s->sta[i];
        if (!s->sta[i].used && !free_one)
            free_one = &s->sta[i];
    }
    if (!add || !free_one)
        return NULL;
    memset(free_one, 0, sizeof *free_one);
    free_one->used = 1;
    memcpy(free_one->mac, mac, 6);
    return free_one;
}

static unsigned next_aid(ap_state *s)
{
    unsigned aid;
    int i, taken;
    for (aid = 1; aid < 2008u; aid++) {
        for (taken = 0, i = 0; i < AP_MAX_STA; i++)
            if (s->sta[i].used && s->sta[i].assoc && s->sta[i].aid == aid)
                taken = 1;
        if (!taken)
            return aid;
    }
    return 0;
}

/* an element's body in a frame's elements, or NULL */
static const uint8_t *ie_find(const uint8_t *ies, uint32_t len, unsigned id, unsigned *ie_len)
{
    uint32_t i = 0;
    while (i + 2u <= len && i + 2u + ies[i + 1] <= len) {
        if (ies[i] == id) {
            *ie_len = ies[i + 1];
            return ies + i + 2;
        }
        i += 2u + ies[i + 1];
    }
    return NULL;
}

static void on_mgmt(ap_state *s, const esk_air_frame *fr, es_time_t now)
{
    const uint8_t *f = fr->data, *sa = f + 10;
    unsigned subtype = (f[0] >> 4) & 15u;
    uint8_t out[AP_FRAME];
    uint32_t n;
    ap_sta *st;
    es_time_t reply = now + SIFS_PS + 304000000u + DIFS_PS; /* after its ACK */
    if (subtype == 4) { /* a probe request */
        unsigned l = 0;
        const uint8_t *ssid = fr->len > 24 ? ie_find(f + 24, fr->len - 24, 0, &l) : NULL;
        if (ssid && l && (l != s->ssid_len || memcmp(ssid, s->ssid, l)))
            return; /* another network's */
        s->probes++;
        n = mgmt_header(s, out, 5, sa);
        n = bss_body(s, out, n, 0);
        ap_send(s, out, n, now + DIFS_PS);
        return;
    }
    if (memcmp(f + 4, s->bssid, 6))
        return; /* not for it */
    switch (subtype) {
    case 11: /* authentication: open system, transaction 1 */
        if (fr->len < 30)
            return;
        st = sta_find(s, sa, 1);
        n = mgmt_header(s, out, 11, sa);
        out[n++] = 0; /* open system */
        out[n++] = 0;
        out[n++] = 2; /* transaction 2 */
        out[n++] = 0;
        out[n++] = (uint8_t)(st && f[24] == 0 && f[25] == 0 ? 0 : (st ? 13 : 17)); /* success, unsupported algorithm, too many */
        out[n++] = 0;
        if (st)
            st->assoc = 0;
        ap_send(s, out, n, reply);
        if (st)
            note(s, sa, "authenticated");
        return;
    case 0: /* association request */
    case 2: { /* reassociation request */
        unsigned status = 0, aid = 0;
        st = sta_find(s, sa, 0);
        if (!st)
            status = 1; /* not authenticated: unspecified failure */
        else {
            aid = st->assoc ? st->aid : next_aid(s);
            st->assoc = 1;
            st->aid = aid;
        }
        n = mgmt_header(s, out, subtype == 0 ? 1u : 3u, sa);
        {
            unsigned cap = 0x0001u | 0x0020u | 0x0400u | (s->secure ? 0x0010u : 0u);
            out[n++] = (uint8_t)cap;
            out[n++] = (uint8_t)(cap >> 8);
        }
        out[n++] = (uint8_t)status;
        out[n++] = 0;
        out[n++] = (uint8_t)aid;
        out[n++] = (uint8_t)(0xC0u | (aid >> 8));
        n = put_rates(out, n);
        ap_send(s, out, n, reply);
        if (st) {
            char what[32];
            snprintf(what, sizeof what, "associated, aid %u", aid);
            note(s, sa, what);
        }
        return;
    }
    case 10: /* disassociation */
    case 12: /* deauthentication */
        st = sta_find(s, sa, 0);
        if (st) {
            st->used = 0;
            note(s, sa, subtype == 10 ? "disassociated" : "deauthenticated");
        }
        return;
    default:
        return;
    }
}

/* ---- the air ------------------------------------------------------------------ */

static uint32_t ap_tuned(void *ctx, int kind)
{
    ap_state *s = (ap_state *)ctx;
    return kind == ESK_AIR_WIFI ? s->mhz : 0u;
}

static int ap_rx(void *ctx, const esk_air_frame *fr, int rssi, es_time_t at)
{
    ap_state *s = (ap_state *)ctx;
    const uint8_t *f = fr->data;
    unsigned type;
    (void)rssi;
    if (fr->len < 24)
        return 0;
    type = (f[0] >> 2) & 3u;
    if (memcmp(f + 4, s->bssid, 6) && !(f[4] & 1u))
        return 0; /* another's unicast frame */
    s->rx_frames++;
    if (type == 0)
        on_mgmt(s, fr, at);
    return !(f[4] & 1u);
}

/* ---- the component ---------------------------------------------------------------- */

static int parse_mac(const char *t, uint8_t *mac)
{
    unsigned v[6];
    int i;
    if (!t || sscanf(t, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
        return 0;
    for (i = 0; i < 6; i++) {
        if (v[i] > 0xFFu)
            return 0;
        mac[i] = (uint8_t)v[i];
    }
    return 1;
}

static es_status ap_create(esk_comp *c)
{
    ap_state *s = (ap_state *)c->state;
    const char *ssid = esk_comp_param_str(c, "ssid", "EmbSIM");
    const char *pass = esk_comp_param_str(c, "password", "");
    double ch = 6, dbm = 20, interval = 100;
    s->c = c;
    s->ssid_len = strlen(ssid);
    if (s->ssid_len > 32) {
        esk_comp_diag(c, ES_SEV_ERROR, "wifi_ap.ssid", "an SSID is at most 32 bytes");
        return ES_ESCHEMA;
    }
    memcpy(s->ssid, ssid, s->ssid_len);
    s->secure = pass[0] != 0;
    if (s->secure && (strlen(pass) < 8 || strlen(pass) > 63)) {
        esk_comp_diag(c, ES_SEV_ERROR, "wifi_ap.password", "a WPA2 passphrase is 8 to 63 characters");
        return ES_ESCHEMA;
    }
    esk_comp_param_double(c, "channel", &ch);
    esk_comp_param_double(c, "tx_dbm", &dbm);
    esk_comp_param_double(c, "beacon_interval", &interval);
    s->channel = (int)ch;
    s->mhz = esk_air_wifi_mhz(s->channel);
    if (!s->mhz) {
        esk_comp_diag(c, ES_SEV_ERROR, "wifi_ap.channel", "the 2.4 GHz channels are 1 to 14");
        return ES_ESCHEMA;
    }
    s->dbm = dbm;
    s->interval_tu = interval >= 1 && interval <= 65535 ? (unsigned)interval : 100u;
    if (!parse_mac(esk_comp_param_str(c, "bssid", ""), s->bssid)) {
        /* a locally administered address from the instance's name */
        uint32_t h = 2166136261u;
        const char *p;
        for (p = c->id; *p; p++)
            h = (h ^ (uint8_t)*p) * 16777619u;
        s->bssid[0] = 0x02;
        s->bssid[1] = 0xe5;
        s->bssid[2] = (uint8_t)(h >> 24);
        s->bssid[3] = (uint8_t)(h >> 16);
        s->bssid[4] = (uint8_t)(h >> 8);
        s->bssid[5] = (uint8_t)h;
    }
    s->node.ctx = s;
    s->node.comp = c;
    s->node.kind = ESK_AIR_WIFI;
    s->node.tuned = ap_tuned;
    s->node.rx = ap_rx;
    s->node.tx_done = NULL;
    if (esk_air_attach(c->e, &s->node) != ES_OK)
        return ES_ENOMEM;
    s->attached = 1;
    return ES_OK;
}

static void ap_destroy(esk_comp *c)
{
    ap_state *s = (ap_state *)c->state;
    if (s && s->attached)
        esk_air_detach(c->e, &s->node);
    if (s)
        s->attached = 0;
}

static void ap_reset(esk_comp *c)
{
    ap_state *s = (ap_state *)c->state;
    es_time_t first = (es_time_t)(s->bssid[5] % 64u + 1u) * TU_PS; /* it was on before the session */
    memset(s->sta, 0, sizeof s->sta);
    s->seq = 0;
    s->free_at = 0;
    s->beacons = s->rx_frames = s->tx_frames = s->probes = 0;
    s->log_len = 0;
    s->log[0] = 0;
    esk_comp_wake_at(c, first, WAKE_BEACON);
}

static void ap_time(esk_comp *c, uint32_t token)
{
    ap_state *s = (ap_state *)c->state;
    if (token != WAKE_BEACON)
        return;
    beacon(s);
    esk_comp_wake_at(c, esk_comp_now(c) + (es_time_t)s->interval_tu * TU_PS, WAKE_BEACON);
}

static void ap_observe(esk_comp *c, esk_json *out)
{
    ap_state *s = (ap_state *)c->state;
    char mac[18];
    int i, n = 0;
    for (i = 0; i < AP_MAX_STA; i++)
        n += s->sta[i].used && s->sta[i].assoc;
    snprintf(mac, sizeof mac, "%02x:%02x:%02x:%02x:%02x:%02x", s->bssid[0], s->bssid[1], s->bssid[2], s->bssid[3],
             s->bssid[4], s->bssid[5]);
    esk_json_set_string(c->e->a, out, "mac", mac);
    esk_json_set_integer(c->e->a, out, "stations", n);
    esk_json_set_integer(c->e->a, out, "beacons", (int64_t)s->beacons);
    esk_json_set_integer(c->e->a, out, "probe_responses", (int64_t)s->probes);
    esk_json_set_integer(c->e->a, out, "rx_frames", (int64_t)s->rx_frames);
    esk_json_set_integer(c->e->a, out, "tx_frames", (int64_t)s->tx_frames);
    esk_json_set_string(c->e->a, out, "log", s->log);
}

const esk_model_ops esk_wifi_ap_ops = { "wifi_access_point", sizeof(ap_state), ap_create, ap_destroy, ap_reset, NULL,
                                        ap_time, NULL, ap_observe, NULL, 0 };
