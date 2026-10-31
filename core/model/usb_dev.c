/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* usb_dev.c -- a USB part's device side (usb_dev.h says what it does). */
#include <string.h>

#include "model/usb_dev.h"

#define VBUS_VALID_V 4.4 /* a bus-powered device's VBUS (USB 2.0 7.2.1: 4.40 V at a device) */

uint32_t usbd_mps0(const usbd *u) { return u->dev_desc ? u->dev_desc[7] : 8u; }

static int powered(const usbd *u)
{
    const esk_pin *p = u->vbus;
    if (!u->plugged)
        return 0;
    if (!p || !p->net)
        return 1;
    if (p->watch_on)
        return p->watch_state == ESK_L1;
    return esk_net_value(p->net) == ESK_L1;
}

static int d_pullup(void *ctx)
{
    usbd *u = (usbd *)ctx;
    if (!powered(u))
        return ESK_USB_DETACHED;
    return u->low_speed ? ESK_USB_LOW_SPEED : ESK_USB_FULL_SPEED;
}

static void bus_reset(usbd *u)
{
    int i;
    u->addr = u->new_addr = u->addr_pending = 0;
    u->config = 0;
    u->alt = 0;
    u->halted_in = u->halted_out = 0;
    u->remote_wakeup = 0;
    for (i = 0; i < USBD_EPS; i++)
        u->pid_in[i] = u->pid_out[i] = 0;
    u->ep0_len = u->ep0_pos = 0;
    u->ep0_stalled = 0;
    if (u->cls && u->cls->configured)
        u->cls->configured(u, 0);
}

static void d_bus(void *ctx, int ev, uint32_t frame, es_time_t at)
{
    usbd *u = (usbd *)ctx;
    (void)frame;
    (void)at;
    u->suspended = 0;
    if (ev == ESK_USB_RESET)
        bus_reset(u);
    else if (ev == ESK_USB_SOF)
        u->frames++;
}

static int for_me(usbd *u, int addr)
{
    if (!powered(u))
        return 0;
    if (addr == u->addr)
        return 1;
    if (u->addr_pending && addr == u->new_addr) { /* the host moved on: in effect */
        u->addr = u->new_addr;
        u->addr_pending = 0;
        return 1;
    }
    return 0;
}

/* a string descriptor from ASCII */
static uint32_t string_desc(const char *s, uint8_t *out, uint32_t cap)
{
    uint32_t n = 2, i;
    for (i = 0; s[i] && n + 2u <= cap && n + 2u <= 254u; i++) {
        out[n++] = (uint8_t)s[i];
        out[n++] = 0;
    }
    out[0] = (uint8_t)n;
    out[1] = 3;
    return n;
}

/* EP0's standard request: the IN data to ep0[], or STALL */
static int standard(usbd *u, const uint8_t *s)
{
    unsigned type = s[0], req = s[1], value = (unsigned)s[2] | (unsigned)s[3] << 8;
    unsigned index = (unsigned)s[4] | (unsigned)s[5] << 8;
    int ep = (int)(index & 0x0fu), in = (index & 0x80u) != 0;
    switch (req) {
    case 6: /* GET_DESCRIPTOR */
        switch (value >> 8) {
        case 1: memcpy(u->ep0, u->dev_desc, 18); u->ep0_len = 18; return ESK_USB_ACK;
        case 2:
            memcpy(u->ep0, u->cfg_desc, u->cfg_len);
            u->ep0_len = u->cfg_len;
            return ESK_USB_ACK;
        case 3:
            if ((value & 0xffu) == 0) {
                static const uint8_t lang[4] = { 4, 3, 0x09, 0x04 };
                memcpy(u->ep0, lang, 4);
                u->ep0_len = 4;
                return ESK_USB_ACK;
            }
            if ((value & 0xffu) <= 7u && u->strings[(value & 0xffu) - 1u]) {
                u->ep0_len = string_desc(u->strings[(value & 0xffu) - 1u], u->ep0, sizeof u->ep0);
                return ESK_USB_ACK;
            }
            return ESK_USB_STALL;
        default:
            return ESK_USB_STALL; /* a class descriptor: the class's */
        }
    case 5: /* SET_ADDRESS: in effect after the status stage */
        u->new_addr = (int)(value & 0x7fu);
        u->addr_pending = 1;
        return ESK_USB_ACK;
    case 9: /* SET_CONFIGURATION */
        if (value > 1u)
            return ESK_USB_STALL;
        u->config = (int)value;
        memset(u->pid_in, 0, sizeof u->pid_in);
        memset(u->pid_out, 0, sizeof u->pid_out);
        u->halted_in = u->halted_out = 0;
        if (u->cls && u->cls->configured)
            u->cls->configured(u, u->config);
        return ESK_USB_ACK;
    case 8: /* GET_CONFIGURATION */
        u->ep0[0] = (uint8_t)u->config;
        u->ep0_len = 1;
        return ESK_USB_ACK;
    case 10: /* GET_INTERFACE */
        u->ep0[0] = (uint8_t)u->alt;
        u->ep0_len = 1;
        return ESK_USB_ACK;
    case 11: /* SET_INTERFACE: one alternate setting each */
        return value == 0 ? ESK_USB_ACK : ESK_USB_STALL;
    case 0: /* GET_STATUS */
        u->ep0[0] = u->ep0[1] = 0;
        if ((type & 3u) == 0)
            u->ep0[0] = (uint8_t)((u->cfg_desc[7] & 0x40u ? 1u : 0u) | (u->remote_wakeup ? 2u : 0u));
        else if ((type & 3u) == 2)
            u->ep0[0] = (uint8_t)(((in ? u->halted_in : u->halted_out) >> ep) & 1u);
        u->ep0_len = 2;
        return ESK_USB_ACK;
    case 1: /* CLEAR_FEATURE */
    case 3: /* SET_FEATURE */
        if ((type & 3u) == 0 && value == 1) { /* DEVICE_REMOTE_WAKEUP */
            u->remote_wakeup = req == 3;
            return ESK_USB_ACK;
        }
        if ((type & 3u) == 2 && value == 0 && ep) { /* ENDPOINT_HALT */
            uint32_t *h = in ? &u->halted_in : &u->halted_out;
            if (req == 3)
                *h |= 1u << ep;
            else {
                *h &= ~(1u << ep);
                if (in)
                    u->pid_in[ep] = 0;
                else
                    u->pid_out[ep] = 0;
            }
            return ESK_USB_ACK;
        }
        return ESK_USB_STALL;
    default:
        return ESK_USB_STALL;
    }
}

