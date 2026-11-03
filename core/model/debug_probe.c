/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* debug_probe.c -- the Raspberry Pi Debug Probe: an RP2040 running
 * Raspberry Pi's debugprobe firmware (its CMSIS-DAP v2 build), a USB
 * device with a CMSIS-DAP interface on two bulk endpoints, a CDC ACM UART
 * and picotool's reset interface; its "D" port (SC, SD) drives a target's
 * serial wire, its "U" port (TX, RX) is the UART.
 *
 * What a computer sees, from the firmware's sources (debugprobe's
 * usb_descriptors.c, DAP_config.h, tusb_edpt_handler.c, cdc_uart.c; Arm's
 * CMSIS-DAP DAP.c it builds):
 *   - the device 2e8a:000c "Debug Probe (CMSIS-DAP)" by "Raspberry Pi",
 *     USB 2.1, bcdDevice 0x0231, its serial the probe's flash unique ID;
 *     interface 0 "CMSIS-DAP v2 Interface" (vendor class, bulk OUT 0x04,
 *     IN 0x85), interfaces 1 and 2 the CDC ACM UART (notification 0x81,
 *     data 0x02 and 0x83), interface 3 the reset interface; the BOS with
 *     its Microsoft OS 2.0 descriptor set (vendor request 1, index 7) for
 *     WinUSB;
 *   - CMSIS-DAP v2.0.0: DAP_Info (no vendor, product or serial strings: a
 *     host takes the USB ones; capabilities SWD, atomic commands and the
 *     timestamp; 64-byte packets, 6 of them), HostStatus's two LEDs,
 *     Connect (SWD only), Disconnect, TransferConfigure, Transfer (posted
 *     AP reads, value match with its mask and retries, WAIT retries, the
 *     microsecond timestamps), TransferBlock, WriteABORT, Delay,
 *     ResetTarget (no reset sequence), SWJ_Pins (no nRESET on the D port:
 *     every input reads 0), SWJ_Clock, SWJ_Sequence, SWD_Configure,
 *     SWD_Sequence, ExecuteCommands, QueueCommands (held until a packet
 *     that is not one, as its endpoint handler does); the JTAG commands
 *     answer DAP_ERROR (no JTAG), the rest, SWO and UART among them, and
 *     TransferAbort, ID_DAP_Invalid as this build of DAP.c does;
 *   - eight request and eight response buffers of 64 bytes: a request
 *     waits for room, a full queue NAKs the OUT endpoint;
 *   - each request's bits clocked on the wire at SWJ_Clock's rate: its
 *     answer is there when the wire has carried them;
 *   - the UART on its U port at the line coding's rate, data bits, parity
 *     and stop bits, bridged while DTR is set (a closed port leaves the
 *     receiver's 32-byte FIFO to fill and drop), SEND_BREAK holding TX low.
 *
 * Inside: the serial wire is the target's own (mcu/swd.h) clocked bit by
 * bit, SWCLK not toggled on the net (as the ST-LINK's, the clock is not
 * simulated edge by edge). The firmware's own processing time between
 * packets, autobaud (the magic baud rate), the reset interface's reboots
 * (BOOTSEL, flash), the LEDs' pins and the UART's flow control are not
 * modelled. */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "engine/usb.h"
#include "mcu/mcu.h"
#include "model/registry.h"

#define PKT 64u
#define NPKT 8u
#define CDC_OUT_CAP 64u   /* CFG_TUD_CDC_RX_BUFSIZE */
#define CDC_IN_CAP 4096u  /* CFG_TUD_CDC_TX_BUFSIZE */
#define UART_FIFO 32u     /* the PL011's receive FIFO */
#define LOG_CAP 4096

enum { P_SC, P_SD, P_TX, P_RX, P_USB_DP, P_COUNT };
static const char *const pin_names[P_COUNT] = { "SC", "SD", "TX", "RX", "USB_DP" };

enum { TOK_TX = 1, TOK_RX = 2, TOK_BREAK = 3 };

/* the DAP's transfer request bits */
#define T_APNDP 0x01u
#define T_RNW 0x02u
#define T_MATCH_VALUE 0x10u
#define T_MATCH_MASK 0x20u
#define T_TIMESTAMP 0x80u
#define A_OK 1u
#define A_WAIT 2u
#define A_FAULT 4u
#define A_ERROR 8u
#define A_MISMATCH 16u
#define DP_RDBUFF_RD (0x0Cu | T_RNW)
#define DP_ABORT 0x00u

/* ---- the USB descriptors (debugprobe's usb_descriptors.c) ------------------- */

static const uint8_t dev_desc[18] = { 18, 1, 0x10, 0x02, 0x00, 0x00, 0x00, 64, 0x8A, 0x2E, 0x0C, 0x00,
                                      0x31, 0x02, 1, 2, 3, 1 };
static const uint8_t cfg_desc[] = {
    9, 2, 107, 0, 4, 1, 0, 0x80, 50,
    /* CMSIS-DAP v2: vendor class, two bulk endpoints */
    9, 4, 0, 0, 2, 0xFF, 0x00, 0x00, 5,
    7, 5, 0x04, 2, 64, 0, 0,
    7, 5, 0x85, 2, 64, 0, 0,
    /* the CDC ACM UART */
    8, 0x0B, 1, 2, 0x02, 0x02, 0x00, 0,
    9, 4, 1, 0, 1, 0x02, 0x02, 0x00, 6,
    5, 0x24, 0x00, 0x20, 0x01,
    5, 0x24, 0x01, 0, 2,
    4, 0x24, 0x02, 0x06,
    5, 0x24, 0x06, 1, 2,
    7, 5, 0x81, 3, 64, 0, 16,
    9, 4, 2, 0, 2, 0x0A, 0, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0,
    7, 5, 0x83, 2, 64, 0, 0,
    /* picotool's reset interface */
    9, 4, 3, 0, 0, 0xFF, 0x00, 0x01, 7,
};
#define MS_OS_LEN (0x0A + 0x08 + 0xA0 + 0x9C)
static const uint8_t bos_desc[33] = {
    5, 0x0F, 33, 0, 1,
    28, 0x10, 0x05, 0x00, 0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C, 0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64,
    0x8A, 0x9F, 0x00, 0x00, 0x03, 0x06, (uint8_t)(MS_OS_LEN & 0xFF), (uint8_t)(MS_OS_LEN >> 8), 1, 0,
};
static const char *const strings[] = {
    NULL, "Raspberry Pi", "Debug Probe (CMSIS-DAP)", NULL, "CMSIS-DAP v1 Interface", "CMSIS-DAP v2 Interface",
    "CDC-ACM UART Interface", "Reset",
};
static const char probe_guid[] = "{CDB3B5AD-293B-4663-AA36-1AAE46463776}";
static const char reset_guid[] = "{bc7398c1-73cd-4cb7-98b8-913a8fca7bf6}";

typedef struct dp_state {
    esk_comp *c;
    esk_pin *pin[P_COUNT];
    /* the DAP */
    int port; /* 0 none, 1 SWD */
    uint32_t clock_hz, idle, retry, match_retry, match_mask;
    int trn, data_phase;
    int led_connect, led_run;
    uint64_t bits; /* clocked by the request in progress */
    es_time_t wire_free;
    uint64_t commands, transfers, faults;
    /* the USB device */
    esk_usb_dev dev;
    int linked, addr, pending_addr, configured;
    uint8_t ctl[512];
    uint32_t ctl_len, ctl_off;
    int ctl_in, ctl_pid, ctl_out_req;
    uint8_t setup[8];
    uint8_t tog_in[16], tog_out[16];
    /* the DAP's buffers */
    uint8_t req[NPKT][PKT];
    unsigned req_r, req_w;
    uint8_t rsp[NPKT][PKT];
    uint32_t rsp_len[NPKT];
    es_time_t rsp_at[NPKT];
    unsigned rsp_r, rsp_w;
    /* the UART */
    uint8_t coding[7];
    int dtr, rts, brk;
    uint8_t out_q[CDC_OUT_CAP];
    uint32_t out_n;
    uint8_t in_q[CDC_IN_CAP];
    uint32_t in_n;
    uint8_t fifo[UART_FIFO];
    uint32_t fifo_n;
    int tx_busy, tx_bit, tx_bits;
    uint32_t tx_frame;
    int rx_busy, rx_bit, rx_bits;
    uint32_t rx_frame;
    uint64_t uart_tx, uart_rx, uart_lost, uart_errors;
    char serial[17];
    char log[LOG_CAP];
    size_t log_len;
} dp_state;

static dp_state *st(esk_comp *c) { return (dp_state *)c->state; }

static void say(dp_state *s, const char *fmt, const char *a)
{
    char line[200];
    int n = snprintf(line, sizeof line, fmt, a);
    if (n <= 0)
        return;
    if ((size_t)n >= sizeof line)
        n = (int)sizeof line - 1;
    if (s->log_len + (size_t)n + 2 >= LOG_CAP) {
        size_t keep = LOG_CAP / 2;
        memmove(s->log, s->log + (s->log_len - keep), keep);
        s->log_len = keep;
    }
    memcpy(s->log + s->log_len, line, (size_t)n);
    s->log_len += (size_t)n;
    s->log[s->log_len++] = '\n';
    s->log[s->log_len] = 0;
}

static uint32_t get32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

static int parity32(uint32_t v)
{
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (int)(v & 1u);
}

/* ---- the D port: the target's serial wire ----------------------------------- */

/* the MCU on the SD net */
static esk_comp *target(dp_state *s)
{
    esk_pin *p = s->pin[P_SD];
    esk_engine *e = s->c->e;
    size_t i;
    uint32_t k;
    if (!p || !p->net)
        return NULL;
    for (i = 0; i < e->comps.len; i++) {
        esk_comp *c = e->comps.data[i];
        if (c == s->c || !esk_mcu_is(c))
            continue;
        for (k = 0; k < c->npins; k++)
            if (c->pins[k].net == p->net)
                return c;
    }
    return NULL;
}

static void wire_out(dp_state *s, uint64_t v, uint32_t n)
{
    uint8_t b[8];
    esk_comp *t = target(s);
    uint32_t i;
    for (i = 0; i < 8; i++)
        b[i] = (uint8_t)(v >> (8 * i));
    s->bits += n;
    if (t && n)
        esk_mcu_swd_out(t, b, n);
}

static void wire_out_bytes(dp_state *s, const uint8_t *b, uint32_t n)
{
    esk_comp *t = target(s);
    s->bits += n;
    if (t && n)
        esk_mcu_swd_out(t, b, n);
}

static uint64_t wire_in(dp_state *s, uint32_t n)
{
    uint8_t b[8] = { 0 };
    esk_comp *t = target(s);
    uint64_t v = 0;
    uint32_t i;
    s->bits += n;
    if (!n)
        return 0;
    if (t)
        esk_mcu_swd_in(t, b, n);
    else
        memset(b, 0xFF, sizeof b);
    for (i = 0; i < 8; i++)
        v |= (uint64_t)b[i] << (8 * i);
    return n >= 64 ? v : v & ((UINT64_C(1) << n) - 1u);
}

static void wire_in_bytes(dp_state *s, uint8_t *b, uint32_t n)
{
    esk_comp *t = target(s);
    s->bits += n;
    if (t && n)
        esk_mcu_swd_in(t, b, n);
    else if (n)
        memset(b, 0xFF, (n + 7) / 8);
}

/* one SWD packet as CMSIS-DAP's SWD_Transfer clocks it: its ACK (A_ERROR
 * for a read's bad parity) */
static uint32_t swd_transfer(dp_state *s, uint32_t request, uint32_t *data)
{
    uint32_t hdr = 1u | (request & 0xFu) << 1, ack, v, p;
    hdr |= (uint32_t)parity32(request & 0xFu) << 5 | 1u << 7;
    wire_out(s, hdr, 8);
    (void)wire_in(s, (uint32_t)s->trn);
    ack = (uint32_t)wire_in(s, 3);
    s->transfers++;
    if (ack == A_OK) {
        if (request & T_RNW) {
            v = (uint32_t)wire_in(s, 32);
            p = (uint32_t)wire_in(s, 1);
            (void)wire_in(s, (uint32_t)s->trn);
            if ((int)p != parity32(v))
                ack = A_ERROR;
            if (data)
                *data = v;
        } else {
            (void)wire_in(s, (uint32_t)s->trn);
            v = data ? *data : 0;
            wire_out(s, v, 32);
            wire_out(s, (uint64_t)parity32(v), 1);
        }
        if (s->idle)
            wire_out(s, 0, s->idle > 64 ? 64 : s->idle);
        return ack;
    }
    if (ack == A_WAIT || ack == A_FAULT) {
        if (ack == A_FAULT)
            s->faults++;
        if (s->data_phase && (request & T_RNW))
            (void)wire_in(s, 33);
        (void)wire_in(s, (uint32_t)s->trn);
        if (s->data_phase && !(request & T_RNW))
            wire_out(s, 0, 33);
        return ack;
    }
    /* a protocol error (nobody answered): back off the data phase */
    (void)wire_in(s, (uint32_t)s->trn + 33u);
    return ack;
}

/* ---- the DAP's commands (Arm's CMSIS-DAP DAP.c as debugprobe builds it) ----- */

static uint32_t timestamp(dp_state *s) { return (uint32_t)(esk_comp_now(s->c) / 1000000u); }

/* a transfer retried while the target answers WAIT */
static uint32_t xfer(dp_state *s, uint32_t request, uint32_t *data)
{
    uint32_t ack, retry = s->retry;
    do {
        ack = swd_transfer(s, request, data);
    } while (ack == A_WAIT && retry--);
    return ack;
}

/* DAP_Transfer, SWD: (request length << 16) | response length */
static uint32_t dap_transfer(dp_state *s, const uint8_t *rq, uint32_t rq_cap, uint8_t *rs)
{
    const uint8_t *head = rq, *end = rq + rq_cap;
    uint8_t *out = rs + 2;
    uint32_t count, value = 0, rvalue = 0, done = 0, data = 0, match, mretry;
    int post = 0, check = 0;
    rq++; /* the DAP index */
    count = rq < end ? *rq++ : 0;
    if (s->port != 1) {
        for (; count && rq < end; count--) {
            value = *rq++;
            if (!(value & T_RNW) || (value & T_MATCH_VALUE))
                rq += 4;
        }
        rs[0] = 0;
        rs[1] = 0;
        return (uint32_t)(rq - head) << 16 | 2u;
    }
    for (; count && rq < end; count--) {
        value = *rq++;
        if (value & T_RNW) {
            if (post) {
                if ((value & (T_APNDP | T_MATCH_VALUE)) == T_APNDP) {
                    rvalue = xfer(s, value, &data); /* the previous AP data, the next posted */
                } else {
                    rvalue = xfer(s, DP_RDBUFF_RD, &data);
                    post = 0;
                }
                if (rvalue != A_OK)
                    break;
                put32(out, data);
                out += 4;
                if (post && (value & T_TIMESTAMP)) {
                    put32(out, timestamp(s));
                    out += 4;
                }
            }
            if (value & T_MATCH_VALUE) {
                match = get32(rq);
                rq += 4;
                mretry = s->match_retry;
                if (value & T_APNDP) {
                    rvalue = xfer(s, value, NULL);
                    if (rvalue != A_OK)
                        break;
                }
                do {
                    rvalue = xfer(s, value, &data);
                    if (rvalue != A_OK)
                        break;
                } while ((data & s->match_mask) != match && mretry--);
                if ((data & s->match_mask) != match)
                    rvalue |= A_MISMATCH;
                if (rvalue != A_OK)
                    break;
            } else if (value & T_APNDP) {
                if (!post) {
                    rvalue = xfer(s, value, NULL);
                    if (rvalue != A_OK)
                        break;
                    if (value & T_TIMESTAMP) {
                        put32(out, timestamp(s));
                        out += 4;
                    }
                    post = 1;
                }
            } else {
                rvalue = xfer(s, value, &data);
                if (rvalue != A_OK)
                    break;
                if (value & T_TIMESTAMP) {
                    put32(out, timestamp(s));
                    out += 4;
                }
                put32(out, data);
                out += 4;
            }
            check = 0;
        } else {
            if (post) {
                rvalue = xfer(s, DP_RDBUFF_RD, &data);
                if (rvalue != A_OK)
                    break;
                put32(out, data);
                out += 4;
                post = 0;
            }
            data = get32(rq);
            rq += 4;
            if (value & T_MATCH_MASK) {
                s->match_mask = data;
                rvalue = A_OK;
            } else {
                rvalue = xfer(s, value, &data);
                if (rvalue != A_OK)
                    break;
                if (value & T_TIMESTAMP) {
                    put32(out, timestamp(s));
                    out += 4;
                }
                check = 1;
            }
        }
        done++;
    }
    /* the requests not carried out (counting, as DAP.c does, the one that
     * failed, whose data was already taken) */
    for (; count && rq < end; count--) {
        value = *rq++;
        if (!(value & T_RNW) || (value & T_MATCH_VALUE))
            rq += 4;
    }
    if (rvalue == A_OK) {
        if (post) {
            rvalue = xfer(s, DP_RDBUFF_RD, &data);
            if (rvalue == A_OK) {
                put32(out, data);
                out += 4;
            }
        } else if (check) {
            rvalue = xfer(s, DP_RDBUFF_RD, NULL);
        }
    }
    rs[0] = (uint8_t)done;
    rs[1] = (uint8_t)rvalue;
    return (uint32_t)(rq - head) << 16 | (uint32_t)(out - rs);
}

static uint32_t dap_transfer_block(dp_state *s, const uint8_t *rq, uint8_t *rs)
{
    uint32_t count = (uint32_t)rq[1] | (uint32_t)rq[2] << 8, value = rq[3], rvalue = 0, done = 0, data;
    uint32_t rqlen = (value & T_RNW) ? 4u : 4u + 4u * count;
    uint8_t *out = rs + 3;
    const uint8_t *d = rq + 4;
    if (s->port != 1 || !count)
        goto end;
    if (value & T_RNW) {
        if (value & T_APNDP) {
            rvalue = xfer(s, value, NULL);
            if (rvalue != A_OK)
                goto end;
        }
        while (count--) {
            uint32_t r = (!count && (value & T_APNDP)) ? DP_RDBUFF_RD : value;
            rvalue = xfer(s, r, &data);
            if (rvalue != A_OK)
                goto end;
            put32(out, data);
            out += 4;
            done++;
        }
    } else {
        while (count--) {
            data = get32(d);
            d += 4;
            rvalue = xfer(s, value, &data);
            if (rvalue != A_OK)
                goto end;
            done++;
        }
        rvalue = xfer(s, DP_RDBUFF_RD, NULL);
    }
end:
    rs[0] = (uint8_t)done;
    rs[1] = (uint8_t)(done >> 8);
    rs[2] = (uint8_t)rvalue;
    return rqlen << 16 | (uint32_t)(out - rs);
}

static uint32_t dap_info(dp_state *s, uint8_t id, uint8_t *rs)
{
    static const char fw[] = "2.0.0";
    uint8_t *v = rs + 1;
    uint8_t n = 0;
    (void)s;
    switch (id) {
    case 0x04:
        n = (uint8_t)sizeof fw;
        memcpy(v, fw, sizeof fw);
        break;
    case 0xF0: /* SWD, atomic commands, timestamps */
        v[0] = 0x01u | 0x10u | 0x20u;
        n = 1;
        break;
    case 0xF1:
        put32(v, 1000000u);
        n = 4;
        break;
    case 0xFE:
        v[0] = (uint8_t)(NPKT - 2u);
        n = 1;
        break;
    case 0xFF:
        v[0] = (uint8_t)PKT;
        v[1] = 0;
        n = 2;
        break;
    default:
        break; /* no vendor, product or serial string: the USB ones */
    }
    rs[0] = n;
    return 2u << 16 | (2u + n - 1u);
}

/* one command: (request length << 16) | response length, the response's
 * first byte the command's own */
static uint32_t dap_command(dp_state *s, const uint8_t *rq, uint32_t cap, uint8_t *rs)
{
    uint32_t n, k, count;
    uint8_t id = rq[0];
    if (id >= 0x80 && id <= 0x9F) {
        rs[0] = 0xFF;
        return 1u << 16 | 1u;
    }
    rs[0] = id;
    rq++;
    rs++;
    switch (id) {
    case 0x00:
        return dap_info(s, rq[0], rs) + 1u;
    case 0x01: /* HostStatus */
        if (rq[0] == 0)
            s->led_connect = rq[1] & 1;
        else if (rq[0] == 1)
            s->led_run = rq[1] & 1;
        rs[0] = rq[0] <= 1 ? 0x00 : 0xFF;
        return 3u << 16 | 2u;
    case 0x02: /* Connect */
        s->port = rq[0] == 0 || rq[0] == 1 ? 1 : 0;
        rs[0] = (uint8_t)s->port;
        say(s, "connect: %s", s->port ? "SWD" : "refused (no JTAG)");
        return 2u << 16 | 2u;
    case 0x03:
        s->port = 0;
        rs[0] = 0;
        return 1u << 16 | 2u;
    case 0x04: /* TransferConfigure */
        s->idle = rq[0];
        s->retry = (uint32_t)rq[1] | (uint32_t)rq[2] << 8;
        s->match_retry = (uint32_t)rq[3] | (uint32_t)rq[4] << 8;
        rs[0] = 0;
        return 6u << 16 | 2u;
    case 0x05:
        n = dap_transfer(s, rq, cap - 1u, rs);
        return n + (1u << 16) + 1u;
    case 0x06:
        n = dap_transfer_block(s, rq, rs);
        return n + (1u << 16) + 1u;
    case 0x08: /* WriteABORT */
        if (s->port == 1) {
            uint32_t v = get32(rq + 1);
            (void)swd_transfer(s, DP_ABORT, &v);
            rs[0] = 0;
        } else {
            rs[0] = 0xFF;
        }
        return 6u << 16 | 2u;
    case 0x09: /* Delay: microseconds */
        s->wire_free += (es_time_t)((uint32_t)rq[0] | (uint32_t)rq[1] << 8) * 1000000u;
        rs[0] = 0;
        return 3u << 16 | 2u;
    case 0x0A: /* ResetTarget: no device-specific sequence */
        rs[0] = 0;
        rs[1] = 0;
        return 1u << 16 | 3u;
    case 0x10: { /* SWJ_Pins: the D port has no nRESET; the inputs read 0, so
                  * a wait for a selected input to read 1 runs its course */
        uint32_t wait = get32(rq + 2);
        if (wait && (rq[0] & rq[1] & 0xA7u))
            s->wire_free += (es_time_t)(wait > 3000000u ? 3000000u : wait) * 1000000u;
        rs[0] = 0;
        return 7u << 16 | 2u;
    }
    case 0x11: /* SWJ_Clock */
        n = get32(rq);
        if (n)
            s->clock_hz = n;
        rs[0] = n ? 0x00 : 0xFF;
        return 5u << 16 | 2u;
    case 0x12: /* SWJ_Sequence */
        count = rq[0] ? rq[0] : 256u;
        wire_out_bytes(s, rq + 1, count);
        rs[0] = 0;
        return (2u + (count + 7u) / 8u) << 16 | 2u;
    case 0x13: /* SWD_Configure */
        s->trn = (rq[0] & 3) + 1;
        s->data_phase = (rq[0] & 4) != 0;
        rs[0] = 0;
        return 2u << 16 | 2u;
    case 0x1D: { /* SWD_Sequence */
        uint32_t rql = 2, rsl = 2;
        const uint8_t *p = rq + 1;
        uint8_t *o = rs + 1;
        rs[0] = 0;
        for (k = rq[0]; k; k--) {
            uint32_t info = *p++, bits = info & 0x3Fu ? info & 0x3Fu : 64u, bytes = (bits + 7u) / 8u;
            if (info & 0x80u) {
                wire_in_bytes(s, o, bits);
                o += bytes;
                rsl += bytes;
                rql++;
            } else {
                wire_out_bytes(s, p, bits);
                p += bytes;
                rql += bytes + 1u;
            }
        }
        return rql << 16 | rsl;
    }
    case 0x14: { /* JTAG_Sequence: no JTAG */
        uint32_t rql = 2;
        const uint8_t *p = rq + 1;
        for (k = rq[0]; k; k--) {
            uint32_t info = *p++, bits = info & 0x3Fu ? info & 0x3Fu : 64u, bytes = (bits + 7u) / 8u;
            p += bytes;
            rql += bytes + 1u;
        }
        rs[0] = 0xFF;
        return rql << 16 | 2u;
    }
    case 0x15:
        rs[0] = 0xFF;
        return (2u + rq[0]) << 16 | 2u;
    case 0x16:
        rs[0] = 0xFF;
        return 2u << 16 | 2u;
    default: /* SWO, UART, TransferAbort and the rest: not in this build */
        rs[-1] = 0xFF;
        return 1u << 16 | 1u;
    }
}

/* a packet: ExecuteCommands (and a QueueCommands released) runs the
 * commands it holds, each answered after the other; any other packet is
 * one command */
static uint32_t dap_execute(dp_state *s, const uint8_t *rq, uint32_t cap, uint8_t *rs)
{
    uint32_t rql = 2, rsl = 2, i, n;
    const uint8_t *p = rq + 2;
    uint8_t *o = rs + 2;
    if (rq[0] != 0x7F && rq[0] != 0x7E)
        return dap_command(s, rq, cap, rs);
    rs[0] = 0x7F;
    rs[1] = rq[1];
    for (i = rq[1]; i && rql < cap; i--) {
        n = dap_command(s, p, cap - rql, o);
        p += n >> 16;
        o += n & 0xFFFFu;
        rql += n >> 16;
        rsl += n & 0xFFFFu;
    }
    return rql << 16 | rsl;
}

/* the requests waiting, run while there is room for their answers; a
 * QueueCommands packet waits for one that is not */
static void pump(dp_state *s)
{
    while (s->req_r != s->req_w && s->rsp_w - s->rsp_r < NPKT) {
        uint8_t *rq = s->req[s->req_r % NPKT];
        unsigned k = s->req_r;
        uint8_t out[PKT * 4];
        uint32_t n;
        es_time_t now = esk_comp_now(s->c), t;
        if (rq[0] == 0x7E) {
            while (k != s->req_w && s->req[k % NPKT][0] == 0x7E)
                k++;
            if (k == s->req_w)
                return; /* queued until a packet that is not */
        }
        memset(out, 0, sizeof out);
        s->bits = 0;
        if (s->wire_free < now)
            s->wire_free = now;
        n = dap_execute(s, rq, PKT, out) & 0xFFFFu;
        s->commands++;
        t = s->clock_hz ? (es_time_t)(s->bits * 1000000000000ull / s->clock_hz) : 0;
        s->wire_free += t;
        if (n > PKT)
            n = PKT;
        memcpy(s->rsp[s->rsp_w % NPKT], out, n);
        s->rsp_len[s->rsp_w % NPKT] = n;
        s->rsp_at[s->rsp_w % NPKT] = s->wire_free;
        s->rsp_w++;
        s->req_r++;
    }
}

/* ---- the U port: the UART ----------------------------------------------------- */

static uint32_t baud(const dp_state *s)
{
    uint32_t b = get32(s->coding);
    return b ? b : 115200u;
}

static int data_bits(const dp_state *s) { return s->coding[6] >= 5 && s->coding[6] <= 8 ? s->coding[6] : 8; }
static int parity_kind(const dp_state *s) { return s->coding[5] == 1 || s->coding[5] == 2 ? s->coding[5] : 0; }
static int stop_bits(const dp_state *s) { return s->coding[4] ? 2 : 1; }

static es_time_t bit_ps(const dp_state *s) { return (es_time_t)(1000000000000ull / baud(s)); }

static void tx_next(dp_state *s)
{
    uint32_t b, frame, i, nb;
    int pk;
    esk_pin *tx = s->pin[P_TX];
    if (s->tx_busy || s->brk || !s->dtr || !s->out_n || !tx)
        return;
    b = s->out_q[0];
    memmove(s->out_q, s->out_q + 1, --s->out_n);
    nb = (uint32_t)data_bits(s);
    b &= (1u << nb) - 1u;
    frame = b << 1; /* the start bit 0 */
    i = nb + 1;
    pk = parity_kind(s);
    if (pk) {
        uint32_t p = (uint32_t)parity32(b) ^ (pk == 1 ? 1u : 0u); /* odd: the count made odd */
        frame |= p << i++;
    }
    frame |= ((1u << stop_bits(s)) - 1u) << i;
    s->tx_frame = frame;
    s->tx_bits = (int)i + stop_bits(s);
    s->tx_bit = 0;
    s->tx_busy = 1;
    esk_pin_drive(tx, ESK_L0, ESK_STR_STRONG, 0);
    (void)esk_comp_wake_at(s->c, esk_comp_now(s->c) + bit_ps(s), TOK_TX);
}

static void rx_byte(dp_state *s, uint32_t b)
{
    if (s->dtr) {
        if (s->in_n < CDC_IN_CAP)
            s->in_q[s->in_n++] = (uint8_t)b;
        else
            s->uart_lost++;
    } else if (s->fifo_n < UART_FIFO) {
        s->fifo[s->fifo_n++] = (uint8_t)b; /* nobody reads it: the FIFO fills */
    } else {
        s->uart_lost++;
    }
    s->uart_rx++;
}

/* ---- the USB device ------------------------------------------------------------- */

static void bus_reset(dp_state *s)
{
    s->addr = s->pending_addr = 0;
    s->configured = 0;
    memset(s->tog_in, 0, sizeof s->tog_in);
    memset(s->tog_out, 0, sizeof s->tog_out);
    s->ctl_len = s->ctl_off = 0;
}

static int u_pullup(void *ctx)
{
    (void)ctx;
    return ESK_USB_FULL_SPEED; /* powered from the cable: always there */
}

static void u_bus(void *ctx, int ev, uint32_t frame, es_time_t at)
{
    (void)frame;
    (void)at;
    if (ev == ESK_USB_RESET)
        bus_reset((dp_state *)ctx);
}

static uint32_t string_desc(const dp_state *s, uint8_t *d, uint32_t idx)
{
    const char *str = idx < sizeof strings / sizeof strings[0] ? strings[idx] : NULL;
    uint32_t n = 0, i;
    if (idx == 0) {
        d[0] = 4;
        d[1] = 3;
        d[2] = 0x09;
        d[3] = 0x04;
        return 4;
    }
    if (idx == 3)
        str = s->serial;
    if (!str)
        return 0;
    while (str[n] && n < 31)
        n++;
    d[0] = (uint8_t)(2 + 2 * n);
    d[1] = 3;
    for (i = 0; i < n; i++) {
        d[2 + 2 * i] = (uint8_t)str[i];
        d[3 + 2 * i] = 0;
    }
    return 2 + 2 * n;
}

static uint8_t *u16le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    return p + 2;
}

