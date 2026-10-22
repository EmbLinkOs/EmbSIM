/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* air_sniffer.c -- a sniffer on the session's air (engine/air.h; docs/
 * engineering/c3-radios.md, step 7), as engineers capture radio traffic
 * for Wireshark: a Wi-Fi adapter in monitor mode on one channel, or a
 * Bluetooth LE sniffer (Nordic's nRF Sniffer on a dongle) on one
 * advertising channel:
 *
 *   - `radio` "wifi" listens on Wi-Fi channel `channel` (1 to 14) and
 *     takes every 802.11 frame it hears, whoever it is for, with its FCS
 *     (the CRC-32 the air carries implicitly, IEEE 802.11-2020 9.2.4.8);
 *   - `radio` "ble" listens on advertising channel `channel` (37, 38 or
 *     39) and takes every packet it hears as the link layer has it: the
 *     access address, the PDU and its CRC (CRC-24 from 0x555555, Bluetooth
 *     Core 5.4 Vol 6 Part B 3.1.1);
 *   - like tcpdump's -s, it keeps the first `snaplen` bytes of each (256
 *     by default) and the length it had;
 *   - its `capture` output holds the last 64 frames, each a line "seq
 *     time_ps kind MHz RSSI length hex", for the workbench's capture to
 *     gather and save as a pcap file for Wireshark (radiotap for Wi-Fi,
 *     LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR for BLE).
 *
 * Where it differs inside: the air carries a frame whole and no ACK or
 * other control response as a frame, so the capture has no ACKs. Not yet:
 * following a BLE connection onto the data channels (as nRF Sniffer
 * does after a CONNECT_IND), hopping, decryption. */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"

#define RING      64
#define SNAP_MAX  2400u
#define FRAME_MAX 2400u
#define LINE_CAP  (32u + 2u * SNAP_MAX)
#define ADV_AA    0x8E89BED6u

typedef struct sn_frame {
    uint64_t seq;
    es_time_t at;
    uint32_t mhz, len, caplen;
    int rssi, kind;
    uint8_t data[SNAP_MAX];
} sn_frame;

typedef struct sn_state {
    esk_comp *c;
    esk_air_node node;
    int attached;
    int kind; /* ESK_AIR_WIFI or ESK_AIR_BLE */
    uint32_t mhz;
    uint32_t snaplen;
    uint64_t frames, bytes;
    sn_frame ring[RING];
    char *text;
    size_t text_cap;
} sn_state;

static uint32_t sn_tuned(void *ctx, int kind)
{
    sn_state *s = (sn_state *)ctx;
    return kind == s->kind ? s->mhz : 0u;
}

/* the 802.11 FCS: CRC-32, reflected, from all ones, inverted */
static uint32_t crc32_of(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (c & 1u ? 0xEDB88320u : 0u);
    }
    return ~c;
}

/* BLE's CRC-24 (polynomial 0x65B) of the PDU from `init`, shifted out
 * least significant bit first: its bytes go on the air from the lowest */
static uint32_t crc24_of(const uint8_t *p, uint32_t n, uint32_t init)
{
    uint32_t c = 0, i;
    int k;
    for (k = 0; k < 24; k++) /* the register holds the initial value reflected */
        if (init & (1u << k))
            c |= 1u << (23 - k);
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (c & 1u ? 0xDA6000u : 0u);
    }
    return c;
}

static int sn_rx(void *ctx, const esk_air_frame *f, int rssi, es_time_t at)
{
    sn_state *s = (sn_state *)ctx;
    uint8_t whole[FRAME_MAX + 8];
    uint32_t n = 0, crc;
    sn_frame *r;
    if (f->kind != s->kind || f->len > FRAME_MAX)
        return 0;
    if (f->kind == ESK_AIR_BLE) { /* the access address, the PDU, its CRC */
        whole[n++] = (uint8_t)(ADV_AA & 0xFFu);
        whole[n++] = (uint8_t)((ADV_AA >> 8) & 0xFFu);
        whole[n++] = (uint8_t)((ADV_AA >> 16) & 0xFFu);
        whole[n++] = (uint8_t)(ADV_AA >> 24);
        memcpy(whole + n, f->data, f->len);
        n += f->len;
        crc = crc24_of(f->data, f->len, 0x555555u);
        whole[n++] = (uint8_t)(crc & 0xFFu);
        whole[n++] = (uint8_t)((crc >> 8) & 0xFFu);
        whole[n++] = (uint8_t)((crc >> 16) & 0xFFu);
    } else { /* the frame and its FCS */
        memcpy(whole, f->data, f->len);
        n = f->len;
        crc = crc32_of(f->data, f->len);
        whole[n++] = (uint8_t)(crc & 0xFFu);
        whole[n++] = (uint8_t)((crc >> 8) & 0xFFu);
        whole[n++] = (uint8_t)((crc >> 16) & 0xFFu);
        whole[n++] = (uint8_t)(crc >> 24);
    }
    r = &s->ring[s->frames % RING];
    r->seq = ++s->frames;
    r->at = at;
    r->mhz = f->mhz;
    r->rssi = rssi;
    r->kind = f->kind;
    r->len = n;
    r->caplen = n < s->snaplen ? n : s->snaplen;
    memcpy(r->data, whole, r->caplen);
    s->bytes += n;
    return 0; /* it listens only */
}

