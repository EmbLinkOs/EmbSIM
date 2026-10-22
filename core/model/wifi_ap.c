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
 *   - a unicast frame for it is answered by an ACK (the air's);
 *   - data frames (802.2 LLC/SNAP) to and from its stations: the router
 *     behind it at `ip` (192.168.1.1/24 by default, its MAC the BSSID)
 *     answers ARP for its address and pings, and leases each station an
 *     address by DHCP (RFC 2131: DISCOVER, OFFER, REQUEST, ACK or NAK;
 *     the mask, router, DNS server and a day's lease); frames between its
 *     stations, and broadcasts, are relayed;
 *   - power save: a station's power management bit is followed; frames for
 *     a dozing station are kept and announced in the beacon's TIM, sent on
 *     its PS-Poll (one, with More Data while more wait) or when it wakes;
 *     group frames wait for the DTIM beacon (every beacon) while any
 *     station dozes.
 *
 * Unicast data goes at 54 Mbit/s, management and group frames at 1 Mbit/s.
 * Not yet: WPA2's 4-way handshake and CCMP (a network with a password
 * associates but passes no data); the network beyond the router (DNS,
 * TCP and UDP to the host); 802.11n (HT capabilities and operation); WPA3
 * (SAE); hidden SSIDs; retransmissions; contention (it sends when the air
 * is free of its own frames). */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"

#define AP_MAX_STA   8
#define AP_FRAME     1600
#define AP_PS_FRAMES 32      /* frames kept for dozing stations */
#define RATE_DATA    54000u
#define TU_PS        UINT64_C(1024000000) /* a time unit, 1024 us */
#define SIFS_PS      UINT64_C(10000000)
#define DIFS_PS      UINT64_C(50000000) /* 802.11b: SIFS + 2 slots of 20 us */
#define RATE_1M      1000u
#define LOG_CAP      4096

enum { WAKE_BEACON = 1 };

typedef struct ap_sta {
    int used, assoc;
    int ps; /* dozing: its last frame's power management bit */
    uint8_t mac[6];
    unsigned aid;
    int leased;
} ap_sta;

/* a frame kept for a dozing station (sta >= 0) or for the DTIM (-1) */
typedef struct ap_kept {
    int used, sta;
    uint64_t order;
    uint32_t len;
    uint8_t f[AP_FRAME];
} ap_kept;

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
    ap_kept kept[AP_PS_FRAMES];
    uint64_t kept_order;
    uint8_t ip[4], mask[4]; /* the router's */
    unsigned ip_id;
    uint64_t beacons, rx_frames, tx_frames, probes;
    char log[LOG_CAP];
    size_t log_len;
} ap_state;

/* ---- frames ------------------------------------------------------------------ */

static void release(ap_state *s, int sta, int max, es_time_t at);
static int sta_index(ap_state *s, const ap_sta *st) { return (int)(st - s->sta); }

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

static void ap_send_at(ap_state *s, const uint8_t *f, uint32_t len, es_time_t at, uint32_t kbps)
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
    fr.rate_kbps = kbps;
    fr.kind = ESK_AIR_WIFI;
    air = esk_air_wifi_airtime(len, kbps);
    if (esk_air_send(e, &s->node, &fr, at, air, 0) == ES_OK) {
        s->free_at = at + air;
        s->tx_frames++;
    }
}

static void ap_send(ap_state *s, const uint8_t *f, uint32_t len, es_time_t at) { ap_send_at(s, f, len, at, RATE_1M); }

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
    if (beacon) { /* TIM: DTIM count 0, period 1; the AIDs with frames kept, group frames */
        unsigned bitmap = 0, group = 0;
        for (i = 0; i < AP_PS_FRAMES; i++)
            if (s->kept[i].used) {
                if (s->kept[i].sta < 0)
                    group = 1;
                else
                    bitmap |= 1u << (s->sta[s->kept[i].sta].aid & 15u);
            }
        f[n++] = 5;
        f[n++] = bitmap > 0xFFu ? 5 : 4;
        f[n++] = 0;
        f[n++] = 1;
        f[n++] = (uint8_t)group;
        f[n++] = (uint8_t)bitmap;
        if (bitmap > 0xFFu)
            f[n++] = (uint8_t)(bitmap >> 8);
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
    release(s, -1, -1, s->c->e->sched.now); /* every beacon is a DTIM one */
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
            int i;
            for (i = 0; i < AP_PS_FRAMES; i++)
                if (s->kept[i].used && s->kept[i].sta == sta_index(s, st))
                    s->kept[i].used = 0;
            st->used = 0;
            note(s, sa, subtype == 10 ? "disassociated" : "deauthenticated");
        }
        return;
    default:
        return;
    }
}