static uint8_t *utf16(uint8_t *p, const char *str)
{
    while (*str) {
        *p++ = (uint8_t)*str++;
        *p++ = 0;
    }
    *p++ = 0;
    *p++ = 0;
    return p;
}

/* the Microsoft OS 2.0 descriptor set: WinUSB for the CMSIS-DAP interface
 * and for the reset interface, with their interface GUIDs */
static uint32_t ms_os_20(uint8_t *d)
{
    uint8_t *p = d;
    p = u16le(p, 0x0A);
    p = u16le(p, 0x00);
    put32(p, 0x06030000u);
    p += 4;
    p = u16le(p, MS_OS_LEN);
    p = u16le(p, 0x08);
    p = u16le(p, 0x01);
    *p++ = 0;
    *p++ = 0;
    p = u16le(p, MS_OS_LEN - 0x0A);
    p = u16le(p, 0x08);
    p = u16le(p, 0x02);
    *p++ = 0; /* the CMSIS-DAP interface */
    *p++ = 0;
    p = u16le(p, 0xA0);
    p = u16le(p, 0x14);
    p = u16le(p, 0x03);
    memcpy(p, "WINUSB\0\0\0\0\0\0\0\0\0\0", 16);
    p += 16;
    p = u16le(p, 0xA0 - 0x08 - 0x14);
    p = u16le(p, 0x04);
    p = u16le(p, 0x0007);
    p = u16le(p, 0x002A);
    p = utf16(p, "DeviceInterfaceGUIDs");
    p = u16le(p, 0x0050);
    p = utf16(p, probe_guid);
    p = u16le(p, 0x0000); /* the multi-string's end */
    /* picotool's reset interface */
    p = u16le(p, 0x08);
    p = u16le(p, 0x02);
    *p++ = 3;
    *p++ = 0;
    p = u16le(p, 0x9C);
    p = u16le(p, 0x14);
    p = u16le(p, 0x03);
    memcpy(p, "WINUSB\0\0\0\0\0\0\0\0\0\0", 16);
    p += 16;
    p = u16le(p, 0x0080);
    p = u16le(p, 0x04);
    p = u16le(p, 0x0001);
    p = u16le(p, 0x0028);
    p = utf16(p, "DeviceInterfaceGUID");
    p = u16le(p, 0x004E);
    p = utf16(p, reset_guid);
    return (uint32_t)(p - d);
}

