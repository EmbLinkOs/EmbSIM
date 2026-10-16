/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* gdb.c -- the GDB remote serial protocol server for a board's MCU (SPEC-008
 * debug.gdb, SPEC-009 §10): `embsim gdb PROJECT`, so gdb, lldb and IDEs
 * debug firmware running in its circuit.
 *
 * It answers as QEMU's stub does, the register numbers, the `g` packet and
 * the stop replies (as EmbCC's server did, whose packet handling this
 * follows), with a target description from the core (qXfer:features:read),
 * QStartNoAckMode, vCont, software and hardware breakpoints (Z0, Z1) kept
 * by the machine, never written into memory, watchpoints (Z2-Z4) and
 * `monitor` commands: reset, time, stats.
 *
 * While the target runs, the whole session advances in slices of simulated
 * time and the socket is polled between them for the interrupt (0x03). A
 * breakpoint stops the session at its instruction; the firmware exiting
 * through semihosting ends the session with a W reply.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/gdb.c (the protocol's
 * shape and QEMU's numbering), written again over the session and the host
 * layer's sockets. */
#include <stdio.h>
#include <string.h>

#include "embsim/session.h"
#include "host/host.h"
#include "mcu/mcu.h"
#include "session/session_priv.h"
#include "util/num.h"
#include "util/str.h"

#define PKT_MAX 0x4000
#define SLICE_PS (1 * ES_PS_PER_MS)

typedef struct gdb {
    es_session *s;
    esk_board_inst *board;
    struct esk_mach *m;
    esk_host_sock fd;
    int noack;
    unsigned char in[4096];
    long ilen, ipos;
    char pkt[PKT_MAX + 1];
    int plen;
    char out[2 * PKT_MAX + 64];
    char reply[2 * PKT_MAX + 64];
    int last_sig;
    int ended;
} gdb;

static const char hexd[] = "0123456789abcdef";

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static u32 hexnum(const char **p)
{
    u32 v = 0;
    int h;
    while ((h = hexval((unsigned char)**p)) >= 0) {
        v = v << 4 | (u32)h;
        (*p)++;
    }
    return v;
}

/* a byte from the connection, waiting at most `ms` (-1: for ever): the byte,
 * -2 on timeout, -1 when the connection is gone */
static int getbyte(gdb *g, int ms)
{
    if (g->ipos == g->ilen) {
        long n = esk_host_sock_recv(g->fd, g->in, sizeof g->in, ms);
        if (n == 0)
            return -2;
        if (n < 0)
            return -1;
        g->ilen = n;
        g->ipos = 0;
    }
    return g->in[g->ipos++];
}

static int send_raw(gdb *g, const char *data, size_t n)
{
    size_t k = 0, i;
    unsigned sum = 0;
    if (n + 4 > sizeof g->out)
        n = sizeof g->out - 4;
    g->out[k++] = '$';
    for (i = 0; i < n; i++) {
        g->out[k++] = data[i];
        sum += (unsigned char)data[i];
    }
    g->out[k++] = '#';
    g->out[k++] = hexd[(sum >> 4) & 15];
    g->out[k++] = hexd[sum & 15];
    for (;;) {
        int c;
        if (esk_host_sock_send(g->fd, g->out, k, 5000) != (long)k)
            return -1;
        if (g->noack)
            return 0;
        do
            c = getbyte(g, 5000);
        while (c >= 0 && c != '+' && c != '-');
        if (c == '+')
            return 0;
        if (c < 0)
            return -1;
    }
}

static int send(gdb *g, const char *s) { return send_raw(g, s, strlen(s)); }

/* a packet into g->pkt (escapes undone): 0, or -1 when the connection is gone */
static int recv_packet(gdb *g)
{
    for (;;) {
        int c = getbyte(g, -1), sum = 0, csum;
        if (c < 0)
            return -1;
        if (c == 0x03) {
            g->pkt[0] = 0x03;
            g->plen = 1;
            return 0;
        }
        if (c != '$')
            continue;
        g->plen = 0;
        for (;;) {
            c = getbyte(g, -1);
            if (c < 0)
                return -1;
            if (c == '#')
                break;
            sum += c;
            if (c == '}') {
                int e = getbyte(g, -1);
                if (e < 0)
                    return -1;
                sum += e;
                c = e ^ 0x20;
            }
            if (g->plen < PKT_MAX)
                g->pkt[g->plen++] = (char)c;
        }
        g->pkt[g->plen] = 0;
        {
            int h1 = getbyte(g, -1), h2 = getbyte(g, -1);
            if (h1 < 0 || h2 < 0)
                return -1;
            csum = hexval(h1) * 16 + hexval(h2);
        }
        if (!g->noack) {
            char ack = (csum == (sum & 0xff)) ? '+' : '-';
            if (esk_host_sock_send(g->fd, &ack, 1, 5000) != 1)
                return -1;
            if (ack == '-')
                continue;
        }
        return 0;
    }
}

/* ---- the target --------------------------------------------------------------- */

static void put_hex(char *o, const unsigned char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        o[2 * i] = hexd[b[i] >> 4];
        o[2 * i + 1] = hexd[b[i] & 15];
    }
    o[2 * n] = 0;
}

