/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The USB cables of the bench (ADR-031): a host's port and the device on
 * the other end, carried as what the bus does, not as D+ and D- edges. A
 * device is the USB controller of a part (an MCU's OTG core), found by the
 * net its D+ pin is on; a host is a part whose D+ pin is on that net too
 * (a cable: the two D+ pins joined, as the cable's wire joins them).
 *
 * What crosses the cable:
 *   - the device's pull-up: none (detached), D+'s (full speed) or D-'s
 *     (low speed); the host is told when it changes;
 *   - the bus states the host drives: a reset (SE0) and its end, a start of
 *     frame with its number, resume signalling (K) and its end; and the
 *     device's remote wakeup signalling back;
 *   - the transactions: SETUP with its 8 bytes, OUT with a data packet, IN
 *     with the device's packet; each answered ACK, NAK, STALL, or nothing
 *     (an address or endpoint the device does not answer: the host's
 *     time-out).
 * Each happens at the engine time it is called at; the host keeps the
 * bus's time (a transaction's bits at 12 Mbit/s, 1 ms frames).
 *
 * Inside: no J, K or SE0 levels on the nets, no bit stuffing, CRC or
 * packet errors; one device on a cable (no hub).
 */
#ifndef ESK_ENGINE_USB_H
#define ESK_ENGINE_USB_H

#include "embsim/types.h"
#include "util/alloc.h"
#include "util/vec.h"

struct esk_engine;
struct esk_comp;
struct esk_pin;

/* a transaction's answer */
enum { ESK_USB_NONE = 0, ESK_USB_ACK = 1, ESK_USB_NAK = 2, ESK_USB_STALL = 3 };
/* the bus states a host drives: a reset's SE0 from RESET to RESET_END, a
 * frame's start, resume signalling (K) from RESUME to RESUME_END; no SOF
 * for 3 ms is the suspend the device sees */
enum { ESK_USB_RESET = 1, ESK_USB_RESET_END = 2, ESK_USB_SOF = 3, ESK_USB_RESUME = 4, ESK_USB_RESUME_END = 5 };
/* the device's pull-up */
enum { ESK_USB_DETACHED = 0, ESK_USB_FULL_SPEED = 1, ESK_USB_LOW_SPEED = 2 };

typedef struct esk_usb_dev {
    void *ctx;
    struct esk_comp *comp;
    struct esk_pin *dp; /* its D+ pin: the cable's end */
    int (*pullup)(void *ctx);
    void (*bus)(void *ctx, int ev, uint32_t frame, es_time_t at);
    int (*setup)(void *ctx, int addr, int ep, const uint8_t data[8], es_time_t at);
    /* `data1`: the packet is DATA1 (else DATA0) */
    int (*out)(void *ctx, int addr, int ep, int data1, const uint8_t *data, uint32_t len, es_time_t at);
    /* up to `cap` bytes of the device's packet to `data`, its length to
     * *len and its PID to *data1 when it answers ACK (it sends data) */
    int (*in)(void *ctx, int addr, int ep, uint8_t *data, uint32_t cap, uint32_t *len, int *data1, es_time_t at);
} esk_usb_dev;

typedef struct esk_usb_host {
    void *ctx;
    struct esk_comp *comp;
    struct esk_pin *dp;
    /* the pull-up of the device on its cable changed */
    void (*changed)(void *ctx, es_time_t at);
    /* the device on its suspended cable signals remote wakeup (K) */
    void (*wakeup)(void *ctx, es_time_t at);
} esk_usb_host;

typedef struct esk_usb {
    ESK_VEC(esk_usb_dev *) devs;
    ESK_VEC(esk_usb_host *) hosts;
} esk_usb;

void esk_usb_init(esk_usb *u);
void esk_usb_free(esk_alloc *a, esk_usb *u);
es_status esk_usb_attach_dev(struct esk_engine *e, esk_usb_dev *d);
void esk_usb_detach_dev(struct esk_engine *e, esk_usb_dev *d);
es_status esk_usb_attach_host(struct esk_engine *e, esk_usb_host *h);
void esk_usb_detach_host(struct esk_engine *e, esk_usb_host *h);
/* the device on the host's cable (its D+ on the host's D+ net, another
 * part than the host: an OTG core is both), or NULL */
esk_usb_dev *esk_usb_device_of(struct esk_engine *e, const esk_usb_host *h);
/* a device's pull-up changed: the host on its cable hears it */
void esk_usb_changed(struct esk_engine *e, const esk_usb_dev *d, es_time_t at);
/* a device signals remote wakeup: the host on its cable hears it */
void esk_usb_remote_wakeup(struct esk_engine *e, const esk_usb_dev *d, es_time_t at);
/* a full-speed transaction's time on the bus: its token, its data packet
 * of `len` bytes (none for -1) and the handshake (when `handshake`), each
 * with its SYNC, PID, CRC and EOP, and the inter-packet gaps */
es_time_t esk_usb_fs_time(int len, int handshake);
/* the same at low speed (1.5 Mbit/s, a low-speed device on a root port) */
es_time_t esk_usb_ls_time(int len, int handshake);

#endif