static int u_setup(void *ctx, int addr, int ep, const uint8_t data[8], es_time_t at)
{
    dp_state *s = (dp_state *)ctx;
    uint32_t want = (uint32_t)data[6] | (uint32_t)data[7] << 8, n = 0;
    uint8_t type = data[0], req = data[1];
    (void)at;
    if (addr != s->addr || ep != 0)
        return ESK_USB_NONE;
    memcpy(s->setup, data, 8);
    s->ctl_in = (type & 0x80) != 0;
    s->ctl_pid = 1;
    s->ctl_off = 0;
    s->ctl_out_req = 0;
    if ((type & 0x60) == 0) { /* standard */
        switch (req) {
        case 6: { /* GET_DESCRIPTOR */
            uint32_t kind = data[3], idx = data[2];
            if (kind == 1) {
                memcpy(s->ctl, dev_desc, sizeof dev_desc);
                n = sizeof dev_desc;
            } else if (kind == 2) {
                memcpy(s->ctl, cfg_desc, sizeof cfg_desc);
                n = sizeof cfg_desc;
            } else if (kind == 3) {
                n = string_desc(s, s->ctl, idx);
                if (!n)
                    return ESK_USB_STALL;
            } else if (kind == 0x0F) {
                memcpy(s->ctl, bos_desc, sizeof bos_desc);
                n = sizeof bos_desc;
            } else {
                return ESK_USB_STALL;
            }
            break;
        }
        case 5:
            s->pending_addr = data[2] & 0x7f;
            break;
        case 9:
            s->configured = data[2] != 0;
            memset(s->tog_in + 1, 0, sizeof s->tog_in - 1);
            memset(s->tog_out + 1, 0, sizeof s->tog_out - 1);
            break;
        case 8:
            s->ctl[0] = (uint8_t)s->configured;
            n = 1;
            break;
        case 0:
            s->ctl[0] = s->ctl[1] = 0;
            n = 2;
            break;
        case 1:
            if ((type & 0x1f) == 2) {
                uint32_t e = data[4] & 0x0fu;
                if (data[4] & 0x80)
                    s->tog_in[e] = 0;
                else
                    s->tog_out[e] = 0;
            }
            break;
        case 3:
        case 11:
            break;
        case 10:
            s->ctl[0] = 0;
            n = 1;
            break;
        default:
            return ESK_USB_STALL;
        }
    } else if ((type & 0x60) == 0x20 && (type & 0x1f) == 1 && data[4] == 1) { /* the CDC interface */
        switch (req) {
        case 0x20: /* SET_LINE_CODING */
            s->ctl_out_req = 0x20;
            break;
        case 0x21:
            memcpy(s->ctl, s->coding, 7);
            n = 7;
            break;
        case 0x22: /* SET_CONTROL_LINE_STATE */
            s->dtr = data[2] & 1;
            s->rts = (data[2] >> 1) & 1;
            if (s->dtr) {
                /* the bridge runs: what the FIFO held goes up */
                uint32_t i;
                for (i = 0; i < s->fifo_n && s->in_n < CDC_IN_CAP; i++)
                    s->in_q[s->in_n++] = s->fifo[i];
                s->fifo_n = 0;
                tx_next(s);
            }
            break;
        case 0x23: { /* SEND_BREAK: wValue ms, 0xFFFF until 0 */
            uint32_t ms = (uint32_t)data[2] | (uint32_t)data[3] << 8;
            s->brk = ms != 0;
            if (s->pin[P_TX])
                esk_pin_drive(s->pin[P_TX], ms ? ESK_L0 : ESK_L1, ESK_STR_STRONG, 0);
            if (ms && ms != 0xFFFFu)
                (void)esk_comp_wake_at(s->c, esk_comp_now(s->c) + (es_time_t)ms * 1000000000u, TOK_BREAK);
            if (!ms)
                tx_next(s);
            break;
        }
        default:
            return ESK_USB_STALL;
        }
    } else if ((type & 0x60) == 0x40 && req == 1 && data[4] == 7) { /* the MS OS 2.0 descriptor set */
        n = ms_os_20(s->ctl);
    } else if ((type & 0x60) == 0x40 && (type & 0x1f) == 1 && data[4] == 3) {
        /* the reset interface's BOOTSEL and flash reboots: not modelled */
        say(s, "reset interface: request %s (not modelled)", req == 1 ? "BOOTSEL" : req == 2 ? "flash" : "?");
    } else {
        return ESK_USB_STALL;
    }
    s->ctl_len = n < want ? n : want;
    return ESK_USB_ACK;
}

