/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* hostport.c -- a board's serial port on the host (ADR-031, ADR-032).
 *
 * The Uno's ATmega16U2 (a CH340, a CP2102 on other boards) is a USB CDC
 * device: the computer sees a serial port, any terminal or tool opens it,
 * and what it writes reaches the MCU's RX at the baud rate the program set
 * on the line; what the MCU sends comes back the same way. Here that port
 * is a pseudo-terminal on the host, attached to a board's console ("B1") or
 * to a terminal part on the circuit's own pins (a USB-serial adapter, "VT1").
 *
 * What a program on the host sees, as on a real board:
 * - the port exists while the session has it open, whether or not anything
 *   runs; bytes the MCU sends while no program has the port open are lost,
 *   as the bridge drops what the host does not read;
 * - opening the port raises DTR (Linux and macOS raise it on open and drop
 *   it on close, HUPCL); on the Uno the 16U2 then pulls RESET low through
 *   its 100 nF capacitor for about 0.7 ms and the sketch starts again
 *   after an external reset (the board's console.bridge.dtr_reset names
 *   the switch whose contacts do the same); closing it does not reset;
 * - the line's baud rate is the program's: when it differs from the sketch's
 *   by more than a receiver tolerates, both directions decode as a real
 *   UART would decode frames sent at the other rate (garbage, as on the
 *   bench);
 * - the line starts at 9600 baud, canonical with echo, as a ttyACM does.
 *
 * Where the inside differs: a pseudo-terminal has no modem lines, so DTR is
 * "a program has the port open"; a tool that pulses DTR by ioctl (avrdude's
 * arduino programmer) is not seen, and RTS, CTS and breaks are not passed.
 * The bridge's bytes reach the console's USART directly (the session's
 * serial input), not as frames on PD0, so a shield driving D0 does not
 * fight the 16U2 yet. Bytes from the host are inputs at the time they
 * arrive: recorded, so a replay without the host gives the same run. */
#include "session/session_priv.h"
#include "host/host.h"
#include "mcu/mcu.h"
#include "protocol/wsutil.h"

#include <math.h>
#include <string.h>

typedef struct esk_host_port {
    char *target;      /* "B1" a board's console, "VT1" a terminal part */
    esk_host_pty *pty;
    int peer;          /* a program had the port open at the last poll */
    uint32_t baud;     /* the program's line rate */
    uint64_t baud_ms;  /* when it was last read */
    uint64_t sent;     /* the console's bytes passed on (or dropped) */
    uint64_t to_host, from_host;
} esk_host_port;

/* the board whose console `target` names, or NULL */
static esk_board_inst *port_board(es_session *s, const char *target)
{
    size_t i;
    for (i = 0; i < s->boards.len; i++)
        if (esk_str_eq(s->boards.data[i]->id, target))
            return s->boards.data[i];
    return NULL;
}

/* a terminal part: it takes tx_text and reports rx_text */
static esk_comp *port_part(es_session *s, const char *target)
{
    esk_comp *c = esk_engine_comp(&s->eng, target);
    const esk_json *inputs = c ? esk_json_get(c->desc, "inputs") : NULL;
    size_t i;
    if (!c || !c->ops->observe)
        return NULL;
    for (i = 0; i < esk_json_len(inputs); i++)
        if (esk_str_eq(esk_json_get_str(esk_json_at(inputs, i), "name", ""), "tx_text"))
            return c;
    return NULL;
}

static esk_host_port *port_named(es_session *s, const char *target)
{
    size_t i;
    for (i = 0; i < s->host_ports.len; i++)
        if (esk_str_eq(s->host_ports.data[i]->target, target))
            return s->host_ports.data[i];
    return NULL;
}

/* The console's line rate: the MCU's receiver as the firmware set it. */
static uint32_t sketch_baud(es_session *s, const esk_host_port *hp)
{
    esk_board_inst *bi = port_board(s, hp->target);
    if (bi && bi->mcu)
        return esk_mcu_console_baud(bi->mcu);
    return 0;
}

/* Bytes sent back to back at `from` baud (8N1) as a receiver at `to` baud
 * decodes them: it waits for a falling edge, samples each bit at its middle
 * by its own clock, and starts looking again after its stop bit. Framing
 * errors still give a byte (the 16U2's firmware and Arduino's core both
 * keep it). */
