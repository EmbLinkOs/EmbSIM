/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* usb.c -- the bench's USB cables (usb.h says what they carry). */
#include <string.h>

#include "engine/engine.h"

/* 12 Mbit/s: a bit is 83.333 ns */
#define FS_BIT_PS_X3 UINT64_C(250000) /* three bit times, in ps */

void esk_usb_init(esk_usb *u) { memset(u, 0, sizeof *u); }

void esk_usb_free(esk_alloc *a, esk_usb *u)
{
    esk_vec_free(a, &u->devs);
    esk_vec_free(a, &u->hosts);
}

es_status esk_usb_attach_dev(esk_engine *e, esk_usb_dev *d)
{
    size_t i;
    for (i = 0; i < e->usb.devs.len; i++)
        if (e->usb.devs.data[i] == d)
            return ES_OK;
    return esk_vec_push(e->a, &e->usb.devs, d) == 0 ? ES_OK : ES_ENOMEM;
}

void esk_usb_detach_dev(esk_engine *e, esk_usb_dev *d)
{
    size_t i;
    for (i = 0; i < e->usb.devs.len; i++)
        if (e->usb.devs.data[i] == d) {
            esk_vec_remove(&e->usb.devs, i);
            return;
        }
}

es_status esk_usb_attach_host(esk_engine *e, esk_usb_host *h)
{
    size_t i;
    for (i = 0; i < e->usb.hosts.len; i++)
        if (e->usb.hosts.data[i] == h)
            return ES_OK;
    return esk_vec_push(e->a, &e->usb.hosts, h) == 0 ? ES_OK : ES_ENOMEM;
}

void esk_usb_detach_host(esk_engine *e, esk_usb_host *h)
{
    size_t i;
    for (i = 0; i < e->usb.hosts.len; i++)
        if (e->usb.hosts.data[i] == h) {
            esk_vec_remove(&e->usb.hosts, i);
            return;
        }
}

esk_usb_dev *esk_usb_device_of(esk_engine *e, const esk_usb_host *h)
{
    size_t i;
    if (!h->dp || !h->dp->net)
        return NULL;
    for (i = 0; i < e->usb.devs.len; i++) {
        esk_usb_dev *d = e->usb.devs.data[i];
        if (d->dp && d->dp->net == h->dp->net)
            return d;
    }
    return NULL;
}

void esk_usb_changed(esk_engine *e, const esk_usb_dev *d, es_time_t at)
{
    size_t i;
    if (!d->dp || !d->dp->net)
        return;
    for (i = 0; i < e->usb.hosts.len; i++) {
        esk_usb_host *h = e->usb.hosts.data[i];
        if (h->dp && h->dp->net == d->dp->net && h->changed)
            h->changed(h->ctx, at);
    }
}

/* SYNC (8 bits), PID (8), EOP (3 bits' time); a token's 11 bits of address
 * and endpoint and its CRC5; a data packet's bytes and CRC16; the gap
 * between packets, which USB 2.0 7.1.18 bounds at 2 to 7.5 bit times for
 * the device and the host waits up to 16 for: 8 here */
es_time_t esk_usb_fs_time(int len, int handshake)
{
    uint64_t bits = 8 + 8 + 16 + 3; /* the token */
    if (len >= 0)
        bits += 8 + 8 + 8 * (uint64_t)len + 16 + 3 + 8;
    if (handshake)
        bits += 8 + 8 + 3 + 8;
    return (es_time_t)(bits * FS_BIT_PS_X3 / 3);
}