static int u_out(void *ctx, int addr, int ep, int data1, const uint8_t *data, uint32_t len, es_time_t at)
{
    dp_state *s = (dp_state *)ctx;
    (void)at;
    if (addr != s->addr || ep < 0 || ep > 15)
        return ESK_USB_NONE;
    if (ep == 0) {
        if (!s->ctl_in && s->ctl_out_req == 0x20 && len) {
            memcpy(s->coding, data, len < 7 ? len : 7);
            {
                char b[64];
                snprintf(b, sizeof b, "%lu baud, %d%c%d", (unsigned long)baud(s), data_bits(s),
                         "NOE"[parity_kind(s)], stop_bits(s));
                say(s, "uart: %s", b);
            }
        }
        return ESK_USB_ACK;
    }
    if (!s->configured)
        return ESK_USB_STALL;
    if (ep == 4) { /* a CMSIS-DAP request */
        if (s->req_w - s->req_r >= NPKT)
            return ESK_USB_NAK;
        if (data1 != s->tog_out[ep])
            return ESK_USB_ACK; /* a repeat: dropped */
        memset(s->req[s->req_w % NPKT], 0, PKT);
        memcpy(s->req[s->req_w % NPKT], data, len < PKT ? len : PKT);
        s->req_w++;
        s->tog_out[ep] ^= 1;
        pump(s);
        return ESK_USB_ACK;
    }
    if (ep == 2) { /* the UART's data */
        if (s->out_n + len > CDC_OUT_CAP)
            return ESK_USB_NAK;
        if (data1 != s->tog_out[ep])
            return ESK_USB_ACK;
        memcpy(s->out_q + s->out_n, data, len);
        s->out_n += len;
        s->tog_out[ep] ^= 1;
        tx_next(s);
        return ESK_USB_ACK;
    }
    return ESK_USB_STALL;
}

