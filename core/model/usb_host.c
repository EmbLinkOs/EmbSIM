/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* usb_host.c -- a computer's USB 2.0 port as the device on its cable sees
 * it (engine/usb.h, ADR-031): the host side of the bus, enumerating what
 * is plugged in the way the operating systems do.
 *
 *   - a device's pull-up on D+ is an attach: after the 100 ms debounce
 *     (USB 2.0 7.1.7.3, TATTDB) the port is reset for `reset_ms` (50 ms,
 *     a root port's TDRSTR, 7.1.7.5), then frames start: a SOF every
 *     1.000 ms with its 11-bit number;
 *   - after the 10 ms reset recovery (TRSTRCY) the host reads the first
 *     64 bytes of the device descriptor at address 0, resets the port
 *     again (as Windows and Linux do), sets address `address` (and waits
 *     2 ms, 9.2.6.3), reads the device descriptor, the configuration's
 *     first 9 bytes and then all of it, the language table and the
 *     manufacturer, product and serial strings, and sets the first
 *     configuration; for each HID interface it sends SET_IDLE (a STALL is
 *     allowed, HID 1.11 7.2.4) and reads its report descriptor;
 *   - control transfers go as SETUP, the data stage's IN packets of the
 *     device's bMaxPacketSize0 (with their DATA0/DATA1 toggles checked),
 *     then the status stage; a NAK is tried again at the next chance, a
 *     time-out (no answer) up to three times, a STALL or 5 s fail the
 *     request (9.2.6.4) and enumeration stops with the error in the log;
 *   - once configured, each HID interface's interrupt IN endpoint is
 *     polled every bInterval frames, at the start of the frame (periodic
 *     before control traffic, as host controllers schedule them); a boot
 *     mouse's reports (buttons, X, Y) move its cursor;
 *   - each transaction takes its time on the bus: its packets' bits at
 *     12 Mbit/s with their SYNC, PID, CRC, EOP and gaps (esk_usb_fs_time),
 *     and none crosses a frame's start;
 *   - VBUS is the port's 5 V (the component's composite), through the
 *     cable's resistance.
 * Inside: no class driver beyond HID's polling (a CDC, MSC or DFU device
 * is enumerated and configured, then left idle); one device, no hub; the
 * port's 500 mA limit and its over-current shutdown; suspend, resume and
 * remote wakeup; low-speed devices. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"

#define LOG_CAP 8192u
#define BUF_CAP 1024u
#define MAX_HID 4
#define PS_US UINT64_C(1000000)
#define PS_MS UINT64_C(1000000000)
#define FRAME_PS PS_MS
#define CTRL_TIMEOUT_PS (5u * 1000u * PS_MS)
#define ERR_RETRIES 3
/* the bus time a token takes when nothing answers: the device's
 * turnaround limit, 16 bit times (USB 2.0 7.1.19.1) */
#define TIMEOUT_EXTRA_PS UINT64_C(1333333)
/* the end of a frame the host keeps free (EOF1): no transaction starts in
 * the last 32 bit times or so */
#define EOF_PS (3u * PS_US)

enum { T_RUN = 1 };
enum { P_DETACHED, P_DEBOUNCE, P_RESET, P_RECOVERY, P_RUN };
enum { S_DETACHED, S_ATTACHED, S_DEFAULT, S_ADDRESS, S_CONFIGURED, S_ERROR };
enum { C_SETUP, C_DATA_IN, C_STATUS_OUT, C_STATUS_IN };
enum {
    E_DESC64, E_SET_ADDRESS, E_DESC18, E_CFG9, E_CFG, E_LANG, E_STR_MANU, E_STR_PROD, E_STR_SER, E_SET_CONFIG,
    E_HID_IDLE, E_HID_REPORT, E_DONE, E_WAKE_ON, E_WAKE_OFF
};

static const char *const state_names[] = { "detached", "attached", "default", "address", "configured", "error" };
#define RESUME_PS (20u * PS_MS) /* TDRSMDN: the host's resume signalling (USB 2.0 7.1.7.7) */
#define VBUS_SENSE_V 4.0        /* VBUS at the cable's device end: plugged */

typedef struct uh_hid {
    int intf, ep, mps, interval, pid, mouse;
    uint32_t report_len; /* from its HID descriptor */
    uint32_t next_frame;
    int due;
} uh_hid;

