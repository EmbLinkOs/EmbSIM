/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The air (docs/engineering/c3-radios.md, step 4): the 2.4 GHz medium the
 * bench's radios share. A frame is carried whole, not as a waveform (as
 * ADR-031 carries USB as packets): its bytes, centre frequency, transmit
 * power, PHY rate and airtime. It reaches every other node tuned to its
 * frequency when its airtime ends, at the power the free-space path loss
 * leaves over the distance between the two on the bench, if that is above
 * the receiver's sensitivity; a receiver that takes a unicast frame for
 * itself answers it (an 802.11 ACK), which the sender hears an ACK's time
 * later. Fading, interference, collisions and adjacent-channel leakage are
 * not modelled.
 */
#ifndef ESK_ENGINE_AIR_H
#define ESK_ENGINE_AIR_H

#include "embsim/types.h"
#include "util/alloc.h"
#include "util/vec.h"

struct esk_engine;
struct esk_comp;

enum { ESK_AIR_WIFI = 1, ESK_AIR_BLE = 2 };

typedef struct esk_air_frame {
    const uint8_t *data; /* the frame without its FCS (802.11) or CRC (BLE) */
    uint32_t len;
    uint32_t mhz;        /* centre frequency */
    double dbm;     /* transmit power */
    uint32_t rate_kbps;  /* PHY rate */
    int kind;       /* ESK_AIR_WIFI or ESK_AIR_BLE */
} esk_air_frame;

typedef struct esk_air_node {
    void *ctx;
    struct esk_comp *comp; /* where it is: its pose on the bench, or NULL (1 m from all) */
    int kind;              /* the radios it has: ESK_AIR_WIFI | ESK_AIR_BLE */
    /* the frequency it listens on now for `kind` (MHz), 0 when it does not */
    uint32_t (*tuned)(void *ctx, int kind);
    /* a frame arrived at `at` with `rssi` dBm: 1 when it answers it (an ACK) */
    int (*rx)(void *ctx, const esk_air_frame *f, int rssi, es_time_t at);
    /* its frame `token` went out: answered or not, at `at` */
    void (*tx_done)(void *ctx, uint32_t token, int acked, es_time_t at);
} esk_air_node;

typedef struct esk_air {
    ESK_VEC(esk_air_node *) nodes;
    uint64_t frames;  /* frames sent */
    void *store; /* the frames in flight */
} esk_air;

void esk_air_init(esk_air *air);
void esk_air_free(esk_alloc *a, esk_air *air);
/* the session restarts: nothing is on the air */
void esk_air_reset(esk_air *air);
/* `n` joins (or leaves) the air */
es_status esk_air_attach(struct esk_engine *e, esk_air_node *n);
void esk_air_detach(struct esk_engine *e, esk_air_node *n);
/* `from` sends `f` from `start` for `airtime` ps: it reaches the others when
 * it ends, and `from` hears whether it was answered (tx_done with `token`)
 * an ACK's time after that */
es_status esk_air_send(struct esk_engine *e, esk_air_node *from, const esk_air_frame *f, es_time_t start,
                       es_time_t airtime, uint32_t token);
/* the received power (dBm) of a frame `from` sends at `dbm` and `mhz`, at `to` */
double esk_air_rssi(const esk_air_node *from, const esk_air_node *to, double dbm, uint32_t mhz);
/* an 802.11 frame's airtime at `rate_kbps`: the preamble and header (192 us,
 * the long preamble, at the DSSS and CCK rates 1, 2, 5.5 and 11 Mbit/s; 20 us
 * at the OFDM rates) and its bytes with the FCS (OFDM: in 4 us symbols with
 * the SERVICE and tail bits). HT frames are counted as OFDM ones. */
es_time_t esk_air_wifi_airtime(uint32_t len, uint32_t rate_kbps);
/* a BLE packet's airtime with `len` bytes of PDU at 1000 or 2000 kbit/s:
 * the preamble (1 or 2 bytes), the access address, the PDU, the CRC */
es_time_t esk_air_ble_airtime(uint32_t len, uint32_t rate_kbps);
/* the 2.4 GHz Wi-Fi channel a centre frequency is (1-14), 0 for none */
int esk_air_wifi_channel(uint32_t mhz);
uint32_t esk_air_wifi_mhz(int channel);

#endif