static int u_in(void *ctx, int addr, int ep, uint8_t *data, uint32_t cap, uint32_t *len, int *data1, es_time_t at)
{
    dp_state *s = (dp_state *)ctx;
    uint32_t n;
    *len = 0;
    if (addr != s->addr || ep < 0 || ep > 15)
        return ESK_USB_NONE;
    if (ep == 0) {
        if (!s->ctl_in) {
            if (s->pending_addr && s->setup[1] == 5 && !(s->setup[0] & 0x60)) {
                s->addr = s->pending_addr;
                s->pending_addr = 0;
            }
            *data1 = 1;
            return ESK_USB_ACK;
        }
        n = s->ctl_len - s->ctl_off;
        if (n > cap)
            n = cap;
        if (n > 64)
            n = 64;
        memcpy(data, s->ctl + s->ctl_off, n);
        s->ctl_off += n;
        *len = n;
        *data1 = s->ctl_pid;
        s->ctl_pid ^= 1;
        return ESK_USB_ACK;
    }
    if (!s->configured)
        return ESK_USB_STALL;
    if (ep == 5) { /* a CMSIS-DAP response, once the wire has carried it */
        unsigned k = s->rsp_r % NPKT;
        pump(s);
        if (s->rsp_r == s->rsp_w || at < s->rsp_at[k])
            return ESK_USB_NAK;
        n = s->rsp_len[k] < cap ? s->rsp_len[k] : cap;
        memcpy(data, s->rsp[k], n);
        s->rsp_r++;
        pump(s);
    } else if (ep == 3) { /* the UART's received bytes */
        if (!s->in_n)
            return ESK_USB_NAK;
        n = s->in_n < cap ? s->in_n : cap;
        if (n > 64)
            n = 64;
        memcpy(data, s->in_q, n);
        memmove(s->in_q, s->in_q + n, s->in_n - n);
        s->in_n -= n;
    } else if (ep == 1) {
        return ESK_USB_NAK; /* no serial state notifications */
    } else {
        return ESK_USB_STALL;
    }
    *len = n;
    *data1 = s->tog_in[ep];
    s->tog_in[ep] ^= 1;
    return ESK_USB_ACK;
}