static int line_at(const unsigned char *in, size_t n, size_t m)
{
    size_t i = m / 10, j = m % 10;
    if (i >= n)
        return 1;
    if (j == 0)
        return 0;
    if (j == 9)
        return 1;
    return (in[i] >> (j - 1)) & 1;
}

size_t esk_uart_rebaud(const unsigned char *in, size_t n, double from, double to, unsigned char *out, size_t cap)
{
    double bit = 1.0 / from, rbit = 1.0 / to, t = 0.0;
    size_t k = 0, total = n * 10, m;
    int high = 1;
    while (k < cap) {
        double start = -1.0, at;
        unsigned v = 0;
        int b;
        /* `high`: the line's level where the search starts; the first
         * interval searched holds that level */
        for (m = (size_t)floor(t / bit + 1e-9); m < total; m++) {
            if (line_at(in, n, m)) {
                high = 1;
            } else if (high) {
                start = (double)m * bit > t ? (double)m * bit : t;
                break;
            }
        }
        if (start < 0.0)
            break;
        for (b = 0; b < 8; b++) {
            at = start + (1.5 + b) * rbit;
            if (line_at(in, n, (size_t)floor(at / bit + 1e-9)))
                v |= 1u << b;
        }
        out[k++] = (unsigned char)v;
        t = start + 9.5 * rbit;
        high = line_at(in, n, (size_t)floor(t / bit + 1e-9));
    }
    return k;
}

/* both rates known and far enough apart that a UART misreads the frames */
static int mismatched(uint32_t a, uint32_t b)
{
    return a && b && fabs((double)a - (double)b) > 0.03 * (double)b;
}

static void port_state(es_session *s, const esk_host_port *hp)
{
    esk_json *o = esk_json_new_object(s->a);
    if (!o)
        return;
    esk_json_set_string(s->a, o, "target", hp->target);
    esk_json_set_string(s->a, o, "path", esk_host_pty_path(hp->pty));
    esk_json_set_bool(s->a, o, "connected", hp->peer);
    esk_json_set_integer(s->a, o, "baud", (int64_t)hp->baud);
    esk_json_set_integer(s->a, o, "sketch_baud", (int64_t)sketch_baud(s, hp));
    esk_json_set_integer(s->a, o, "to_host", (int64_t)hp->to_host);
    esk_json_set_integer(s->a, o, "from_host", (int64_t)hp->from_host);
    esk_session_notify(s, "port.state", o);
}

esk_json *esk_session_port_json(es_session *s, const struct esk_host_port *hp)
{
    esk_json *o = esk_json_new_object(s->a);
    if (!o)
        return NULL;
    esk_json_set_string(s->a, o, "target", hp->target);
    esk_json_set_string(s->a, o, "path", esk_host_pty_path(hp->pty));
    esk_json_set_bool(s->a, o, "connected", hp->peer);
    esk_json_set_integer(s->a, o, "baud", (int64_t)hp->baud);
    esk_json_set_integer(s->a, o, "sketch_baud", (int64_t)sketch_baud(s, hp));
    esk_json_set_integer(s->a, o, "to_host", (int64_t)hp->to_host);
    esk_json_set_integer(s->a, o, "from_host", (int64_t)hp->from_host);
    return o;
}

