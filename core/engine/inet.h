/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The internet beyond the bench's routers (docs/engineering/c3-radios.md,
 * step 4): the network a home router's WAN port reaches. Its nodes are
 * the routers' WAN sides (emblink/wifi_ap) and the hosts out there
 * (emblink/web_server), each with a public IPv4 address, perhaps a DNS
 * name, and its link's one-way delay to the internet's core. An IPv4
 * packet sent reaches the node its destination address names after the
 * sender's and the receiver's delays, whole (the routers between them are
 * not modelled: the TTL is not lowered on the way, no packet is lost or
 * reordered); a packet for an address no node has is dropped, as one to
 * an unrouted address is. A name resolves to the node that has it, as the
 * routers' DNS forwarders see the internet's DNS.
 *
 * Where the inside differs: the WAN is a delay, not links and routers; the
 * DNS hierarchy is one lookup. Not yet: IPv6, fragmentation (packets are
 * at most 1500 bytes), loss and congestion, the host's own network.
 */
#ifndef ESK_ENGINE_INET_H
#define ESK_ENGINE_INET_H

#include "embsim/types.h"
#include "util/alloc.h"
#include "util/vec.h"

struct esk_engine;

typedef struct esk_inet_node {
    void *ctx;
    uint8_t ip[4];
    const char *name;   /* its DNS name, or NULL */
    es_time_t latency;  /* its link's one-way delay to the core */
    /* an IPv4 packet for it, whole, at `at` */
    void (*rx)(void *ctx, const uint8_t *pkt, uint32_t len, es_time_t at);
} esk_inet_node;

typedef struct esk_inet {
    ESK_VEC(esk_inet_node *) nodes;
    uint64_t packets, dropped;
    void *store; /* the packets in flight */
} esk_inet;

#define ESK_INET_MTU 1500u

void esk_inet_init(esk_inet *n);
void esk_inet_free(esk_alloc *a, esk_inet *n);
/* the session restarts: nothing is in flight */
void esk_inet_reset(esk_inet *n);
es_status esk_inet_attach(struct esk_engine *e, esk_inet_node *n);
void esk_inet_detach(struct esk_engine *e, esk_inet_node *n);
/* `from` sends the IPv4 packet `p` at `at`: ES_OK (delivered, or dropped
 * for want of its destination), ES_EINVAL for no packet, ES_ELIMIT when
 * too many are in flight */
es_status esk_inet_send(struct esk_engine *e, esk_inet_node *from, const uint8_t *p, uint32_t len, es_time_t at);
/* the node with DNS name `name` (any case), or NULL */
const esk_inet_node *esk_inet_lookup(struct esk_engine *e, const char *name);

/* the Internet checksum of `n` bytes, `sum` carried in */
uint16_t esk_inet_csum(const uint8_t *p, uint32_t n, uint32_t sum);
/* an IPv4 packet in `p` (room for 20 + blen bytes): its header from `src`
 * to `dst` with `proto`, `ttl` and `id`, then `body`, the header's and a
 * TCP, UDP or ICMP body's checksums filled in; its length */
uint32_t esk_inet_packet(uint8_t *p, const uint8_t src[4], const uint8_t dst[4], unsigned proto, unsigned ttl,
                         unsigned id, const uint8_t *body, uint32_t blen);
/* the header's and the TCP, UDP or ICMP body's checksums recomputed after
 * an address or port was rewritten (a NAT); 0 when `p` is no IPv4 packet */
int esk_inet_rechecksum(uint8_t *p, uint32_t len);

#endif
