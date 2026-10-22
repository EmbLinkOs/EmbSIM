/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rmt.c -- the ESP32-C3's remote control peripheral (RMT, TRM v1.4 chapter
 * 33): two transmitters (channels 0 and 1, signals rmt_sig_out0/1, 51 and
 * 52 of the GPIO matrix) and two receivers (channels 2 and 3, rmt_sig_in0/1)
 * sharing a 192 x 32-bit RAM at +0x400, 48 words to a block.
 *
 *   - A word holds two entries, the low half first: bits 14:0 a period in
 *     the channel's clock, bit 15 the level; a period of 0 ends the data.
 *     The channel's clock is rmt_sclk (SYS_CONF's SCLK_SEL: 1 APB_CLK,
 *     2 RC_FAST_CLK, 3 XTAL_CLK; divided by SCLK_DIV_NUM + 1 + DIV_A /
 *     DIV_B; stopped while SCLK_ACTIVE is 0) divided by the channel's
 *     DIV_CNT (0 is 256).
 *   - A channel uses MEM_SIZE blocks from its own (channel n's block n).
 *     The CPU reaches the RAM at its addresses or, with APB_FIFO_MASK 0,
 *     through CHnDATA: a write appends to a TX channel's block, a read
 *     takes the next word of an RX channel's; APB_MEM_RST rewinds it.
 *   - The settings table 33.3-1 lists take effect when CONF_UPDATE is
 *     written (the TX channels' whole CONF0, CARRIER_DUTY, TX_LIM and
 *     TX_SIM_EN; the RX channels' CONF0, RX_EN, RX_FILTER_THRES, the
 *     carrier thresholds and RX_LIM); until then the previous ones hold.
 *   - TX_START sends from the start of the channel's memory: each entry's
 *     level for its period, edges at the clock that makes them. An end
 *     marker stops it (CHn_TX_END) and leaves the output at IDLE_OUT_LV
 *     when IDLE_OUT_EN is set, else at the marker's level; TX_STOP (it
 *     clears itself) stops it at once. Past the end of its memory it goes
 *     on from the start with MEM_TX_WRAP_EN, else stops with CHn_TX_ERR.
 *     Every TX_LIM words sent raise CHn_TX_THR_EVENT. In continuous mode
 *     (TX_CONTI_MODE) an end marker, or the end of the memory, starts the
 *     data again; with TX_LOOP_CNT_EN each end marker counts a loop and
 *     the TX_LOOP_NUMth raises CHn_TX_LOOP (the C3's loop does not stop by
 *     itself: ESP-IDF stops it). With TX_SIM_EN the channels TX_SIM names
 *     start together once each has its TX_START.
 *   - The carrier (CARRIER_EN): during the level CARRIER_OUT_LV, the
 *     output is CARRIER_HIGH + 1 sclk cycles high and CARRIER_LOW + 1 low,
 *     from the start of each such stretch; with CARRIER_EFF_EN 0 also on
 *     an idle output at that level.
 *   - RX_EN starts a receiver: from the input's first edge it writes each
 *     level and its length in the channel's clock; a level longer than
 *     IDLE_THRES ends it with an end marker (period 0, the idle level) and
 *     CHm_RX_END. With RX_FILTER_EN a pulse shorter than RX_FILTER_THRES
 *     sclk cycles is dropped; with CARRIER_EN a gap shorter than the other
 *     level's threshold inside a burst of CARRIER_OUT_LV joins the burst
 *     (the carrier removed). Every RX_LIM words raise CHm_RX_THR_EVENT;
 *     at the end of its memory a receiver goes on from the start with
 *     MEM_RX_WRAP_EN, else stops with CHm_RX_ERR and MEM_FULL. A receiver
 *     whose memory the CPU owns (MEM_OWNER 0) writes nothing and sets
 *     MEM_OWNER_ERR. MEM_WADDR_EX is the word after the last it wrote.
 *   - INT_ST is INT_RAW and INT_ENA, raising matrix source 28; INT_CLR
 *     clears.
 * Where it differs inside: the edges are timed from the start of each
 * transmission, not from a free-running divider (REF_CNT_RST has nothing
 * to reset), and a carrier restarts with each stretch it modulates; the
 * receiver's filter does not delay what it passes. Not yet: the memory's
 * power-down (MEM_FORCE_PD), the status registers' STATE and the
 * APB-side error bits, the clock gate's effect on a running channel. */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define SOURCE 28
