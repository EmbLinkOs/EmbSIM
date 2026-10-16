/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* i2c.c -- the RP2040's I2C (Synopsys' DW_apb_i2c, its datasheet, 4.3) as a
 * master, over the register file, on its pins: SCL and SDA open-drain (the
 * pad's output enable pulls the line low), the pull-ups, the pads' or the
 * circuit's, bringing them high.
 *
 *   - IC_DATA_CMD's writes are commands into a 16-entry transmit FIFO: a
 *     byte to write (CMD 0, DAT) or one to read (CMD 1), with RESTART and
 *     STOP; its reads pop the 16-entry receive FIFO (RX_UNDER when it is
 *     empty). While IC_ENABLE.ENABLE is set and MASTER_MODE is, the master
 *     works through the commands: a START and IC_TAR's 7-bit address with
 *     the first command's direction; a repeated START and the address again
 *     when a command asks for RESTART or turns the direction; each byte
 *     after it, MSB first, with its ACK bit; a STOP after a command with
 *     STOP;
 *   - with the transmit FIFO empty and no STOP asked for, the master holds
 *     SCL low and waits for the next command (EMPTYFIFO_HOLD_MASTER); a
 *     byte read goes to the receive FIFO as its eighth bit is in, and its
 *     ACK waits for the next command too: ACKed when that is a read that
 *     goes on, NACKed before a STOP, a RESTART or a write;
 *   - a NACK of the address or of a written byte aborts: IC_TX_ABRT_SOURCE
 *     says which (ABRT_7B_ADDR_NOACK, ABRT_TXDATA_NOACK) and how many
 *     commands were flushed, TX_ABRT is raised, both FIFOs are flushed and
 *     a STOP made; until IC_CLR_TX_ABRT (or IC_CLR_INTR) is read the
 *     transmit FIFO takes no commands. IC_ENABLE.ABORT aborts the same way
 *     (ABRT_USER_ABRT), after the byte under way;
 *   - IC_RAW_INTR_STAT: TX_EMPTY while the transmit FIFO holds TX_TL or
 *     fewer (and, with IC_CON.TX_EMPTY_CTRL, the command under way is
 *     done), RX_FULL while the receive FIFO holds more than RX_TL, and the
 *     latched RX_UNDER, RX_OVER, TX_OVER, TX_ABRT, ACTIVITY, STOP_DET and
 *     START_DET, each cleared by reading its IC_CLR_ register; IC_INTR_STAT
 *     is that & IC_INTR_MASK, and raises I2Cn_IRQ; IC_STATUS, IC_TXFLR,
 *     IC_RXFLR and IC_ENABLE_STATUS follow;
 *   - SCL is low for LCNT + 1 and high for HCNT + IC_FS_SPKLEN + 7 clk_sys
 *     cycles (the SS_ counts at standard speed, the FS_ ones above it); SDA
 *     changes IC_SDA_HOLD.IC_SDA_TX_HOLD cycles after SCL falls and is read
 *     as SCL falls;
 *   - IC_CON, IC_TAR and the SCL counts take writes only while the block
 *     is disabled. Disabling it while it holds the bus between commands
 *     lets the bus go (a STOP, after a NACK when a read waits for its ACK)
 *     only if it is still disabled one SCL low time later: the SDK disables
 *     it to change IC_TAR between a write without STOP and a read, and the
 *     read then starts with a repeated START;
 *   - the pins are those whose FUNCSEL is I2C (3) for this unit (io.c);
 *   - a step is a device event, at most one in an instruction.
 *
 * Not modelled: slave mode, 10-bit addresses, general call and START
 * bytes, IC_RESTART_EN 0 (a repeated START is made anyway), clock
 * stretching by the slave, arbitration, the spike filter, RX_FIFO_FULL
 * hold, DMA requests. */
#include <string.h>

#include "mcu/rp2040/rp2040.h"

#define CON_MASTER (1u << 0)
#define CON_TX_EMPTY_CTRL (1u << 8)
#define CMD_READ (1u << 8)
#define CMD_STOP (1u << 9)
#define CMD_RESTART (1u << 10)
#define EN_ENABLE (1u << 0)
#define EN_ABORT (1u << 1)
#define R_RX_UNDER (1u << 0)
#define R_RX_OVER (1u << 1)
#define R_RX_FULL (1u << 2)
#define R_TX_OVER (1u << 3)
#define R_TX_EMPTY (1u << 4)
#define R_TX_ABRT (1u << 6)
#define R_ACTIVITY (1u << 8)
#define R_STOP_DET (1u << 9)
#define R_START_DET (1u << 10)
#define ABRT_7B_ADDR_NOACK (1u << 0)
#define ABRT_TXDATA_NOACK (1u << 3)
#define ABRT_USER_ABRT (1u << 16)
#define DEPTH 16

enum {
    M_IDLE,    /* the bus is free */
    M_START,   /* making a START or a repeated START */
    M_BYTE,    /* the address, or a command's byte */
    M_ACKWAIT, /* a read byte's ACK waits for the next command, SCL low */
    M_HOLD,    /* between commands, SCL held low */
    M_STOP,    /* making a STOP */
};

struct rpi2c {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *con, *tar, *data_cmd, *ss_hcnt, *ss_lcnt, *fs_hcnt, *fs_lcnt, *intr_stat, *intr_mask,
        *raw, *rx_tl, *tx_tl, *enable, *status, *txflr, *rxflr, *sda_hold, *abrt, *en_status, *spklen;
    u32 o_clr_intr, o_clr_rx_under, o_clr_rx_over, o_clr_tx_over, o_clr_tx_abrt, o_clr_activity,
        o_clr_stop_det, o_clr_start_det;
    int unit, irq, running;
    u16 tx[DEPTH];
    u8 rx[DEPTH];
    int ntx, nrx;
    int mode, step;
    int scl, sda;     /* 0: pulled low, 1: released */
    u32 shift;
    int bit;
    int addr;         /* the byte under way is the address */
    int reading;      /* the address said read */
    int active;       /* between a START and its STOP */
    u16 cur;          /* the command under way */
    int cmd_busy;     /* ... and it is not done */
    int stop_after;   /* a STOP follows it */
    int ack;          /* the master's ACK of a read byte */
    int acked;        /* the ACK bit just clocked */
    int user_abort;   /* IC_ENABLE.ABORT, at the end of the byte under way */
    u32 lat;          /* the latched raw interrupts */
    u32 abrt_src;
    u64 left, since;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static int enabled(const struct rpi2c *k) { return (reg(k->enable) & EN_ENABLE) != 0; }

static const struct rpi2c *i2c_unit(struct esk_mach *s, int unit)
{
    char name[8] = "I2Cx";
    struct esk_rf_periph *p;
    name[3] = (char)('0' + unit);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_rp2040_i2c_ops ? (const struct rpi2c *)p->ctx : 0;
}

int esk_rp2040_i2c_out(struct esk_mach *s, int unit, int signal, int *level)
{
    const struct rpi2c *k = i2c_unit(s, unit);
    if (!k || esk_rf_gated(k->p))
        return 0;
    *level = signal == RP_I2C_SCL ? k->scl : k->sda;
    return 1;
}

static void lines(struct rpi2c *k, int scl, int sda)
{
    if (k->scl == scl && k->sda == sda)
        return;
    k->scl = scl;
    k->sda = sda;
    esk_rp2040_pins_changed(k->s);
}

static int sda_in(struct rpi2c *k)
{
    int level = 1;
    if (esk_rp2040_func_in(k->s, RP_FUNC_I2C, k->unit, RP_I2C_SDA, &level))
        return level;
    return k->sda;
}

static int tx_empty(const struct rpi2c *k)
{
    return (u32)k->ntx <= (reg(k->tx_tl) & 0xffu) && (!(reg(k->con) & CON_TX_EMPTY_CTRL) || !k->cmd_busy);
}

static void update(struct rpi2c *k)
{
    u32 raw = k->lat, was = reg(k->intr_stat), st, sr = 0;
    if ((u32)k->nrx > (reg(k->rx_tl) & 0xffu))
        raw |= R_RX_FULL;
    if (tx_empty(k))
        raw |= R_TX_EMPTY;
    esk_rf_hw(k->raw, raw);
    st = raw & reg(k->intr_mask);
    esk_rf_hw(k->intr_stat, st);
    if (k->mode != M_IDLE || k->ntx)
        sr |= 1u << 0; /* ACTIVITY */
    if (k->ntx < DEPTH)
        sr |= 1u << 1; /* TFNF */
    if (!k->ntx)
        sr |= 1u << 2; /* TFE */
    if (k->nrx)
        sr |= 1u << 3; /* RFNE */
    if (k->nrx == DEPTH)
        sr |= 1u << 4; /* RFF */
    if (k->mode != M_IDLE)
        sr |= 1u << 5; /* MST_ACTIVITY */
    esk_rf_hw(k->status, sr);
    esk_rf_hw(k->txflr, (u32)k->ntx);
    esk_rf_hw(k->rxflr, (u32)k->nrx);
    esk_rf_hw(k->en_status, enabled(k) || k->mode != M_IDLE ? 1u : 0u);
    esk_rf_hw(k->abrt, k->abrt_src);
    if (st & ~was)
        esk_rf_interrupt(k->p, k->irq);
}

/* SCL's low (0) or high (1) time in core cycles: clk_sys's */
static u64 period(const struct rpi2c *k, int high)
{
    int fast = (reg(k->con) >> 1 & 3u) >= 2;
    u32 h = reg(fast ? k->fs_hcnt : k->ss_hcnt) & 0xffffu, l = reg(fast ? k->fs_lcnt : k->ss_lcnt) & 0xffffu;
    return high ? (u64)h + (reg(k->spklen) & 0xffu) + 7u : (u64)l + 1u;
}

static u64 hold_time(const struct rpi2c *k)
{
    u64 h = reg(k->sda_hold) & 0xffffu, low = period(k, 0);
    if (h >= low)
        h = low - 1;
    return h ? h : 1;
}

static void wait_n(struct rpi2c *k, u64 n)
{
    k->left = n ? n : 1;
    k->since = k->s->cycles;
    esk_rf_clock(k->p, &k->running, 1);
}

static void idle_clock(struct rpi2c *k) { esk_rf_clock(k->p, &k->running, 0); }

static void begin_start(struct rpi2c *k)
{
    k->mode = M_START;
    k->step = 0;
    wait_n(k, period(k, 0));
}

static void begin_stop(struct rpi2c *k)
{
    k->mode = M_STOP;
    k->step = 0;
    wait_n(k, hold_time(k));
}

static void begin_byte(struct rpi2c *k, int addr, u32 byte)
{
    k->mode = M_BYTE;
    k->addr = addr;
    k->shift = byte;
    k->bit = 0;
    k->step = 0;
    wait_n(k, hold_time(k));
}

static u16 pop(struct rpi2c *k)
{
    u16 c = k->tx[0];
    k->ntx--;
    memmove(k->tx, k->tx + 1, (size_t)k->ntx * sizeof k->tx[0]);
    return c;
}

static void abort_with(struct rpi2c *k, u32 why)
{
    k->abrt_src = why | (u32)k->ntx << 23;
    k->lat |= R_TX_ABRT;
    k->ntx = k->nrx = 0;
    k->cmd_busy = 0;
    k->stop_after = 0;
    update(k);
    begin_stop(k);
}

/* SCL low between commands, or the bus free: what comes next */
static void decide(struct rpi2c *k)
{
    if (k->mode == M_IDLE) {
        if (!enabled(k) || !(reg(k->con) & CON_MASTER) || !k->ntx || (k->lat & R_TX_ABRT)) {
            idle_clock(k);
            update(k);
            return;
        }
        k->cur = pop(k);
        k->cmd_busy = 1;
        begin_start(k);
        update(k);
        return;
    }
    if (k->stop_after || !enabled(k)) {
        k->stop_after = 0;
        begin_stop(k);
        return;
    }
    if (!k->ntx) {
        k->mode = M_HOLD;
        idle_clock(k);
        update(k);
        return;
    }
    k->cur = pop(k);
    k->cmd_busy = 1;
    if ((k->cur & CMD_RESTART) || ((k->cur & CMD_READ) != 0) != k->reading)
        begin_start(k);
    else
        begin_byte(k, 0, k->cur & 0xffu);
    update(k);
}

/* a read byte's ACK, by what follows it */
static int read_ack(const struct rpi2c *k)
{
    if ((k->cur & CMD_STOP) || !enabled(k) || !k->ntx)
        return 0;
    return (k->tx[0] & (CMD_READ | CMD_RESTART)) == CMD_READ;
}

static void byte_done(struct rpi2c *k)
{
    if (k->user_abort) {
        k->user_abort = 0;
        abort_with(k, ABRT_USER_ABRT);
        return;
    }
    if (k->addr) {
        if (!k->acked) {
            abort_with(k, ABRT_7B_ADDR_NOACK);
            return;
        }
        begin_byte(k, 0, k->reading ? 0xffu : (k->cur & 0xffu));
        return;
    }
    if (!k->reading && !k->acked) {
        abort_with(k, ABRT_TXDATA_NOACK);
        return;
    }
    k->cmd_busy = 0;
    k->stop_after = (k->cur & CMD_STOP) != 0;
    k->mode = M_HOLD;
    update(k);
    decide(k);
}

/* the eighth bit of a read byte is in */
static void read_in(struct rpi2c *k)
{
    if (k->nrx < DEPTH)
        k->rx[k->nrx++] = (u8)k->shift;
    else
        k->lat |= R_RX_OVER;
    update(k);
    if (!(k->cur & CMD_STOP) && enabled(k) && !k->ntx) {
        k->mode = M_ACKWAIT;
        idle_clock(k);
        return;
    }
    k->ack = read_ack(k);
}

static void step(struct rpi2c *k)
{
    switch (k->mode) {
    case M_START:
        /* from a free bus, or a held one: SDA released, SCL released, then
         * SDA falls, then SCL falls */
        if (k->step == 0) {
            lines(k, k->scl, 1);
            k->step = 1;
            wait_n(k, period(k, 0) - hold_time(k));
        } else if (k->step == 1) {
            lines(k, 1, 1);
            k->step = 2;
            wait_n(k, period(k, 1));
        } else if (k->step == 2) {
            lines(k, 1, 0);
            k->active = 1;
            k->lat |= R_START_DET | R_ACTIVITY;
            update(k);
            k->step = 3;
            wait_n(k, period(k, 1));
        } else {
            lines(k, 0, 0);
            k->reading = (k->cur & CMD_READ) != 0;
            begin_byte(k, 1, (reg(k->tar) & 0x7fu) << 1 | (u32)k->reading);
        }
        return;
    case M_STOP:
        if (k->step == 0) {
            lines(k, 0, 0);
            k->step = 1;
            wait_n(k, period(k, 0) - hold_time(k));
        } else if (k->step == 1) {
            lines(k, 1, 0);
            k->step = 2;
            wait_n(k, period(k, 1));
        } else {
            lines(k, 1, 1);
            k->active = 0;
            k->mode = M_IDLE;
            k->lat |= R_STOP_DET;
            if (k->user_abort) {
                k->user_abort = 0;
                k->abrt_src = ABRT_USER_ABRT;
                k->lat |= R_TX_ABRT;
            }
            if (reg(k->enable) & EN_ABORT)
                esk_rf_hw(k->enable, reg(k->enable) & ~(u64)EN_ABORT);
            if (!enabled(k))
                k->ntx = k->nrx = 0;
            update(k);
            decide(k);
        }
        return;
    case M_BYTE: {
        /* each bit: SDA set the hold time after SCL fell (step 0), SCL
         * high (1), SDA read and SCL low (2), the next bit (3); bit 8 is
         * the ACK */
        int rx = k->reading && !k->addr;
        if (k->step == 0) {
            int sda;
            if (k->bit < 8)
                sda = rx ? 1 : (int)(k->shift >> (7 - k->bit) & 1u);
            else
                sda = rx ? !k->ack : 1;
            lines(k, 0, sda);
            k->step = 1;
            wait_n(k, period(k, 0) - hold_time(k));
            return;
        }
        if (k->step == 1) {
            lines(k, 1, k->sda);
            k->step = 2;
            wait_n(k, period(k, 1));
            return;
        }
        if (k->step == 2) {
            int v = sda_in(k);
            if (k->bit < 8 && rx)
                k->shift = (k->shift << 1 | (u32)v) & 0xffu;
            if (k->bit == 8)
                k->acked = rx ? 1 : v == 0;
            lines(k, 0, k->sda);
            k->step = 3;
            wait_n(k, hold_time(k));
            return;
        }
        if (k->bit == 8) {
            lines(k, 0, 1);
            k->step = 0;
            byte_done(k);
            return;
        }
        k->bit++;
        k->step = 0;
        if (k->bit == 8 && rx) {
            read_in(k);
            if (k->mode == M_ACKWAIT)
                return;
        }
        step(k);
        return;
    }
    case M_HOLD:
    case M_ACKWAIT:
        /* the time a disable waits for: let the bus go if it holds */
        if (!enabled(k)) {
            if (k->mode == M_ACKWAIT) {
                k->mode = M_BYTE;
                k->ack = 0;
                k->step = 0;
                step(k);
                return;
            }
            k->stop_after = 0;
            begin_stop(k);
            return;
        }
        idle_clock(k);
        return;
    default:
        idle_clock(k);
        return;
    }
}

/* a command came: the master goes on if it waits */
static void kick(struct rpi2c *k)
{
    if (k->mode == M_IDLE || k->mode == M_HOLD) {
        decide(k);
    } else if (k->mode == M_ACKWAIT && k->ntx) {
        k->ack = read_ack(k);
        k->mode = M_BYTE;
        k->step = 0;
        step(k);
    }
}

static void i2c_tick(void *ctx, u32 cycles)
{
    struct rpi2c *k = (struct rpi2c *)ctx;
    if (!k->running)
        return;
    cycles = esk_mach_after(k->s, cycles, k->since);
    k->since = 0;
    if (k->left > cycles) {
        k->left -= cycles;
        return;
    }
    {
        u64 over = cycles - k->left;
        k->left = 0;
        step(k);
        if (k->running && k->left) {
            u64 cut = over < k->left ? over : k->left - 1;
            k->left -= cut;
            k->since = 0;
        }
    }
}

static int i2c_next(void *ctx, u32 *cycles)
{
    struct rpi2c *k = (struct rpi2c *)ctx;
    if (!k->running)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)(k->left ? k->left : 1);
    return 1;
}

static u32 i2c_read(void *ctx, u32 off, int n)
{
    struct rpi2c *k = (struct rpi2c *)ctx;
    u32 clr = 0;
    if (off == esk_rf_off(k->data_cmd)) {
        u32 v = k->nrx ? k->rx[0] : 0;
        if (k->s->bus.debug)
            return v;
        if (k->nrx) {
            k->nrx--;
            memmove(k->rx, k->rx + 1, (size_t)k->nrx * sizeof k->rx[0]);
        } else {
            k->lat |= R_RX_UNDER;
        }
        update(k);
        return v;
    }
    if (k->s->bus.debug)
        return esk_rf_read(k->p, off, n);
    if (off == k->o_clr_intr)
        clr = R_RX_UNDER | R_RX_OVER | R_TX_OVER | R_TX_ABRT | R_ACTIVITY | R_STOP_DET | R_START_DET;
    else if (off == k->o_clr_rx_under)
        clr = R_RX_UNDER;
    else if (off == k->o_clr_rx_over)
        clr = R_RX_OVER;
    else if (off == k->o_clr_tx_over)
        clr = R_TX_OVER;
    else if (off == k->o_clr_tx_abrt)
        clr = R_TX_ABRT;
    else if (off == k->o_clr_activity)
        clr = R_ACTIVITY;
    else if (off == k->o_clr_stop_det)
        clr = R_STOP_DET;
    else if (off == k->o_clr_start_det)
        clr = R_START_DET;
    if (clr) {
        u32 had = k->lat & clr;
        k->lat &= ~clr;
        if ((clr & R_ACTIVITY) && k->active)
            k->lat |= R_ACTIVITY; /* still active */
        if (clr & R_TX_ABRT)
            k->abrt_src = 0;
        update(k);
        return had ? 1u : 0u;
    }
    return esk_rf_read(k->p, off, n);
}

static void i2c_write(void *ctx, u32 off, int n, u32 v)
{
    struct rpi2c *k = (struct rpi2c *)ctx;
    if (off == esk_rf_off(k->data_cmd)) {
        if (!enabled(k) || (k->lat & R_TX_ABRT))
            return; /* held flushed */
        if (k->ntx == DEPTH) {
            k->lat |= R_TX_OVER;
            update(k);
            return;
        }
        k->tx[k->ntx++] = (u16)(v & 0x7ffu);
        update(k);
        kick(k);
        return;
    }
    if (enabled(k) && (off == esk_rf_off(k->con) || off == esk_rf_off(k->tar) || off == esk_rf_off(k->ss_hcnt) ||
                       off == esk_rf_off(k->ss_lcnt) || off == esk_rf_off(k->fs_hcnt) ||
                       off == esk_rf_off(k->fs_lcnt)))
        return; /* only while disabled */
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->enable)) {
        if (!enabled(k)) {
            k->ntx = 0;
            if (k->mode == M_IDLE)
                k->nrx = 0;
            else if (k->mode == M_HOLD || k->mode == M_ACKWAIT)
                wait_n(k, period(k, 0)); /* let go if still disabled then */
        } else if (k->mode == M_HOLD || k->mode == M_ACKWAIT) {
            idle_clock(k); /* enabled again in time: the bus stays held */
        }
        if ((reg(k->enable) & EN_ABORT) && enabled(k)) {
            if (k->mode == M_IDLE)
                esk_rf_hw(k->enable, reg(k->enable) & ~(u64)EN_ABORT);
            else if (k->mode == M_HOLD || k->mode == M_ACKWAIT)
                abort_with(k, ABRT_USER_ABRT);
            else
                k->user_abort = 1; /* after the byte under way */
        }
        update(k);
        if (enabled(k))
            kick(k);
        return;
    }
    update(k);
}