/* ---- the data path ------------------------------------------------------------ */


/* a frame kept for a dozing station, or for after the DTIM beacon */
static void keep(ap_state *s, int sta, const uint8_t *f, uint32_t len)
{
    int i, oldest = -1;
    for (i = 0; i < AP_PS_FRAMES; i++) {
        if (!s->kept[i].used)
            break;
        if (oldest < 0 || s->kept[i].order < s->kept[oldest].order)
            oldest = i;
    }
    if (i == AP_PS_FRAMES)
        i = oldest; /* full: the oldest is dropped, as an AP's buffer does */
    s->kept[i].used = 1;
    s->kept[i].sta = sta;
    s->kept[i].order = s->kept_order++;
    s->kept[i].len = len;
    memcpy(s->kept[i].f, f, len);
}

/* the oldest frame kept for `sta`, or -1 */
static int kept_next(ap_state *s, int sta)
{
    int i, best = -1;
    for (i = 0; i < AP_PS_FRAMES; i++)
        if (s->kept[i].used && s->kept[i].sta == sta && (best < 0 || s->kept[i].order < s->kept[best].order))
            best = i;
    return best;
}

/* the frames kept for `sta` go out, `max` of them (-1: all), with More Data
 * on each but the last */
static void release(ap_state *s, int sta, int max, es_time_t at)
{
    int k;
    while (max-- != 0 && (k = kept_next(s, sta)) >= 0) {
        ap_kept *kp = &s->kept[k];
        kp->used = 0;
        if (kept_next(s, sta) >= 0)
            kp->f[1] |= 0x20u; /* More Data */
        ap_send_at(s, kp->f, kp->len, at, sta < 0 || (kp->f[4] & 1u) ? RATE_1M : RATE_DATA);
    }
}

static int any_dozing(ap_state *s)
{
    int i;
    for (i = 0; i < AP_MAX_STA; i++)
        if (s->sta[i].used && s->sta[i].assoc && s->sta[i].ps)
            return 1;
    return 0;
}

/* a data frame from the distribution system to `da` (a station, or a group),
 * from `sa`, with an LLC/SNAP header and its ethertype */
static void data_out(ap_state *s, const uint8_t *da, const uint8_t *sa, unsigned ethertype, const uint8_t *payload,
                     uint32_t plen, es_time_t at)
{
    uint8_t f[AP_FRAME];
    ap_sta *st = NULL;
    uint32_t n;
    if (plen + 32u > AP_FRAME)
        return;
    if (!(da[0] & 1u)) {
        st = sta_find(s, da, 0);
        if (!st || !st->assoc)
            return; /* not one of its stations */
    }
    f[0] = 0x08; /* data */
    f[1] = 0x02; /* from DS */
    f[2] = f[3] = 0;
    memcpy(f + 4, da, 6);
    memcpy(f + 10, s->bssid, 6);
    memcpy(f + 16, sa, 6);
    f[22] = (uint8_t)((s->seq & 0xFu) << 4);
    f[23] = (uint8_t)((s->seq >> 4) & 0xFFu);
    s->seq = (s->seq + 1u) & 0xFFFu;
    n = 24;
    f[n++] = 0xAA;
    f[n++] = 0xAA;
    f[n++] = 0x03;
    f[n++] = 0; /* the OUI: an Ethernet type follows */
    f[n++] = 0;
    f[n++] = 0;
    f[n++] = (uint8_t)(ethertype >> 8);
    f[n++] = (uint8_t)ethertype;
    memcpy(f + n, payload, plen);
    n += plen;
    if (st && st->ps)
        keep(s, sta_index(s, st), f, n);
    else if (!st && any_dozing(s))
        keep(s, -1, f, n);
    else
        ap_send_at(s, f, n, at, st ? RATE_DATA : RATE_1M);
}

