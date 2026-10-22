/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* inet.c -- the internet beyond the routers (inet.h says what it does). */
#include <ctype.h>
#include <string.h>

#include "engine/engine.h"

#define INET_SLOTS 128

typedef struct inet_slot {
    int used;
    uint32_t len;
    uint8_t p[ESK_INET_MTU];
} inet_slot;

typedef struct inet_store {
    inet_slot slot[INET_SLOTS];
} inet_store;

static inet_store *store_of(esk_engine *e, int make)
{
    if (!e->inet.store && make)
        e->inet.store = esk_calloc(e->a, 1, sizeof(inet_store));
    return (inet_store *)e->inet.store;
}

void esk_inet_init(esk_inet *n) { memset(n, 0, sizeof *n); }

void esk_inet_free(esk_alloc *a, esk_inet *n)
{
    esk_vec_free(a, &n->nodes);
    if (n->store)
        esk_free(a, n->store, sizeof(inet_store));
    n->store = NULL;
}

void esk_inet_reset(esk_inet *n)
{
    if (n->store)
        memset(n->store, 0, sizeof(inet_store));
    n->packets = n->dropped = 0;
}

es_status esk_inet_attach(esk_engine *e, esk_inet_node *n)
{
    size_t i;
    for (i = 0; i < e->inet.nodes.len; i++)
        if (e->inet.nodes.data[i] == n)
            return ES_OK;
    return esk_vec_push(e->a, &e->inet.nodes, n) == 0 ? ES_OK : ES_ENOMEM;
}

void esk_inet_detach(esk_engine *e, esk_inet_node *n)
{
    size_t i;
    for (i = 0; i < e->inet.nodes.len; i++)
        if (e->inet.nodes.data[i] == n) {
            esk_vec_remove(&e->inet.nodes, i);
            return;
        }
}

static esk_inet_node *node_of(esk_engine *e, const uint8_t ip[4])
{
    size_t i;
    for (i = 0; i < e->inet.nodes.len; i++)
        if (!memcmp(e->inet.nodes.data[i]->ip, ip, 4))
            return e->inet.nodes.data[i];
    return NULL;
}

/* a packet's arrival: to the node its address names now (it may have left) */
static void arrive_cb(void *ctx, void *arg, es_time_t now)
{
    esk_engine *e = (esk_engine *)ctx;
    inet_store *st = store_of(e, 0);
    inet_slot *s = st ? &st->slot[(uintptr_t)arg] : NULL;
    esk_inet_node *to;
    uint8_t p[ESK_INET_MTU];
    uint32_t len;
    if (!s || !s->used)
        return;
    len = s->len;
    memcpy(p, s->p, len);
    s->used = 0; /* free before delivery: the receiver may answer at once */
    to = node_of(e, p + 16);
    if (to && to->rx)
        to->rx(to->ctx, p, len, now);
    else
        e->inet.dropped++;
}

es_status esk_inet_send(esk_engine *e, esk_inet_node *from, const uint8_t *p, uint32_t len, es_time_t at)
{
    inet_store *st;
    esk_inet_node *to;
    uintptr_t i;
    if (len < 20 || len > ESK_INET_MTU || (p[0] >> 4) != 4)
        return ES_EINVAL;
    e->inet.packets++;
    to = node_of(e, p + 16);
    if (!to) {
        e->inet.dropped++;
        return ES_OK;
    }
    if (!(st = store_of(e, 1)))
        return ES_ENOMEM;
    for (i = 0; i < INET_SLOTS && st->slot[i].used; i++)
        ;
    if (i == INET_SLOTS)
        return ES_ELIMIT;
    st->slot[i].used = 1;
    st->slot[i].len = len;
    memcpy(st->slot[i].p, p, len);
    if (at < e->sched.now)
        at = e->sched.now;
    at += (from ? from->latency : 0) + to->latency;
    if (esk_sched_at(&e->sched, at, ESK_EV_EXTERNAL, arrive_cb, e, (void *)i, NULL) != ES_OK) {
        st->slot[i].used = 0;
        return ES_ELIMIT;
    }
    return ES_OK;
}

const esk_inet_node *esk_inet_lookup(esk_engine *e, const char *name)
{
    size_t i;
    for (i = 0; name && i < e->inet.nodes.len; i++) {
        const char *a = e->inet.nodes.data[i]->name, *b = name;
        if (!a)
            continue;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b))
            a++, b++;
        if (!*a && !*b)
            return e->inet.nodes.data[i];
    }
    return NULL;
}

uint16_t esk_inet_csum(const uint8_t *p, uint32_t n, uint32_t sum)
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

/* the transport checksum's place in a body of `proto`, or -1 */
static int csum_at(unsigned proto)
{
    return proto == 6 ? 16 : proto == 17 ? 6 : proto == 1 ? 2 : -1;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* the TCP, UDP or ICMP checksum of the body at `p + hl` */
static void body_csum(uint8_t *p, uint32_t hl, uint32_t blen)
{
    unsigned proto = p[9];
    int at = csum_at(proto);
    uint32_t sum = 0, i;
    uint16_t c;
    if (at < 0 || blen < (uint32_t)at + 2u)
        return;
    p[hl + (uint32_t)at] = p[hl + (uint32_t)at + 1] = 0;
    if (proto != 1) { /* the pseudo-header */
        for (i = 12; i < 20; i += 2)
            sum += (uint32_t)p[i] << 8 | p[i + 1];
        sum += proto + blen;
    }
    c = esk_inet_csum(p + hl, blen, sum);
    if (proto == 17 && !c)
        c = 0xFFFFu;
    put16(p + hl + (uint32_t)at, c);
}

uint32_t esk_inet_packet(uint8_t *p, const uint8_t src[4], const uint8_t dst[4], unsigned proto, unsigned ttl,
                         unsigned id, const uint8_t *body, uint32_t blen)
{
    uint32_t n = 20u + blen;
    memset(p, 0, 20);
    p[0] = 0x45;
    put16(p + 2, n);
    put16(p + 4, id & 0xFFFFu);
    p[8] = (uint8_t)ttl;
    p[9] = (uint8_t)proto;
    memcpy(p + 12, src, 4);
    memcpy(p + 16, dst, 4);
    put16(p + 10, esk_inet_csum(p, 20, 0));
    memmove(p + 20, body, blen);
    body_csum(p, 20, blen);
    return n;
}

int esk_inet_rechecksum(uint8_t *p, uint32_t len)
{
    uint32_t hl, tot;
    if (len < 20 || (p[0] >> 4) != 4)
        return 0;
    hl = (p[0] & 15u) * 4u;
    tot = (uint32_t)p[2] << 8 | p[3];
    if (hl < 20 || tot < hl || tot > len)
        return 0;
    p[10] = p[11] = 0;
    put16(p + 10, esk_inet_csum(p, hl, 0));
    body_csum(p, hl, tot - hl);
    return 1;
}