es_status esk_session_port_open(es_session *s, const char *target, const struct esk_host_port **out, char *why,
                                size_t cap)
{
    esk_host_port *hp;
    esk_board_inst *bi;
    es_status st;
    *out = NULL;
    if (!s->loaded) {
        esk_snprintf(why, cap, "no project is loaded");
        return ES_ESTATE;
    }
    if ((hp = port_named(s, target)) != NULL) {
        *out = hp;
        return ES_OK;
    }
    bi = port_board(s, target);
    if (bi ? !esk_json_get(esk_json_get(bi->item ? bi->item->desc : NULL, "console"), "peripheral") : !port_part(s, target)) {
        esk_snprintf(why, cap, "%s is neither a board with a serial console nor a terminal part", target);
        return ES_ENOTFOUND;
    }
    hp = (esk_host_port *)esk_calloc(s->a, 1, sizeof *hp);
    if (!hp)
        return ES_ENOMEM;
    hp->target = esk_strdup(s->a, target);
    st = hp->target ? esk_host_pty_open(s->a, &hp->pty) : ES_ENOMEM;
    if (st == ES_OK && esk_vec_push(s->a, &s->host_ports, hp) != 0)
        st = ES_ENOMEM;
    if (st != ES_OK) {
        esk_host_pty_close(hp->pty);
        esk_strfree(s->a, hp->target);
        esk_free(s->a, hp, sizeof *hp);
        esk_snprintf(why, cap, st == ES_EUNSUPPORTED ? "this host has no pseudo-terminals (Windows: Q16)"
                                                     : "the host could not open a pseudo-terminal");
        return st;
    }
    /* what the console already sent is not the new port's */
    if (bi && bi->mcu) {
        size_t len;
        (void)esk_mcu_console(bi->mcu, &len, &hp->sent);
    } else {
        esk_comp *c = port_part(s, target);
        esk_json *o = c ? esk_json_new_object(s->a) : NULL;
        if (o) {
            c->ops->observe(c, o);
            hp->sent = strlen(esk_json_get_str(o, "rx_text", ""));
            esk_json_free(s->a, o);
        }
    }
    *out = hp;
    return ES_OK;
}

static void port_free(es_session *s, esk_host_port *hp)
{
    esk_host_pty_close(hp->pty);
    esk_strfree(s->a, hp->target);
    esk_free(s->a, hp, sizeof *hp);
}

es_status esk_session_port_close(es_session *s, const char *target)
{
    size_t i;
    for (i = 0; i < s->host_ports.len; i++) {
        esk_host_port *hp = s->host_ports.data[i];
        if (!esk_str_eq(hp->target, target))
            continue;
        port_free(s, hp);
        s->host_ports.data[i] = s->host_ports.data[--s->host_ports.len];
        return ES_OK;
    }
    return ES_ENOTFOUND;
}

void esk_session_ports_free(es_session *s)
{
    size_t i;
    for (i = 0; i < s->host_ports.len; i++)
        port_free(s, s->host_ports.data[i]);
    esk_vec_free(s->a, &s->host_ports);
}

/* the host's bytes as an input value: text while it is plain ASCII, base64
 * otherwise (any byte, NUL among them) */
static esk_json *bytes_value(es_session *s, const unsigned char *b, size_t n, int text_only)
{
    size_t i, k = 0;
    int plain = 1;
    char *txt;
    esk_json *v;
    for (i = 0; i < n; i++)
        if (!b[i] || b[i] >= 0x80)
            plain = 0;
    if (plain || text_only) {
        txt = (char *)esk_malloc(s->a, n + 1);
        if (!txt)
            return NULL;
        for (i = 0; i < n; i++)
            if (b[i] && b[i] < 0x80)
                txt[k++] = (char)b[i];
        txt[k] = 0;
        v = k ? esk_json_new_string(s->a, txt) : NULL;
        esk_free(s->a, txt, n + 1);
        return v;
    } else {
        size_t cap = 4 * ((n + 2) / 3) + 1;
        char *b64 = (char *)esk_malloc(s->a, cap);
        if (!b64)
            return NULL;
        esk_base64_encode(b, n, b64, cap);
        v = esk_json_new_object(s->a);
        if (v)
            esk_json_set_string(s->a, v, "base64", b64);
        esk_free(s->a, b64, cap);
        return v;
    }
}

/* what the console sent since the last poll: a pointer into its text and
 * the byte count (those older than the text it keeps are lost) */
static const unsigned char *port_output(es_session *s, esk_host_port *hp, size_t *n, esk_json **hold)
{
    esk_board_inst *bi = port_board(s, hp->target);
    *n = 0;
    *hold = NULL;
    if (bi && bi->mcu) {
        size_t len;
        uint64_t total, fresh;
        const char *text = esk_mcu_console(bi->mcu, &len, &total);
        if (total < hp->sent)
            hp->sent = 0; /* the project was loaded again */
        fresh = total - hp->sent;
        hp->sent = total;
        if (fresh > len)
            fresh = len;
        *n = (size_t)fresh;
        return (const unsigned char *)text + len - (size_t)fresh;
    } else {
        esk_comp *c = port_part(s, hp->target);
        const char *text;
        size_t len;
        if (!c || !(*hold = esk_json_new_object(s->a)))
            return NULL;
        c->ops->observe(c, *hold);
        text = esk_json_get_str(*hold, "rx_text", "");
        len = strlen(text);
        if (len < hp->sent)
            hp->sent = 0;
        *n = len - (size_t)hp->sent;
        text += hp->sent;
        hp->sent = len;
        return (const unsigned char *)text;
    }
}

