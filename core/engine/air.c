/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* air.c -- the 2.4 GHz medium (air.h says what it does). */
#include <math.h>
#include <string.h>

#include "engine/engine.h"

#define AIR_SLOTS 64
#define AIR_MAX_FRAME 2400u
#define SENSITIVITY_DBM (-97.0) /* the ESP32-C3's 802.11b 1 Mbps sensitivity (datasheet: -98.4 dBm) */
#define DEFAULT_DISTANCE_MM 1000.0
#define SIFS_PS UINT64_C(10000000)          /* 10 us */
#define ACK_PS (UINT64_C(304000000))         /* an ACK at 1 Mbps: 192 us preamble, 14 bytes */

typedef struct air_slot {
    int used;
    esk_air_node *from;
    esk_air_frame f;
    uint8_t data[AIR_MAX_FRAME];
    uint32_t token;
} air_slot;

typedef struct air_store {
    air_slot slot[AIR_SLOTS];
} air_store;

static air_store *store_of(esk_engine *e, int make)
{
    if (!e->air.store && make)
        e->air.store = esk_calloc(e->a, 1, sizeof(air_store));
    return (air_store *)e->air.store;
}

void esk_air_init(esk_air *air)
{
    memset(air, 0, sizeof *air);
}

void esk_air_free(esk_alloc *a, esk_air *air)
{
    esk_vec_free(a, &air->nodes);
    if (air->store)
        esk_free(a, air->store, sizeof(air_store));
    air->store = NULL;
}

void esk_air_reset(esk_air *air)
{
    if (air->store)
        memset(air->store, 0, sizeof(air_store));
    air->frames = 0;
}

es_status esk_air_attach(esk_engine *e, esk_air_node *n)
{
    size_t i;
    for (i = 0; i < e->air.nodes.len; i++)
        if (e->air.nodes.data[i] == n)
            return ES_OK;
    return esk_vec_push(e->a, &e->air.nodes, n) == 0 ? ES_OK : ES_ENOMEM;
}

void esk_air_detach(esk_engine *e, esk_air_node *n)
{
    size_t i;
    air_store *st = store_of(e, 0);
    for (i = 0; i < e->air.nodes.len; i++)
        if (e->air.nodes.data[i] == n) {
            esk_vec_remove(&e->air.nodes, i);
            break;
        }
    for (i = 0; st && i < AIR_SLOTS; i++)
        if (st->slot[i].used && st->slot[i].from == n)
            st->slot[i].from = NULL; /* in flight still, but no one hears its end */
}

static double distance_mm(const esk_air_node *a, const esk_air_node *b)
{
    double dx, dy;
    if (!a->comp || !b->comp || !a->comp->posed || !b->comp->posed)
        return DEFAULT_DISTANCE_MM;
    dx = a->comp->pose[0] - b->comp->pose[0];
    dy = a->comp->pose[1] - b->comp->pose[1];
    return sqrt(dx * dx + dy * dy);
}

double esk_air_rssi(const esk_air_node *from, const esk_air_node *to, double dbm, uint32_t mhz)
{
    /* free-space path loss: 20 log10(d / 1 m) + 20 log10(f / 1 MHz) - 27.55 dB,
     * no closer than 10 cm (the antennas' near field) */
    double d = distance_mm(from, to) / 1000.0;
    if (d < 0.1)
        d = 0.1;
    return dbm - (20.0 * log10(d) + 20.0 * log10((double)mhz) - 27.55);
}

es_time_t esk_air_wifi_airtime(uint32_t len, uint32_t rate_kbps)
{
    uint64_t bits = (uint64_t)(len + 4u) * 8u, us_ps = UINT64_C(1000000);
    if (!rate_kbps)
        rate_kbps = 1000u;
    if (rate_kbps == 1000u || rate_kbps == 2000u || rate_kbps == 5500u || rate_kbps == 11000u)
        /* DSSS/CCK: the long preamble and PLCP header */
        return (es_time_t)(192u * us_ps + bits * UINT64_C(1000000000) / rate_kbps);
    /* OFDM: 20 us of preamble and SIGNAL, 4 us symbols of rate * 4 bits */
    {
        uint64_t per_symbol = (uint64_t)rate_kbps * 4u / 1000u, symbols = (16u + bits + 6u + per_symbol - 1u) / per_symbol;
        return (es_time_t)((20u + 4u * symbols) * us_ps);
    }
}

