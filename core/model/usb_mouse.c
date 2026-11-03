/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* usb_mouse.c -- a USB mouse: a low-speed HID boot mouse (HID 1.11,
 * appendix B.2 and E.10) on a cable of the bench (usb_dev.h).
 *
 *   - its descriptors: a device of class 0, one configuration (bus
 *     powered, remote wakeup, 100 mA), one interface of class 3, subclass
 *     1 (boot), protocol 2 (mouse), its HID descriptor and an interrupt IN
 *     endpoint 0x81 of 4 bytes polled every 10 ms; EP0's 8-byte packets, as
 *     a low-speed device has them;
 *   - its report: buttons (3 bits), X, Y and the wheel, each a signed
 *     byte, the report protocol's report being the boot protocol's (its
 *     report descriptor says so);
 *   - the inputs move it (move_x, move_y, wheel: counts added) and press
 *     its buttons (buttons, bit 0 left): a report goes at the next poll
 *     when something changed, at most 127 counts each way in one, the rest
 *     in the next; nothing changed, the endpoint NAKs, or repeats the last
 *     report every SET_IDLE duration (HID 7.2.4; 0: never, the default
 *     after SET_IDLE(0));
 *   - GET_REPORT, GET_IDLE, SET_IDLE, GET_PROTOCOL, SET_PROTOCOL; the HID
 *     and report descriptors by GET_DESCRIPTOR on the interface;
 *   - its outputs: what it has sent and the host's view of it.
 * Inside: no optical sensor (the movement is the inputs'), no remote
 * wakeup signalling yet. */
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"
#include "model/usb_dev.h"

static const uint8_t dev_desc[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 8,   /* USB 2.0, class in the interface, EP0 8 bytes */
    0x6D, 0x04, 0x6C, 0xC0,          /* idVendor 0x046D, idProduct 0xC06C: a generic optical mouse's */
    0x00, 0x01, 1, 2, 0, 1,          /* bcdDevice 1.00, strings 1, 2, no serial, one configuration */
};

static const uint8_t report_desc[50] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03,
    0x81, 0x06, /* X, Y, wheel: relative */
};
#define REPORT_LEN 4u

static const uint8_t cfg_desc[34] = {
    9, 2, 34, 0, 1, 1, 0, 0xA0, 50,               /* bus powered, remote wakeup, 100 mA */
    9, 4, 0, 0, 1, 3, 1, 2, 0,                    /* HID, boot, mouse */
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof report_desc, 0,
    7, 5, 0x81, 3, REPORT_LEN, 0, 10,             /* interrupt IN, 4 bytes, 10 ms */
};

static const char *const strings[2] = { "EmbSIM", "USB Optical Mouse" };

typedef struct mouse_state {
    usbd u;
    int64_t dx, dy, dw; /* movement still to send */
    int buttons, sent_buttons;
    int idle_4ms, protocol;
    uint8_t last[REPORT_LEN];
    uint64_t reports;
    int64_t sent_x, sent_y;
    uint64_t idle_frame;
} mouse_state;

static int clamp(int64_t *v)
{
    int64_t n = *v > 127 ? 127 : *v < -127 ? -127 : *v;
    *v -= n;
    return (int)n;
}

static void make_report(mouse_state *m, uint8_t *r)
{
    int x = clamp(&m->dx), y = clamp(&m->dy), w = clamp(&m->dw);
    r[0] = (uint8_t)(m->buttons & 7);
    r[1] = (uint8_t)(int8_t)x;
    r[2] = (uint8_t)(int8_t)y;
    r[3] = (uint8_t)(int8_t)w;
    m->sent_x += x;
    m->sent_y += y;
}

static int m_setup(usbd *u, const uint8_t s[8], const uint8_t *data, uint8_t *out, uint32_t cap, uint32_t *len)
{
    mouse_state *m = (mouse_state *)u->cls_ctx;
    (void)data;
    (void)cap;
    if ((s[0] & 0x60u) != 0x20u) /* class */
        return ESK_USB_STALL;
    switch (s[1]) {
    case 0x01: /* GET_REPORT */
        memcpy(out, m->last, REPORT_LEN);
        *len = REPORT_LEN;
        return ESK_USB_ACK;
    case 0x02: /* GET_IDLE */
        out[0] = (uint8_t)m->idle_4ms;
        *len = 1;
        return ESK_USB_ACK;
    case 0x03: /* GET_PROTOCOL */
        out[0] = (uint8_t)m->protocol;
        *len = 1;
        return ESK_USB_ACK;
    case 0x0A: /* SET_IDLE: the duration in 4 ms units */
        m->idle_4ms = s[3];
        m->idle_frame = u->frames;
        return ESK_USB_ACK;
    case 0x0B: /* SET_PROTOCOL */
        m->protocol = s[2] & 1;
        return ESK_USB_ACK;
    default:
        return ESK_USB_STALL;
    }
}