void esk_session_ports_poll(es_session *s)
{
    size_t i;
    uint64_t now_ms = esk_host_monotonic_ms();
    for (i = 0; i < s->host_ports.len; i++) {
        esk_host_port *hp = s->host_ports.data[i];
        esk_board_inst *bi = port_board(s, hp->target);
        int peer = esk_host_pty_peer(hp->pty), changed = 0;
        unsigned char buf[512], conv[512];
        const unsigned char *out;
        uint32_t sketch;
        esk_json *hold;
        size_t n;
        long got;
        if (peer != hp->peer) {
            hp->peer = peer;
            changed = 1;
            if (!peer) {
                esk_host_pty_flush(hp->pty);
                hp->baud = 0;
            } else {
                hp->baud = esk_host_pty_baud(hp->pty);
                hp->baud_ms = now_ms;
                /* DTR raised on open: the 16U2's pin falls, and its 100 nF
                 * capacitor pulls RESET low until the 10 kOhm pull-up
                 * recharges it past the threshold; the board's reset switch
                 * joins RESET to ground for as long */
                const esk_json *dtr = esk_json_get(
                    esk_json_get(esk_json_get(bi ? bi->item ? bi->item->desc : NULL : NULL, "console"), "bridge"),
                    "dtr_reset");
                const char *sw = esk_json_get_str(dtr, "component", NULL);
                if (bi && sw) {
                    char id[160];
                    double ms = esk_json_get_num(dtr, "ms", 1.0);
                    es_time_t until = s->eng.sched.now + (es_time_t)(ms * (double)ES_PS_PER_MS);
                    esk_json *on = esk_json_new_bool(s->a, 1), *off = esk_json_new_bool(s->a, 0);
                    esk_snprintf(id, sizeof id, "%s.%s", bi->id, sw);
                    if (on && off) {
                        (void)esk_session_apply_input(s, id, "pressed", on, s->eng.sched.now, NULL, NULL);
                        (void)esk_session_apply_input(s, id, "pressed", off, until, NULL, NULL);
                    }
                    esk_json_free(s->a, on);
                    esk_json_free(s->a, off);
                }
            }
        } else if (peer && now_ms - hp->baud_ms >= 250) {
            uint32_t b = esk_host_pty_baud(hp->pty);
            hp->baud_ms = now_ms;
            if (b != hp->baud) {
                hp->baud = b;
                changed = 1;
            }
        }
        sketch = sketch_baud(s, hp);

        /* the MCU's bytes to the program, or dropped while none has the port */
        out = port_output(s, hp, &n, &hold);
        while (out && n) {
            size_t chunk = n < sizeof buf ? n : sizeof buf, m = chunk;
            const unsigned char *p = out;
            if (mismatched(sketch, hp->baud)) {
                m = esk_uart_rebaud(out, chunk, sketch, hp->baud, conv, sizeof conv);
                p = conv;
            }
            if (peer && m && esk_host_pty_write(hp->pty, p, m) > 0)
                hp->to_host += m;
            out += chunk;
            n -= chunk;
        }
        esk_json_free(s->a, hold);

        /* the program's bytes to the MCU, as inputs now */
        while (peer && (got = esk_host_pty_read(hp->pty, buf, sizeof buf)) > 0) {
            const unsigned char *p = buf;
            size_t m = (size_t)got;
            esk_json *v;
            if (bi && mismatched(hp->baud, sketch)) {
                m = esk_uart_rebaud(buf, (size_t)got, hp->baud, sketch, conv, sizeof conv);
                p = conv;
            }
            hp->from_host += m;
            v = m ? bytes_value(s, p, m, !bi) : NULL;
            if (v) {
                (void)esk_session_apply_input(s, hp->target, bi ? "serial" : "tx_text", v, s->eng.sched.now, NULL, NULL);
                esk_json_free(s->a, v);
            }
        }
        if (changed)
            port_state(s, hp);
    }
}
