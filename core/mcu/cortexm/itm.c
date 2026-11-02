/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* itm.c -- the ARMv7-M's instrumentation trace macrocell (0xE0000000) and
 * its trace port interface unit (0xE0040000), as far as serial wire output
 * goes (ARM DDI 0403E C1.7 and D4.2; the Cortex-M4 TRM, ARM DDI 0439B,
 * chapters 9 and 11):
 *
 *   - ITM: 32 stimulus ports. A write of 8, 16 or 32 bits to STIMn sends a
 *     software source packet (a header of the port number and the size,
 *     then the bytes, least significant first) while DEMCR.TRCENA,
 *     TCR.ITMENA and TER's bit n are set; a read gives FIFOREADY, 0 while
 *     the output's few bytes of buffering are full (a write then is lost).
 *     TER, TPR and TCR (ITMENA, TSENA, SYNCENA, TXENA, SWOENA, TraceBusID;
 *     BUSY while bytes wait) are written by the program once LAR has had
 *     0xC5ACCE55 (LSR shows the lock), by a debugger at any time;
 *   - TPIU: SPPR's protocol (1 Manchester, 2 NRZ: the UART a probe like the
 *     ST-LINK reads; reset 1), ACPR's SWOSCALER, the output's bit rate
 *     TRACECLKIN / (SWOSCALER + 1), each byte ten bits (a start bit, eight,
 *     a stop bit) after the one before; CSPSR, FFCR and FFSR as written and
 *     read (the formatter is bypassed in the serial wire modes, as on the
 *     Cortex-M4); the identification registers.
 * Both are in the debug domain: a power-on resets them, a system reset
 * keeps what a debugger set (as DBGMCU's registers).
 * The bytes leave on the trace pin while the part's debug component gives
 * it its trace function (an STM32's DBGMCU_CR TRACE_IOEN with the
 * asynchronous TRACE_MODE: m->dbg.swo); a probe takes them as they arrive
 * (esk_cm_swo_take). TRACECLKIN is the core's clock (HCLK on the STM32s).
 *
 * Not modelled: the DWT's packets (PC sampling, exception trace, data
 * trace: TXENA forwards nothing), local and global timestamps (TSENA:
 * no timestamp packets), synchronisation packets, the overflow packet when
 * a write is lost, TPR's privilege check (an unprivileged write is
 * accepted), the TPIU's parallel trace port and its formatter. */
#include <string.h>

#include "mcu/cortexm/cortexm.h"

#define SWO_Q 8192u /* the bytes on their way out, and those a probe has not taken */
#define FIFO_DEPTH 8u /* what the ITM and TPIU buffer before FIFOREADY drops */

#define TCR_ITMENA 1u
#define TCR_BUSY (1u << 23)
#define DEMCR_TRCENA (1u << 24)

struct swo {
    u8 b[SWO_Q];
    u64 t[SWO_Q]; /* the cycle each byte's stop bit ends */
    u32 head, n;  /* the oldest, and how many */
    u64 last;     /* the cycle the last byte queued ends */
    u32 lost;     /* bytes no one took before the queue was full */
};

struct itm {
    struct esk_mach *sim;
    struct esk_cm_state *core;
    u32 ter, tpr, tcr, locked;
    /* the TPIU */
    u32 acpr, sppr, cspsr, ffcr;
    struct swo q;
};

static struct itm *itm_of(struct esk_mach *m) { return (struct itm *)m->itm; }

/* bytes still to leave the part at cycle `now` */
static u32 in_flight(const struct itm *t, u64 now)
{
    u32 k, n = 0;
    for (k = 0; k < t->q.n; k++)
        if (t->q.t[(t->q.head + k) % SWO_Q] > now)
            n++;
    return n;
}

static int enabled(const struct itm *t)
{
    return t->core && (t->core->demcr & DEMCR_TRCENA) && (t->tcr & TCR_ITMENA);
}

/* a byte onto the serial wire, after the one before it */
static void put(struct itm *t, u8 v)
{
    u64 now = t->sim->cycles, start = t->q.last > now ? t->q.last : now;
    u32 at;
    if (t->q.n == SWO_Q) { /* no probe took them: the oldest is gone */
        t->q.head = (t->q.head + 1) % SWO_Q;
        t->q.n--;
        t->q.lost++;
    }
    at = (t->q.head + t->q.n) % SWO_Q;
    t->q.b[at] = v;
    t->q.last = start + 10u * ((u64)(t->acpr & 0xffffu) + 1u);
    t->q.t[at] = t->q.last;
    t->q.n++;
}

static void stimulus(struct itm *t, u32 port, int n, u32 v)
{
    int k;
    if (!enabled(t) || !(t->ter >> port & 1u))
        return;
    if (in_flight(t, t->sim->cycles) + (u32)n + 1u > FIFO_DEPTH)
        return; /* not ready: the write is lost */
    put(t, (u8)(port << 3 | (n == 1 ? 1u : n == 2 ? 2u : 3u)));
    for (k = 0; k < n; k++)
        put(t, (u8)(v >> (8 * k)));
}