/* ---- the part -------------------------------------------------------------------- */

static es_status dp_create(esk_comp *c)
{
    dp_state *s = st(c);
    const char *ser = NULL;
    int i;
    s->c = c;
    for (i = 0; i < P_COUNT; i++)
        s->pin[i] = esk_comp_pin(c, pin_names[i]);
    if (!s->pin[P_SD])
        return ES_ESCHEMA;
    if (s->pin[P_RX])
        esk_pin_watch(s->pin[P_RX], 1);
    if (c->params)
        ser = esk_json_get_str(c->params, "serial", NULL);
    snprintf(s->serial, sizeof s->serial, "%s", ser && *ser ? ser : "E6614103E7176A25");
    return ES_OK;
}

static void dp_destroy(esk_comp *c)
{
    dp_state *s = st(c);
    if (s->linked)
        esk_usb_detach_dev(c->e, &s->dev);
    s->linked = 0;
}

static void dp_reset(esk_comp *c)
{
    dp_state *s = st(c);
    static const uint8_t coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0, 0, 8 }; /* 115200 8N1 */
    s->port = 0;
    s->clock_hz = 1000000u; /* DAP_DEFAULT_SWJ_CLOCK */
    s->idle = 0;
    s->retry = 100;
    s->match_retry = 0;
    s->match_mask = 0;
    s->trn = 1;
    s->data_phase = 0;
    s->led_connect = s->led_run = 0;
    s->wire_free = 0;
    s->commands = s->transfers = s->faults = 0;
    s->req_r = s->req_w = s->rsp_r = s->rsp_w = 0;
    memcpy(s->coding, coding, 7);
    s->dtr = s->rts = s->brk = 0;
    s->out_n = s->in_n = s->fifo_n = 0;
    s->tx_busy = s->rx_busy = 0;
    s->uart_tx = s->uart_rx = s->uart_lost = s->uart_errors = 0;
    s->log_len = 0;
    s->log[0] = 0;
    bus_reset(s);
    if (s->pin[P_TX])
        esk_pin_drive(s->pin[P_TX], ESK_L1, ESK_STR_STRONG, 0);
    if (!s->linked && s->pin[P_USB_DP] && s->pin[P_USB_DP]->net) {
        s->dev.ctx = s;
        s->dev.comp = c;
        s->dev.dp = s->pin[P_USB_DP];
        s->dev.pullup = u_pullup;
        s->dev.bus = u_bus;
        s->dev.setup = u_setup;
        s->dev.out = u_out;
        s->dev.in = u_in;
        if (esk_usb_attach_dev(c->e, &s->dev) == ES_OK)
            s->linked = 1;
    }
}

