/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rmt.c -- the ESP32's remote control peripheral (RMT, TRM v5 chapter 15):
 * eight channels, each a transmitter or a receiver (rmt_sig_out0-7,
 * signals 87-94 of the GPIO matrix; rmt_sig_in0-7, 83-90), sharing a
 * 512 x 32-bit RAM at +0x800, 64 words to a block; matrix source 47. The
 * ESP32-C3's model (mcu/esp32c3/rmt.c) on the ESP32's registers.
 *
 *   - A word holds two entries, the low half first: bits 14:0 a period in
 *     the channel's clock, bit 15 the level; a period of 0 ends the data.
 *     The channel's clock is APB_CLK (CONF1's REF_ALWAYS_ON 1) or
 *     REF_TICK's 1 MHz (0), divided by CONF0's DIV_CNT (0 is 256). The
 *     settings act as written (the ESP32 has no CONF_UPDATE).
 *   - Channel n uses MEM_SIZE blocks from its own (block n). The CPU
 *     reaches the RAM at its addresses or, with APB_CONF's APB_FIFO_MASK 0,
 *     through CHnDATA: a write appends to the channel's memory, a read takes
 *     its next word; APB_MEM_RST rewinds it (CHnADDR).
 *   - TX_START sends from the start of the channel's memory: each entry's
 *     level for its period. An end marker stops it (CHn_TX_END) and leaves
 *     the output at IDLE_OUT_LV when IDLE_OUT_EN is set, else at the
 *     marker's level. Past the end of its memory it goes on from the start
 *     with APB_CONF's MEM_TX_WRAP_EN, else stops with CHn_ERR. Every TX_LIM
 *     words sent raise CHn_TX_THR_EVENT. In continuous mode (TX_CONTI_MODE)
 *     an end marker, or the end of the memory, starts the data again.
 *   - The carrier (CARRIER_EN): while the channel sends the level
 *     CARRIER_OUT_LV, the output is CARRIER_DUTY's HIGH source-clock cycles
 *     high and LOW low, from the start of each such stretch.
 *   - RX_EN starts a receiver: from the input's first edge it writes each
 *     level and its length in the channel's clock; a level longer than
 *     IDLE_THRES ends it with an end marker (period 0, the idle level) and
 *     CHn_RX_END. With RX_FILTER_EN a pulse shorter than RX_FILTER_THRES
 *     APB_CLK cycles is dropped. At the end of its memory a receiver stops
 *     with CHn_ERR and MEM_FULL. A receiver whose memory the CPU owns
 *     (MEM_OWNER 0) writes nothing and sets MEM_OWNER_ERR.
 *   - CHnSTATUS: MEM_WADDR_EX and MEM_RADDR_EX the RAM's word addresses
 *     (ESP-IDF counts what a receiver wrote from the first), MEM_FULL,
 *     MEM_OWNER_ERR.
 *   - INT_ST is INT_RAW and INT_ENA, raising the source; INT_CLR clears.
 * Where it differs inside: the edges are timed from the start of each
 * transmission, not from a free-running divider (REF_CNT_RST has nothing
 * to reset), and a carrier restarts with each stretch it modulates; the
 * receiver's filter does not delay what it passes. Not yet: the memory's
 * power-down (MEM_PD), STATUS's STATE and the APB-side error bits, the
 * clock gate's effect on a running channel. */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32/soc/esp32.h"

#define SOURCE 47
#define NCH 8
#define NWORDS 512
#define BLOCK 64
#define SIG_OUT0 87
#define SIG_IN0 83
#define REF_TICK_HZ 1000000u
#define FS_PER_S 1000000000000000ull

/* CONF0 */
#define CARRIER_EN (1u << 28)
#define CARRIER_LV (1u << 29)
/* CONF1 */
#define TX_START (1u << 0)
#define RX_EN (1u << 1)
#define MEM_WR_RST (1u << 2)
#define MEM_RD_RST (1u << 3)
#define APB_MEM_RST (1u << 4)
#define MEM_OWNER (1u << 5)
#define TX_CONTI (1u << 6)
#define FILTER_EN (1u << 7)
#define REF_ALWAYS_ON (1u << 17)
#define IDLE_LV (1u << 18)
#define IDLE_EN (1u << 19)
/* APB_CONF */
#define FIFO_MASK (1u << 0)
#define TX_WRAP (1u << 1)
/* INT_RAW */
#define I_TX_END(n) (1u << (3 * (n)))
#define I_RX_END(n) (1u << (3 * (n) + 1))
#define I_ERR(n) (1u << (3 * (n) + 2))
#define I_TX_THR(n) (1u << (24 + (n)))