static int unhex(const char *p, unsigned char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int h = hexval((unsigned char)p[2 * i]), l = h < 0 ? -1 : hexval((unsigned char)p[2 * i + 1]);
        if (h < 0 || l < 0)
            return -1;
        b[i] = (unsigned char)(h * 16 + l);
    }
    return 0;
}

static int stopped(gdb *g, const char **why) { return esk_mcu_stopped(g->board->mcu, why); }

/* run until the core stops, the firmware ends, or the debugger interrupts;
 * `step`: one instruction */
static int resume(gdb *g, int step)
{
    es_session *s = g->s;
    const char *why = NULL;
    esk_mcu_resume(g->board->mcu, step ? 1 : 0);
    for (;;) {
        es_time_t until;
        if (esk_time_add(s->eng.sched.now, SLICE_PS, &until))
            until = ES_TIME_MAX - 1;
        if (esk_session_do_advance(s, until) != ES_OK || s->diag.fatal)
            return 'E';
        if (g->m->state != ESK_MACH_RUN)
            return 'W';
        if (stopped(g, &why))
            return why && !strcmp(why, "halt") ? 2 : 5;
        if (esk_host_sock_wait(g->fd, 0) == 1) {
            int c = getbyte(g, 0);
            if (c == 0x03 || c == -1) {
                esk_mcu_halt(g->board->mcu);
                s->eng.sched.stop_requested = 0;
                return c == -1 ? -1 : 2;
            }
        }
    }
}

static int report(gdb *g, int why)
{
    char buf[64];
    if (why == -1)
        return -1;
    if (why == 'W') {
        snprintf(buf, sizeof buf, "W%02x", (unsigned)(g->m->exit_status & 0xff));
        g->ended = 1;
        return send(g, buf);
    }
    if (why == 'E') {
        g->ended = 1;
        return send(g, "X0b");
    }
    g->last_sig = why;
    if (why == 5 && g->m->bus.watch_hit) {
        const char *k = g->m->bus.watch_kind == ESK_WATCH_WRITE ? "watch"
                        : g->m->bus.watch_kind == ESK_WATCH_READ ? "rwatch"
                                                                 : "awatch";
        snprintf(buf, sizeof buf, "T05%s:%08lx;thread:1;", k, (unsigned long)g->m->bus.watch_addr);
        g->m->bus.watch_hit = 0;
        return send(g, buf);
    }
    snprintf(buf, sizeof buf, "T%02xthread:1;", (unsigned)why);
    return send(g, buf);
}

/* a program address as the bus has it (a MIPS core's kseg0 and kseg1) */
static u32 bus_addr(const gdb *g, u32 a)
{
    struct esk_cpu *c = g->m->cpu;
    return c->ops->phys ? c->ops->phys(c, a) : a;
}