static void dp_input(esk_comp *c, esk_pin *p)
{
    dp_state *s = st(c);
    if (p != s->pin[P_RX] || s->rx_busy)
        return;
    if (esk_pin_read(p) == ESK_L0) { /* a start bit: sampled mid-bit from here */
        s->rx_busy = 1;
        s->rx_bit = 0;
        s->rx_frame = 0;
        s->rx_bits = data_bits(s) + (parity_kind(s) ? 1 : 0);
        (void)esk_comp_wake_at(c, esk_comp_now(c) + bit_ps(s) + bit_ps(s) / 2, TOK_RX);
    }
}

static void dp_time(esk_comp *c, uint32_t token)
{
    dp_state *s = st(c);
    if (token == TOK_TX && s->tx_busy) {
        s->tx_bit++;
        if (s->tx_bit < s->tx_bits) {
            esk_pin_drive(s->pin[P_TX], (s->tx_frame >> s->tx_bit) & 1u ? ESK_L1 : ESK_L0, ESK_STR_STRONG, 0);
            (void)esk_comp_wake_at(c, esk_comp_now(c) + bit_ps(s), TOK_TX);
        } else {
            s->tx_busy = 0;
            s->uart_tx++;
            tx_next(s);
        }
    } else if (token == TOK_RX && s->rx_busy) {
        int v = esk_pin_read(s->pin[P_RX]) == ESK_L1;
        if (s->rx_bit < s->rx_bits) {
            s->rx_frame |= (uint32_t)v << s->rx_bit;
            s->rx_bit++;
            (void)esk_comp_wake_at(c, esk_comp_now(c) + bit_ps(s), TOK_RX);
        } else {
            uint32_t nb = (uint32_t)data_bits(s), b = s->rx_frame & ((1u << nb) - 1u);
            int pk = parity_kind(s);
            s->rx_busy = 0;
            /* a framing or parity error (a break: 0x00 with the line low) is
             * flagged by the PL011, but the firmware's uart_getc takes the
             * byte all the same */
            if (!v || (pk && (int)((s->rx_frame >> nb) & 1u) != (parity32(b) ^ (pk == 1 ? 1 : 0))))
                s->uart_errors++;
            rx_byte(s, b);
        }
    } else if (token == TOK_BREAK && s->brk) {
        s->brk = 0;
        esk_pin_drive(s->pin[P_TX], ESK_L1, ESK_STR_STRONG, 0);
        tx_next(s);
    }
}