static uint16_t csum(const uint8_t *p, uint32_t n, uint32_t sum)
{
    uint32_t i;
    for (i = 0; i + 1 < n; i += 2)
        sum += (uint32_t)p[i] << 8 | p[i + 1];
    if (n & 1u)
        sum += (uint32_t)p[n - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

/* an IPv4 packet from the router to `dst_ip` at `dst_mac` */
static void ip_out(ap_state *s, const uint8_t *dst_mac, const uint8_t *dst_ip, unsigned proto, const uint8_t *body,
                   uint32_t blen, es_time_t at)
{
    uint8_t p[AP_FRAME];
    uint32_t n = 20u + blen;
    uint16_t c;
    if (n > AP_FRAME - 40u)
        return;
    memset(p, 0, 20);
    p[0] = 0x45;
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
    p[4] = (uint8_t)(s->ip_id >> 8);
    p[5] = (uint8_t)s->ip_id;
    s->ip_id++;
    p[8] = 64; /* TTL */
    p[9] = (uint8_t)proto;
    memcpy(p + 12, s->ip, 4);
    memcpy(p + 16, dst_ip, 4);
    c = csum(p, 20, 0);
    p[10] = (uint8_t)(c >> 8);
    p[11] = (uint8_t)c;
    memcpy(p + 20, body, blen);
    if (proto == 17) { /* UDP: the checksum over the pseudo-header */
        uint8_t ph[12];
        uint32_t sum = 0, i;
        memcpy(ph, s->ip, 4);
        memcpy(ph + 4, dst_ip, 4);
        ph[8] = 0;
        ph[9] = 17;
        ph[10] = (uint8_t)(blen >> 8);
        ph[11] = (uint8_t)blen;
        for (i = 0; i < 12; i += 2)
            sum += (uint32_t)ph[i] << 8 | ph[i + 1];
        p[26] = p[27] = 0;
        c = csum(p + 20, blen, sum);
        if (!c)
            c = 0xFFFFu;
        p[26] = (uint8_t)(c >> 8);
        p[27] = (uint8_t)c;
    }
    data_out(s, dst_mac, s->bssid, 0x0800, p, n, at);
}

static void lease_ip(ap_state *s, const ap_sta *st, uint8_t *ip)
{
    memcpy(ip, s->ip, 4);
    ip[3] = (uint8_t)(100 + sta_index(s, st));
}

/* a DHCP message from a station (RFC 2131, 2132) */
static void dhcp_in(ap_state *s, ap_sta *st, const uint8_t *m, uint32_t len, es_time_t at)
{
    static const uint8_t cookie[4] = { 99, 130, 83, 99 };
    uint8_t out[548], yi[4], req[4] = { 0, 0, 0, 0 }, udp[556];
    unsigned type = 0, reply;
    uint32_t i, n;
    if (len < 240 || m[0] != 1 || m[1] != 1 || m[2] != 6 || memcmp(m + 236, cookie, 4) || memcmp(m + 28, st->mac, 6))
        return;
    for (i = 240; i + 1 < len && m[i] != 255;) {
        if (m[i] == 0) {
            i++;
            continue;
        }
        if (i + 2u + m[i + 1] > len)
            break;
        if (m[i] == 53 && m[i + 1] >= 1)
            type = m[i + 2];
        if (m[i] == 50 && m[i + 1] == 4)
            memcpy(req, m + i + 2, 4);
        i += 2u + m[i + 1];
    }
    lease_ip(s, st, yi);
    if (type == 1)
        reply = 2; /* DISCOVER: OFFER */
    else if (type == 3) {
        /* REQUEST: its address in the option (selecting, init-reboot) or
         * ciaddr (renewing) must be the one leased */
        const uint8_t *want = req[0] ? req : m + 12;
        reply = memcmp(want, yi, 4) ? 6u : 5u; /* NAK or ACK */
    } else if (type == 4 || type == 7) { /* DECLINE, RELEASE */
        st->leased = 0;
        return;
    } else
        return;
    memset(out, 0, sizeof out);
    out[0] = 2;
    out[1] = 1;
    out[2] = 6;
    memcpy(out + 4, m + 4, 4);   /* xid */
    memcpy(out + 10, m + 10, 2); /* flags */
    if (reply != 6) {
        memcpy(out + 12, m + 12, 4);
        memcpy(out + 16, yi, 4);
    }
    memcpy(out + 24, m + 24, 4); /* giaddr */
    memcpy(out + 28, m + 28, 16);
    memcpy(out + 236, cookie, 4);
    n = 240;
    out[n++] = 53;
    out[n++] = 1;
    out[n++] = (uint8_t)reply;
    out[n++] = 54;
    out[n++] = 4;
    memcpy(out + n, s->ip, 4);
    n += 4;
    if (reply != 6) {
        static const uint8_t lease[] = { 51, 4, 0, 1, 0x51, 0x80, 58, 4, 0, 0, 0xA8, 0xC0, 59, 4, 0, 1, 0x27, 0x8C };
        memcpy(out + n, lease, sizeof lease); /* 86400 s, T1 43200 s, T2 75600 s */
        n += (uint32_t)sizeof lease;
        out[n++] = 1;
        out[n++] = 4;
        memcpy(out + n, s->mask, 4);
        n += 4;
        out[n++] = 28; /* the broadcast address */
        out[n++] = 4;
        for (i = 0; i < 4; i++)
            out[n++] = (uint8_t)(s->ip[i] | ~s->mask[i]);
        out[n++] = 3;
        out[n++] = 4;
        memcpy(out + n, s->ip, 4);
        n += 4;
        out[n++] = 6;
        out[n++] = 4;
        memcpy(out + n, s->ip, 4);
        n += 4;
    }
    out[n++] = 255;
    if (n < 300)
        n = 300; /* BOOTP's minimum */
    udp[0] = 0;
    udp[1] = 67;
    udp[2] = 0;
    udp[3] = 68;
    udp[4] = (uint8_t)((n + 8u) >> 8);
    udp[5] = (uint8_t)(n + 8u);
    udp[6] = udp[7] = 0;
    memcpy(udp + 8, out, n);
    {
        static const uint8_t all[4] = { 255, 255, 255, 255 };
        ip_out(s, st->mac, all, 17, udp, n + 8u, at);
    }
    if (reply == 5 && !st->leased) {
        char what[40];
        st->leased = 1;
        snprintf(what, sizeof what, "leased %u.%u.%u.%u", yi[0], yi[1], yi[2], yi[3]);
        note(s, st->mac, what);
    }
}

/* an IPv4 packet for the router */
static void ip_in(ap_state *s, ap_sta *st, const uint8_t *p, uint32_t len, es_time_t at)
{
    uint32_t hl, tot;
    static const uint8_t all[4] = { 255, 255, 255, 255 };
    if (len < 20 || (p[0] >> 4) != 4)
        return;
    hl = (p[0] & 15u) * 4u;
    tot = (uint32_t)p[2] << 8 | p[3];
    if (hl < 20 || tot < hl || tot > len || csum(p, hl, 0) != 0)
        return;
    if (p[9] == 17 && tot >= hl + 8u) { /* UDP */
        const uint8_t *u = p + hl;
        unsigned dport = (unsigned)u[2] << 8 | u[3];
        if (dport == 67 && (!memcmp(p + 16, all, 4) || !memcmp(p + 16, s->ip, 4)))
            dhcp_in(s, st, u + 8, tot - hl - 8u, at);
        return;
    }
    if (p[9] == 1 && !memcmp(p + 16, s->ip, 4) && tot >= hl + 8u && p[hl] == 8) { /* an echo request */
        uint8_t icmp[AP_FRAME];
        uint32_t n = tot - hl;
        uint16_t c;
        if (n > sizeof icmp - 64u)
            return;
        memcpy(icmp, p + hl, n);
        icmp[0] = 0; /* echo reply */
        icmp[2] = icmp[3] = 0;
        c = csum(icmp, n, 0);
        icmp[2] = (uint8_t)(c >> 8);
        icmp[3] = (uint8_t)c;
        ip_out(s, st->mac, p + 12, 1, icmp, n, at);
    }
}

/* an ARP request for the router's address is answered with its MAC */
static void arp_in(ap_state *s, ap_sta *st, const uint8_t *a, uint32_t len, es_time_t at)
{
    uint8_t r[28];
    if (len < 28 || a[0] != 0 || a[1] != 1 || a[2] != 8 || a[3] != 0 || a[4] != 6 || a[5] != 4 || a[7] != 1)
        return;
    if (memcmp(a + 24, s->ip, 4))
        return;
    memcpy(r, a, 6);
    r[6] = 0;
    r[7] = 2; /* a reply */
    memcpy(r + 8, s->bssid, 6);
    memcpy(r + 14, s->ip, 4);
    memcpy(r + 18, a + 8, 10); /* the asker's MAC and address */
    data_out(s, st->mac, s->bssid, 0x0806, r, sizeof r, at);
}

/* a data frame from a station to the distribution system */
static void on_data(ap_state *s, ap_sta *st, const uint8_t *f, uint32_t len, es_time_t at)
{
    unsigned subtype = (f[0] >> 4) & 15u, ethertype;
    uint32_t hdr = (subtype & 8u) ? 26u : 24u; /* QoS data has its control field */
    const uint8_t *da = f + 16, *body;
    uint32_t blen;
    if ((f[1] & 3u) != 1u || (subtype & 4u))
        return; /* not to the DS, or a null frame */
    if (f[1] & 0x40u)
        return; /* protected: no keys yet (WPA2's handshake is not modelled) */
    if (len < hdr + 8u || f[hdr] != 0xAA || f[hdr + 1] != 0xAA || f[hdr + 2] != 0x03)
        return;
    ethertype = (unsigned)f[hdr + 6] << 8 | f[hdr + 7];
    body = f + hdr + 8;
    blen = len - hdr - 8u;
    if (da[0] & 1u) {
        /* a broadcast: to the router, and to the other stations */
        int i, others = 0;
        for (i = 0; i < AP_MAX_STA; i++)
            others += s->sta[i].used && s->sta[i].assoc && &s->sta[i] != st;
        if (others)
            data_out(s, da, st->mac, ethertype, body, blen, at);
    } else if (memcmp(da, s->bssid, 6)) {
        ap_sta *to = sta_find(s, da, 0);
        if (to && to->assoc)
            data_out(s, da, st->mac, ethertype, body, blen, at); /* to another station */
        return;                                                   /* beyond the router: not modelled yet */
    }
    if (ethertype == 0x0806)
        arp_in(s, st, body, blen, at);
    else if (ethertype == 0x0800)
        ip_in(s, st, body, blen, at);
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
    ap_sta *st;
    es_time_t reply = at + SIFS_PS + 304000000u + DIFS_PS; /* after its ACK */
    (void)rssi;
    if (fr->len < 10)
        return 0;
    type = (f[0] >> 2) & 3u;
    if (memcmp(f + 4, s->bssid, 6) && !(f[4] & 1u))
        return 0; /* another's unicast frame */
    s->rx_frames++;
    if (type == 1) { /* control: a PS-Poll from a dozing station gets one frame */
        if (f[0] == 0xA4 && fr->len >= 16 && (st = sta_find(s, f + 10, 0)) != NULL && st->assoc)
            release(s, sta_index(s, st), 1, reply);
        return !(f[4] & 1u);
    }
    if (fr->len < 24)
        return 0;
    st = sta_find(s, f + 10, 0);
    if (st && st->assoc && type != 3) {
        /* its power management bit: dozing, or awake for what was kept */
        int ps = (f[1] >> 4) & 1;
        if (st->ps && !ps)
            release(s, sta_index(s, st), -1, reply);
        st->ps = ps;
    }
    if (type == 0)
        on_mgmt(s, fr, at);
    else if (type == 2 && st && st->assoc)
        on_data(s, st, f, fr->len, reply);
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
    int i;
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
    {
        unsigned a[4];
        const char *ip = esk_comp_param_str(c, "ip", "192.168.1.1");
        if (sscanf(ip, "%u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3]) != 4 || a[0] > 255 || a[1] > 255 || a[2] > 255 ||
            a[3] > 255) {
            esk_comp_diag(c, ES_SEV_ERROR, "wifi_ap.ip", "the router's address is a dotted IPv4 one");
            return ES_ESCHEMA;
        }
        for (i = 0; i < 4; i++)
            s->ip[i] = (uint8_t)a[i];
        s->mask[0] = s->mask[1] = s->mask[2] = 255; /* a /24 */
        s->mask[3] = 0;
    }
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
    memset(s->kept, 0, sizeof s->kept);
    s->kept_order = 0;
    s->ip_id = 0;
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