es_time_t esk_air_ble_airtime(uint32_t len, uint32_t rate_kbps)
{
    uint64_t bytes = (rate_kbps >= 2000u ? 2u : 1u) + 4u + len + 3u;
    if (!rate_kbps)
        rate_kbps = 1000u;
    return (es_time_t)(bytes * 8u * UINT64_C(1000000000) / rate_kbps);
}

int esk_air_wifi_channel(uint32_t mhz)
{
    if (mhz == 2484u)
        return 14;
    if (mhz >= 2412u && mhz <= 2472u && (mhz - 2407u) % 5u == 0)
        return (int)((mhz - 2407u) / 5u);
    return 0;
}

uint32_t esk_air_wifi_mhz(int channel)
{
    if (channel == 14)
        return 2484u;
    return channel >= 1 && channel <= 13 ? 2407u + 5u * (uint32_t)channel : 0u;
}

static int group_addressed(const esk_air_frame *f)
{
    /* an 802.11 frame's first address (or an ACK's receiver) with the group bit */
    return f->kind != ESK_AIR_WIFI || f->len < 10 || (f->data[4] & 1u);
}

static void done_cb(void *ctx, void *arg, es_time_t now)
{
    esk_engine *e = (esk_engine *)ctx;
    air_store *st = store_of(e, 0);
    air_slot *s = st ? &st->slot[(uintptr_t)arg >> 1] : NULL;
    int acked = (int)((uintptr_t)arg & 1u);
    if (!s || !s->used)
        return;
    s->used = 0;
    if (s->from && s->from->tx_done)
        s->from->tx_done(s->from->ctx, s->token, acked, now);
}

static void end_cb(void *ctx, void *arg, es_time_t now)
{
    esk_engine *e = (esk_engine *)ctx;
    air_store *st = store_of(e, 0);
    uintptr_t i = (uintptr_t)arg;
    air_slot *s;
    size_t k;
    int acked = 0;
    if (!st)
        return;
    s = &st->slot[i];
    if (!s->used)
        return;
    s->f.data = s->data;
    for (k = 0; k < e->air.nodes.len; k++) {
        esk_air_node *n = e->air.nodes.data[k];
        double rssi;
        if (n == s->from || !(n->kind & s->f.kind) || !n->tuned || n->tuned(n->ctx, s->f.kind) != s->f.mhz)
            continue;
        rssi = s->from ? esk_air_rssi(s->from, n, s->f.dbm, s->f.mhz) : s->f.dbm - 40.0;
        if (rssi < SENSITIVITY_DBM || !n->rx)
            continue;
        if (n->rx(n->ctx, &s->f, (int)floor(rssi + 0.5), now))
            acked = 1;
    }
    if (group_addressed(&s->f) || !acked) {
        /* nothing answers: the sender finds out after the ACK it waited for */
        es_time_t at = group_addressed(&s->f) ? now : now + SIFS_PS + ACK_PS;
        if (esk_sched_at(&e->sched, at, ESK_EV_EXTERNAL, done_cb, e, (void *)(i << 1), NULL) != ES_OK)
            s->used = 0;
        return;
    }
    if (esk_sched_at(&e->sched, now + SIFS_PS + ACK_PS, ESK_EV_EXTERNAL, done_cb, e, (void *)(i << 1 | 1u), NULL) !=
        ES_OK)
        s->used = 0;
}

es_status esk_air_send(esk_engine *e, esk_air_node *from, const esk_air_frame *f, es_time_t start,
                       es_time_t airtime, uint32_t token)
{
    air_store *st = store_of(e, 1);
    uintptr_t i;
    if (!st)
        return ES_ENOMEM;
    if (f->len > AIR_MAX_FRAME)
        return ES_EINVAL;
    for (i = 0; i < AIR_SLOTS && st->slot[i].used; i++)
        ;
    if (i == AIR_SLOTS)
        return ES_ELIMIT;
    st->slot[i].used = 1;
    st->slot[i].from = from;
    st->slot[i].f = *f;
    memcpy(st->slot[i].data, f->data, f->len);
    st->slot[i].f.data = st->slot[i].data;
    st->slot[i].token = token;
    e->air.frames++;
    if (start < e->sched.now)
        start = e->sched.now;
    if (esk_sched_at(&e->sched, start + airtime, ESK_EV_EXTERNAL, end_cb, e, (void *)i, NULL) != ES_OK) {
        st->slot[i].used = 0;
        return ES_ELIMIT;
    }
    return ES_OK;
}
