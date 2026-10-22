/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ble_central.c -- a phone with a GATT browser (nRF Connect, LightBlue) on
 * the session's air (engine/air.h; docs/engineering/c3-radios.md, step 6):
 * it scans, connects to the device it is told to and lists its GATT
 * database, as such an app shows it (Bluetooth Core 5.4):
 *
 *   - scanning: passive, channels 37, 38 and 39 in turn, 30 ms each; the
 *     first connectable advertiser whose name (or address) is `target`
 *     receives a CONNECT_IND T_IFS (150 us) after its ADV_IND (Vol 6 Part B
 *     2.3.3.1, 4.4.4): the phone's random static address, an access
 *     address, a CRC initial value, a 2.5 ms transmit window 1.25 ms after
 *     it, the connection interval, latency 0, the supervision timeout, all
 *     37 data channels and a hop increment, channel selection algorithm #1;
 *   - the connection, as its master (central) (4.5): a connection event
 *     every interval on the channel algorithm #1 gives (4.5.8.2), its first
 *     packet at the anchor, the slave's answer T_IFS after it, more while
 *     either has more data; the sequence numbers acknowledge and resend
 *     (4.5.9); 27-byte payloads, L2CAP's frames split and joined (Vol 3 Part
 *     A); the supervision timeout ends it; when nothing is heard from the
 *     slave in its first six intervals the connection failed to be
 *     established (4.5.2, 0x3E) and it scans and connects again;
 *   - the link layer's control procedures the slave starts answered (Vol 6
 *     Part B 5.1): the feature exchange, the version, the data length (27
 *     bytes kept), LL_UNKNOWN_RSP to those it does not do (the PHY update,
 *     the connection parameters request), LL_TERMINATE_IND;
 *   - GATT as a client (Vol 3 Part G 4): the MTU exchange, the primary
 *     services discovered by Read By Group Type, each one's characteristics
 *     by Read By Type, the readable ones read (listed as text when they are
 *     two or more printable bytes, otherwise in hex); the server's requests
 *     answered (its MTU exchange; others not supported), indications
 *     confirmed, notifications counted; L2CAP's connection parameter update
 *     request rejected; pairing not supported (SMP's Pairing Failed).
 *
 * Not yet: active scanning, the 2M and Coded PHYs, channel selection #2,
 * data length extension, encryption and pairing, writes and subscribing to
 * notifications, the descriptors, connection updates, slave latency. */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"

#define LOG_CAP   2048
#define GATT_CAP  3072
#define MAX_SVC   16
#define MAX_CHR   48
#define T_IFS_PS  (150u * ES_PS_PER_US)
#define SCAN_PS   (30u * ES_PS_PER_MS)
#define ADV_AA    0x8E89BED6u
#define ACCESS    0x71764129u
#define CRC_INIT  0x9A3F21u
#define HOP       7u
#define TX_DBM    0.0
#define MAX_PDU   27u
#define QUEUE     32

enum { WAKE_HOP = 1, WAKE_CONNECT, WAKE_EVENT, WAKE_SEND, WAKE_CLOSE };
enum { ST_SCANNING, ST_CONNECTING, ST_CONNECTED, ST_DISCONNECTED };
enum { G_IDLE, G_MTU, G_SERVICES, G_CHARS, G_READS, G_DONE };

typedef struct bc_svc {
    uint16_t start, end;
    uint8_t uuid[16];
    int uuid_len;
} bc_svc;

typedef struct bc_chr {
    int svc;
    uint16_t decl, value;
    uint8_t props;
    uint8_t uuid[16];
    int uuid_len;
    uint8_t data[64];
    int data_len, read;
} bc_chr;

/* an LL PDU queued: header (LLID) and payload */
typedef struct bc_pdu {
    uint8_t llid, len;
    uint8_t data[MAX_PDU];
} bc_pdu;

typedef struct bc_state {
    esk_comp *c;
    esk_air_node node;
    int attached;
    const char *target;
    uint8_t me[6];
    uint16_t interval; /* 1.25 ms units */
    uint16_t timeout;  /* 10 ms units */
    int state;
    /* scanning */
    int ch;
    uint32_t gen; /* wakes of older plans are stale */
    /* the peer */
    uint8_t peer[6];
    int peer_random;
    char peer_name[32];
    /* the connection */
    uint32_t listen_mhz;
    es_time_t listen_until;
    es_time_t anchor, last_rx, connect_at;
    int established; /* a packet heard from the slave */
    uint32_t unmapped, event;
    int sn, nesn, md_peer, in_event, sent_in_event, awaiting;
    bc_pdu q[QUEUE];
    int qh, qn;
    int version_sent;
    /* L2CAP reassembly */
    uint8_t l2[600];
    uint32_t l2_len, l2_want;
    /* GATT */
    int g, gi;
    uint16_t mtu, next;
    bc_svc svc[MAX_SVC];
    int nsvc;
    bc_chr chr[MAX_CHR];
    int nchr;
    uint64_t events, notifications;
    char log[LOG_CAP];
    char gatt[GATT_CAP];
} bc_state;

static const uint32_t ADV_MHZ[3] = { 2402u, 2426u, 2480u };

static uint32_t data_mhz(uint32_t ch) { return ch <= 10u ? 2404u + 2u * ch : 2428u + 2u * (ch - 11u); }

static void logf_(bc_state *s, const char *fmt, const char *a, unsigned long n)
{
    size_t len = strlen(s->log);
    char line[160];
    snprintf(line, sizeof line, fmt, a, n);
    if (len + strlen(line) + 2 >= LOG_CAP)
        return;
    snprintf(s->log + len, LOG_CAP - len, "%s\n", line);
}

static void wake(bc_state *s, es_time_t at, uint32_t kind) { esk_comp_wake_at(s->c, at, kind | s->gen << 4); }

static int send_pdu(bc_state *s, const uint8_t *p, uint32_t len, uint32_t mhz, es_time_t start, uint32_t aa)
{
    esk_air_frame f;
    (void)aa; /* the access address is not carried: the receivers listen by channel */
    f.data = p;
    f.len = len;
    f.mhz = mhz;
    f.dbm = TX_DBM;
    f.rate_kbps = 1000u;
    f.kind = ESK_AIR_BLE;
    return esk_air_send(s->c->e, &s->node, &f, start, esk_air_ble_airtime(len, 1000u), 0u) == ES_OK;
}

static uint32_t bc_tuned(void *ctx, int kind)
{
    bc_state *s = (bc_state *)ctx;
    if (kind != ESK_AIR_BLE)
        return 0;
    if (s->state == ST_SCANNING)
        return ADV_MHZ[s->ch];
    if (s->listen_mhz && esk_comp_now(s->c) <= s->listen_until)
        return s->listen_mhz;
    return 0;
}

/* ---- the queue of LL PDUs to send --------------------------------------------- */

static void push(bc_state *s, uint8_t llid, const uint8_t *d, uint32_t n)
{
    bc_pdu *p;
    if (s->qn == QUEUE || n > MAX_PDU)
        return;
    p = &s->q[(s->qh + s->qn++) % QUEUE];
    p->llid = llid;
    p->len = (uint8_t)n;
    memcpy(p->data, d, n);
}

/* an L2CAP frame on channel `cid`, in 27-byte fragments */
static void l2cap(bc_state *s, uint16_t cid, const uint8_t *d, uint32_t n)
{
    uint8_t f[600];
    uint32_t off;
    if (n + 4u > sizeof f)
        return;
    f[0] = (uint8_t)n;
    f[1] = (uint8_t)(n >> 8);
    f[2] = (uint8_t)cid;
    f[3] = (uint8_t)(cid >> 8);
    memcpy(f + 4, d, n);
    for (off = 0; off < n + 4u; off += MAX_PDU)
        push(s, off ? 1u : 2u, f + off, n + 4u - off < MAX_PDU ? n + 4u - off : MAX_PDU);
}

static void control(bc_state *s, const uint8_t *d, uint32_t n) { push(s, 3u, d, n); }

/* ---- GATT, the client ---------------------------------------------------------- */

static void att(bc_state *s, const uint8_t *d, uint32_t n) { l2cap(s, 4u, d, n); }

static void read_by_group(bc_state *s, uint16_t from)
{
    uint8_t r[7] = { 0x10, (uint8_t)from, (uint8_t)(from >> 8), 0xFF, 0xFF, 0x00, 0x28 };
    att(s, r, sizeof r);
}

static void read_by_type(bc_state *s, uint16_t from, uint16_t to)
{
    uint8_t r[7] = { 0x08, (uint8_t)from, (uint8_t)(from >> 8), (uint8_t)to, (uint8_t)(to >> 8), 0x03, 0x28 };
    att(s, r, sizeof r);
}

static void uuid_str(char *out, size_t cap, const uint8_t *u, int len)
{
    if (len == 2)
        snprintf(out, cap, "0x%04X", (unsigned)(u[0] | u[1] << 8));
    else
        snprintf(out, cap, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", u[15], u[14],
                 u[13], u[12], u[11], u[10], u[9], u[8], u[7], u[6], u[5], u[4], u[3], u[2], u[1], u[0]);
}

static void gatt_next(bc_state *s);

static void list_gatt(bc_state *s)
{
    size_t n = 0;
    int i, j;
    char u[40];
    s->gatt[0] = 0;
    for (i = 0; i < s->nsvc; i++) {
        uuid_str(u, sizeof u, s->svc[i].uuid, s->svc[i].uuid_len);
        n += (size_t)snprintf(s->gatt + n, GATT_CAP - n, "service %s handles %u-%u\n", u, s->svc[i].start,
                              s->svc[i].end);
        if (n >= GATT_CAP)
            break;
        for (j = 0; j < s->nchr; j++) {
            const bc_chr *ch = &s->chr[j];
            char props[64];
            int k, printable = 1;
            if (ch->svc != i)
                continue;
            uuid_str(u, sizeof u, ch->uuid, ch->uuid_len);
            snprintf(props, sizeof props, "%s%s%s%s%s", ch->props & 0x02 ? " read" : "",
                     ch->props & 0x04 ? " write-no-response" : "", ch->props & 0x08 ? " write" : "",
                     ch->props & 0x10 ? " notify" : "", ch->props & 0x20 ? " indicate" : "");
            n += (size_t)snprintf(s->gatt + n, GATT_CAP - n, "  characteristic %s handle %u [%s ]", u, ch->value,
                                  props);
            if (n >= GATT_CAP)
                break;
            if (ch->read) {
                for (k = 0; k < ch->data_len; k++)
                    if (ch->data[k] < 0x20 || ch->data[k] > 0x7E)
                        printable = 0;
                if (printable && ch->data_len > 1) /* text; a byte in hex */
                    n += (size_t)snprintf(s->gatt + n, GATT_CAP - n, " = \"%.*s\"", ch->data_len,
                                          (const char *)ch->data);
                else {
                    n += (size_t)snprintf(s->gatt + n, GATT_CAP - n, " =");
                    for (k = 0; k < ch->data_len && n < GATT_CAP; k++)
                        n += (size_t)snprintf(s->gatt + n, GATT_CAP - n, " %02x", ch->data[k]);
                }
            }
            if (n < GATT_CAP)
                n += (size_t)snprintf(s->gatt + n, GATT_CAP - n, "\n");
            if (n >= GATT_CAP)
                break;
        }
        if (n >= GATT_CAP)
            break;
    }
    if (n >= GATT_CAP)
        s->gatt[GATT_CAP - 1] = 0;
}

/* the next step of the discovery */
static void gatt_next(bc_state *s)
{
    if (s->g == G_IDLE) {
        uint8_t r[3] = { 0x02, 247, 0 };
        s->g = G_MTU;
        att(s, r, sizeof r);
        return;
    }
    if (s->g == G_MTU) {
        s->g = G_SERVICES;
        read_by_group(s, 1);
        return;
    }
    if (s->g == G_SERVICES) {
        s->g = G_CHARS;
        s->gi = 0;
        if (s->nsvc) {
            read_by_type(s, s->svc[0].start, s->svc[0].end);
            return;
        }
    }
    if (s->g == G_CHARS) {
        s->g = G_READS;
        s->gi = -1;
    }
    if (s->g != G_READS)
        return;
    for (s->gi++; s->gi < s->nchr; s->gi++)
        if (s->chr[s->gi].props & 0x02) {
            uint8_t r[3] = { 0x0A, (uint8_t)s->chr[s->gi].value, (uint8_t)(s->chr[s->gi].value >> 8) };
            att(s, r, sizeof r);
            return;
        }
    s->g = G_DONE;
    list_gatt(s);
    logf_(s, "%sGATT database discovered: %lu services", "", (unsigned long)s->nsvc);
}

static void att_rx(bc_state *s, const uint8_t *p, uint32_t n)
{
    uint32_t i, step;
    if (!n)
        return;
    switch (p[0]) {
    case 0x01: /* an error: the end of a discovery */
        if (s->g == G_SERVICES)
            gatt_next(s);
        else if (s->g == G_CHARS) {
            if (++s->gi < s->nsvc)
                read_by_type(s, s->svc[s->gi].start, s->svc[s->gi].end);
            else
                gatt_next(s);
        } else if (s->g == G_READS)
            gatt_next(s);
        return;
    case 0x02: { /* the server's MTU exchange */
        uint8_t r[3] = { 0x03, 247, 0 };
        att(s, r, sizeof r);
        return;
    }
    case 0x03:
        if (n >= 3) {
            uint16_t m = (uint16_t)(p[1] | p[2] << 8);
            s->mtu = m < 247 ? m : 247;
        }
        if (s->g == G_MTU)
            gatt_next(s);
        return;
    case 0x11: /* Read By Group Type: services */
        if (n < 2 || (step = p[1]) < 6)
            return;
        for (i = 2; i + step <= n && s->nsvc < MAX_SVC; i += step) {
            bc_svc *v = &s->svc[s->nsvc++];
            v->start = (uint16_t)(p[i] | p[i + 1] << 8);
            v->end = (uint16_t)(p[i + 2] | p[i + 3] << 8);
            v->uuid_len = (int)step - 4;
            memcpy(v->uuid, p + i + 4, step - 4u > 16u ? 16u : step - 4u);
        }
        if (s->nsvc && s->svc[s->nsvc - 1].end != 0xFFFF && s->nsvc < MAX_SVC)
            read_by_group(s, (uint16_t)(s->svc[s->nsvc - 1].end + 1u));
        else
            gatt_next(s);
        return;
    case 0x09: { /* Read By Type: characteristic declarations */
        uint16_t last = 0;
        if (n < 2 || (step = p[1]) < 7)
            return;
        for (i = 2; i + step <= n && s->nchr < MAX_CHR; i += step) {
            bc_chr *ch = &s->chr[s->nchr++];
            memset(ch, 0, sizeof *ch);
            ch->svc = s->gi;
            ch->decl = (uint16_t)(p[i] | p[i + 1] << 8);
            ch->props = p[i + 2];
            ch->value = (uint16_t)(p[i + 3] | p[i + 4] << 8);
            ch->uuid_len = (int)step - 5;
            memcpy(ch->uuid, p + i + 5, step - 5u > 16u ? 16u : step - 5u);
            last = ch->decl;
        }
        if (last && last < s->svc[s->gi].end)
            read_by_type(s, (uint16_t)(last + 1u), s->svc[s->gi].end);
        else if (++s->gi < s->nsvc)
            read_by_type(s, s->svc[s->gi].start, s->svc[s->gi].end);
        else
            gatt_next(s);
        return;
    }
    case 0x0B: /* Read */
        if (s->g == G_READS && s->gi < s->nchr) {
            bc_chr *ch = &s->chr[s->gi];
            ch->data_len = (int)(n - 1u < sizeof ch->data ? n - 1u : sizeof ch->data);
            memcpy(ch->data, p + 1, (size_t)ch->data_len);
            ch->read = 1;
            gatt_next(s);
        }
        return;
    case 0x1B: /* a notification */
        s->notifications++;
        return;
    case 0x1D: { /* an indication: confirmed */
        uint8_t r[1] = { 0x1E };
        s->notifications++;
        att(s, r, 1);
        return;
    }
    default:
        if (!(p[0] & 0x01) && p[0] != 0x52 && p[0] != 0xD2) { /* a request it does not serve */
            uint8_t r[5] = { 0x01, p[0], 0, 0, 0x06 };
            att(s, r, sizeof r);
        }
        return;
    }
}

static void l2cap_rx(bc_state *s, const uint8_t *f, uint32_t n)
{
    uint16_t cid = (uint16_t)(f[2] | f[3] << 8);
    const uint8_t *p = f + 4;
    n -= 4u;
    if (cid == 4u)
        att_rx(s, p, n);
    else if (cid == 5u && n >= 4) { /* signalling: a parameter update is rejected */
        uint8_t r[8] = { 0x13, p[1], 2, 0, 1, 0 };
        if (p[0] == 0x12)
            l2cap(s, 5u, r, 6);
        else if (!(p[0] & 1u)) {
            uint8_t rej[6] = { 0x01, p[1], 2, 0, 0, 0 };
            l2cap(s, 5u, rej, sizeof rej);
        }
    } else if (cid == 6u && n >= 1 && (p[0] == 0x01 || p[0] == 0x0B)) { /* SMP: no pairing */
        uint8_t r[2] = { 0x05, 0x05 };
        l2cap(s, 6u, r, sizeof r);
    }
}

/* an LL control PDU from the slave */
static void ll_control(bc_state *s, const uint8_t *p, uint32_t n)
{
    if (!n)
        return;
    switch (p[0]) {
    case 0x02: /* LL_TERMINATE_IND */
        s->state = ST_DISCONNECTED;
        logf_(s, "%sdisconnected by the peer, reason 0x%02lx", "", n > 1 ? (unsigned long)p[1] : 0ul);
        return;
    case 0x08: /* LL_FEATURE_REQ */
    case 0x0E: { /* LL_PERIPHERAL_FEATURE_REQ */
        uint8_t r[9] = { 0x09, 0, 0, 0, 0, 0, 0, 0, 0 };
        control(s, r, sizeof r);
        return;
    }
    case 0x0C: /* LL_VERSION_IND */
        if (!s->version_sent) {
            uint8_t r[6] = { 0x0C, 0x0B, 0x5F, 0x00, 0x01, 0x00 }; /* 5.2 */
            s->version_sent = 1;
            control(s, r, sizeof r);
        }
        return;
    case 0x14: { /* LL_LENGTH_REQ: 27 bytes, 328 us each way */
        uint8_t r[9] = { 0x15, 27, 0, 0x48, 0x01, 27, 0, 0x48, 0x01 };
        control(s, r, sizeof r);
        return;
    }
    case 0x07: /* LL_UNKNOWN_RSP */
    case 0x09:
    case 0x15:
        return;
    default: {
        uint8_t r[2] = { 0x07, p[0] };
        control(s, r, sizeof r);
        return;
    }
    }
}

/* ---- the connection ------------------------------------------------------------ */

static void send_next(bc_state *s, es_time_t at)
{
    uint8_t p[2 + MAX_PDU];
    uint32_t ch_mhz = data_mhz(s->unmapped);
    const bc_pdu *q = s->qn ? &s->q[s->qh] : NULL;
    uint32_t len = q ? q->len : 0u;
    p[0] = (uint8_t)((q ? q->llid : 1u) | (unsigned)s->nesn << 2 | (unsigned)s->sn << 3 | (s->qn > 1 ? 1u << 4 : 0u));
    p[1] = (uint8_t)len;
    if (q)
        memcpy(p + 2, q->data, len);
    if (!send_pdu(s, p, 2u + len, ch_mhz, at, ACCESS))
        return;
    s->sent_in_event++;
    s->awaiting = 1;
    /* the slave answers T_IFS after the end: listen until its access address
     * could have been heard */
    s->listen_mhz = ch_mhz;
    s->listen_until = at + esk_air_ble_airtime(2u + len, 1000u) + T_IFS_PS + 40u * ES_PS_PER_US + 2u * ES_PS_PER_US;
    wake(s, s->listen_until + ES_PS_PER_US, WAKE_CLOSE);
}

static void event_start(bc_state *s)
{
    if (s->state != ST_CONNECTED)
        return;
    if (!s->established && esk_comp_now(s->c) - s->connect_at >= 6u * (es_time_t)s->interval * 1250u * ES_PS_PER_US) {
        /* nothing heard in six intervals: it failed to be established
         * (4.5.2); the app tries again */
        s->state = ST_SCANNING;
        s->listen_mhz = 0;
        s->gen++;
        logf_(s, "%sconnection failed to be established (0x3E); scanning again", "", 0ul);
        esk_comp_wake_at(s->c, esk_comp_now(s->c) + SCAN_PS, WAKE_HOP);
        return;
    }
    if (esk_comp_now(s->c) - s->last_rx > (es_time_t)s->timeout * 10u * ES_PS_PER_MS) {
        s->state = ST_DISCONNECTED;
        s->listen_mhz = 0;
        logf_(s, "%sconnection lost (supervision timeout) after %lu events", "", (unsigned long)s->events);
        return;
    }
    s->unmapped = (s->unmapped + HOP) % 37u;
    s->event++;
    s->events++;
    s->sent_in_event = 0;
    s->in_event = 1;
    s->md_peer = 0;
    send_next(s, s->anchor);
    s->anchor += (es_time_t)s->interval * 1250u * ES_PS_PER_US;
    wake(s, s->anchor, WAKE_EVENT);
}

static void conn_rx(bc_state *s, const uint8_t *p, uint32_t len, es_time_t end)
{
    uint32_t n = p[1], llid = p[0] & 3u;
    int sn = (p[0] >> 3) & 1, nesn = (p[0] >> 2) & 1;
    s->listen_mhz = 0;
    s->awaiting = 0;
    s->last_rx = end;
    s->established = 1;
    s->md_peer = (p[0] >> 4) & 1;
    if (nesn != s->sn && s->qn) { /* ours was received */
        s->qh = (s->qh + 1) % QUEUE;
        s->qn--;
        s->sn ^= 1;
    } else if (nesn != s->sn)
        s->sn ^= 1;
    if (sn == s->nesn && n + 2u <= len) { /* new */
        s->nesn ^= 1;
        if (llid == 3u)
            ll_control(s, p + 2, n);
        else if (llid == 2u && n >= 2u) {
            s->l2_want = (uint32_t)(p[2] | p[3] << 8) + 4u;
            s->l2_len = 0;
        }
        if ((llid == 2u || llid == 1u) && n && s->l2_want && s->l2_len + n <= sizeof s->l2) {
            memcpy(s->l2 + s->l2_len, p + 2, n);
            s->l2_len += n;
            if (s->l2_len >= s->l2_want) {
                l2cap_rx(s, s->l2, s->l2_want);
                s->l2_want = 0;
            }
        }
    }
    /* more in this event while either has more, up to 8 exchanges */
    if (s->state == ST_CONNECTED && (s->md_peer || s->qn) && s->sent_in_event < 8)
        wake(s, end + T_IFS_PS, WAKE_SEND);
}

static void connect(bc_state *s, const uint8_t *adv, es_time_t end)
{
    uint8_t p[36];
    uint32_t i;
    p[0] = (uint8_t)(0x05u | 1u << 6 | (adv[0] & 0x40u) << 1); /* random InitA; RxAdd from the AdvA's */
    p[1] = 34;
    for (i = 0; i < 6; i++)
        p[2 + i] = s->me[5 - i];
    memcpy(p + 8, adv + 2, 6);
    p[14] = (uint8_t)ACCESS;
    p[15] = (uint8_t)(ACCESS >> 8);
    p[16] = (uint8_t)(ACCESS >> 16);
    p[17] = (uint8_t)(ACCESS >> 24);
    p[18] = (uint8_t)CRC_INIT;
    p[19] = (uint8_t)(CRC_INIT >> 8);
    p[20] = (uint8_t)(CRC_INIT >> 16);
    p[21] = 2; /* WinSize: 2.5 ms */
    p[22] = 0; /* WinOffset */
    p[23] = 0;
    p[24] = (uint8_t)s->interval;
    p[25] = (uint8_t)(s->interval >> 8);
    p[26] = 0; /* latency */
    p[27] = 0;
    p[28] = (uint8_t)s->timeout;
    p[29] = (uint8_t)(s->timeout >> 8);
    p[30] = 0xFF; /* all 37 data channels */
    p[31] = 0xFF;
    p[32] = 0xFF;
    p[33] = 0xFF;
    p[34] = 0x1F;
    p[35] = (uint8_t)HOP; /* SCA 0: 251-500 ppm */
    if (!send_pdu(s, p, sizeof p, ADV_MHZ[s->ch], end + T_IFS_PS, ADV_AA))
        return;
    for (i = 0; i < 6; i++)
        s->peer[i] = adv[2 + 5 - i];
    s->peer_random = (adv[0] >> 6) & 1;
    s->state = ST_CONNECTED;
    s->established = 0;
    s->connect_at = end + T_IFS_PS + esk_air_ble_airtime(sizeof p, 1000u);
    s->gen++;
    s->unmapped = 0;
    s->sn = s->nesn = 0;
    s->qh = s->qn = 0;
    s->g = G_IDLE;
    /* the first anchor: the transmit window's start, 1.25 ms after the
     * CONNECT_IND ends */
    s->anchor = end + T_IFS_PS + esk_air_ble_airtime(sizeof p, 1000u) + 1250u * ES_PS_PER_US;
    s->last_rx = s->anchor;
    wake(s, s->anchor, WAKE_EVENT);
    {
        char a[24];
        snprintf(a, sizeof a, "%02x:%02x:%02x:%02x:%02x:%02x", s->peer[0], s->peer[1], s->peer[2], s->peer[3],
                 s->peer[4], s->peer[5]);
        logf_(s, "connecting to %s, interval %lu x 1.25 ms", a, (unsigned long)s->interval);
    }
    gatt_next(s); /* the MTU exchange first */
}

/* an advertiser is the target: its name or address */
static int is_target(bc_state *s, const uint8_t *p, uint32_t len)
{
    char addr[24];
    uint32_t i;
    snprintf(addr, sizeof addr, "%02x:%02x:%02x:%02x:%02x:%02x", p[7], p[6], p[5], p[4], p[3], p[2]);
    if (!s->target[0] || !strcmp(s->target, addr))
        return 1;
    for (i = 8; i + 1u < len;) {
        uint32_t l = p[i], t = p[i + 1];
        if (!l || i + 1u + l > len)
            break;
        if ((t == 0x09 || t == 0x08) && l - 1u == strlen(s->target) && !memcmp(p + i + 2, s->target, l - 1u)) {
            memcpy(s->peer_name, p + i + 2, l - 1u < sizeof s->peer_name - 1 ? l - 1u : sizeof s->peer_name - 1);
            return 1;
        }
        i += 1u + l;
    }
    return 0;
}

static int bc_rx(void *ctx, const esk_air_frame *f, int rssi, es_time_t at)
{
    bc_state *s = (bc_state *)ctx;
    const uint8_t *p = f->data;
    (void)rssi;
    if (f->len < 2)
        return 0;
    if (s->state == ST_SCANNING) {
        uint32_t type = p[0] & 15u;
        if ((type == 0u || type == 1u) && f->len >= 8u && p[1] + 2u <= f->len && is_target(s, p, f->len))
            connect(s, p, at);
        return 0;
    }
    if (s->state == ST_CONNECTED && s->awaiting && f->mhz == s->listen_mhz)
        conn_rx(s, p, f->len, at);
    return 0;
}

static void bc_time(esk_comp *c, uint32_t token)
{
    bc_state *s = (bc_state *)c->state;
    uint32_t kind = token & 15u;
    if (kind == WAKE_HOP) {
        if (s->state == ST_SCANNING) {
            s->ch = (s->ch + 1) % 3;
            esk_comp_wake_at(c, esk_comp_now(c) + SCAN_PS, WAKE_HOP);
        }
        return;
    }
    if (token >> 4 != s->gen)
        return; /* a plan since replaced */
    if (kind == WAKE_EVENT)
        event_start(s);
    else if (kind == WAKE_SEND && s->state == ST_CONNECTED)
        send_next(s, esk_comp_now(c));
    else if (kind == WAKE_CLOSE && s->awaiting && esk_comp_now(c) > s->listen_until) {
        s->awaiting = 0; /* no answer: the event is over */
        s->listen_mhz = 0;
    }
}

static int parse_addr(const char *t, uint8_t a[6])
{
    unsigned v[6];
    int i;
    if (sscanf(t, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
        return 0;
    for (i = 0; i < 6; i++)
        a[i] = (uint8_t)v[i];
    return 1;
}

static es_status bc_create(esk_comp *c)
{
    bc_state *s = (bc_state *)c->state;
    double interval = 30, timeout = 5000;
    s->c = c;
    s->target = esk_comp_param_str(c, "target", "");
    if (!parse_addr(esk_comp_param_str(c, "address", "c4:5e:7a:11:22:33"), s->me) || (s->me[0] & 0xC0u) != 0xC0u) {
        esk_comp_diag(c, ES_SEV_ERROR, "ble_central.address", "a random static address: its top two bits are 1");
        return ES_ESCHEMA;
    }
    esk_comp_param_double(c, "interval", &interval);
    esk_comp_param_double(c, "timeout", &timeout);
    if (interval < 7.5 || interval > 4000 || timeout < 100 || timeout > 32000 || timeout <= 2 * interval) {
        esk_comp_diag(c, ES_SEV_ERROR, "ble_central.interval",
                      "the interval is 7.5 ms to 4 s and the timeout 100 ms to 32 s, more than twice it");
        return ES_ESCHEMA;
    }
    s->interval = (uint16_t)(interval / 1.25 + 0.5);
    s->timeout = (uint16_t)(timeout / 10 + 0.5);
    s->node.ctx = s;
    s->node.comp = c;
    s->node.kind = ESK_AIR_BLE;
    s->node.tuned = bc_tuned;
    s->node.rx = bc_rx;
    s->node.tx_done = NULL;
    if (esk_air_attach(c->e, &s->node) != ES_OK)
        return ES_ENOMEM;
    s->attached = 1;
    return ES_OK;
}

static void bc_destroy(esk_comp *c)
{
    bc_state *s = (bc_state *)c->state;
    if (s && s->attached)
        esk_air_detach(c->e, &s->node);
    if (s)
        s->attached = 0;
}

static void bc_reset(esk_comp *c)
{
    bc_state *s = (bc_state *)c->state;
    s->state = ST_SCANNING;
    s->ch = 0;
    s->gen++;
    s->listen_mhz = 0;
    s->awaiting = 0;
    s->nsvc = s->nchr = 0;
    s->g = G_IDLE;
    s->mtu = 23;
    s->events = s->notifications = 0;
    s->version_sent = 0;
    s->log[0] = s->gatt[0] = 0;
    s->peer_name[0] = 0;
    esk_comp_wake_at(c, SCAN_PS, WAKE_HOP);
}

static void bc_observe(esk_comp *c, esk_json *out)
{
    bc_state *s = (bc_state *)c->state;
    static const char *const states[] = { "scanning", "connecting", "connected", "disconnected" };
    char peer[24] = "";
    if (s->state != ST_SCANNING)
        snprintf(peer, sizeof peer, "%02x:%02x:%02x:%02x:%02x:%02x", s->peer[0], s->peer[1], s->peer[2], s->peer[3],
                 s->peer[4], s->peer[5]);
    esk_json_set_string(c->e->a, out, "state", states[s->state == ST_CONNECTED && !s->established ? ST_CONNECTING : s->state]);
    esk_json_set_string(c->e->a, out, "peer", peer);
    esk_json_set_integer(c->e->a, out, "events", (int64_t)s->events);
    esk_json_set_integer(c->e->a, out, "mtu", s->mtu);
    esk_json_set_integer(c->e->a, out, "services", s->nsvc);
    esk_json_set_integer(c->e->a, out, "notifications", (int64_t)s->notifications);
    esk_json_set_string(c->e->a, out, "gatt", s->gatt);
    esk_json_set_string(c->e->a, out, "log", s->log);
}

const esk_model_ops esk_ble_central_ops = { "ble_central", sizeof(bc_state), bc_create, bc_destroy, bc_reset, NULL,
                                            bc_time, NULL, bc_observe, NULL, 0 };