static es_status sn_create(esk_comp *c)
{
    sn_state *s = (sn_state *)c->state;
    const char *radio = esk_comp_param_str(c, "radio", "wifi");
    double channel = -1, snaplen = 256;
    s->c = c;
    esk_comp_param_double(c, "channel", &channel);
    esk_comp_param_double(c, "snaplen", &snaplen);
    if (!strcmp(radio, "ble")) {
        s->kind = ESK_AIR_BLE;
        if (channel < 0)
            channel = 37;
        if (channel != 37 && channel != 38 && channel != 39) {
            esk_comp_diag(c, ES_SEV_ERROR, "air_sniffer.channel", "a BLE sniffer listens on advertising channel 37, 38 or 39");
            return ES_ESCHEMA;
        }
        s->mhz = channel == 37 ? 2402u : channel == 38 ? 2426u : 2480u;
    } else if (!strcmp(radio, "wifi")) {
        s->kind = ESK_AIR_WIFI;
        if (channel < 0)
            channel = 6;
        if (channel < 1 || channel > 14 || channel != (double)(int)channel) {
            esk_comp_diag(c, ES_SEV_ERROR, "air_sniffer.channel", "a Wi-Fi sniffer listens on a channel from 1 to 14");
            return ES_ESCHEMA;
        }
        s->mhz = esk_air_wifi_mhz((int)channel);
    } else {
        esk_comp_diag(c, ES_SEV_ERROR, "air_sniffer.radio", "the radio is \"wifi\" or \"ble\"");
        return ES_ESCHEMA;
    }
    if (snaplen < 16 || snaplen > SNAP_MAX) {
        esk_comp_diag(c, ES_SEV_ERROR, "air_sniffer.snaplen", "the snapshot length is from 16 to 2400 bytes");
        return ES_ESCHEMA;
    }
    s->snaplen = (uint32_t)snaplen;
    s->text_cap = (size_t)RING * (LINE_CAP + 1u) + 1u;
    s->text = (char *)esk_calloc(c->e->a, 1, s->text_cap);
    if (!s->text)
        return ES_ENOMEM;
    s->node.ctx = s;
    s->node.comp = c;
    s->node.kind = s->kind;
    s->node.tuned = sn_tuned;
    s->node.rx = sn_rx;
    s->node.tx_done = NULL;
    if (esk_air_attach(c->e, &s->node) != ES_OK)
        return ES_ENOMEM;
    s->attached = 1;
    return ES_OK;
}

static void sn_destroy(esk_comp *c)
{
    sn_state *s = (sn_state *)c->state;
    if (!s)
        return;
    if (s->attached)
        esk_air_detach(c->e, &s->node);
    s->attached = 0;
    if (s->text)
        esk_free(c->e->a, s->text, s->text_cap);
    s->text = NULL;
}

static void sn_reset(esk_comp *c)
{
    sn_state *s = (sn_state *)c->state;
    s->frames = s->bytes = 0;
    memset(s->ring, 0, sizeof s->ring);
}

static void sn_observe(esk_comp *c, esk_json *out)
{
    static const char hex[] = "0123456789abcdef";
    sn_state *s = (sn_state *)c->state;
    uint64_t first = s->frames > RING ? s->frames - RING + 1u : 1u, q;
    size_t n = 0;
    for (q = first; q <= s->frames && s->text; q++) {
        const sn_frame *r = &s->ring[(q - 1u) % RING];
        uint32_t i;
        int k = snprintf(s->text + n, s->text_cap - n, "%llu %llu %c %u %d %u ", (unsigned long long)r->seq,
                         (unsigned long long)r->at, r->kind == ESK_AIR_BLE ? 'b' : 'w', (unsigned)r->mhz, r->rssi,
                         (unsigned)r->len);
        if (k < 0 || (size_t)k + 2u * r->caplen + 2u >= s->text_cap - n)
            break;
        n += (size_t)k;
        for (i = 0; i < r->caplen; i++) {
            s->text[n++] = hex[r->data[i] >> 4];
            s->text[n++] = hex[r->data[i] & 15u];
        }
        s->text[n++] = '\n';
    }
    if (s->text)
        s->text[n] = 0;
    esk_json_set_integer(c->e->a, out, "frames", (int64_t)s->frames);
    esk_json_set_integer(c->e->a, out, "bytes", (int64_t)s->bytes);
    esk_json_set_string(c->e->a, out, "capture", s->text ? s->text : "");
}

const esk_model_ops esk_air_sniffer_ops = { "air_sniffer", sizeof(sn_state), sn_create, sn_destroy, sn_reset, NULL,
                                            NULL, NULL, sn_observe, NULL, 0 };