static int run_setup(usbd *u, const uint8_t *data)
{
    uint32_t len = 0;
    int r;
    u->ep0_len = 0;
    if ((u->setup[0] & 0x60u) == 0) {
        r = standard(u, u->setup);
        if (r == ESK_USB_ACK || !u->cls || !u->cls->setup)
            return r;
        /* a standard request for the class's part: its descriptors on an
         * interface (a HID's report descriptor) */
    }
    r = u->cls && u->cls->setup ? u->cls->setup(u, u->setup, data, u->ep0, sizeof u->ep0, &len) : ESK_USB_STALL;
    u->ep0_len = len;
    return r;
}

static int d_setup(void *ctx, int addr, int ep, const uint8_t data[8], es_time_t at)
{
    usbd *u = (usbd *)ctx;
    unsigned wlen;
    (void)at;
    if (!for_me(u, addr) || ep != 0)
        return ESK_USB_NONE;
    u->setups++;
    memcpy(u->setup, data, 8);
    wlen = (unsigned)data[6] | (unsigned)data[7] << 8;
    u->ep0_dir_in = (data[0] & 0x80u) != 0;
    u->ep0_pos = 0;
    u->ep0_stalled = 0;
    u->ep0_status_zlp_sent = 0;
    u->pid_in[0] = u->pid_out[0] = 1;
    if (!u->ep0_dir_in && wlen) { /* the OUT data comes first */
        u->ep0_out_want = wlen > sizeof u->ep0 ? (uint32_t)sizeof u->ep0 : wlen;
        u->ep0_len = 0;
        return ESK_USB_ACK; /* a SETUP is always ACKed */
    }
    u->ep0_out_want = 0;
    if (run_setup(u, NULL) != ESK_USB_ACK)
        u->ep0_stalled = 1;
    else if (u->ep0_dir_in && u->ep0_len > wlen)
        u->ep0_len = wlen;
    return ESK_USB_ACK;
}

static int d_out(void *ctx, int addr, int ep, int data1, const uint8_t *data, uint32_t len, es_time_t at)
{
    usbd *u = (usbd *)ctx;
    int r;
    (void)at;
    if (!for_me(u, addr) || ep < 0 || ep >= USBD_EPS)
        return ESK_USB_NONE;
    if (ep == 0) {
        if (u->ep0_stalled)
            return ESK_USB_STALL;
        if (u->ep0_out_want) { /* the data stage of a control write */
            if ((data1 != 0) != (u->pid_out[0] != 0))
                return ESK_USB_ACK;
            if (u->ep0_len + len > u->ep0_out_want)
                len = u->ep0_out_want - u->ep0_len;
            memcpy(u->ep0 + u->ep0_len, data, len);
            u->ep0_len += len;
            u->pid_out[0] ^= 1;
            if (u->ep0_len >= u->ep0_out_want) {
                uint8_t buf[1024];
                memcpy(buf, u->ep0, u->ep0_len);
                u->ep0_out_want = 0;
                if (run_setup(u, buf) != ESK_USB_ACK)
                    u->ep0_stalled = 1;
                u->ep0_len = 0;
            }
            return ESK_USB_ACK;
        }
        return ESK_USB_ACK; /* the status stage of a control read */
    }
    if (!u->config)
        return ESK_USB_NONE;
    if ((u->halted_out >> ep) & 1u)
        return ESK_USB_STALL;
    if ((data1 != 0) != (u->pid_out[ep] != 0))
        return ESK_USB_ACK; /* a repeat: dropped */
    r = u->cls && u->cls->out ? u->cls->out(u, ep, data, len) : ESK_USB_STALL;
    if (r == ESK_USB_ACK)
        u->pid_out[ep] ^= 1;
    return r;
}