typedef struct uh_state {
    esk_comp *c;
    esk_usb_host link;
    int linked;
    /* parameters */
    es_time_t debounce_ps, reset_ps;
    int new_addr;
    /* the bus */
    int phase;
    es_time_t phase_end, next_sof, busy_until, wake_at;
    int wake_set, sof_on, resets;
    uint32_t frame;
    uint64_t frames;
    int after_reset; /* the step to go on with when the reset ends */
    /* suspend and resume */
    esk_pin *vbus;      /* VBUS at the device end of the cable, or NULL */
    int allow_wakeup;   /* the host arms remote wakeup before a suspend */
    int want_suspend;   /* the `suspend` input */
    int suspended, resuming, wake_armed;
    es_time_t resume_end;
    uint64_t suspends, wakeups;
    /* the device */
    int state, addr, mps0, step, hid_i, cfg_value;
    uint8_t dev[18];
    uint8_t cfg[BUF_CAP];
    uint32_t cfg_len;
    uint8_t rep[BUF_CAP];
    uint32_t rep_len;
    uint16_t lang;
    int i_manu, i_prod, i_ser;
    char manu[128], prod[128], ser[128];
    uh_hid hid[MAX_HID];
    int nhid;
    /* the control transfer in progress */
    struct {
        int on, stage, pid, errs;
        uint8_t setup[8];
        uint8_t buf[BUF_CAP];
        uint32_t want, got;
        es_time_t deadline;
    } ct;
    /* what came */
    uint64_t reports;
    uint8_t last[64];
    uint32_t last_len;
    int64_t x, y;
    int buttons;
    char log[LOG_CAP];
    size_t log_len;
} uh_state;

static uh_state *st_of(esk_comp *c) { return (uh_state *)c->state; }

static void say(uh_state *s, const char *fmt, ...)
{
    char line[256];
    es_time_t now = esk_comp_now(s->c);
    va_list ap;
    int n = snprintf(line, sizeof line, "%llu.%03llu ms: ", (unsigned long long)(now / PS_MS),
                     (unsigned long long)(now % PS_MS / PS_US));
    size_t len;
    if (n < 0)
        return;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    len = strlen(line);
    if (len + 2u > LOG_CAP)
        return;
    if (s->log_len + len + 2u > LOG_CAP) { /* the oldest lines go */
        size_t cut = s->log_len + len + 2u - LOG_CAP;
        const char *nl = (const char *)memchr(s->log + cut, '\n', s->log_len - cut);
        cut = nl ? (size_t)(nl - s->log) + 1u : s->log_len;
        memmove(s->log, s->log + cut, s->log_len - cut);
        s->log_len -= cut;
    }
    memcpy(s->log + s->log_len, line, len);
    s->log_len += len;
    s->log[s->log_len++] = '\n';
    s->log[s->log_len] = 0;
}

static void kick(uh_state *s, es_time_t t)
{
    es_time_t now = esk_comp_now(s->c);
    if (t < now)
        t = now;
    if (s->wake_set && s->wake_at <= t)
        return;
    if (s->wake_set)
        esk_comp_wake_cancel(s->c, T_RUN);
    s->wake_set = 1;
    s->wake_at = t;
    (void)esk_comp_wake_at(s->c, t, T_RUN);
}

static esk_usb_dev *device(uh_state *s) { return esk_usb_device_of(s->c->e, &s->link); }

/* the cable is in: VBUS at its device end (no VBUS pin wired: always) */
static int plugged(uh_state *s)
{
    esk_pin *p = s->vbus;
    if (!p || !p->net)
        return 1;
    if (p->watch_on)
        return p->watch_state == ESK_L1;
    return esk_net_value(p->net) == ESK_L1;
}

static int attached(uh_state *s)
{
    esk_usb_dev *d = device(s);
    return plugged(s) && d && d->pullup(d->ctx) != ESK_USB_DETACHED;
}

/* ---- descriptors ----------------------------------------------------------- */