#define NWORDS 192
#define BLOCK 48
#define SIG_OUT0 51
#define SIG_IN0 51
#define RC_FAST_HZ 17500000u
#define XTAL_HZ 40000000u
#define FS_PER_S 1000000000000000ull

/* TX CONF0 */
#define TX_START (1u << 0)
#define MEM_RD_RST (1u << 1)
#define APB_MEM_RST (1u << 2)
#define TX_CONTI (1u << 3)
#define TX_WRAP (1u << 4)
#define IDLE_LV (1u << 5)
#define IDLE_EN (1u << 6)
#define TX_STOP (1u << 7)
#define CARRIER_EFF (1u << 20)
#define CARRIER_EN_TX (1u << 21)
#define CARRIER_LV_TX (1u << 22)
#define CONF_UPDATE_TX (1u << 24)
/* RX CONF0 and CONF1 */
#define CARRIER_EN_RX (1u << 28)
#define CARRIER_LV_RX (1u << 29)
#define RX_EN (1u << 0)
#define MEM_WR_RST (1u << 1)
#define MEM_OWNER (1u << 3)
#define FILTER_EN (1u << 4)
#define RX_WRAP (1u << 13)
#define CONF_UPDATE_RX (1u << 15)
/* INT_RAW */
#define I_TX_END(n) (1u << (n))
#define I_RX_END(m) (1u << (2 + (m)))
#define I_TX_ERR(n) (1u << (4 + (n)))
#define I_RX_ERR(m) (1u << (6 + (m)))
#define I_TX_THR(n) (1u << (8 + (n)))
#define I_RX_THR(m) (1u << (10 + (m)))
#define I_TX_LOOP(n) (1u << (12 + (n)))

struct tx {
    struct esk_rf_reg *conf, *duty, *lim, *status, *data;
    u32 conf_l, duty_l, lim_l; /* what CONF_UPDATE took */
    int busy, pending;
    u32 e;       /* the entry being sent, from the channel's first */
    u64 left;    /* fs until the entry's end */
    int level;   /* the data's level (while idle: the idle output's) */
    int out;     /* the output */
    int carrying; /* the carrier modulates the output now */
    int cphase;  /* the carrier's high half */
    u64 cleft;   /* fs until the carrier's next edge */
    u32 sent, loops, apb;
};

struct rx {
    struct esk_rf_reg *conf0, *conf1, *rm, *lim, *status, *data;
    u32 conf0_l, conf1_l, rm_l, lim_l;
    int on, active, raw, full;
    int c_level, p_level; /* the level written next, the input's since p_t */
    u64 c_t, p_t;         /* their starts, in core cycles */
    u32 wr;               /* entries written, from the channel's first */
    u32 words, apb;
};