struct ch {
    struct esk_rf_reg *conf0, *conf1, *status, *addr, *duty, *lim;
    /* transmitting */
    int busy;
    u32 e;        /* the entry being sent, from the channel's first */
    u64 left;     /* fs until the entry's end */
    int level;    /* the data's level (while idle: the idle output's) */
    int out;      /* the output */
    int carrying; /* the carrier modulates the output now */
    int cphase;   /* the carrier's high half */
    u64 cleft;    /* fs until the carrier's next edge */
    u32 sent;
    /* receiving */
    int on, active, raw, full;
    int c_level, p_level; /* the level written next, the input's since p_t */
    u64 c_t, p_t;         /* their starts, in core cycles */
    u32 wr;               /* entries written, from the channel's first */
    u32 apb;              /* CHnDATA's word, from the channel's first */
};

struct e32_rmt {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *apb_conf, *raw, *st, *ena, *ram[NWORDS];
    struct ch ch[NCH];
    int clocked, level;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void update(struct e32_rmt *k)
{
    u32 st = reg(k->raw) & reg(k->ena);
    int level = st != 0;
    esk_rf_hw(k->st, st);
    if (level != k->level) {
        k->level = level;
        esk_esp32_source(k->s, SOURCE, level);
    }
}

static void raise(struct e32_rmt *k, u32 bits)
{
    esk_rf_hw(k->raw, reg(k->raw) | bits);
    update(k);
}

/* the channel's source clock period in fs: APB_CLK or REF_TICK */
static double src_fs(const struct e32_rmt *k, const struct ch *c)
{
    u32 hz = (reg(c->conf1) & REF_ALWAYS_ON) ? esk_esp32_apb_hz(k->s) : REF_TICK_HZ;
    return hz ? (double)FS_PER_S / (double)hz : 0;
}

/* the channel clock's period in fs: DIV_CNT source cycles */
static double tick_fs(const struct e32_rmt *k, const struct ch *c)
{
    u32 div = reg(c->conf0) & 0xffu;
    return (div ? (double)div : 256.0) * src_fs(k, c);
}

static u64 fs_per_cycle(const struct e32_rmt *k)
{
    u32 hz = esk_mach_core_hz(k->s);
    return hz ? FS_PER_S / hz : 0;
}

static u64 fs_of(double fs) { return fs <= 0 ? 0 : (u64)(fs + 0.5); }

static u32 mem_size(const struct ch *c)
{
    u32 n = (reg(c->conf0) >> 24) & 15u;
    return n ? n : 1u;
}

static u32 entry(const struct e32_rmt *k, int n, u32 e)
{
    u32 w = (u32)BLOCK * (u32)n + e / 2u, v;
    if (w >= NWORDS)
        return 0;
    v = reg(k->ram[w]);
    return e & 1u ? v >> 16 : v & 0xffffu;
}

static void status(struct e32_rmt *k, int n)
{
    struct ch *c = &k->ch[n];
    u32 raddr = (u32)BLOCK * (u32)n + c->e / 2u, waddr = (u32)BLOCK * (u32)n + (c->wr + 1u) / 2u;
    u32 v = (waddr & 0x3ffu) | (raddr & 0x3ffu) << 12;
    if (c->full)
        v |= 1u << 28;
    if (c->on && c->active && !(reg(c->conf1) & MEM_OWNER))
        v |= 1u << 27;
    esk_rf_hw(c->status, v);
    esk_rf_hw(c->addr, 0x3FF56800u + 4u * ((u32)BLOCK * (u32)n + c->apb));
}

/* ---- transmitting ------------------------------------------------------------ */

static void tx_drive(struct e32_rmt *k, int n)
{
    struct ch *c = &k->ch[n];
    int out = c->carrying ? c->cphase : c->level;
    if (out != c->out) {
        c->out = out;
        esk_esp32_signal(k->s, SIG_OUT0 + n, out, 1);
    }
}

static u32 duty_half(const struct ch *c, int high)
{
    u32 v = high ? reg(c->duty) >> 16 : reg(c->duty) & 0xffffu;
    return v ? v : 1u;
}

/* the carrier for the output's present level: on (from its high half) or off */
static void tx_carrier(struct e32_rmt *k, int n)
{
    struct ch *c = &k->ch[n];
    u32 c0 = reg(c->conf0);
    int want = (c0 & CARRIER_EN) && c->busy && c->level == ((c0 & CARRIER_LV) != 0) && src_fs(k, c) > 0;
    if (want && !c->carrying) {
        c->carrying = 1;
        c->cphase = 1;
        c->cleft = fs_of((double)duty_half(c, 1) * src_fs(k, c));
    } else if (!want) {
        c->carrying = 0;
    }
}

static void tx_idle(struct e32_rmt *k, int n, int marker_level)
{
    struct ch *c = &k->ch[n];
    u32 c1 = reg(c->conf1);
    c->busy = 0;
    c->level = (c1 & IDLE_EN) ? (c1 & IDLE_LV) != 0 : marker_level;
    tx_carrier(k, n);
    tx_drive(k, n);
    status(k, n);
}

/* the entry at c->e, after the one before ended: its level and period, or
 * what an end marker or the memory's end does */
static void tx_load(struct e32_rmt *k, int n)
{
    struct ch *c = &k->ch[n];
    u32 total = 2u * (u32)BLOCK * mem_size(c);
    int restarts = 0;
    for (;;) {
        u32 v, period;
        if (c->e >= total) {
            if ((reg(k->apb_conf) & TX_WRAP) || (reg(c->conf1) & TX_CONTI)) {
                c->e = 0;
            } else {
                raise(k, I_ERR(n));
                tx_idle(k, n, c->level);
                return;
            }
        }
        v = entry(k, n, c->e);
        period = v & 0x7fffu;
        if (period) {
            c->level = (v >> 15) & 1u;
            c->left = fs_of((double)period * tick_fs(k, c));
            if (!c->left)
                c->left = 1;
            tx_carrier(k, n);
            tx_drive(k, n);
            status(k, n);
            return;
        }
        /* an end marker */
        if (!(reg(c->conf1) & TX_CONTI)) {
            raise(k, I_TX_END(n));
            tx_idle(k, n, (int)((v >> 15) & 1u));
            return;
        }
        if (c->e == 0 || ++restarts > 1) { /* nothing but markers: nothing to send */
            raise(k, I_ERR(n));
            tx_idle(k, n, c->level);
            return;
        }
        c->e = 0;
    }
}

static void tx_start(struct e32_rmt *k, int n)
{
    struct ch *c = &k->ch[n];
    if (src_fs(k, c) <= 0)
        return;
    c->busy = 1;
    c->e = 0;
    c->sent = 0;
    c->carrying = 0;
    tx_load(k, n);
}

/* the entry ended: on to the next */
static void tx_next(struct e32_rmt *k, int n)
{
    struct ch *c = &k->ch[n];
    c->e++;
    if (!(c->e & 1u)) { /* a word sent */
        u32 lim = reg(c->lim) & 0x1ffu;
        if (lim && ++c->sent >= lim) {
            c->sent = 0;
            raise(k, I_TX_THR(n));
        }
    }
    tx_load(k, n);
}

/* ---- receiving ---------------------------------------------------------------- */

/* one entry into the memory: level and period */
static void rx_write(struct e32_rmt *k, int n, int level, u32 period)
{
    struct ch *c = &k->ch[n];
    u32 total = 2u * (u32)BLOCK * mem_size(c), w;
    if (c->full)
        return;
    if (!(reg(c->conf1) & MEM_OWNER)) { /* the CPU's: MEM_OWNER_ERR */
        status(k, n);
        return;
    }
    if (c->wr >= total) {
        c->full = 1;
        c->active = 0;
        raise(k, I_ERR(n));
        status(k, n);
        return;
    }
    w = (u32)BLOCK * (u32)n + c->wr / 2u;
    if (w < NWORDS) {
        struct esk_rf_reg *cell = k->ram[w];
        u32 half = (period & 0x7fffu) | (u32)(level != 0) << 15;
        esk_rf_hw(cell, c->wr & 1u ? (reg(cell) & 0xffffu) | half << 16 : (reg(cell) & 0xffff0000u) | half);
    }
    c->wr++;
    status(k, n);
}

static u32 rx_ticks(const struct e32_rmt *k, const struct ch *c, u64 cycles)
{
    double tf = tick_fs(k, c), t;
    if (tf <= 0)
        return 0;
    t = (double)cycles * (double)fs_per_cycle(k) / tf + 0.5;
    return t > 32767.0 ? 32767u : (u32)t;
}

/* the receiver's idle threshold in core cycles */
static u64 rx_idle_cycles(const struct e32_rmt *k, const struct ch *c)
{
    u64 fpc = fs_per_cycle(k);
    double thres = (double)((reg(c->conf0) >> 8) & 0xffffu);
    if (!fpc)
        return 0;
    return (u64)(thres * tick_fs(k, c) / (double)fpc) + 1u;
}

/* a pulse from `from` to `to` (cycles) the filter drops */
static int rx_drops(const struct e32_rmt *k, const struct ch *c, u64 from, u64 to)
{
    u32 apb = esk_esp32_apb_hz(k->s);
    double len = (double)(to - from) * (double)fs_per_cycle(k);
    return (reg(c->conf1) & FILTER_EN) && apb &&
           len < (double)((reg(c->conf1) >> 8) & 0xffu) * (double)FS_PER_S / (double)apb;
}

static void rx_end(struct e32_rmt *k, int n)
{
    struct ch *c = &k->ch[n];
    if (c->p_level != c->c_level) { /* the last change held: it counts */
        rx_write(k, n, c->c_level, rx_ticks(k, c, c->p_t - c->c_t));
        c->c_level = c->p_level;
        c->c_t = c->p_t;
    }
    rx_write(k, n, c->c_level, 0);
    c->active = 0;
    raise(k, I_RX_END(n));
    status(k, n);
}

static void clocking(struct e32_rmt *k);

/* the inputs changed: each receiver's edge at the time the change came */
static void rx_watch(void *ctx)
{
    struct e32_rmt *k = (struct e32_rmt *)ctx;
    struct esk_mach *s = k->s;
    u64 lag = (u64)s->input_lead + s->output_lag, t = s->cycles > lag ? s->cycles - lag : 0;
    int n;
    for (n = 0; n < NCH; n++) {
        struct ch *c = &k->ch[n];
        int level;
        if (!c->on)
            continue;
        level = esk_esp32_signal_in(s, SIG_IN0 + n);
        if (level == c->raw)
            continue;
        c->raw = level;
        if (c->full)
            continue;
        if (!c->active) { /* the first edge starts it */
            c->active = 1;
            c->c_level = c->p_level = level;
            c->c_t = c->p_t = t;
            status(k, n);
            continue;
        }
        if (c->p_level != c->c_level) {
            if (rx_drops(k, c, c->p_t, t)) { /* back as it was */
                c->p_level = c->c_level;
                continue;
            }
            rx_write(k, n, c->c_level, rx_ticks(k, c, c->p_t - c->c_t));
            if (!c->active)
                continue;
            c->c_level = c->p_level;
            c->c_t = c->p_t;
        }
        c->p_level = level;
        c->p_t = t;
    }
    clocking(k);
}

/* ---- time --------------------------------------------------------------------- */

static void clocking(struct e32_rmt *k)
{
    int n, on = 0;
    for (n = 0; n < NCH; n++)
        if (k->ch[n].busy || k->ch[n].carrying || k->ch[n].active)
            on = 1;
    esk_rf_clock(k->p, &k->clocked, on);
}

/* fs until the channel's next edge, or 0 for none */
static u64 tx_due(const struct ch *c)
{
    u64 d = 0;
    if (c->busy)
        d = c->left;
    if (c->carrying && (!d || c->cleft < d))
        d = c->cleft;
    return d;
}

static void rmt_tick(void *ctx, u32 cycles)
{
    struct e32_rmt *k = (struct e32_rmt *)ctx;
    struct esk_mach *s = k->s;
    u64 fpc = fs_per_cycle(k), budget = (u64)cycles * fpc;
    int n;
    if (!fpc)
        return;
    for (;;) {
        u64 d = 0;
        for (n = 0; n < NCH; n++) {
            u64 x = tx_due(&k->ch[n]);
            if (x && (!d || x < d))
                d = x;
        }
        if (!d || d > budget) {
            for (n = 0; n < NCH; n++) {
                struct ch *c = &k->ch[n];
                if (c->busy)
                    c->left -= budget < c->left ? budget : c->left;
                if (c->carrying)
                    c->cleft -= budget < c->cleft ? budget : c->cleft;
            }
            break;
        }
        budget -= d;
        /* the edges switch at their time: this many cycles before now */
        s->output_lag = (u32)(budget / fpc);
        for (n = 0; n < NCH; n++) {
            struct ch *c = &k->ch[n];
            int ended = c->busy && c->left == d;
            if (c->busy)
                c->left -= d;
            if (c->carrying) {
                c->cleft -= d;
                if (!c->cleft) {
                    c->cphase = !c->cphase;
                    c->cleft = fs_of((double)duty_half(c, c->cphase) * src_fs(k, c));
                    if (!c->cleft)
                        c->cleft = 1;
                    if (!ended)
                        tx_drive(k, n);
                }
            }
            if (ended)
                tx_next(k, n);
        }
        s->output_lag = 0;
    }
    for (n = 0; n < NCH; n++) {
        struct ch *c = &k->ch[n];
        u64 last = c->p_t > c->c_t ? c->p_t : c->c_t;
        if (c->active && s->cycles >= last + rx_idle_cycles(k, c))
            rx_end(k, n);
    }
    clocking(k);
}

static int rmt_next(void *ctx, u32 *cycles)
{
    struct e32_rmt *k = (struct e32_rmt *)ctx;
    u64 fpc = fs_per_cycle(k), best = 0;
    int n, any = 0;
    if (!fpc)
        return 0;
    for (n = 0; n < NCH; n++) {
        struct ch *c = &k->ch[n];
        u64 d = tx_due(c);
        if (d) {
            u64 x = (d + fpc - 1u) / fpc;
            if (!any || x < best)
                best = x;
            any = 1;
        }
        if (c->active) {
            u64 last = c->p_t > c->c_t ? c->p_t : c->c_t, at = last + rx_idle_cycles(k, c);
            at = at > k->s->cycles ? at - k->s->cycles : 1u;
            if (!any || at < best)
                best = at;
            any = 1;
        }
    }
    if (!any)
        return 0;
    *cycles = best == 0 ? 1u : best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

/* ---- registers ---------------------------------------------------------------- */

static u32 rmt_read(void *ctx, u32 off, int n)
{
    struct e32_rmt *k = (struct e32_rmt *)ctx;
    u32 w = off & ~3u;
    if (w < 0x20u && !(reg(k->apb_conf) & FIFO_MASK)) { /* CHnDATA: the channel's next word */
        int i = (int)(w / 4u);
        struct ch *c = &k->ch[i];
        u32 at = (u32)BLOCK * (u32)i + c->apb, v = at < NWORDS ? reg(k->ram[at]) : 0;
        if (!k->s->bus.debug) {
            c->apb = (c->apb + 1u) % ((u32)BLOCK * mem_size(c));
            status(k, i);
        }
        return v >> (8u * (off & 3u));
    }
    return esk_rf_read(k->p, off, n);
}

static void conf1_write(struct e32_rmt *k, int n, u32 v, u32 was)
{
    struct ch *c = &k->ch[n];
    if (v & MEM_RD_RST)
        c->e = 0;
    if (v & APB_MEM_RST)
        c->apb = 0;
    if (v & MEM_WR_RST) {
        c->wr = 0;
        c->full = 0;
    }
    if ((v & RX_EN) && !(was & RX_EN)) {
        c->on = 1;
        c->active = 0;
        c->full = 0;
        c->raw = esk_esp32_signal_in(k->s, SIG_IN0 + n);
    } else if (!(v & RX_EN)) {
        c->on = 0;
        c->active = 0;
    }
    if (!c->busy) /* the idle output as now configured */
        tx_idle(k, n, c->level);
    if (v & TX_START) {
        esk_rf_hw(c->conf1, reg(c->conf1) & ~TX_START); /* it clears itself */
        tx_start(k, n);
    }
    status(k, n);
}

static void rmt_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_rmt *k = (struct e32_rmt *)ctx;
    u32 w = off & ~3u;
    int i;
    if (w < 0x20u) { /* CHnDATA */
        if (!(reg(k->apb_conf) & FIFO_MASK)) {
            struct ch *c = &k->ch[w / 4u];
            u32 at = (u32)BLOCK * (w / 4u) + c->apb;
            if (at < NWORDS)
                esk_rf_hw(k->ram[at], v);
            c->apb = (c->apb + 1u) % ((u32)BLOCK * mem_size(c));
            status(k, (int)(w / 4u));
        }
        return;
    }
    if (w == 0xACu) { /* INT_CLR */
        esk_rf_hw(k->raw, reg(k->raw) & ~v);
        update(k);
        return;
    }
    if ((w >= 0x60u && w < 0xA8u))
        return; /* STATUS, ADDR, INT_RAW and INT_ST are read-only */
    {
        u32 was[NCH];
        for (i = 0; i < NCH; i++)
            was[i] = reg(k->ch[i].conf1);
        esk_rf_write(k->p, off, n, v);
        if (w == 0xA8u) /* INT_ENA */
            update(k);
        for (i = 0; i < NCH; i++)
            if (w == esk_rf_off(k->ch[i].conf1))
                conf1_write(k, i, reg(k->ch[i].conf1), was[i]);
    }
    clocking(k);
}

static void rmt_reset(void *ctx)
{
    struct e32_rmt *k = (struct e32_rmt *)ctx;
    int i;
    for (i = 0; i < NCH; i++) {
        struct ch *c = &k->ch[i];
        struct ch keep = *c;
        memset(c, 0, sizeof *c);
        c->conf0 = keep.conf0;
        c->conf1 = keep.conf1;
        c->status = keep.status;
        c->addr = keep.addr;
        c->duty = keep.duty;
        c->lim = keep.lim;
        c->out = -1;
    }
    k->level = 0;
    esk_esp32_source(k->s, SOURCE, 0);
    k->clocked = 0;
    for (i = 0; i < NCH; i++) {
        tx_idle(k, i, 0);
        k->ch[i].raw = esk_esp32_signal_in(k->s, SIG_IN0 + i);
    }
    update(k);
    clocking(k);
}

const struct esk_dev_ops esk_esp32_rmt_ops = {
    "esp32-rmt", rmt_read, rmt_write, rmt_reset, rmt_tick, rmt_next,
};

void *esk_esp32_rmt_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_rmt *k = (struct e32_rmt *)esk_mach_alloc(s, sizeof *k);
    char name[24];
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->apb_conf = esk_rf_reg(p, "APB_CONF");
    k->raw = esk_rf_reg(p, "INT_RAW");
    k->st = esk_rf_reg(p, "INT_ST");
    k->ena = esk_rf_reg(p, "INT_ENA");
    if (!k->apb_conf || !k->raw || !k->st || !k->ena)
        return 0;
    for (i = 0; i < NCH; i++) {
        struct ch *c = &k->ch[i];
        snprintf(name, sizeof name, "CH%dCONF0", i);
        c->conf0 = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "CH%dCONF1", i);
        c->conf1 = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "CH%dSTATUS", i);
        c->status = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "CH%dADDR", i);
        c->addr = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "CH%dCARRIER_DUTY", i);
        c->duty = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "CH%d_TX_LIM", i);
        c->lim = esk_rf_reg(p, name);
        if (!c->conf0 || !c->conf1 || !c->status || !c->addr || !c->duty || !c->lim)
            return 0;
    }
    for (i = 0; i < NWORDS; i++) {
        snprintf(name, sizeof name, "RAM%d", i);
        if (!(k->ram[i] = esk_rf_reg(p, name)))
            return 0;
    }
    if (esk_esp32_signal_watch(s, rx_watch, k) != 0)
        return 0;
    return k;
}