static int d_in(void *ctx, int addr, int ep, uint8_t *data, uint32_t cap, uint32_t *len, int *data1, es_time_t at)
{
    usbd *u = (usbd *)ctx;
    int r;
    (void)at;
    if (!for_me(u, addr) || ep < 0 || ep >= USBD_EPS)
        return ESK_USB_NONE;
    if (ep == 0) {
        uint32_t n, mps = usbd_mps0(u);
        if (u->ep0_stalled)
            return ESK_USB_STALL;
        if (u->ep0_out_want) /* the data stage is not done */
            return ESK_USB_NAK;
        if (!u->ep0_dir_in || u->ep0_pos >= u->ep0_len) {
            /* the status stage of a control write, or a read's end */
            if (!u->ep0_dir_in || !u->ep0_last_full || u->ep0_status_zlp_sent) {
                *len = 0;
                *data1 = 1;
                if (!u->ep0_dir_in && u->addr_pending) { /* SET_ADDRESS done */
                    u->addr = u->new_addr;
                    u->addr_pending = 0;
                }
                return ESK_USB_ACK;
            }
        }
        n = u->ep0_len - u->ep0_pos;
        if (n > mps)
            n = mps;
        if (n > cap)
            n = cap;
        memcpy(data, u->ep0 + u->ep0_pos, n);
        u->ep0_pos += n;
        u->ep0_last_full = n == mps;
        if (n == 0)
            u->ep0_status_zlp_sent = 1;
        *len = n;
        *data1 = u->pid_in[0];
        u->pid_in[0] ^= 1;
        return ESK_USB_ACK;
    }
    if (!u->config)
        return ESK_USB_NONE;
    if ((u->halted_in >> ep) & 1u)
        return ESK_USB_STALL;
    r = u->cls && u->cls->in ? u->cls->in(u, ep, data, cap, len) : ESK_USB_STALL;
    if (r == ESK_USB_ACK) {
        *data1 = u->pid_in[ep];
        u->pid_in[ep] ^= 1;
    }
    return r;
}

es_status usbd_init(usbd *u, esk_comp *c, int low_speed, const uint8_t *dev_desc, const uint8_t *cfg_desc,
                    uint32_t cfg_len, const char *const *strings, int nstrings, const usbd_class *cls,
                    void *cls_ctx)
{
    int i;
    memset(u, 0, sizeof *u);
    u->c = c;
    u->low_speed = low_speed;
    u->dev_desc = dev_desc;
    u->cfg_desc = cfg_desc;
    u->cfg_len = cfg_len;
    for (i = 0; i < nstrings && i < 7; i++)
        u->strings[i] = strings[i];
    u->cls = cls;
    u->cls_ctx = cls_ctx;
    u->plugged = 1;
    u->vbus = esk_comp_pin(c, "VBUS");
    u->link.ctx = u;
    u->link.comp = c;
    u->link.dp = esk_comp_pin(c, "DP");
    u->link.pullup = d_pullup;
    u->link.bus = d_bus;
    u->link.setup = d_setup;
    u->link.out = d_out;
    u->link.in = d_in;
    if (esk_usb_attach_dev(c->e, &u->link) != ES_OK)
        return ES_ENOMEM;
    u->linked = 1;
    return ES_OK;
}

void usbd_destroy(usbd *u)
{
    if (u->linked)
        esk_usb_detach_dev(u->c->e, &u->link);
    u->linked = 0;
}

void usbd_reset(usbd *u)
{
    bus_reset(u);
    u->suspended = 0;
    u->frames = u->setups = 0;
    u->plugged = esk_comp_input_bool(u->c, "plugged", 1);
    if (u->vbus && u->vbus->net) {
        (void)esk_pin_compare(u->vbus, VBUS_VALID_V);
        esk_pin_watch(u->vbus, 1);
    }
}

void usbd_power(usbd *u)
{
    if (!powered(u))
        bus_reset(u);
    esk_usb_changed(u->c->e, &u->link, esk_comp_now(u->c));
}

const char *usbd_state(const usbd *u)
{
    if (!powered(u))
        return "detached";
    if (u->config)
        return "configured";
    if (u->addr || u->addr_pending)
        return "address";
    return "default";
}