static int m_in(usbd *u, int ep, uint8_t *data, uint32_t cap, uint32_t *len)
{
    mouse_state *m = (mouse_state *)u->cls_ctx;
    int changed = m->dx || m->dy || m->dw || m->buttons != m->sent_buttons;
    int idle_due = m->idle_4ms && u->frames - m->idle_frame >= (uint64_t)m->idle_4ms * 4u;
    if (ep != 1 || cap < REPORT_LEN)
        return ESK_USB_STALL;
    if (!changed && !idle_due)
        return ESK_USB_NAK;
    if (changed)
        make_report(m, m->last);
    m->sent_buttons = m->buttons;
    m->idle_frame = u->frames;
    memcpy(data, m->last, REPORT_LEN);
    *len = REPORT_LEN;
    m->reports++;
    return ESK_USB_ACK;
}

static void m_configured(usbd *u, int cfg)
{
    mouse_state *m = (mouse_state *)u->cls_ctx;
    (void)cfg;
    m->idle_4ms = 0;
    m->protocol = 1; /* report protocol after a reset (HID 7.2.6) */
}

/* the class's descriptors: GET_DESCRIPTOR on the interface (HID 7.1) */
static int m_setup_std(usbd *u, const uint8_t s[8], const uint8_t *data, uint8_t *out, uint32_t cap, uint32_t *len)
{
    if ((s[0] & 0x60u) == 0 && s[1] == 6 && (s[0] & 3u) == 1) {
        if (s[3] == 0x22) {
            memcpy(out, report_desc, sizeof report_desc);
            *len = sizeof report_desc;
            return ESK_USB_ACK;
        }
        if (s[3] == 0x21) {
            memcpy(out, cfg_desc + 18, 9);
            *len = 9;
            return ESK_USB_ACK;
        }
    }
    return m_setup(u, s, data, out, cap, len);
}

static const usbd_class mouse_class = { m_setup_std, m_in, NULL, m_configured };

static es_status m_create(esk_comp *c)
{
    mouse_state *m = (mouse_state *)c->state;
    return usbd_init(&m->u, c, 1, dev_desc, cfg_desc, sizeof cfg_desc, strings, 2, &mouse_class, m);
}

static void m_destroy(esk_comp *c)
{
    mouse_state *m = (mouse_state *)c->state;
    if (m)
        usbd_destroy(&m->u);
}

static void m_reset(esk_comp *c)
{
    mouse_state *m = (mouse_state *)c->state;
    m->dx = m->dy = m->dw = 0;
    m->buttons = m->sent_buttons = 0;
    m->idle_4ms = 0;
    m->protocol = 1;
    memset(m->last, 0, sizeof m->last);
    m->reports = 0;
    m->sent_x = m->sent_y = 0;
    usbd_reset(&m->u);
}

static void m_input(esk_comp *c, esk_pin *pin)
{
    mouse_state *m = (mouse_state *)c->state;
    (void)pin;
    usbd_power(&m->u);
}

static es_status m_set(esk_comp *c, const char *input, const esk_json *value)
{
    mouse_state *m = (mouse_state *)c->state;
    if (!strcmp(input, "plugged") && value->type == ESK_JSON_BOOL) {
        m->u.plugged = value->u.boolean != 0;
        usbd_power(&m->u);
        return ES_OK;
    }
    if (value->type != ESK_JSON_NUMBER)
        return ES_EINVAL;
    if (!strcmp(input, "move_x"))
        m->dx += (int64_t)value->u.number.value;
    else if (!strcmp(input, "move_y"))
        m->dy += (int64_t)value->u.number.value;
    else if (!strcmp(input, "wheel"))
        m->dw += (int64_t)value->u.number.value;
    else if (!strcmp(input, "buttons"))
        m->buttons = (int)value->u.number.value & 7;
    else
        return ES_EINVAL;
    return ES_OK;
}

static void m_observe(esk_comp *c, esk_json *out)
{
    mouse_state *m = (mouse_state *)c->state;
    esk_alloc *a = c->e->a;
    esk_json_set_string(a, out, "state", usbd_state(&m->u));
    esk_json_set_integer(a, out, "address", m->u.addr);
    esk_json_set_integer(a, out, "reports", (int64_t)m->reports);
    esk_json_set_integer(a, out, "sent_x", m->sent_x);
    esk_json_set_integer(a, out, "sent_y", m->sent_y);
    esk_json_set_integer(a, out, "protocol", m->protocol);
    esk_json_set_integer(a, out, "idle_ms", m->idle_4ms * 4);
}

const esk_model_ops esk_usb_mouse_ops = { "usb_mouse", sizeof(mouse_state), m_create, m_destroy, m_reset, m_input,
                                          NULL, m_set, m_observe, NULL, 0 };