/* the bytes up to the first that cannot be read (a partial read, as the
 * protocol allows: a debugger's cache line may run past a peripheral) */
static size_t mem_read(gdb *g, u32 a, unsigned char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        u32 v = 0;
        if (esk_bus_debug_read(&g->m->bus, bus_addr(g, a + (u32)i), 1, &v) != 0)
            break;
        b[i] = (unsigned char)v;
    }
    return i;
}

static int mem_write(gdb *g, u32 a, const unsigned char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (esk_bus_debug_write(&g->m->bus, bus_addr(g, a + (u32)i), 1, b[i]) != 0)
            return -1;
    return 0;
}

static int reg_hex(gdb *g, int n, char *o)
{
    unsigned char buf[16];
    int len = g->m->cpu->ops->reg_read(g->m->cpu, n, buf);
    if (len <= 0)
        return 0;
    put_hex(o, buf, (size_t)len);
    return 2 * len;
}

static void xfer(gdb *g, const char *doc, const char *args)
{
    const char *p = args;
    u32 off = hexnum(&p), len;
    size_t dl = strlen(doc);
    if (*p == ',')
        p++;
    len = hexnum(&p);
    if (len > PKT_MAX - 2)
        len = PKT_MAX - 2;
    if (off >= dl) {
        send(g, "l");
        return;
    }
    if (len > dl - off)
        len = (u32)(dl - off);
    g->reply[0] = off + len >= dl ? 'l' : 'm';
    memcpy(g->reply + 1, doc + off, len);
    send_raw(g, g->reply, (size_t)len + 1);
}

static void monitor(gdb *g, const char *hex)
{
    char cmd[128], text[256];
    size_t n = strlen(hex) / 2;
    if (n >= sizeof cmd || unhex(hex, (unsigned char *)cmd, n) != 0) {
        send(g, "E01");
        return;
    }
    cmd[n] = 0;
    if (!strcmp(cmd, "reset")) {
        esk_session_do_reset(g->s);
        esk_mcu_halt(g->board->mcu);
        g->s->eng.sched.stop_requested = 0;
        snprintf(text, sizeof text, "%s reset: the session starts again\n", g->board->id);
    } else if (!strcmp(cmd, "time")) {
        snprintf(text, sizeof text, "%llu ps\n", (unsigned long long)g->s->eng.sched.now);
    } else if (!strcmp(cmd, "stats")) {
        snprintf(text, sizeof text, "%llu instructions, %llu cycles, %lu Hz\n", (unsigned long long)g->m->insns,
                 (unsigned long long)g->m->cycles, (unsigned long)esk_mach_core_hz(g->m));
    } else {
        snprintf(text, sizeof text, "monitor commands: reset, time, stats\n");
    }
    g->reply[0] = 'O';
    put_hex(g->reply + 1, (const unsigned char *)text, strlen(text));
    send(g, g->reply);
    send(g, "OK");
}

static void breakpoint(gdb *g, const char *p, int insert)
{
    int type = *p - '0';
    u32 addr, len;
    int r;
    p += 2;
    addr = hexnum(&p);
    if (*p == ',')
        p++;
    len = hexnum(&p);
    if (type == 0 || type == 1)
        r = insert ? esk_mach_bp_add(g->m, addr & ~1u) : esk_mach_bp_remove(g->m, addr & ~1u);
    else if (type >= 2 && type <= 4)
        r = insert ? esk_bus_watch_add(&g->m->bus, type, addr, len ? len : 1)
                   : esk_bus_watch_remove(&g->m->bus, type, addr, len ? len : 1);
    else {
        send(g, "");
        return;
    }
    send(g, r == 0 ? "OK" : "E01");
}