static void i2c_reset(void *ctx)
{
    struct rpi2c *k = (struct rpi2c *)ctx;
    k->running = 0;
    k->mode = M_IDLE;
    k->scl = k->sda = 1;
    k->ntx = k->nrx = 0;
    k->active = k->cmd_busy = k->stop_after = k->user_abort = 0;
    k->lat = 0;
    k->abrt_src = 0;
    k->left = 0;
    update(k);
}

const struct esk_dev_ops esk_rp2040_i2c_ops = {
    "rp2040-i2c", i2c_read, i2c_write, i2c_reset, i2c_tick, i2c_next,
};

void *esk_rp2040_i2c_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct rpi2c k, *c;
    struct esk_rf_reg *r;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
#define REG(f, name)                                                                                           \
    k.f = esk_rf_reg(p, name);                                                                                 \
    if (!k.f)                                                                                                  \
        return 0;
    REG(con, "IC_CON")
    REG(tar, "IC_TAR")
    REG(data_cmd, "IC_DATA_CMD")
    REG(ss_hcnt, "IC_SS_SCL_HCNT")
    REG(ss_lcnt, "IC_SS_SCL_LCNT")
    REG(fs_hcnt, "IC_FS_SCL_HCNT")
    REG(fs_lcnt, "IC_FS_SCL_LCNT")
    REG(intr_stat, "IC_INTR_STAT")
    REG(intr_mask, "IC_INTR_MASK")
    REG(raw, "IC_RAW_INTR_STAT")
    REG(rx_tl, "IC_RX_TL")
    REG(tx_tl, "IC_TX_TL")
    REG(enable, "IC_ENABLE")
    REG(status, "IC_STATUS")
    REG(txflr, "IC_TXFLR")
    REG(rxflr, "IC_RXFLR")
    REG(sda_hold, "IC_SDA_HOLD")
    REG(abrt, "IC_TX_ABRT_SOURCE")
    REG(en_status, "IC_ENABLE_STATUS")
    REG(spklen, "IC_FS_SPKLEN")
#undef REG
#define CLR(f, name)                                                                                           \
    r = esk_rf_reg(p, name);                                                                                   \
    if (!r)                                                                                                    \
        return 0;                                                                                              \
    k.f = esk_rf_off(r);
    CLR(o_clr_intr, "IC_CLR_INTR")
    CLR(o_clr_rx_under, "IC_CLR_RX_UNDER")
    CLR(o_clr_rx_over, "IC_CLR_RX_OVER")
    CLR(o_clr_tx_over, "IC_CLR_TX_OVER")
    CLR(o_clr_tx_abrt, "IC_CLR_TX_ABRT")
    CLR(o_clr_activity, "IC_CLR_ACTIVITY")
    CLR(o_clr_stop_det, "IC_CLR_STOP_DET")
    CLR(o_clr_start_det, "IC_CLR_START_DET")
#undef CLR
    if (strncmp(p->name, "I2C", 3) || p->name[3] < '0' || p->name[3] > '1')
        return 0;
    k.unit = p->name[3] - '0';
    k.irq = esk_rf_irq(p, "I2C");
    k.scl = k.sda = 1;
    c = (struct rpi2c *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    return c;
}