static u32 itm_read(void *ctx, u32 off, int n)
{
    struct itm *t = (struct itm *)ctx;
    (void)n;
    if (off < 0x80u)
        return enabled(t) && in_flight(t, t->sim->cycles) + 5u <= FIFO_DEPTH ? 1u : 0u; /* FIFOREADY */
    switch (off) {
    case 0xE00: return t->ter;
    case 0xE40: return t->tpr;
    case 0xE80: return t->tcr | (in_flight(t, t->sim->cycles) ? TCR_BUSY : 0u);
    case 0xFB4: return 1u | (t->locked ? 2u : 0u); /* LSR: present, locked */
    case 0xFD0: return 0x04;
    case 0xFE0: return 0x01;
    case 0xFE4: return 0xB0;
    case 0xFE8: return 0x3B;
    case 0xFF0: return 0x0D;
    case 0xFF4: return 0xE0;
    case 0xFF8: return 0x05;
    case 0xFFC: return 0xB1;
    }
    return 0;
}

static void itm_write(void *ctx, u32 off, int n, u32 v)
{
    struct itm *t = (struct itm *)ctx;
    int debugger = t->sim->bus.debug || t->sim->bus.dap;
    if (off < 0x80u) {
        stimulus(t, off / 4u, n, v);
        return;
    }
    if (off == 0xFB0) {
        t->locked = v != 0xC5ACCE55u;
        return;
    }
    if (t->locked && !debugger)
        return; /* the lock keeps the program out */
    switch (off) {
    case 0xE00: t->ter = v; break;
    case 0xE40: t->tpr = v & 0xfu; break;
    case 0xE80: t->tcr = v & 0x007F0F1Fu; break;
    }
}

static void itm_reset(void *ctx)
{
    struct itm *t = (struct itm *)ctx;
    if (t->sim->reset_cause != ESK_RESET_POWER)
        return; /* the debug domain: a system reset keeps what a debugger set */
    t->ter = t->tpr = t->tcr = 0;
    t->locked = 1;
}

const struct esk_dev_ops esk_itm_ops = { "itm", itm_read, itm_write, itm_reset, 0, 0 };

/* ---- the TPIU ------------------------------------------------------------ */

static u32 tpiu_read(void *ctx, u32 off, int n)
{
    struct itm *t = (struct itm *)ctx;
    (void)n;
    switch (off) {
    case 0x000: return 0x0000000Fu; /* SSPSR: port sizes 1 to 4 */
    case 0x004: return t->cspsr;
    case 0x010: return t->acpr;
    case 0x0F0: return t->sppr;
    case 0x300: return 0x8u; /* FFSR: FtNonStop */
    case 0x304: return t->ffcr;
    case 0xFC8: return 0xCA1u; /* DEVID: NRZ and Manchester, no formatter needed */
    case 0xFD0: return 0x04;
    case 0xFE0: return 0xA1;
    case 0xFE4: return 0xB9;
    case 0xFE8: return 0x0B;
    case 0xFF0: return 0x0D;
    case 0xFF4: return 0x90;
    case 0xFF8: return 0x05;
    case 0xFFC: return 0xB1;
    }
    return 0;
}

static void tpiu_write(void *ctx, u32 off, int n, u32 v)
{
    struct itm *t = (struct itm *)ctx;
    (void)n;
    switch (off) {
    case 0x004: t->cspsr = v & 0xfu; break;
    case 0x010: t->acpr = v & 0xffffu; break;
    case 0x0F0: t->sppr = v & 3u; break;
    case 0x304: t->ffcr = v & 0x0000F176u; break;
    }
}

static void tpiu_reset(void *ctx)
{
    struct itm *t = (struct itm *)ctx;
    if (t->sim->reset_cause != ESK_RESET_POWER)
        return;
    t->cspsr = 1;
    t->acpr = 0;
    t->sppr = 1;
    t->ffcr = 0x102u;
}

const struct esk_dev_ops esk_tpiu_ops = { "tpiu", tpiu_read, tpiu_write, tpiu_reset, 0, 0 };

void *esk_itm_create(struct esk_mach *s, struct esk_cm_state *core)
{
    struct itm *t = (struct itm *)esk_mach_alloc(s, sizeof *t);
    if (!t)
        return 0;
    memset(t, 0, sizeof *t);
    t->sim = s;
    t->core = core;
    t->locked = 1;
    t->cspsr = 1;
    t->sppr = 1;
    t->ffcr = 0x102u;
    s->itm = t;
    return t;
}

/* the serial wire output's bytes that have arrived by now, at most `cap`
 * of them, with the output's protocol and bit rate: a probe's receiver
 * reads them (st_link.c) */
u32 esk_cm_swo_take(struct esk_mach *m, u8 *buf, u32 cap, u32 *sppr, double *baud)
{
    struct itm *t = itm_of(m);
    u32 k = 0;
    u64 now;
    if (!t)
        return 0;
    now = m->cycles;
    if (sppr)
        *sppr = t->sppr;
    if (baud)
        *baud = (double)esk_mach_core_hz(m) / ((double)(t->acpr & 0xffffu) + 1.0);
    while (k < cap && t->q.n && t->q.t[t->q.head] <= now) {
        u8 v = t->q.b[t->q.head];
        t->q.head = (t->q.head + 1) % SWO_Q;
        t->q.n--;
        if (!m->dbg.swo)
            continue; /* the pin does not carry it */
        buf[k++] = v;
    }
    return k;
}