static unsigned u16le(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

static void parse_config(uh_state *s)
{
    uint32_t i = 0;
    int intf = -1, is_hid = 0, proto = 0;
    uint32_t rlen = 0;
    s->nhid = 0;
    s->cfg_value = s->cfg_len >= 6 ? s->cfg[5] : 1;
    while (i + 2u <= s->cfg_len && s->cfg[i] >= 2) {
        const uint8_t *d = s->cfg + i;
        uint32_t len = d[0];
        if (i + len > s->cfg_len)
            break;
        if (d[1] == 4 && len >= 9) { /* interface */
            intf = d[2];
            is_hid = d[5] == 3;
            proto = d[7];
            rlen = 0;
        } else if (d[1] == 0x21 && len >= 9 && is_hid) { /* HID */
            if (d[6] == 0x22)
                rlen = u16le(d + 7);
        } else if (d[1] == 5 && len >= 7 && is_hid && (d[2] & 0x80) && (d[3] & 3) == 3 && s->nhid < MAX_HID) {
            uh_hid *h = &s->hid[s->nhid++];
            memset(h, 0, sizeof *h);
            h->intf = intf;
            h->ep = d[2] & 0x0f;
            h->mps = (int)(u16le(d + 4) & 0x7ffu);
            h->interval = d[6] ? d[6] : 1;
            h->mouse = proto == 2;
            h->report_len = rlen;
        }
        i += len;
    }
}

static void string_of(const uint8_t *b, uint32_t got, char *out, size_t cap)
{
    uint32_t i, n = 0, len = got >= 2 ? (b[0] < got ? b[0] : got) : 0;
    for (i = 2; i + 1u < len && n + 1u < cap; i += 2) {
        unsigned ch = u16le(b + i);
        out[n++] = ch >= 32 && ch < 127 ? (char)ch : '?';
    }
    out[n] = 0;
}

static void hex_of(const uint8_t *p, uint32_t n, char *out, size_t cap)
{
    static const char hex[] = "0123456789abcdef";
    uint32_t i;
    size_t k = 0;
    for (i = 0; i < n && k + 2u < cap; i++) {
        out[k++] = hex[p[i] >> 4];
        out[k++] = hex[p[i] & 15u];
    }
    out[k] = 0;
}

/* ---- control transfers ------------------------------------------------------ */

static void control(uh_state *s, int type, int req, unsigned value, unsigned index, unsigned length)
{
    uint8_t *p = s->ct.setup;
    p[0] = (uint8_t)type;
    p[1] = (uint8_t)req;
    p[2] = (uint8_t)(value & 0xffu);
    p[3] = (uint8_t)(value >> 8);
    p[4] = (uint8_t)(index & 0xffu);
    p[5] = (uint8_t)(index >> 8);
    p[6] = (uint8_t)(length & 0xffu);
    p[7] = (uint8_t)(length >> 8);
    s->ct.on = 1;
    s->ct.stage = C_SETUP;
    s->ct.errs = 0;
    s->ct.want = length > BUF_CAP ? BUF_CAP : length;
    s->ct.got = 0;
    s->ct.deadline = esk_comp_now(s->c) + CTRL_TIMEOUT_PS;
}

static const char *step_name(int step)
{
    switch (step) {
    case E_DESC64: return "GET_DESCRIPTOR(device, 64) at address 0";
    case E_SET_ADDRESS: return "SET_ADDRESS";
    case E_DESC18: return "GET_DESCRIPTOR(device)";
    case E_CFG9: return "GET_DESCRIPTOR(configuration, 9)";
    case E_CFG: return "GET_DESCRIPTOR(configuration)";
    case E_LANG: return "GET_DESCRIPTOR(string 0)";
    case E_STR_MANU: return "GET_DESCRIPTOR(manufacturer string)";
    case E_STR_PROD: return "GET_DESCRIPTOR(product string)";
    case E_STR_SER: return "GET_DESCRIPTOR(serial number string)";
    case E_SET_CONFIG: return "SET_CONFIGURATION";
    case E_HID_IDLE: return "SET_IDLE";
    case E_HID_REPORT: return "GET_DESCRIPTOR(HID report)";
    case E_WAKE_ON: return "SET_FEATURE(DEVICE_REMOTE_WAKEUP)";
    case E_WAKE_OFF: return "CLEAR_FEATURE(DEVICE_REMOTE_WAKEUP)";
    default: return "?";
    }
}

static void start_reset(uh_state *s, int then);

/* the next request of enumeration, from `step` on */
static void begin(uh_state *s, int step)
{
    for (;;) {
        s->step = step;
        switch (step) {
        case E_DESC64: control(s, 0x80, 6, 0x0100, 0, 64); return;
        case E_SET_ADDRESS: control(s, 0x00, 5, (unsigned)s->new_addr, 0, 0); return;
        case E_DESC18: control(s, 0x80, 6, 0x0100, 0, 18); return;
        case E_CFG9: control(s, 0x80, 6, 0x0200, 0, 9); return;
        case E_CFG: control(s, 0x80, 6, 0x0200, 0, s->cfg_len); return;
        case E_LANG:
            if (!s->i_manu && !s->i_prod && !s->i_ser) {
                step = E_SET_CONFIG;
                continue;
            }
            control(s, 0x80, 6, 0x0300, 0, 255);
            return;
        case E_STR_MANU:
        case E_STR_PROD:
        case E_STR_SER: {
            int idx = step == E_STR_MANU ? s->i_manu : step == E_STR_PROD ? s->i_prod : s->i_ser;
            if (!idx || !s->lang) {
                step++;
                continue;
            }
            control(s, 0x80, 6, 0x0300u | (unsigned)idx, s->lang, 255);
            return;
        }
        case E_SET_CONFIG: control(s, 0x00, 9, (unsigned)s->cfg_value, 0, 0); return;
        case E_HID_IDLE:
            if (s->hid_i >= s->nhid) {
                step = E_DONE;
                continue;
            }
            control(s, 0x21, 0x0A, 0, (unsigned)s->hid[s->hid_i].intf, 0);
            return;
        case E_HID_REPORT:
            control(s, 0x81, 6, 0x2200, (unsigned)s->hid[s->hid_i].intf,
                    s->hid[s->hid_i].report_len ? s->hid[s->hid_i].report_len : 255u);
            return;
        case E_WAKE_ON: control(s, 0x00, 3, 1, 0, 0); return;
        case E_WAKE_OFF: control(s, 0x00, 1, 1, 0, 0); return;
        default:
            s->step = E_DONE;
            s->ct.on = 0;
            return;
        }
    }
}

/* the port stops its frames: the device suspends after 3 ms of idle */
static void enter_suspend(uh_state *s)
{
    s->suspended = 1;
    s->sof_on = 0;
    s->suspends++;
    say(s, "port suspended%s", s->wake_armed ? " (remote wakeup enabled)" : "");
}

/* the host drives resume (K) for 20 ms, then EOP and frames again */
static void start_resume(uh_state *s, const char *why)
{
    esk_usb_dev *d = device(s);
    es_time_t now = esk_comp_now(s->c);
    if (!s->suspended || s->resuming)
        return;
    s->resuming = 1;
    s->resume_end = now + RESUME_PS;
    if (d)
        d->bus(d->ctx, ESK_USB_RESUME, 0, now);
    say(s, "%s: resume signalling (20 ms)", why);
}

static void fail(uh_state *s, const char *why)
{
    s->ct.on = 0;
    s->state = S_ERROR;
    say(s, "%s failed (%s): enumeration stops", step_name(s->step), why);
    esk_comp_diag(s->c, ES_SEV_WARNING, "usb.enumeration", "the device did not enumerate: %s failed (%s)",
                  step_name(s->step), why);
}

/* a request ended well: what it brought, then the next */
static void done(uh_state *s)
{
    const uint8_t *b = s->ct.buf;
    uint32_t got = s->ct.got;
    int ok = 1;
    s->ct.on = 0;
    switch (s->step) {
    case E_DESC64:
        if (got < 8) {
            fail(s, "the descriptor is shorter than 8 bytes");
            return;
        }
        s->mps0 = b[7] == 8 || b[7] == 16 || b[7] == 32 || b[7] == 64 ? b[7] : 64;
        say(s, "device descriptor read: bMaxPacketSize0 %d", s->mps0);
        start_reset(s, E_SET_ADDRESS);
        return;
    case E_SET_ADDRESS:
        s->addr = s->new_addr;
        s->state = S_ADDRESS;
        say(s, "address %d", s->addr);
        s->phase = P_RECOVERY;
        s->phase_end = esk_comp_now(s->c) + 2u * PS_MS; /* SET_ADDRESS recovery */
        s->step = E_DESC18;
        return;
    case E_DESC18:
        if (got < 18) {
            fail(s, "the descriptor is shorter than 18 bytes");
            return;
        }
        memcpy(s->dev, b, 18);
        s->i_manu = b[14];
        s->i_prod = b[15];
        s->i_ser = b[16];
        say(s, "new full-speed USB device: idVendor %04x, idProduct %04x, bcdDevice %x.%02x", u16le(b + 8),
            u16le(b + 10), (unsigned)b[13], (unsigned)b[12]);
        break;
    case E_CFG9:
        if (got < 9) {
            fail(s, "the configuration descriptor is shorter than 9 bytes");
            return;
        }
        s->cfg_len = u16le(b + 2) < BUF_CAP ? u16le(b + 2) : BUF_CAP;
        break;
    case E_CFG:
        memcpy(s->cfg, b, got);
        s->cfg_len = got;
        parse_config(s);
        say(s, "configuration read: %u bytes", (unsigned)got);
        break;
    case E_LANG:
        s->lang = (uint16_t)(got >= 4 ? u16le(b + 2) : 0u);
        break;
    case E_STR_MANU:
        string_of(b, got, s->manu, sizeof s->manu);
        say(s, "Manufacturer: %s", s->manu);
        break;
    case E_STR_PROD:
        string_of(b, got, s->prod, sizeof s->prod);
        say(s, "Product: %s", s->prod);
        break;
    case E_STR_SER:
        string_of(b, got, s->ser, sizeof s->ser);
        say(s, "SerialNumber: %s", s->ser);
        break;
    case E_SET_CONFIG:
        s->state = S_CONFIGURED;
        say(s, "configuration %d set", s->cfg_value);
        s->hid_i = 0;
        break;
    case E_HID_IDLE:
        break;
    case E_WAKE_ON:
        s->wake_armed = 1;
        s->step = E_DONE;
        if (s->want_suspend)
            enter_suspend(s);
        return;
    case E_WAKE_OFF:
        s->wake_armed = 0;
        s->step = E_DONE;
        return;
    case E_HID_REPORT:
        if (s->hid_i == 0) {
            memcpy(s->rep, b, got);
            s->rep_len = got;
        }
        say(s, "HID interface %d ready", s->hid[s->hid_i].intf);
        s->hid[s->hid_i].next_frame = s->frame;
        s->hid[s->hid_i].pid = 0;
        s->hid_i++;
        begin(s, E_HID_IDLE);
        return;
    default:
        ok = 0;
        break;
    }
    if (ok)
        begin(s, s->step + 1);
}

/* a request's STALL: the ones that may be refused go on */
static void stalled(uh_state *s)
{
    if (s->step == E_HID_IDLE) { /* HID 1.11 7.2.4: a device may not support it */
        s->ct.on = 0;
        begin(s, E_HID_REPORT);
        return;
    }
    if (s->step == E_LANG || s->step == E_STR_MANU || s->step == E_STR_PROD || s->step == E_STR_SER) {
        s->ct.on = 0;
        begin(s, s->step == E_LANG ? E_SET_CONFIG : s->step + 1);
        return;
    }
    if (s->step == E_WAKE_ON || s->step == E_WAKE_OFF) { /* not supported: suspend without it */
        s->ct.on = 0;
        s->step = E_DONE;
        if (s->want_suspend && !s->suspended)
            enter_suspend(s);
        return;
    }
    fail(s, "STALL");
}

/* one transaction of the control transfer: its time on the bus */
static es_time_t control_step(uh_state *s, esk_usb_dev *d, es_time_t now)
{
    uint8_t pkt[64];
    uint32_t len = 0;
    int pid = 0, r;
    if (now > s->ct.deadline) {
        fail(s, "no answer in 5 s");
        return 0;
    }
    switch (s->ct.stage) {
    case C_SETUP:
        r = d->setup(d->ctx, s->addr, 0, s->ct.setup, now);
        if (r == ESK_USB_ACK) {
            s->ct.pid = 1;
            s->ct.errs = 0;
            s->ct.stage = s->ct.want == 0 ? C_STATUS_IN : (s->ct.setup[0] & 0x80) ? C_DATA_IN : C_STATUS_IN;
        } else if (++s->ct.errs >= ERR_RETRIES) {
            fail(s, "no handshake to SETUP");
        }
        return esk_usb_fs_time(8, r != ESK_USB_NONE) + (r == ESK_USB_NONE ? TIMEOUT_EXTRA_PS : 0);
    case C_DATA_IN: {
        uint32_t room = s->ct.want - s->ct.got;
        r = d->in(d->ctx, s->addr, 0, pkt, sizeof pkt, &len, &pid, now);
        if (r == ESK_USB_ACK) {
            if (len > (uint32_t)s->mps0) {
                fail(s, "a packet longer than bMaxPacketSize0 (babble)");
                return esk_usb_fs_time((int)len, 1);
            }
            if ((pid != 0) == (s->ct.pid != 0)) {
                memcpy(s->ct.buf + s->ct.got, pkt, len < room ? len : room);
                s->ct.got += len < room ? len : room;
                s->ct.pid ^= 1;
                if (len < (uint32_t)s->mps0 || s->ct.got >= s->ct.want)
                    s->ct.stage = C_STATUS_OUT;
            } /* else a repeat: the host ACKs it and keeps what it has */
            s->ct.errs = 0;
            return esk_usb_fs_time((int)len, 1);
        }
        if (r == ESK_USB_STALL)
            stalled(s);
        else if (r == ESK_USB_NONE && ++s->ct.errs >= ERR_RETRIES)
            fail(s, "no answer to IN");
        return esk_usb_fs_time(-1, r != ESK_USB_NONE) + (r == ESK_USB_NONE ? TIMEOUT_EXTRA_PS : 0);
    }
    case C_STATUS_OUT:
        r = d->out(d->ctx, s->addr, 0, 1, NULL, 0, now);
        if (r == ESK_USB_ACK)
            done(s);
        else if (r == ESK_USB_STALL)
            stalled(s);
        else if (r == ESK_USB_NONE && ++s->ct.errs >= ERR_RETRIES)
            fail(s, "no answer to the status stage");
        return esk_usb_fs_time(0, r != ESK_USB_NONE) + (r == ESK_USB_NONE ? TIMEOUT_EXTRA_PS : 0);
    default: /* C_STATUS_IN */
        r = d->in(d->ctx, s->addr, 0, pkt, sizeof pkt, &len, &pid, now);
        if (r == ESK_USB_ACK) {
            if (len == 0)
                done(s);
            else
                fail(s, "data in the status stage");
            return esk_usb_fs_time((int)len, 1);
        }
        if (r == ESK_USB_STALL)
            stalled(s);
        else if (r == ESK_USB_NONE && ++s->ct.errs >= ERR_RETRIES)
            fail(s, "no answer to the status stage");
        return esk_usb_fs_time(-1, r != ESK_USB_NONE) + (r == ESK_USB_NONE ? TIMEOUT_EXTRA_PS : 0);
    }
}

/* an interrupt IN poll of a HID endpoint */
static es_time_t poll_hid(uh_state *s, esk_usb_dev *d, uh_hid *h, es_time_t now)
{
    uint8_t pkt[64];
    uint32_t len = 0;
    int pid = 0, r = d->in(d->ctx, s->addr, h->ep, pkt, sizeof pkt, &len, &pid, now);
    h->due = 0;
    if (r == ESK_USB_ACK) {
        if ((pid != 0) == (h->pid != 0)) {
            h->pid ^= 1;
            s->reports++;
            memcpy(s->last, pkt, len);
            s->last_len = len;
            if (h->mouse && len >= 3) {
                s->buttons = pkt[0] & 7;
                s->x += (int8_t)pkt[1];
                s->y += (int8_t)pkt[2];
            }
        }
        return esk_usb_fs_time((int)len, 1);
    }
    if (r == ESK_USB_STALL)
        say(s, "HID endpoint %d stalls", h->ep);
    return esk_usb_fs_time(-1, r != ESK_USB_NONE) + (r == ESK_USB_NONE ? TIMEOUT_EXTRA_PS : 0);
}

/* ---- the port ---------------------------------------------------------------- */

static void start_reset(uh_state *s, int then)
{
    esk_usb_dev *d = device(s);
    es_time_t now = esk_comp_now(s->c);
    s->phase = P_RESET;
    s->phase_end = now + s->reset_ps;
    s->sof_on = 0;
    s->after_reset = then;
    s->resets++;
    s->addr = 0;
    s->ct.on = 0;
    if (d)
        d->bus(d->ctx, ESK_USB_RESET, 0, now);
    say(s, "port reset (%u ms)", (unsigned)(s->reset_ps / PS_MS));
}

static void detach(uh_state *s)
{
    if (s->phase != P_DETACHED)
        say(s, "USB disconnect, device at address %d", s->addr);
    s->phase = P_DETACHED;
    s->sof_on = 0;
    s->state = S_DETACHED;
    s->addr = 0;
    s->ct.on = 0;
    s->nhid = 0;
    s->suspended = s->resuming = s->wake_armed = 0;
}

static void run(uh_state *s)
{
    es_time_t now = esk_comp_now(s->c), d_t;
    esk_usb_dev *d = device(s);
    int i;
    if (!attached(s)) {
        if (s->phase != P_DETACHED)
            detach(s);
        return;
    }
    switch (s->phase) {
    case P_DETACHED:
        /* until the device descriptor says, EP0 takes 64-byte packets at
         * full speed (as Linux and Windows assume) */
        s->mps0 = 64;
        s->phase = P_DEBOUNCE;
        s->state = S_ATTACHED;
        s->phase_end = now + s->debounce_ps;
        say(s, "full-speed device attached");
        kick(s, s->phase_end);
        return;
    case P_DEBOUNCE:
        if (now < s->phase_end) {
            kick(s, s->phase_end);
            return;
        }
        s->state = S_ATTACHED;
        start_reset(s, E_DESC64);
        kick(s, s->phase_end);
        return;
    case P_RESET:
        if (now < s->phase_end) {
            kick(s, s->phase_end);
            return;
        }
        d->bus(d->ctx, ESK_USB_RESET_END, 0, now);
        if (s->state != S_ERROR)
            s->state = S_DEFAULT;
        s->sof_on = 1;
        s->next_sof = now;
        s->phase = P_RECOVERY;
        s->phase_end = now + 10u * PS_MS; /* TRSTRCY */
        s->step = s->after_reset;
        break;
    default:
        break;
    }
    /* resume signalling ends: EOP, the frames start again */
    if (s->resuming) {
        if (now < s->resume_end) {
            kick(s, s->resume_end);
            return;
        }
        s->resuming = 0;
        s->suspended = 0;
        d->bus(d->ctx, ESK_USB_RESUME_END, 0, now);
        s->sof_on = 1;
        s->next_sof = now;
        say(s, "port resumed");
        if (s->wake_armed && s->state == S_CONFIGURED)
            begin(s, E_WAKE_OFF); /* as Linux does after a resume */
    }
    if (s->suspended)
        return; /* nothing on the bus until a resume */
    /* a suspend asked for: once no request is under way, arm remote
     * wakeup where the configuration offers it, then stop the frames */
    if (s->want_suspend && s->phase == P_RUN && !s->ct.on && s->state != S_ERROR) {
        if (s->allow_wakeup && s->state == S_CONFIGURED && s->cfg_len >= 8 && (s->cfg[7] & 0x20) && !s->wake_armed) {
            begin(s, E_WAKE_ON);
        } else {
            enter_suspend(s);
            return;
        }
    }
    /* the frames */
    while (s->sof_on && now >= s->next_sof) {
        s->frame = (s->frame + 1u) & 0x7ffu;
        s->frames++;
        d->bus(d->ctx, ESK_USB_SOF, s->frame, now);
        s->next_sof += FRAME_PS;
        s->busy_until = now + esk_usb_fs_time(-1, 0);
        for (i = 0; i < s->nhid && s->state == S_CONFIGURED; i++) {
            uh_hid *h = &s->hid[i];
            if (s->hid_i > i && ((s->frame - h->next_frame) & 0x7ffu) < 0x400u) {
                h->due = 1;
                h->next_frame = (h->next_frame + (uint32_t)h->interval) & 0x7ffu;
            }
        }
    }
    if (s->phase == P_RECOVERY && now >= s->phase_end) {
        s->phase = P_RUN;
        begin(s, s->step);
    }
    if (s->phase == P_RUN && s->state != S_ERROR) {
        if (s->busy_until > now) {
            kick(s, s->busy_until < s->next_sof ? s->busy_until : s->next_sof);
            return;
        }
        d_t = 0;
        for (i = 0; i < s->nhid; i++)
            if (s->hid[i].due) {
                d_t = poll_hid(s, d, &s->hid[i], now);
                break;
            }
        if (!d_t && s->ct.on) {
            if (now + esk_usb_fs_time(64, 1) + EOF_PS > s->next_sof) {
                kick(s, s->next_sof); /* not across the frame's start */
                return;
            }
            d_t = control_step(s, d, now);
        }
        if (s->phase == P_RESET) {
            kick(s, s->phase_end);
            return;
        }
        if (d_t) {
            s->busy_until = now + d_t;
            kick(s, s->busy_until < s->next_sof ? s->busy_until : s->next_sof);
            return;
        }
    }
    if (s->phase == P_RECOVERY)
        kick(s, s->phase_end < s->next_sof ? s->phase_end : s->next_sof);
    else if (s->sof_on)
        kick(s, s->next_sof);
}

static void link_changed(void *ctx, es_time_t at)
{
    uh_state *s = (uh_state *)ctx;
    kick(s, at);
}

/* the device's K on the suspended bus: the host resumes the port within
 * 1 ms (TURSM) and drives resume itself */
static void link_wakeup(void *ctx, es_time_t at)
{
    uh_state *s = (uh_state *)ctx;
    (void)at;
    if (!s->suspended || s->resuming)
        return;
    s->wakeups++;
    s->want_suspend = 0;
    start_resume(s, s->wake_armed ? "remote wakeup" : "remote wakeup (not enabled by the host)");
    kick(s, s->resume_end);
}

/* VBUS at the cable's device end crossed its threshold: plugged in or out */
static void uh_input(esk_comp *c, esk_pin *pin)
{
    uh_state *s = st_of(c);
    (void)pin;
    kick(s, esk_comp_now(c));
}

static es_status uh_set(esk_comp *c, const char *input, const esk_json *value)
{
    uh_state *s = st_of(c);
    if (!strcmp(input, "suspend") && value->type == ESK_JSON_BOOL) {
        int want = value->u.boolean != 0;
        if (want == s->want_suspend)
            return ES_OK;
        s->want_suspend = want;
        if (!want && s->suspended)
            start_resume(s, "resume");
        kick(s, esk_comp_now(c));
        return ES_OK;
    }
    return ES_EINVAL;
}

/* ---- the component --------------------------------------------------------- */

static es_status uh_create(esk_comp *c)
{
    uh_state *s = st_of(c);
    double v;
    int64_t a = 1;
    s->c = c;
    v = 100;
    esk_comp_param_double(c, "debounce_ms", &v);
    s->debounce_ps = (es_time_t)(v * 1e9);
    v = 50;
    esk_comp_param_double(c, "reset_ms", &v);
    if (v < 10.0) {
        esk_comp_diag(c, ES_SEV_ERROR, "usb_host.reset", "a port reset lasts at least 10 ms (USB 2.0 7.1.7.5)");
        return ES_ESCHEMA;
    }
    s->reset_ps = (es_time_t)(v * 1e9);
    esk_comp_param_int(c, "address", &a);
    if (a < 1 || a > 127) {
        esk_comp_diag(c, ES_SEV_ERROR, "usb_host.address", "a device address is 1 to 127");
        return ES_ESCHEMA;
    }
    s->new_addr = (int)a;
    s->link.ctx = s;
    s->link.comp = c;
    s->link.dp = esk_comp_pin(c, "DP");
    s->link.changed = link_changed;
    s->link.wakeup = link_wakeup;
    s->allow_wakeup = 1;
    esk_comp_param_bool(c, "remote_wakeup", &s->allow_wakeup);
    s->vbus = esk_comp_pin(c, "VBUS");
    if (esk_usb_attach_host(c->e, &s->link) != ES_OK)
        return ES_ENOMEM;
    s->linked = 1;
    return ES_OK;
}

static void uh_destroy(esk_comp *c)
{
    uh_state *s = st_of(c);
    if (s && s->linked)
        esk_usb_detach_host(c->e, &s->link);
}

static void uh_reset(esk_comp *c)
{
    uh_state *s = st_of(c);
    es_time_t deb = s->debounce_ps, rst = s->reset_ps;
    int a = s->new_addr, wake = s->allow_wakeup;
    esk_pin *vbus = s->vbus;
    esk_usb_host link = s->link;
    int linked = s->linked;
    memset(s, 0, sizeof *s);
    s->c = c;
    s->debounce_ps = deb;
    s->reset_ps = rst;
    s->new_addr = a;
    s->link = link;
    s->linked = linked;
    s->allow_wakeup = wake;
    s->vbus = vbus;
    if (vbus && vbus->net) {
        /* an analog VBUS net: the comparator's crossings; a digital one: its edges */
        (void)esk_pin_compare(vbus, VBUS_SENSE_V);
        esk_pin_watch(vbus, 1);
    }
    s->frame = 0x7ffu; /* the first SOF is frame 0 */
    s->log[0] = 0;
    kick(s, esk_comp_now(c)); /* a device already pulled up */
}

static void uh_time(esk_comp *c, uint32_t token)
{
    uh_state *s = st_of(c);
    if (token != T_RUN)
        return;
    s->wake_set = 0;
    run(s);
}

static void uh_observe(esk_comp *c, esk_json *out)
{
    uh_state *s = st_of(c);
    esk_alloc *a = c->e->a;
    char hex[2u * BUF_CAP + 1u];
    esk_json_set_string(a, out, "state", state_names[s->state]);
    esk_json_set_integer(a, out, "device_address", s->state >= S_ADDRESS && s->state != S_ERROR ? s->addr : 0);
    esk_json_set_integer(a, out, "vid", s->dev[0] ? (int64_t)u16le(s->dev + 8) : 0);
    esk_json_set_integer(a, out, "pid", s->dev[0] ? (int64_t)u16le(s->dev + 10) : 0);
    esk_json_set_string(a, out, "manufacturer", s->manu);
    esk_json_set_string(a, out, "product", s->prod);
    esk_json_set_string(a, out, "serial", s->ser);
    hex_of(s->dev, s->dev[0] ? 18u : 0u, hex, sizeof hex);
    esk_json_set_string(a, out, "device_descriptor", hex);
    hex_of(s->cfg, s->state >= S_ADDRESS ? s->cfg_len : 0u, hex, sizeof hex);
    esk_json_set_string(a, out, "configuration", hex);
    hex_of(s->rep, s->rep_len, hex, sizeof hex);
    esk_json_set_string(a, out, "report_descriptor", hex);
    esk_json_set_integer(a, out, "reports", (int64_t)s->reports);
    hex_of(s->last, s->last_len, hex, sizeof hex);
    esk_json_set_string(a, out, "report", hex);
    esk_json_set_integer(a, out, "cursor_x", s->x);
    esk_json_set_integer(a, out, "cursor_y", s->y);
    esk_json_set_integer(a, out, "buttons", s->buttons);
    esk_json_set_integer(a, out, "frames", (int64_t)s->frames);
    esk_json_set_integer(a, out, "resets", s->resets);
    esk_json_set_bool(a, out, "plugged", plugged(s));
    esk_json_set_bool(a, out, "suspended", s->suspended && !s->resuming);
    esk_json_set_integer(a, out, "suspends", (int64_t)s->suspends);
    esk_json_set_integer(a, out, "wakeups", (int64_t)s->wakeups);
    esk_json_set_string(a, out, "log", s->log);
}

const esk_model_ops esk_usb_host_ops = { "usb_host", sizeof(uh_state), uh_create, uh_destroy, uh_reset, uh_input,
                                         uh_time, uh_set, uh_observe, NULL, 0 };