/* one packet; 1 when the session is over */
static int handle(gdb *g)
{
    char *p = g->pkt;
    const int *regs;
    char *o;
    if (p[0] == 0x03) {
        return report(g, 2) < 0;
    }
    switch (p[0]) {
    case '?':
        return report(g, g->last_sig ? g->last_sig : 5) < 0;
    case 'g':
        o = g->reply;
        for (regs = g->m->cpu->ops->gdb_g_regs(g->m->cpu); *regs >= 0; regs++)
            o += reg_hex(g, *regs, o);
        *o = 0;
        return send(g, g->reply) < 0;
    case 'G': {
        const char *q = p + 1;
        for (regs = g->m->cpu->ops->gdb_g_regs(g->m->cpu); *regs >= 0 && strlen(q) >= 8; regs++, q += 8) {
            unsigned char b[4];
            if (unhex(q, b, 4) == 0)
                g->m->cpu->ops->reg_write(g->m->cpu, *regs, b);
        }
        return send(g, "OK") < 0;
    }
    case 'p': {
        const char *q = p + 1;
        int n = (int)hexnum(&q);
        if (!reg_hex(g, n, g->reply))
            return send(g, "E45") < 0;
        return send(g, g->reply) < 0;
    }
    case 'P': {
        const char *q = p + 1;
        int n = (int)hexnum(&q);
        unsigned char b[8];
        size_t len = strlen(q + 1) / 2;
        if (*q != '=' || len > sizeof b || unhex(q + 1, b, len) != 0 || g->m->cpu->ops->reg_write(g->m->cpu, n, b) <= 0)
            return send(g, "E45") < 0;
        return send(g, "OK") < 0;
    }
    case 'm': {
        const char *q = p + 1;
        u32 a = hexnum(&q), n;
        unsigned char b[PKT_MAX / 2];
        if (*q == ',')
            q++;
        n = hexnum(&q);
        if (n > sizeof b)
            n = sizeof b;
        n = (u32)mem_read(g, a, b, n);
        if (!n)
            return send(g, "E14") < 0;
        put_hex(g->reply, b, n);
        return send(g, g->reply) < 0;
    }
    case 'M':
    case 'X': {
        const char *q = p + 1;
        u32 a = hexnum(&q), n;
        unsigned char b[PKT_MAX];
        int bad;
        if (*q == ',')
            q++;
        n = hexnum(&q);
        if (*q != ':' || n > sizeof b)
            return send(g, "E01") < 0;
        q++;
        if (p[0] == 'M') {
            bad = unhex(q, b, n) != 0;
        } else {
            bad = (size_t)(g->plen - (q - p)) < n;
            if (!bad)
                memcpy(b, q, n);
        }
        if (bad || mem_write(g, a, b, n) != 0)
            return send(g, "E14") < 0;
        return send(g, "OK") < 0;
    }
    case 'c':
    case 's':
        if (p[1]) {
            const char *q = p + 1;
            u32 a = hexnum(&q);
            unsigned char b[4] = { (unsigned char)a, (unsigned char)(a >> 8), (unsigned char)(a >> 16),
                                   (unsigned char)(a >> 24) };
            g->m->cpu->ops->reg_write(g->m->cpu, g->m->cpu->ops->pc_regnum, b);
        }
        return report(g, resume(g, p[0] == 's')) < 0 || g->ended;
    case 'Z':
    case 'z':
        breakpoint(g, p + 1, p[0] == 'Z');
        return 0;
    case 'k':
        return 1;
    case 'D':
        send(g, "OK");
        return 1;
    case 'H':
    case 'T':
        return send(g, "OK") < 0;
    case 'v':
        if (!strcmp(p, "vCont?"))
            return send(g, "vCont;c;C;s;S") < 0;
        if (!strncmp(p, "vCont;", 6)) {
            int step = p[6] == 's' || p[6] == 'S';
            return report(g, resume(g, step)) < 0 || g->ended;
        }
        if (!strcmp(p, "vMustReplyEmpty"))
            return send(g, "") < 0;
        if (!strncmp(p, "vKill", 5)) {
            send(g, "OK");
            return 1;
        }
        return send(g, "") < 0;
    case 'q':
        if (!strncmp(p, "qSupported", 10)) {
            snprintf(g->reply, sizeof g->reply, "PacketSize=%x;qXfer:features:read+;QStartNoAckMode+;vContSupported+", PKT_MAX);
            return send(g, g->reply) < 0;
        }
        if (!strncmp(p, "qXfer:features:read:", 20)) {
            const char *annex = p + 20, *colon = strchr(annex, ':');
            char name[64];
            const char *doc;
            size_t al = colon ? (size_t)(colon - annex) : 0;
            if (!colon || al >= sizeof name)
                return send(g, "E00") < 0;
            memcpy(name, annex, al);
            name[al] = 0;
            doc = g->m->cpu->ops->gdb_xml(g->m->cpu, name);
            if (!doc)
                return send(g, "E00") < 0;
            xfer(g, doc, colon + 1);
            return 0;
        }
        if (!strcmp(p, "qAttached"))
            return send(g, "1") < 0;
        if (!strcmp(p, "qC"))
            return send(g, "QC1") < 0;
        if (!strcmp(p, "qfThreadInfo"))
            return send(g, "m1") < 0;
        if (!strcmp(p, "qsThreadInfo"))
            return send(g, "l") < 0;
        if (!strncmp(p, "qRcmd,", 6)) {
            monitor(g, p + 6);
            return 0;
        }
        if (!strncmp(p, "qSymbol", 7))
            return send(g, "OK") < 0;
        return send(g, "") < 0;
    case 'Q':
        if (!strcmp(p, "QStartNoAckMode")) {
            int r = send(g, "OK");
            g->noack = 1;
            return r < 0;
        }
        return send(g, "") < 0;
    default:
        return send(g, "") < 0;
    }
}

