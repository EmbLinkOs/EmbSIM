/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* busctrl.c -- the RP2040's bus fabric and its BUSCTRL registers (RP2040
 * datasheet 2.1, "Bus Fabric", and 2.1.5's register list; the Pico SDK's
 * hardware/structs/busctrl.h names the events):
 *
 *   - the crossbar's four upstream masters: core 0, core 1, and the DMA's
 *     read and write ports; its downstream ports, each serving one access a
 *     cycle: the ROM, the XIP, SRAM0 to SRAM5 (the striped 256 KiB at
 *     0x20000000 word by word across SRAM0-3, SRAM4 at 0x20040000 and
 *     SRAM5 at 0x20041000; the non-striped alias at 0x21000000 would reach
 *     SRAM0-3 one after another, but the part's description does not map
 *     it), FASTPERI
 *     (the AHB-Lite peripherals at 0x50000000: DMA, USB, PIO) and the APB
 *     bridge. The SIO and each core's private peripheral bus are not on the
 *     crossbar and never contend;
 *   - two masters reaching one port in the same cycle: the one BUS_PRIORITY
 *     sets high wins over a low one; between two of the same level, round
 *     robin (the one that lost last time on that port wins). The other
 *     waits a cycle, and tries again;
 *   - PERFCTR0-3 count the event their PERFSEL selects: each port's
 *     accesses and its contested accesses (the ones that waited),
 *     saturating at 24 bits; writing a counter clears it. BUS_PRIORITY_ACK
 *     reads 1 once a priority written has reached the arbiters (at once).
 *
 * Inside: each core's loads and stores (and its exception stacking) are
 * seen as they are made, at the core's cycle; its instruction fetches as
 * one access each time the core's program counter enters another word (the
 * Cortex-M0+ fetches 32 bits at a time); the DMA's read at the cycle its
 * transfer is made and its write the cycle after. The arbitration looks
 * at a window of the last 1024 cycles of each master on each port: an
 * access that comes later than that after another master's in the same
 * cycle is not seen as contended. A master that had already gone ahead in
 * the cycle another of higher priority then takes is set back one cycle.
 * Not modelled: the wait states of the APB bridge and of the XIP cache's
 * misses (the ports' own latency: an access is one cycle), the fetches the
 * core's prefetch makes but does not use, the arbiters' pipelining, the
 * accesses the USB controller and the debugger make. */
#include <string.h>

#include "mcu/rp2040/rp2040.h"

#define NPORT 10
#define NMASTER 4
#define WIN_WORDS 16u /* 1024 cycles */
#define WIN_CYCLES (64u * WIN_WORDS)

enum { P_APB, P_FASTPERI, P_SRAM5, P_SRAM4, P_SRAM3, P_SRAM2, P_SRAM1, P_SRAM0, P_XIP, P_ROM, P_NONE = -1 };

struct window {
    u64 base;            /* the cycle bit 0 of bits[0] stands for */
    u64 bits[WIN_WORDS]; /* the cycles the master accessed the port */
};

struct busctrl {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *prio, *ack, *ctr[4], *sel[4];
    struct window w[NPORT][NMASTER];
    int rr[NPORT]; /* the master that won last on the port, or -1 */
};

/* the port an address reaches, or P_NONE */
static int port_of(u32 a)
{
    switch (a >> 28) {
    case 0x0: return P_ROM;
    case 0x1: return P_XIP;
    case 0x2:
        if (a < 0x20040000u)
            return P_SRAM0 - (int)(a >> 2 & 3u);
        if (a < 0x20041000u)
            return P_SRAM4;
        if (a < 0x20042000u)
            return P_SRAM5;
        if (a >= 0x21000000u && a < 0x21040000u)
            return P_SRAM0 - (int)(a >> 16 & 3u);
        return P_NONE;
    case 0x4: return P_APB;
    case 0x5: return P_FASTPERI;
    }
    return P_NONE;
}

static int high(const struct busctrl *b, int master)
{
    static const u32 bit[NMASTER] = { 1u << 0, 1u << 4, 1u << 8, 1u << 12 };
    return b->prio && ((u32)b->prio->value & bit[master]) != 0;
}

/* a performance event: the counters that select it count it */
static void event(struct busctrl *b, int ev)
{
    int k;
    for (k = 0; k < 4; k++)
        if (b->sel[k] && b->ctr[k] && (int)(b->sel[k]->value & 0x1fu) == ev && b->ctr[k]->value < 0xFFFFFFu)
            esk_rf_hw(b->ctr[k], b->ctr[k]->value + 1u);
}

static int seen(const struct window *w, u64 t)
{
    u64 d;
    if (t < w->base)
        return 0;
    d = t - w->base;
    return d < WIN_CYCLES && (w->bits[d / 64u] >> (d % 64u) & 1u);
}

static void mark(struct window *w, u64 t)
{
    u64 d;
    if (t < w->base)
        return;
    d = t - w->base;
    if (d >= WIN_CYCLES) {
        /* slide the window forward to end at t */
        u64 nb = t - (WIN_CYCLES - 64u), shift = (nb - w->base) / 64u, k;
        nb = w->base + shift * 64u;
        if (shift >= WIN_WORDS) {
            memset(w->bits, 0, sizeof w->bits);
        } else {
            for (k = 0; k + shift < WIN_WORDS; k++)
                w->bits[k] = w->bits[k + shift];
            for (; k < WIN_WORDS; k++)
                w->bits[k] = 0;
        }
        w->base = nb;
        d = t - w->base;
    }
    w->bits[d / 64u] |= (u64)1 << (d % 64u);
}

/* master `m` sets back a cycle */
static void set_back(struct busctrl *b, int m)
{
    if (m < 2)
        esk_cortexm_core_stall(b->s, m, 1);
    else
        esk_rp2040_dma_stall(b->s, 1);
}

/* master `m` accesses address `a` at cycle `t`: the cycles it waits */
static u32 access(struct busctrl *b, int m, u32 a, u64 t)
{
    int port = port_of(a), o, tries;
    u32 waited = 0;
    if (port == P_NONE)
        return 0;
    for (tries = 0; tries < NMASTER; tries++) {
        int rival = -1;
        for (o = 0; o < NMASTER; o++)
            if (o != m && seen(&b->w[port][o], t)) {
                rival = o;
                break;
            }
        if (rival < 0)
            break;
        {
            int hm = high(b, m), ho = high(b, rival), m_wins;
            m_wins = hm != ho ? hm : b->rr[port] == rival;
            b->rr[port] = m_wins ? m : rival;
            event(b, 2 * port); /* the losing access was contested */
            if (!m_wins) {
                t++;
                waited++;
                continue;
            }
            set_back(b, rival); /* it had gone ahead in a cycle it lost */
            break;
        }
    }
    mark(&b->w[port][m], t);
    event(b, 2 * port + 1);
    return waited;
}

/* the bus's arbiter hook: an access of the master the bus names, or of the
 * core running */
static void arb(void *ctx, u32 a)
{
    struct busctrl *b = (struct busctrl *)ctx;
    struct esk_bus *bus = &b->s->bus;
    int m;
    u32 w;
    if (bus->master >= 0) {
        m = bus->master;
        (void)access(b, m, a, bus->master_t);
        return;
    }
    m = esk_cortexm_core(b->s);
    if (m < 0 || m > 1)
        return;
    w = access(b, m, a, b->s->cycles);
    if (w)
        esk_cortexm_core_stall(b->s, m, w);
}

static u32 bc_read(void *ctx, u32 off, int n) { return esk_rf_read(((struct busctrl *)ctx)->p, off, n); }

static void bc_write(void *ctx, u32 off, int n, u32 v)
{
    struct busctrl *b = (struct busctrl *)ctx;
    int k;
    for (k = 0; k < 4; k++)
        if (b->ctr[k] && off == esk_rf_off(b->ctr[k])) {
            esk_rf_hw(b->ctr[k], 0); /* write to clear */
            return;
        }
    if (b->ack && off == esk_rf_off(b->ack))
        return; /* read-only */
    esk_rf_write(b->p, off, n, v);
    if (b->prio && off == esk_rf_off(b->prio) && b->ack)
        esk_rf_hw(b->ack, 1); /* the arbiters have it */
}

static void bc_reset(void *ctx)
{
    struct busctrl *b = (struct busctrl *)ctx;
    memset(b->w, 0, sizeof b->w);
    memset(b->rr, 0xff, sizeof b->rr);
    b->s->bus.arb = arb;
    b->s->bus.arb_ctx = b;
    b->s->bus.master = -1;
}

const struct esk_dev_ops esk_rp2040_busctrl_ops = { "rp2040-busctrl", bc_read, bc_write, bc_reset, 0, 0 };

void *esk_rp2040_busctrl_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    static const char *const ctr[4] = { "PERFCTR0", "PERFCTR1", "PERFCTR2", "PERFCTR3" };
    static const char *const sel[4] = { "PERFSEL0", "PERFSEL1", "PERFSEL2", "PERFSEL3" };
    struct busctrl *b = (struct busctrl *)esk_mach_alloc(s, sizeof *b);
    int k;
    if (!b)
        return 0;
    memset(b, 0, sizeof *b);
    b->s = s;
    b->p = p;
    b->prio = esk_rf_reg(p, "BUS_PRIORITY");
    b->ack = esk_rf_reg(p, "BUS_PRIORITY_ACK");
    for (k = 0; k < 4; k++) {
        b->ctr[k] = esk_rf_reg(p, ctr[k]);
        b->sel[k] = esk_rf_reg(p, sel[k]);
    }
    if (!b->prio)
        return 0;
    bc_reset(b);
    return b;
}