struct c3_rmt {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *sys, *sim, *raw, *st, *ena, *ram[NWORDS];
    u32 sim_l;
    struct tx tx[2];
    struct rx rx[2];
    int clocked, level;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void update(struct c3_rmt *k)
{
    u32 st = reg(k->raw) & reg(k->ena) & 0x3fffu;
    int level = st != 0;
    esk_rf_hw(k->st, st);
    if (level != k->level) {
        k->level = level;
        esk_esp32c3_source(k->s, SOURCE, level);
    }
}

static void raise(struct c3_rmt *k, u32 bits)
{
    esk_rf_hw(k->raw, reg(k->raw) | bits);
    update(k);
}

/* rmt_sclk's period in fs, or 0 while it is stopped */
static double sclk_fs(const struct c3_rmt *k)
{
    u32 v = reg(k->sys), sel = (v >> 24) & 3u, num = (v >> 4) & 0xffu;
    u32 a = (v >> 12) & 0x3fu, b = (v >> 18) & 0x3fu, hz;
    if (!(v & (1u << 26)))
        return 0;
    hz = sel == 1 ? esk_esp32c3_apb_hz(k->s) : sel == 2 ? RC_FAST_HZ : sel == 3 ? XTAL_HZ : 0;
    if (!hz)
        return 0;
    return ((double)num + 1.0 + (b ? (double)a / (double)b : 0.0)) * (double)FS_PER_S / (double)hz;
}

/* the channel clock's period in fs: DIV_CNT sclk cycles */
static double tick_fs(const struct c3_rmt *k, u32 div)
{
    return (div ? (double)div : 256.0) * sclk_fs(k);
}

static u64 fs_per_cycle(const struct c3_rmt *k)
{
    u32 hz = esk_mach_core_hz(k->s);
    return hz ? FS_PER_S / hz : 0;
}

static u64 fs_of(double ticks_fs) { return ticks_fs <= 0 ? 0 : (u64)(ticks_fs + 0.5); }

/* ---- transmitters ---------------------------------------------------------- */

static u32 tx_size(const struct tx *t)
{
    u32 n = (t->conf_l >> 16) & 7u;
    return n ? n : 1u;
}

static u32 tx_entry(const struct c3_rmt *k, int n, u32 e)
{
    u32 w = (u32)BLOCK * (u32)n + e / 2u, v;
    if (w >= NWORDS)
        return 0;
    v = reg(k->ram[w]);
    return e & 1u ? v >> 16 : v & 0xffffu;
}

static void tx_status(struct c3_rmt *k, int n)
{
    struct tx *t = &k->tx[n];
    u32 raddr = (u32)BLOCK * (u32)n + t->e / 2u;
    esk_rf_hw(t->status, (raddr & 0x1ffu) | (u32)(t->busy ? 1 : 0) << 9 | (t->apb & 0x1ffu) << 12);
}

static void tx_drive(struct c3_rmt *k, int n)
{
    struct tx *t = &k->tx[n];
    int out = t->carrying ? t->cphase : t->level;
    if (out != t->out) {
        t->out = out;
        esk_esp32c3_signal(k->s, SIG_OUT0 + n, out, 1);
    }
}

/* the carrier for the output's present level: on (from its high half) or off */
static void tx_carrier(struct c3_rmt *k, int n)
{
    struct tx *t = &k->tx[n];
    int want = (t->conf_l & CARRIER_EN_TX) && (t->busy || !(t->conf_l & CARRIER_EFF)) &&
               t->level == ((t->conf_l & CARRIER_LV_TX) != 0) && sclk_fs(k) > 0;
    if (want && !t->carrying) {
        t->carrying = 1;
        t->cphase = 1;
        t->cleft = fs_of((double)((t->duty_l >> 16) + 1u) * sclk_fs(k));
    } else if (!want) {
        t->carrying = 0;
    }
}

static void tx_idle(struct c3_rmt *k, int n, int marker_level)
{
    struct tx *t = &k->tx[n];
    t->busy = 0;
    t->pending = 0;
    t->level = (t->conf_l & IDLE_EN) ? (t->conf_l & IDLE_LV) != 0 : marker_level;
    tx_carrier(k, n);
    tx_drive(k, n);
    tx_status(k, n);
}

/* the entry at t->e, after the one before ended: its level and period, or
 * what an end marker or the memory's end does */
static void tx_load(struct c3_rmt *k, int n)
{
    struct tx *t = &k->tx[n];
    u32 total = 2u * (u32)BLOCK * tx_size(t);
    int restarts = 0;
    for (;;) {
        u32 v, period;
        if (t->e >= total) {
            if (t->conf_l & (TX_WRAP | TX_CONTI)) {
                t->e = 0;
            } else {
                raise(k, I_TX_ERR(n));
                tx_idle(k, n, t->level);
                return;
            }
        }
        v = tx_entry(k, n, t->e);
        period = v & 0x7fffu;
        if (period) {
            double tf = tick_fs(k, (t->conf_l >> 8) & 0xffu);
            t->level = (v >> 15) & 1u;
            t->left = fs_of((double)period * tf);
            if (!t->left)
                t->left = 1;
            tx_carrier(k, n);
            tx_drive(k, n);
            tx_status(k, n);
            return;
        }
        /* an end marker */
        if (!(t->conf_l & TX_CONTI)) {
            raise(k, I_TX_END(n));
            tx_idle(k, n, (int)((v >> 15) & 1u));
            return;
        }
        if (t->lim_l & (1u << 19)) {
            t->loops++;
            if (t->loops == ((t->lim_l >> 9) & 0x3ffu))
                raise(k, I_TX_LOOP(n));
        }
        if (t->e == 0 || ++restarts > 1) { /* nothing but markers: nothing to send */
            raise(k, I_TX_ERR(n));
            tx_idle(k, n, t->level);
            return;
        }
        t->e = 0;
    }
}

static void tx_start(struct c3_rmt *k, int n)
{
    struct tx *t = &k->tx[n];
    if (sclk_fs(k) <= 0)
        return;
    t->busy = 1;
    t->pending = 0;
    t->e = 0;
    t->sent = 0;
    t->carrying = 0;
    tx_load(k, n);
}

/* the entry ended: on to the next */
static void tx_next(struct c3_rmt *k, int n)
{
    struct tx *t = &k->tx[n];
    t->e++;
    if (!(t->e & 1u)) { /* a word sent */
        u32 lim = t->lim_l & 0x1ffu;
        if (lim && ++t->sent >= lim) {
            t->sent = 0;
            raise(k, I_TX_THR(n));
        }
    }
    tx_load(k, n);
}

/* ---- receivers -------------------------------------------------------------- */

static u32 rx_size(const struct rx *r)
{
    u32 n = (r->conf0_l >> 23) & 7u;
    return n ? n : 1u;
}

static void rx_status(struct c3_rmt *k, int m)
{
    struct rx *r = &k->rx[m];
    u32 waddr = (u32)BLOCK * (u32)(m + 2) + (r->wr + 1u) / 2u;
    u32 v = (waddr & 0x1ffu) | (r->apb & 0x1ffu) << 12 | (u32)(r->active ? 1 : 0) << 22;
    if (r->full)
        v |= 1u << 26;
    if (!(reg(r->conf1) & MEM_OWNER) && r->active)
        v |= 1u << 25;
    esk_rf_hw(r->status, v);
}

/* one entry into the memory: level and period */
static void rx_write(struct c3_rmt *k, int m, int level, u32 period)
{
    struct rx *r = &k->rx[m];
    u32 total = 2u * (u32)BLOCK * rx_size(r), w;
    struct esk_rf_reg *cell;
    if (r->full)
        return;
    if (!(reg(r->conf1) & MEM_OWNER)) { /* the CPU's: MEM_OWNER_ERR */
        rx_status(k, m);
        return;
    }
    if (r->wr >= total) {
        if (reg(r->conf1) & RX_WRAP) {
            r->wr = 0;
        } else {
            r->full = 1;
            r->active = 0;
            raise(k, I_RX_ERR(m));
            rx_status(k, m);
            return;
        }
    }
    w = (u32)BLOCK * (u32)(m + 2) + r->wr / 2u;
    if (w < NWORDS) {
        u32 half = (period & 0x7fffu) | (u32)(level != 0) << 15;
        cell = k->ram[w];
        esk_rf_hw(cell, r->wr & 1u ? (reg(cell) & 0xffffu) | half << 16 : (reg(cell) & 0xffff0000u) | half);
    }
    r->wr++;
    if (!(r->wr & 1u)) {
        u32 lim = r->lim_l & 0x1ffu;
        if (lim && ++r->words >= lim) {
            r->words = 0;
            raise(k, I_RX_THR(m));
        }
    }
    rx_status(k, m);
}

static double rx_tick_fs(const struct c3_rmt *k, const struct rx *r)
{
    return tick_fs(k, r->conf0_l & 0xffu);
}

static u32 rx_ticks(const struct c3_rmt *k, const struct rx *r, u64 cycles)
{
    double tf = rx_tick_fs(k, r), t;
    if (tf <= 0)
        return 0;
    t = (double)cycles * (double)fs_per_cycle(k) / tf + 0.5;
    return t > 32767.0 ? 32767u : (u32)t;
}

/* the receiver's idle threshold in core cycles */
static u64 rx_idle_cycles(const struct c3_rmt *k, const struct rx *r)
{
    u64 fpc = fs_per_cycle(k);
    double thres = (double)((r->conf0_l >> 8) & 0x7fffu);
    if (!fpc)
        return 0;
    return (u64)(thres * rx_tick_fs(k, r) / (double)fpc) + 1u;
}

/* a pulse of the input's level `level` from `from` to `to` (cycles): one
 * the filter or the demodulator drops */
static int rx_drops(const struct c3_rmt *k, const struct rx *r, int level, u64 from, u64 to)
{
    u64 fpc = fs_per_cycle(k);
    double len = (double)(to - from) * (double)fpc;
    if ((reg(r->conf1) & FILTER_EN) && len < (double)((r->conf1_l >> 5) & 0xffu) * sclk_fs(k))
        return 1;
    if (r->conf0_l & CARRIER_EN_RX) {
        int lv = (r->conf0_l & CARRIER_LV_RX) != 0;
        u32 thres = level ? r->rm_l >> 16 : r->rm_l & 0xffffu;
        if (level != lv && len < (double)thres * rx_tick_fs(k, r))
            return 1;
    }
    return 0;
}

static void rx_end(struct c3_rmt *k, int m)
{
    struct rx *r = &k->rx[m];
    if (r->p_level != r->c_level) { /* the last change held: it counts */
        rx_write(k, m, r->c_level, rx_ticks(k, r, r->p_t - r->c_t));
        r->c_level = r->p_level;
        r->c_t = r->p_t;
    }
    rx_write(k, m, r->c_level, 0);
    r->active = 0;
    raise(k, I_RX_END(m));
    rx_status(k, m);
}

static void clocking(struct c3_rmt *k);

/* the inputs changed: each receiver's edge at the time the change came */
static void rx_watch(void *ctx)
{
    struct c3_rmt *k = (struct c3_rmt *)ctx;
    struct esk_mach *s = k->s;
    u64 lag = (u64)s->input_lead + s->output_lag, t = s->cycles > lag ? s->cycles - lag : 0;
    int m;
    for (m = 0; m < 2; m++) {
        struct rx *r = &k->rx[m];
        int level = esk_esp32c3_signal_in(s, SIG_IN0 + m);
        if (level == r->raw)
            continue;
        r->raw = level;
        if (!r->on || r->full)
            continue;
        if (!r->active) { /* the first edge starts it */
            r->active = 1;
            r->c_level = r->p_level = level;
            r->c_t = r->p_t = t;
            rx_status(k, m);
            continue;
        }
        if (r->p_level != r->c_level) {
            if (rx_drops(k, r, r->p_level, r->p_t, t)) { /* back as it was */
                r->p_level = r->c_level;
                continue;
            }
            rx_write(k, m, r->c_level, rx_ticks(k, r, r->p_t - r->c_t));
            if (!r->active)
                continue;
            r->c_level = r->p_level;
            r->c_t = r->p_t;
        }
        r->p_level = level;
        r->p_t = t;
    }
    clocking(k);
}

/* ---- time -------------------------------------------------------------------- */

static int tx_runs(const struct tx *t) { return t->busy || t->carrying; }

static void clocking(struct c3_rmt *k)
{
    int on = tx_runs(&k->tx[0]) || tx_runs(&k->tx[1]) || k->rx[0].active || k->rx[1].active;
    esk_rf_clock(k->p, &k->clocked, on);
}

/* fs until the channel's next edge, or 0 for none */
static u64 tx_due(const struct tx *t)
{
    u64 d = 0;
    if (t->busy)
        d = t->left;
    if (t->carrying && (!d || t->cleft < d))
        d = t->cleft;
    return d;
}

static void rmt_tick(void *ctx, u32 cycles)
{
    struct c3_rmt *k = (struct c3_rmt *)ctx;
    struct esk_mach *s = k->s;
    u64 fpc = fs_per_cycle(k), budget = (u64)cycles * fpc;
    int n, m;
    if (!fpc)
        return;
    for (;;) {
        u64 d = 0;
        for (n = 0; n < 2; n++) {
            u64 x = tx_due(&k->tx[n]);
            if (x && (!d || x < d))
                d = x;
        }
        if (!d || d > budget) {
            for (n = 0; n < 2; n++) {
                struct tx *t = &k->tx[n];
                if (t->busy)
                    t->left -= budget < t->left ? budget : t->left;
                if (t->carrying)
                    t->cleft -= budget < t->cleft ? budget : t->cleft;
            }
            break;
        }
        budget -= d;
        /* the edges switch at their time: this many cycles before now */
        s->output_lag = (u32)(budget / fpc);
        for (n = 0; n < 2; n++) {
            struct tx *t = &k->tx[n];
            int ended = t->busy && t->left == d;
            if (t->busy)
                t->left -= d;
            if (t->carrying) {
                t->cleft -= d;
                if (!t->cleft) {
                    double sc = sclk_fs(k);
                    t->cphase = !t->cphase;
                    t->cleft = fs_of((double)((t->cphase ? t->duty_l >> 16 : t->duty_l & 0xffffu) + 1u) * sc);
                    if (!t->cleft)
                        t->cleft = 1;
                    if (!ended)
                        tx_drive(k, n);
                }
            }
            if (ended)
                tx_next(k, n);
        }
        s->output_lag = 0;
    }
    for (m = 0; m < 2; m++) {
        struct rx *r = &k->rx[m];
        u64 last = r->p_t > r->c_t ? r->p_t : r->c_t;
        if (r->active && s->cycles >= last + rx_idle_cycles(k, r))
            rx_end(k, m);
    }
    clocking(k);
}

static int rmt_next(void *ctx, u32 *cycles)
{
    struct c3_rmt *k = (struct c3_rmt *)ctx;
    u64 fpc = fs_per_cycle(k), best = 0;
    int n, m, any = 0;
    if (!fpc)
        return 0;
    for (n = 0; n < 2; n++) {
        u64 d = tx_due(&k->tx[n]);
        if (d) {
            u64 c = (d + fpc - 1u) / fpc;
            if (!any || c < best)
                best = c;
            any = 1;
        }
    }
    for (m = 0; m < 2; m++) {
        struct rx *r = &k->rx[m];
        u64 last = r->p_t > r->c_t ? r->p_t : r->c_t, at;
        if (!r->active)
            continue;
        at = last + rx_idle_cycles(k, r);
        at = at > k->s->cycles ? at - k->s->cycles : 1u;
        if (!any || at < best)
            best = at;
        any = 1;
    }
    if (!any)
        return 0;
    *cycles = best == 0 ? 1u : best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

/* ---- registers --------------------------------------------------------------- */

static u32 rmt_read(void *ctx, u32 off, int n)
{
    struct c3_rmt *k = (struct c3_rmt *)ctx;
    u32 w = off & ~3u;
    if (w >= 0x08u && w <= 0x0Cu && !(reg(k->sys) & 1u)) { /* an RX channel's FIFO */
        struct rx *r = &k->rx[(w - 0x08u) / 4u];
        u32 i = (u32)BLOCK * ((w - 0x08u) / 4u + 2u) + r->apb, v = i < NWORDS ? reg(k->ram[i]) : 0;
        r->apb = (r->apb + 1u) % ((u32)BLOCK * rx_size(r));
        return v >> (8u * (off & 3u));
    }
    return esk_rf_read(k->p, off, n);
}

static void tx_conf_write(struct c3_rmt *k, int n, u32 v)
{
    struct tx *t = &k->tx[n];
    if (v & MEM_RD_RST)
        t->e = 0;
    if (v & APB_MEM_RST)
        t->apb = 0;
    if (v & CONF_UPDATE_TX) {
        t->conf_l = reg(t->conf) | (v & TX_STOP);
        t->duty_l = reg(t->duty);
        t->lim_l = reg(t->lim);
        k->sim_l = reg(k->sim);
        esk_rf_hw(t->conf, reg(t->conf) & ~TX_STOP); /* it clears itself */
        if (t->conf_l & TX_STOP) {
            t->conf_l &= ~TX_STOP;
            if (t->busy || t->pending)
                tx_idle(k, n, t->level);
        }
        if (!t->busy) { /* the idle output as now configured */
            tx_idle(k, n, t->level);
        }
    }
    if (v & TX_START) {
        if ((k->sim_l & 4u) && (k->sim_l >> n & 1u)) {
            int i, all = 1;
            t->pending = 1;
            for (i = 0; i < 2; i++)
                if ((k->sim_l >> i & 1u) && !k->tx[i].pending && !k->tx[i].busy)
                    all = 0;
            if (all)
                for (i = 0; i < 2; i++)
                    if (k->tx[i].pending)
                        tx_start(k, i);
        } else {
            tx_start(k, n);
        }
    }
    tx_status(k, n);
}

static void rx_conf_write(struct c3_rmt *k, int m, u32 v)
{
    struct rx *r = &k->rx[m];
    if (v & MEM_WR_RST) {
        r->wr = 0;
        r->words = 0;
        r->full = 0;
    }
    if (v & APB_MEM_RST)
        r->apb = 0;
    if (v & CONF_UPDATE_RX) {
        int was = r->on;
        r->conf0_l = reg(r->conf0);
        r->conf1_l = reg(r->conf1);
        r->rm_l = reg(r->rm);
        r->lim_l = reg(r->lim);
        r->on = (r->conf1_l & RX_EN) != 0;
        if (r->on && !was) {
            r->active = 0;
            r->full = 0;
            r->raw = esk_esp32c3_signal_in(k->s, SIG_IN0 + m);
        } else if (!r->on) {
            r->active = 0;
        }
    }
    rx_status(k, m);
}

static void rmt_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_rmt *k = (struct c3_rmt *)ctx;
    u32 w = off & ~3u;
    int i;
    if (w <= 0x04u && !(reg(k->sys) & 1u)) { /* a TX channel's FIFO */
        struct tx *t = &k->tx[w / 4u];
        u32 at = (u32)BLOCK * (w / 4u) + t->apb;
        if (at < NWORDS)
            esk_rf_hw(k->ram[at], v);
        t->apb = (t->apb + 1u) % ((u32)BLOCK * tx_size(t));
        tx_status(k, (int)(w / 4u));
        return;
    }
    if (w <= 0x0Cu)
        return; /* the FIFO registers, in direct mode */
    if (w == 0x44u) { /* INT_CLR */
        esk_rf_hw(k->raw, reg(k->raw) & ~v);
        update(k);
        return;
    }
    if (w == 0x3Cu || (w >= 0x28u && w <= 0x34u))
        return; /* INT_ST and the status registers are read-only */
    esk_rf_write(k->p, off, n, v);
    if (w == 0x38u || w == 0x40u) /* INT_RAW (as the SVD has it, writable), INT_ENA */
        update(k);
    for (i = 0; i < 2; i++) {
        if (w == esk_rf_off(k->tx[i].conf))
            tx_conf_write(k, i, v);
        if (w == esk_rf_off(k->tx[i].lim) && (v & (1u << 20)))
            k->tx[i].loops = 0; /* LOOP_COUNT_RESET */
        if (w == esk_rf_off(k->rx[i].conf1))
            rx_conf_write(k, i, v);
    }
    clocking(k);
}

static void rmt_reset(void *ctx)
{
    struct c3_rmt *k = (struct c3_rmt *)ctx;
    int i;
    for (i = 0; i < 2; i++) {
        struct tx *t = &k->tx[i];
        struct rx *r = &k->rx[i];
        struct tx keep_t = *t;
        struct rx keep_r = *r;
        memset(t, 0, sizeof *t);
        t->conf = keep_t.conf;
        t->duty = keep_t.duty;
        t->lim = keep_t.lim;
        t->status = keep_t.status;
        t->data = keep_t.data;
        t->conf_l = reg(t->conf);
        t->duty_l = reg(t->duty);
        t->lim_l = reg(t->lim);
        t->out = -1;
        memset(r, 0, sizeof *r);
        r->conf0 = keep_r.conf0;
        r->conf1 = keep_r.conf1;
        r->rm = keep_r.rm;
        r->lim = keep_r.lim;
        r->status = keep_r.status;
        r->data = keep_r.data;
        r->conf0_l = reg(r->conf0);
        r->conf1_l = reg(r->conf1);
        r->rm_l = reg(r->rm);
        r->lim_l = reg(r->lim);
    }
    k->sim_l = reg(k->sim);
    k->level = 0;
    esk_esp32c3_source(k->s, SOURCE, 0);
    k->clocked = 0;
    for (i = 0; i < 2; i++) {
        tx_idle(k, i, 0);
        rx_status(k, i);
        k->rx[i].raw = esk_esp32c3_signal_in(k->s, SIG_IN0 + i);
    }
    update(k);
    clocking(k);
}

const struct esk_dev_ops esk_esp32c3_rmt_ops = {
    "esp32c3-rmt", rmt_read, rmt_write, rmt_reset, rmt_tick, rmt_next,
};

void *esk_esp32c3_rmt_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_rmt *k = (struct c3_rmt *)esk_mach_alloc(s, sizeof *k);
    static const char *const tx_names[2][5] = {
        { "CH0_TX_CONF0", "CH0CARRIER_DUTY", "CH0_TX_LIM", "CH0_TX_STATUS", "CH0DATA" },
        { "CH1_TX_CONF0", "CH1CARRIER_DUTY", "CH1_TX_LIM", "CH1_TX_STATUS", "CH1DATA" },
    };
    static const char *const rx_names[2][6] = {
        { "CH2_RX_CONF0", "CH2_RX_CONF1", "CH2_RX_CARRIER_RM", "CH2_RX_LIM", "CH2_RX_STATUS", "CH2DATA" },
        { "CH3_RX_CONF0", "CH3_RX_CONF1", "CH3_RX_CARRIER_RM", "CH3_RX_LIM", "CH3_RX_STATUS", "CH3DATA" },
    };
    char name[16];
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->sys = esk_rf_reg(p, "SYS_CONF");
    k->sim = esk_rf_reg(p, "TX_SIM");
    k->raw = esk_rf_reg(p, "INT_RAW");
    k->st = esk_rf_reg(p, "INT_ST");
    k->ena = esk_rf_reg(p, "INT_ENA");
    if (!k->sys || !k->sim || !k->raw || !k->st || !k->ena)
        return 0;
    for (i = 0; i < 2; i++) {
        struct tx *t = &k->tx[i];
        struct rx *r = &k->rx[i];
        t->conf = esk_rf_reg(p, tx_names[i][0]);
        t->duty = esk_rf_reg(p, tx_names[i][1]);
        t->lim = esk_rf_reg(p, tx_names[i][2]);
        t->status = esk_rf_reg(p, tx_names[i][3]);
        t->data = esk_rf_reg(p, tx_names[i][4]);
        r->conf0 = esk_rf_reg(p, rx_names[i][0]);
        r->conf1 = esk_rf_reg(p, rx_names[i][1]);
        r->rm = esk_rf_reg(p, rx_names[i][2]);
        r->lim = esk_rf_reg(p, rx_names[i][3]);
        r->status = esk_rf_reg(p, rx_names[i][4]);
        r->data = esk_rf_reg(p, rx_names[i][5]);
        if (!t->conf || !t->duty || !t->lim || !t->status || !t->data || !r->conf0 || !r->conf1 || !r->rm ||
            !r->lim || !r->status || !r->data)
            return 0;
    }
    for (i = 0; i < NWORDS; i++) {
        snprintf(name, sizeof name, "RAM%d", i);
        if (!(k->ram[i] = esk_rf_reg(p, name)))
            return 0;
    }
    if (esk_esp32c3_signal_watch(s, rx_watch, k) != 0)
        return 0;
    return k;
}