static void dp_observe(esk_comp *c, esk_json *o)
{
    dp_state *s = st(c);
    esk_alloc *a = c->e->a;
    esk_comp *t = target(s);
    uint64_t pk = 0, fl = 0;
    int sel = -1;
    if (t)
        esk_mcu_swd_counts(t, &pk, &fl, &sel);
    esk_json_set_string(a, o, "target", t ? t->id : "");
    esk_json_set_bool(a, o, "usb_attached", s->linked);
    esk_json_set_integer(a, o, "usb_address", s->addr);
    esk_json_set_bool(a, o, "usb_configured", s->configured);
    esk_json_set_bool(a, o, "dap_connected", s->port == 1);
    esk_json_set_bool(a, o, "led_connected", s->led_connect);
    esk_json_set_bool(a, o, "led_running", s->led_run);
    esk_json_set_integer(a, o, "swj_clock_hz", s->clock_hz);
    esk_json_set_integer(a, o, "commands", (int64_t)s->commands);
    esk_json_set_integer(a, o, "transfers", (int64_t)s->transfers);
    esk_json_set_integer(a, o, "faults", (int64_t)s->faults);
    esk_json_set_integer(a, o, "target_packets", (int64_t)pk);
    esk_json_set_integer(a, o, "target_port", sel);
    esk_json_set_integer(a, o, "uart_baud", baud(s));
    esk_json_set_bool(a, o, "uart_open", s->dtr);
    esk_json_set_integer(a, o, "uart_tx_bytes", (int64_t)s->uart_tx);
    esk_json_set_integer(a, o, "uart_rx_bytes", (int64_t)s->uart_rx);
    esk_json_set_integer(a, o, "uart_lost", (int64_t)s->uart_lost);
    esk_json_set_integer(a, o, "uart_errors", (int64_t)s->uart_errors);
    esk_json_set_string(a, o, "log", s->log);
}

const esk_model_ops esk_debug_probe_ops = {
    "debug_probe", sizeof(dp_state), dp_create, dp_destroy, dp_reset, dp_input, dp_time, NULL, dp_observe, NULL, 0,
};