es_status es_serve_gdb(es_session *s, const char *board, const char *host, int port,
                       void (*listening)(void *ctx, int port), void *ctx)
{
    gdb *g;
    esk_host_sock lfd = ESK_HOST_SOCK_INVALID;
    size_t i;
    int bound = 0;
    es_status st;
    if (!s || !s->loaded)
        return ES_ESTATE;
    g = (gdb *)esk_calloc(s->a, 1, sizeof *g);
    if (!g)
        return ES_ENOMEM;
    g->s = s;
    for (i = 0; i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        if ((board ? esk_str_eq(bi->id, board) : 1) && bi->mcu && esk_mcu_machine(bi->mcu)) {
            g->board = bi;
            g->m = esk_mcu_machine(bi->mcu);
            break;
        }
    }
    if (!g->board) {
        esk_free(s->a, g, sizeof *g);
        return ES_ENOTFOUND;
    }
    st = esk_host_sock_init();
    if (st == ES_OK)
        st = esk_host_sock_listen(host, port, &lfd);
    if (st == ES_OK)
        st = esk_host_sock_local_port(lfd, &bound);
    if (st != ES_OK) {
        if (lfd != ESK_HOST_SOCK_INVALID)
            esk_host_sock_close(lfd);
        esk_host_sock_shutdown();
        esk_free(s->a, g, sizeof *g);
        return st;
    }
    /* the core waits at its reset vector for the debugger */
    esk_session_do_reset(s);
    esk_mcu_halt(g->board->mcu);
    s->eng.sched.stop_requested = 0;
    g->last_sig = 5;
    if (listening)
        listening(ctx, bound);
    while (esk_host_sock_wait(lfd, -1) != 1)
        ;
    st = esk_host_sock_accept(lfd, &g->fd);
    esk_host_sock_close(lfd);
    if (st == ES_OK) {
        while (recv_packet(g) == 0)
            if (handle(g))
                break;
        esk_host_sock_close(g->fd);
    }
    esk_host_sock_shutdown();
    esk_free(s->a, g, sizeof *g);
    return st;
}
