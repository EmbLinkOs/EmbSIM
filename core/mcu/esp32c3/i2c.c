/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* i2c.c -- the ESP32-C3's I2C controller (I2C0, TRM v1.4 chapter 28) on the
 * GPIO matrix's I2CEXT0_SCL and I2CEXT0_SDA (signals 53 and 54), master or
 * slave (CTR's MS_MODE), its interrupt on matrix source 29.
 *
 *   - I2C_SCLK is XTAL_CLK (CLK_CONF's SCLK_SEL 0) or RC_FAST_CLK (1)
 *     divided by SCLK_DIV_NUM + 1 + DIV_A / DIV_B, while SCLK_ACTIVE is set.
 *   - The settings table 28.4-1 lists take effect when CONF_UPGATE is
 *     written: CTR's modes, TO, SLAVE_ADDR, SCL_SP_CONF, SCL_STRETCH_CONF
 *     and the timing registers. FSM_RST returns the state machines to idle
 *     and lets the lines go.
 *   - A master runs COMD0-7 from TRANS_START: each command's byte_num
 *     (bits 7:0), ack_check_en (8), ack_exp (9), ack_value (10) and
 *     op_code (13:11: 6 RSTART, 1 WRITE, 3 READ, 2 STOP, 4 END), its
 *     COMMAND_DONE (bit 31) set when it is done. RSTART makes a START (once
 *     SCL is high) or a repeated START (TRANS_START); WRITE sends byte_num
 *     bytes from the TX RAM, the address among them, each with its ACK
 *     checked against ack_exp when ack_check_en is set (NACK, then a STOP
 *     and the commands stop); READ takes byte_num bytes into the RX RAM,
 *     answering ack_value; STOP ends the transfer (TRANS_COMPLETE) and the
 *     commands; END holds SCL low and stops (END_DETECT) until the next
 *     TRANS_START runs the commands from COMD0 again.
 *   - The bus's timing in I2C_SCLK cycles: SCL low SCL_LOW_PERIOD + 1;
 *     released, high from SCL_WAIT_HIGH_PERIOD + 1 later for
 *     SCL_HIGH_PERIOD + 1 (a slave holding SCL low stretches it: the high
 *     time counts from its rise); SDA changes SDA_HOLD + 1 after SCL falls
 *     and is read SDA_SAMPLE + 1 after SCL is released; a START holds SDA
 *     low SCL_START_HOLD + 1 before SCL falls, a repeated START waits
 *     SCL_RSTART_SETUP + 1 with SCL high, a STOP SCL_STOP_SETUP + 1 before
 *     SDA rises and SCL_STOP_HOLD + 1 after it.
 *   - The lines are open-drain: driven low or let go, with SCL_FORCE_OUT
 *     and SDA_FORCE_OUT the output enabled throughout (the pad's
 *     PAD_DRIVER then makes it open-drain); SCL_PD_EN and SDA_PD_EN pull a
 *     forced line low. With ARBITRATION_EN, a master letting SDA go that
 *     reads it low loses the bus (ARBITRATION_LOST, SR's ARB_LOST) and lets
 *     both lines go. With TIME_OUT_EN, SCL held low by someone else for
 *     more than 2^TIME_OUT_VALUE cycles ends the transfer (TIME_OUT).
 *   - SCL_RST_SLV_EN sends SCL_RST_SLV_NUM clock pulses with SDA let go
 *     (to free a stuck slave), then clears itself.
 *   - The TX and RX RAMs (32 bytes, a word each at +0x100 and +0x180) are
 *     FIFOs through DATA (TXFIFO_OVF writing a full one, RXFIFO_UDF reading
 *     an empty one, RXFIFO_OVF receiving into a full one, MST_TXFIFO_UDF
 *     sending from an empty one), or read and written directly with
 *     NONFIFO_EN; TX_FIFO_RST and RX_FIFO_RST empty them; with FIFO_PRT_EN,
 *     TXFIFO_WM while the TX FIFO holds fewer than TXFIFO_WM_THRHD bytes and
 *     RXFIFO_WM while the RX FIFO holds more than RXFIFO_WM_THRHD.
 *   - A slave answers SLAVE_ADDR (7 bits, or 10 with ADDR_10BIT_EN: the
 *     first byte 0x78 | its two high bits, then the low eight), and the
 *     general call with ADDR_BROADCASTING_EN (GENERAL_CALL): it ACKs, puts
 *     the bytes written into the RX RAM (with FIFO_ADDR_CFG_EN the first is
 *     the RAM address the bytes go to and come from), sends the TX RAM's
 *     bytes when read (a NACK from the master ends them: NACK), ACKs a full
 *     RX RAM with RX_FULL_ACK_LEVEL and, with SLAVE_BYTE_ACK_CTL_EN, each
 *     byte with SLAVE_BYTE_ACK_LVL. With SLAVE_SCL_STRETCH_EN it holds SCL
 *     low (SLAVE_STRETCH, SR's STRETCH_CAUSE: 0 addressed to send, 1 TX RAM
 *     empty, 2 RX RAM full, 3 ACK control) until SLAVE_SCL_STRETCH_CLR.
 *     SR's SLAVE_ADDRESSED and SLAVE_RW say how it was addressed.
 *   - Both see the bus: DET_START at a START, BUS_BUSY from a START to a
 *     STOP, TRANS_COMPLETE at a STOP; BYTE_TRANS_DONE after each byte;
 *     TRANS_START when a master makes a START; RESP_REC is the last ACK.
 *   - INT_STATUS is INT_RAW and INT_ENA; INT_CLR clears.
 * Where it differs inside: the filters (FILTER_CFG) are not applied (the
 * nets' levels are clean); the state machines' own timeouts (SCL_ST_TO,
 * SCL_MAIN_ST_TO) are not raised; SR's state fields read 0; a slave reacts
 * at the edge it sees, not at its SDA_SAMPLE point. Not yet: a slave's
 * SLV_TX_AUTO_START_EN 0 (it always answers), 10-bit addressing's R/W
 * check, the general call's second byte. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define SOURCE 29
#define SIG_SCL 53
#define SIG_SDA 54
#define RC_FAST_HZ 17500000u
#define XTAL_HZ 40000000u
#define FS_PER_S 1000000000000000ull

/* CTR */
#define C_SDA_FORCE (1u << 0)
#define C_SCL_FORCE (1u << 1)
#define C_RX_FULL_ACK (1u << 3)
#define C_MS_MODE (1u << 4)
#define C_TRANS_START (1u << 5)
#define C_TX_LSB (1u << 6)
#define C_RX_LSB (1u << 7)
#define C_ARB_EN (1u << 9)
#define C_FSM_RST (1u << 10)
#define C_CONF_UPGATE (1u << 11)
#define C_BROADCAST (1u << 14)
/* FIFO_CONF */
#define F_NONFIFO (1u << 10)
#define F_ADDR_CFG (1u << 11)
#define F_RX_RST (1u << 12)
#define F_TX_RST (1u << 13)
#define F_PRT_EN (1u << 14)
/* SCL_SP_CONF, SCL_STRETCH_CONF */
#define SP_RST_SLV_EN (1u << 0)
#define SP_SCL_PD (1u << 6)
#define SP_SDA_PD (1u << 7)
#define ST_EN (1u << 10)
#define ST_CLR (1u << 11)
#define ST_ACK_CTL (1u << 12)
#define ST_ACK_LVL (1u << 13)
/* INT_RAW */
#define I_RXFIFO_WM (1u << 0)
#define I_TXFIFO_WM (1u << 1)
#define I_RXFIFO_OVF (1u << 2)
#define I_END_DETECT (1u << 3)
#define I_BYTE_DONE (1u << 4)
#define I_ARB_LOST (1u << 5)
#define I_MST_TX_UDF (1u << 6)
#define I_TRANS_COMPLETE (1u << 7)
#define I_TIME_OUT (1u << 8)
#define I_TRANS_START (1u << 9)
#define I_NACK (1u << 10)
#define I_TXFIFO_OVF (1u << 11)
#define I_RXFIFO_UDF (1u << 12)
#define I_DET_START (1u << 15)
#define I_SLAVE_STRETCH (1u << 16)
#define I_GENERAL_CALL (1u << 17)
/* SR */
#define SR_RESP_REC (1u << 0)
#define SR_SLAVE_RW (1u << 1)
#define SR_ARB_LOST (1u << 3)
#define SR_BUS_BUSY (1u << 4)
#define SR_ADDRESSED (1u << 5)

enum { OP_WRITE = 1, OP_STOP = 2, OP_READ = 3, OP_END = 4, OP_RSTART = 6 };

/* the master's step */
enum {
    P_IDLE,
    P_WAIT_BUS,  /* SCL low (someone holds it): a START waits for it */
    P_START,     /* SDA low: SCL falls after the START hold */
    P_LOW0,      /* SCL low: SDA changes after the hold */
    P_LOW1,      /* SCL low: let go of it at the low period's end */
    P_WAITHI,    /* SCL let go, waiting for it to rise */
    P_HIGH0,     /* SCL high: SDA read at the sample point */
    P_HIGH1,     /* SCL high: SCL falls at the high period's end */
    P_RS_SETUP,  /* SCL high: SDA falls for the repeated START */
    P_STOP_SETUP, /* SCL high: SDA rises for the STOP */
    P_STOP_HOLD, /* the bus free time after a STOP */
    P_HELD       /* END: SCL held low */
};

/* what a clock pulse carries */
enum { K_DOUT, K_DIN, K_AIN, K_AOUT, K_RS, K_STOP, K_CLR };

/* the slave's state */
enum { S_IDLE, S_ADDR, S_ADDR2, S_RX, S_TX, S_WAIT };

struct c3_i2c {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *ctr, *sr, *to, *saddr, *fifo_st, *fifo_conf, *data, *raw, *ena, *st;
    struct esk_rf_reg *low, *high, *hold, *sample, *sth, *rss, *sph, *sps, *clk, *sp, *stretch;
    struct esk_rf_reg *comd[8], *txram[32], *rxram[32];
    /* what CONF_UPGATE took */
    u32 ctr_l, to_l, saddr_l, sp_l, st_l, low_l, high_l, hold_l, sample_l, sth_l, rss_l, sph_l, sps_l;
    /* the FIFOs */
    u32 twr, trd, tcnt, rwr, rrd, rcnt;
    /* the lines: what it drives (1 lets go), what it sees */
    int scl_o, sda_o, scl_i, sda_i, busy;
    /* the next event: an absolute cycle and fs past it */
    int ev;
    u64 ev_c, ev_f, cur_c, cur_f;
    /* the master */
    int phase, kind, stretched, waiting_to, ci, op, bitn, clr_left;
    u64 rel_c, rel_f; /* when SCL was let go */
    u32 cmd, nbytes, byte;
    int sda_next;
    /* the slave */
    int sstate, sbit, srw, saddressed, sfirst, sack, holding, spend, spend_sda;
    u32 sbyte;
    int clocked, level, in_step;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void update(struct c3_i2c *k)
{
    u32 st = reg(k->raw) & reg(k->ena) & 0x3ffffu;
    int level = st != 0;
    esk_rf_hw(k->st, st);
    if (level != k->level) {
        k->level = level;
        esk_esp32c3_source(k->s, SOURCE, level);
    }
}

static void raise(struct c3_i2c *k, u32 bits)
{
    esk_rf_hw(k->raw, reg(k->raw) | bits);
    update(k);
}

static void sr_set(struct c3_i2c *k, u32 bits, int on)
{
    esk_rf_hw(k->sr, on ? reg(k->sr) | bits : reg(k->sr) & ~bits);
}

/* ---- time ------------------------------------------------------------------ */

static double sclk_fs(const struct c3_i2c *k)
{
    u32 v = reg(k->clk), num = v & 0xffu, a = (v >> 8) & 0x3fu, b = (v >> 14) & 0x3fu;
    u32 hz = (v & (1u << 20)) ? RC_FAST_HZ : XTAL_HZ;
    if (!(v & (1u << 21)))
        return 0;
    return ((double)num + 1.0 + (b ? (double)a / (double)b : 0.0)) * (double)FS_PER_S / (double)hz;
}

static u64 fs_per_cycle(const struct c3_i2c *k)
{
    u32 hz = esk_mach_core_hz(k->s);
    return hz ? FS_PER_S / hz : 0;
}

/* the event `n` I2C_SCLK cycles after the present */
static void after(struct c3_i2c *k, u32 n)
{
    u64 fpc = fs_per_cycle(k), d;
    double sf = sclk_fs(k);
    if (!fpc || sf <= 0) {
        k->ev = 0;
        return;
    }
    d = (u64)((double)n * sf + 0.5) + k->cur_f;
    k->ev_c = k->cur_c + d / fpc;
    k->ev_f = d % fpc;
    k->ev = 1;
    k->s->bus.touched = 1; /* the machine's next event may be this one */
}

/* the present: a register write's, an edge's or an event's time */
static void now_at(struct c3_i2c *k, u64 cycle)
{
    k->cur_c = cycle;
    k->cur_f = 0;
}

static void clocking(struct c3_i2c *k)
{
    esk_rf_clock(k->p, &k->clocked, k->ev);
}

/* ---- the lines ------------------------------------------------------------------ */

static void lines(struct c3_i2c *k, int scl, int sda)
{
    int sclv, sdav, force_scl = (k->ctr_l & C_SCL_FORCE) != 0, force_sda = (k->ctr_l & C_SDA_FORCE) != 0;
    k->scl_o = scl;
    k->sda_o = sda;
    sclv = scl && !(force_scl && (k->sp_l & SP_SCL_PD));
    sdav = sda && !(force_sda && (k->sp_l & SP_SDA_PD));
    esk_esp32c3_signal(k->s, SIG_SCL, sclv, force_scl || !sclv);
    esk_esp32c3_signal(k->s, SIG_SDA, sdav, force_sda || !sdav);
}

static int scl_in(struct c3_i2c *k) { return esk_esp32c3_signal_in(k->s, SIG_SCL); }
static int sda_in(struct c3_i2c *k) { return esk_esp32c3_signal_in(k->s, SIG_SDA); }

/* ---- the FIFOs ------------------------------------------------------------------- */

static void fifo_status(struct c3_i2c *k)
{
    u32 conf = reg(k->fifo_conf), raw = reg(k->raw);
    esk_rf_hw(k->sr, (reg(k->sr) & ~(0x3fu << 8 | 0x3fu << 18)) | (k->rcnt & 0x3fu) << 8 | (k->tcnt & 0x3fu) << 18);
    esk_rf_hw(k->fifo_st, (k->rrd & 31u) | (k->rwr & 31u) << 5 | (k->trd & 31u) << 10 | (k->twr & 31u) << 15);
    if ((conf & F_PRT_EN) && k->tcnt < ((conf >> 5) & 31u))
        raw |= I_TXFIFO_WM;
    if ((conf & F_PRT_EN) && k->rcnt > (conf & 31u))
        raw |= I_RXFIFO_WM;
    esk_rf_hw(k->raw, raw);
    update(k);
}

static u32 tx_pop(struct c3_i2c *k)
{
    u32 b = reg(k->txram[k->trd & 31u]) & 0xffu;
    k->trd = (k->trd + 1u) & 31u;
    if (k->tcnt)
        k->tcnt--;
    else if (!(reg(k->fifo_conf) & F_NONFIFO))
        raise(k, I_MST_TX_UDF);
    fifo_status(k);
    return b;
}

static void rx_push(struct c3_i2c *k, u32 b)
{
    if (k->rcnt >= 32u) {
        raise(k, I_RXFIFO_OVF);
        return;
    }
    esk_rf_hw(k->rxram[k->rwr & 31u], b & 0xffu);
    k->rwr = (k->rwr + 1u) & 31u;
    k->rcnt++;
    fifo_status(k);
}

/* ---- the master ------------------------------------------------------------------ */

static u32 t_low(const struct c3_i2c *k) { return (k->low_l & 0x1ffu) + 1u; }
static u32 t_hold(const struct c3_i2c *k) { return (k->hold_l & 0x1ffu) + 1u; }
static u32 t_wait(const struct c3_i2c *k) { return ((k->high_l >> 9) & 0x7fu) + 1u; }
static u32 t_high(const struct c3_i2c *k) { return (k->high_l & 0x1ffu) + 1u; }
static u32 t_sample(const struct c3_i2c *k) { return (k->sample_l & 0x1ffu) + 1u; }

static void master_idle(struct c3_i2c *k)
{
    k->phase = P_IDLE;
    k->ev = 0;
    lines(k, 1, 1);
}

static void cmd_done(struct c3_i2c *k)
{
    if (k->ci < 8)
        esk_rf_hw(k->comd[k->ci], reg(k->comd[k->ci]) | 1u << 31);
}

/* a clock pulse from SCL's fall: SDA's level for it after the hold */
static void begin_pulse(struct c3_i2c *k, int kind, int sda)
{
    u32 hold = t_hold(k), low = t_low(k);
    k->kind = kind;
    k->sda_next = sda;
    k->phase = P_LOW0;
    after(k, hold < low ? hold : low - 1u);
}

static void begin_byte(struct c3_i2c *k)
{
    k->bitn = 0;
    if (k->op == OP_WRITE) {
        u32 b = tx_pop(k);
        if (k->ctr_l & C_TX_LSB) { /* the bits reversed: still sent from bit 7 */
            u32 r = 0;
            int i;
            for (i = 0; i < 8; i++)
                r |= ((b >> i) & 1u) << (7 - i);
            b = r;
        }
        k->byte = b;
        begin_pulse(k, K_DOUT, (int)((b >> 7) & 1u));
    } else {
        k->byte = 0;
        begin_pulse(k, K_DIN, 1);
    }
}

/* the next command, SCL low (or the bus idle for a first START) */
static void next_command(struct c3_i2c *k)
{
    for (;;) {
        u32 c;
        if (k->ci >= 8) {
            k->phase = P_HELD;
            k->ev = 0;
            return;
        }
        c = reg(k->comd[k->ci]);
        k->cmd = c;
        k->op = (int)((c >> 11) & 7u);
        k->nbytes = c & 0xffu;
        switch (k->op) {
        case OP_RSTART:
            if (k->phase == P_IDLE || k->phase == P_STOP_HOLD) {
                if (!scl_in(k)) {
                    k->phase = P_WAIT_BUS;
                    k->ev = 0;
                    return;
                }
                k->phase = P_START;
                raise(k, I_TRANS_START);
                lines(k, 1, 0);
                after(k, (k->sth_l & 0x1ffu) + 1u);
            } else {
                begin_pulse(k, K_RS, 1);
            }
            return;
        case OP_WRITE:
        case OP_READ:
            if (!k->nbytes) {
                cmd_done(k);
                k->ci++;
                continue;
            }
            begin_byte(k);
            return;
        case OP_STOP:
            begin_pulse(k, K_STOP, 0);
            return;
        case OP_END:
            cmd_done(k);
            k->phase = P_HELD;
            k->ev = 0;
            raise(k, I_END_DETECT);
            return;
        default: /* not a command: nothing to do */
            cmd_done(k);
            k->ci++;
            continue;
        }
    }
}

/* SCL just fell after a pulse: what comes next */
static void pulse_done(struct c3_i2c *k)
{
    switch (k->kind) {
    case K_DOUT:
        if (++k->bitn < 8) {
            begin_pulse(k, K_DOUT, (int)((k->byte >> (7 - k->bitn)) & 1u));
            return;
        }
        begin_pulse(k, K_AIN, 1);
        return;
    case K_DIN:
        if (++k->bitn < 8) {
            begin_pulse(k, K_DIN, 1);
            return;
        }
        begin_pulse(k, K_AOUT, (int)((k->cmd >> 10) & 1u));
        return;
    case K_AIN: {
        int ack = (reg(k->sr) & SR_RESP_REC) != 0;
        raise(k, I_BYTE_DONE);
        if ((k->cmd & (1u << 8)) && ack != (int)((k->cmd >> 9) & 1u)) {
            raise(k, I_NACK);
            k->ci = 8; /* the commands stop */
            begin_pulse(k, K_STOP, 0);
            return;
        }
        break;
    }
    case K_AOUT: {
        u32 b = k->byte;
        if (k->ctr_l & C_RX_LSB) {
            u32 r = 0;
            int i;
            for (i = 0; i < 8; i++)
                r |= ((b >> i) & 1u) << (7 - i);
            b = r;
        }
        rx_push(k, b);
        raise(k, I_BYTE_DONE);
        break;
    }
    case K_RS:
        cmd_done(k);
        k->ci++;
        next_command(k);
        return;
    case K_CLR:
        if (--k->clr_left > 0) {
            begin_pulse(k, K_CLR, 1);
            return;
        }
        k->sp_l &= ~SP_RST_SLV_EN;
        esk_rf_hw(k->sp, reg(k->sp) & ~SP_RST_SLV_EN);
        master_idle(k);
        return;
    default:
        return;
    }
    /* a byte done */
    if (--k->nbytes > 0) {
        begin_byte(k);
        return;
    }
    cmd_done(k);
    k->ci++;
    next_command(k);
}

/* SCL is high: within SCL_WAIT_HIGH_PERIOD of being let go the high time
 * counts from then, later (stretched) from its rise */
static void high_begins(struct c3_i2c *k)
{
    u32 w = t_wait(k), s = t_sample(k);
    u64 fpc = fs_per_cycle(k);
    double sf = sclk_fs(k), el = 0;
    k->waiting_to = 0;
    if (fpc && sf > 0)
        el = ((double)(k->cur_c - k->rel_c) * (double)fpc + (double)k->cur_f - (double)k->rel_f) / sf;
    k->stretched = el >= (double)w;
    switch (k->kind) {
    case K_RS:
        k->phase = P_RS_SETUP;
        after(k, (k->rss_l & 0x1ffu) + 1u);
        return;
    case K_STOP:
        k->phase = P_STOP_SETUP;
        after(k, (k->sps_l & 0x1ffu) + 1u);
        return;
    default:
        k->phase = P_HIGH0;
        if (k->stretched)
            after(k, s > w ? s - w : 1u);
        else
            after(k, (double)s > el + 1.0 ? s - (u32)el : 1u);
        return;
    }
}

static void master_step(struct c3_i2c *k)
{
    switch (k->phase) {
    case P_START:
        lines(k, 0, 0);
        cmd_done(k);
        k->ci++;
        next_command(k);
        return;
    case P_LOW0:
        lines(k, 0, k->sda_next);
        k->phase = P_LOW1;
        after(k, t_low(k) > t_hold(k) ? t_low(k) - t_hold(k) : 1u);
        return;
    case P_LOW1:
        k->phase = P_WAITHI;
        k->ev = 0;
        k->rel_c = k->cur_c;
        k->rel_f = k->cur_f;
        lines(k, 1, k->sda_o);
        if (k->phase == P_WAITHI && scl_in(k)) {
            high_begins(k);
        } else if (k->phase == P_WAITHI && (k->to_l & (1u << 5))) { /* SCL held: the timeout */
            k->waiting_to = 1;
            after(k, 1u << (k->to_l & 31u));
        }
        return;
    case P_WAITHI: /* the timeout came first */
        if (k->waiting_to) {
            raise(k, I_TIME_OUT);
            master_idle(k);
        }
        return;
    case P_HIGH0: {
        int sda = sda_in(k);
        u32 w = t_wait(k), h = t_high(k), s = t_sample(k), rest;
        if (k->kind == K_DOUT && k->sda_o && !sda && (k->ctr_l & C_ARB_EN)) {
            sr_set(k, SR_ARB_LOST, 1);
            raise(k, I_ARB_LOST);
            k->ci = 8;
            master_idle(k);
            return;
        }
        if (k->kind == K_DIN)
            k->byte = k->byte << 1 | (u32)sda;
        if (k->kind == K_AIN)
            sr_set(k, SR_RESP_REC, sda);
        k->phase = P_HIGH1;
        if (k->stretched)
            rest = h > (s > w ? s - w : 1u) ? h - (s > w ? s - w : 1u) : 1u;
        else
            rest = w + h > s ? w + h - s : 1u;
        after(k, rest);
        return;
    }
    case P_HIGH1:
        lines(k, 0, k->sda_o);
        pulse_done(k);
        return;
    case P_RS_SETUP:
        raise(k, I_TRANS_START);
        lines(k, 1, 0);
        k->phase = P_START;
        after(k, (k->sth_l & 0x1ffu) + 1u);
        return;
    case P_STOP_SETUP:
        lines(k, 1, 1);
        cmd_done(k);
        k->phase = P_STOP_HOLD;
        after(k, (k->sph_l & 0x1ffu) + 1u);
        return;
    case P_STOP_HOLD:
        k->phase = P_IDLE;
        k->ev = 0;
        return;
    default:
        return;
    }
}

/* ---- the slave -------------------------------------------------------------------- */

static void slave_stretch(struct c3_i2c *k, u32 cause)
{
    k->holding = 1;
    esk_rf_hw(k->sr, (reg(k->sr) & ~(3u << 14)) | cause << 14);
    raise(k, I_SLAVE_STRETCH);
    lines(k, 0, k->sda_o);
}

/* SDA's next level, SDA_HOLD + 1 after SCL fell */
static void slave_sda(struct c3_i2c *k, int level)
{
    k->spend = 1;
    k->spend_sda = level;
    after(k, t_hold(k));
}

/* the address byte: its own (7 bits; with 10, the first byte, 0x78 | the
 * two high bits, in SLAVE_ADDR[6:0]) or the general call */
static int addr_match(struct c3_i2c *k, u32 b, int *general)
{
    *general = 0;
    if (b == 0 && (k->ctr_l & C_BROADCAST)) {
        *general = 1;
        return 1;
    }
    return (b >> 1) == (k->saddr_l & 0x7fu);
}

static u32 slave_tx_byte(struct c3_i2c *k)
{
    u32 b;
    if (!k->tcnt && !(reg(k->fifo_conf) & F_NONFIFO)) {
        if (k->st_l & ST_EN)
            slave_stretch(k, 1);
    }
    b = reg(k->txram[k->trd & 31u]) & 0xffu;
    k->trd = (k->trd + 1u) & 31u;
    if (k->tcnt)
        k->tcnt--;
    fifo_status(k);
    return b;
}

static void slave_rise(struct c3_i2c *k)
{
    int sda = k->sda_i;
    switch (k->sstate) {
    case S_ADDR:
    case S_ADDR2:
    case S_RX:
        if (k->sbit < 8) {
            k->sbyte = k->sbyte << 1 | (u32)sda;
            k->sbit++;
        } else {
            k->sbit = 9;
        }
        return;
    case S_TX:
        if (k->sbit < 8) {
            k->sbit++;
        } else {
            k->sbit = 9;
            sr_set(k, SR_RESP_REC, sda);
            if (sda) { /* the master's NACK: the last byte */
                raise(k, I_NACK);
                k->sstate = S_WAIT;
            }
        }
        return;
    default:
        return;
    }
}

static void slave_fall(struct c3_i2c *k)
{
    int general = 0, ack = 0;
    switch (k->sstate) {
    case S_ADDR:
        if (k->sbit == 8) {
            if (!addr_match(k, k->sbyte, &general)) {
                k->sstate = S_WAIT;
                return;
            }
            k->srw = (int)(k->sbyte & 1u) && !general;
            k->saddressed = 1;
            sr_set(k, SR_ADDRESSED, 1);
            sr_set(k, SR_SLAVE_RW, k->srw);
            if (general)
                raise(k, I_GENERAL_CALL);
            k->sfirst = 1;
            slave_sda(k, 0);
            return;
        }
        if (k->sbit == 9) { /* after the address's ACK */
            k->sbit = 0;
            k->sbyte = 0;
            if ((k->saddr_l & (1u << 31)) && !k->srw && k->sstate == S_ADDR) {
                k->sstate = S_ADDR2;
                slave_sda(k, 1);
                return;
            }
            if (k->srw) {
                u32 b;
                k->sstate = S_TX;
                if (k->st_l & ST_EN)
                    slave_stretch(k, 0);
                b = slave_tx_byte(k);
                k->sbyte = b;
                slave_sda(k, (int)((b >> 7) & 1u));
            } else {
                k->sstate = S_RX;
                slave_sda(k, 1);
            }
        }
        return;
    case S_ADDR2:
        if (k->sbit == 8) {
            if ((k->sbyte & 0xffu) != ((k->saddr_l >> 7) & 0xffu)) {
                k->sstate = S_WAIT;
                return;
            }
            slave_sda(k, 0);
            return;
        }
        if (k->sbit == 9) {
            k->sbit = 0;
            k->sbyte = 0;
            k->sstate = S_RX;
            slave_sda(k, 1);
        }
        return;
    case S_RX:
        if (k->sbit == 8) {
            u32 b = k->sbyte & 0xffu;
            if (k->ctr_l & C_RX_LSB) {
                u32 r = 0;
                int i;
                for (i = 0; i < 8; i++)
                    r |= ((b >> i) & 1u) << (7 - i);
                b = r;
            }
            if (k->sfirst && (reg(k->fifo_conf) & F_ADDR_CFG)) { /* the RAM address */
                k->rwr = k->trd = b & 31u;
            } else if (k->rcnt >= 32u) {
                raise(k, I_RXFIFO_OVF);
                ack = (k->ctr_l & C_RX_FULL_ACK) != 0;
                if (k->st_l & ST_EN)
                    slave_stretch(k, 2);
            } else {
                rx_push(k, b);
            }
            k->sfirst = 0;
            raise(k, I_BYTE_DONE);
            if (k->st_l & ST_ACK_CTL) {
                ack = (k->st_l & ST_ACK_LVL) != 0;
                if (k->st_l & ST_EN)
                    slave_stretch(k, 3);
            }
            slave_sda(k, ack);
            return;
        }
        if (k->sbit == 9) {
            k->sbit = 0;
            k->sbyte = 0;
            slave_sda(k, 1);
        }
        return;
    case S_TX:
        if (k->sbit < 8) {
            slave_sda(k, (int)((k->sbyte >> (7 - k->sbit)) & 1u));
            return;
        }
        if (k->sbit == 8) { /* the master's ACK: let SDA go */
            raise(k, I_BYTE_DONE);
            slave_sda(k, 1);
            return;
        }
        { /* the next byte */
            u32 b;
            k->sbit = 0;
            b = slave_tx_byte(k);
            k->sbyte = b;
            slave_sda(k, (int)((b >> 7) & 1u));
        }
        return;
    default:
        return;
    }
}

static void slave_step(struct c3_i2c *k)
{
    if (k->spend) {
        k->spend = 0;
        lines(k, k->holding ? 0 : 1, k->spend_sda);
    }
}

/* ---- the bus as seen -------------------------------------------------------------- */

static void watch(void *ctx)
{
    struct c3_i2c *k = (struct c3_i2c *)ctx;
    struct esk_mach *s = k->s;
    int scl = scl_in(k), sda = sda_in(k), master = (k->ctr_l & C_MS_MODE) != 0;
    u64 lag = (u64)s->input_lead + s->output_lag;
    if (scl == k->scl_i && sda == k->sda_i)
        return;
    if (!k->in_step)
        now_at(k, s->cycles > lag ? s->cycles - lag : 0);
    /* START and STOP: SDA moving while SCL stays high */
    if (scl && k->scl_i && sda != k->sda_i) {
        k->sda_i = sda;
        if (!sda) {
            k->busy = 1;
            sr_set(k, SR_BUS_BUSY, 1);
            raise(k, I_DET_START);
            if (!master) {
                k->sstate = S_ADDR;
                k->sbit = 0;
                k->sbyte = 0;
                k->spend = 0;
                if (k->sda_o == 0 || k->scl_o == 0)
                    lines(k, 1, 1);
            }
        } else { /* a STOP */
            k->busy = 0;
            sr_set(k, SR_BUS_BUSY, 0);
            raise(k, I_TRANS_COMPLETE);
            if (!master) {
                k->sstate = S_IDLE;
                k->saddressed = 0;
                sr_set(k, SR_ADDRESSED, 0);
                if (k->sda_o == 0)
                    lines(k, k->holding ? 0 : 1, 1);
            }
        }
    }
    k->sda_i = sda;
    if (scl != k->scl_i) {
        k->scl_i = scl;
        if (master) {
            if (scl && k->phase == P_WAITHI) {
                k->ev = 0;
                high_begins(k);
            } else if (scl && k->phase == P_WAIT_BUS) {
                k->phase = P_IDLE;
                next_command(k);
            }
        } else if (scl) {
            slave_rise(k);
        } else {
            slave_fall(k);
        }
    }
    clocking(k);
}

/* ---- registers ---------------------------------------------------------------------- */

static void latch(struct c3_i2c *k)
{
    k->ctr_l = reg(k->ctr);
    k->to_l = reg(k->to);
    k->saddr_l = reg(k->saddr);
    k->sp_l = reg(k->sp);
    k->st_l = reg(k->stretch);
    k->low_l = reg(k->low);
    k->high_l = reg(k->high);
    k->hold_l = reg(k->hold);
    k->sample_l = reg(k->sample);
    k->sth_l = reg(k->sth);
    k->rss_l = reg(k->rss);
    k->sph_l = reg(k->sph);
    k->sps_l = reg(k->sps);
}

static void fsm_reset(struct c3_i2c *k)
{
    k->phase = P_IDLE;
    k->ev = 0;
    k->sstate = S_IDLE;
    k->holding = 0;
    k->spend = 0;
    k->waiting_to = 0;
    lines(k, 1, 1);
    k->scl_i = scl_in(k);
    k->sda_i = sda_in(k);
}

static void i2c_tick(void *ctx, u32 cycles)
{
    struct c3_i2c *k = (struct c3_i2c *)ctx;
    struct esk_mach *s = k->s;
    (void)cycles;
    while (k->ev && k->ev_c <= s->cycles) {
        k->ev = 0;
        k->cur_c = k->ev_c;
        k->cur_f = k->ev_f;
        s->output_lag = (u32)(s->cycles - k->ev_c);
        k->in_step = 1;
        if (k->ctr_l & C_MS_MODE)
            master_step(k);
        else
            slave_step(k);
        k->in_step = 0;
        s->output_lag = 0;
    }
    clocking(k);
}

static int i2c_next(void *ctx, u32 *cycles)
{
    struct c3_i2c *k = (struct c3_i2c *)ctx;
    u64 d;
    if (!k->ev)
        return 0;
    d = k->ev_c > k->s->cycles ? k->ev_c - k->s->cycles : 1u;
    *cycles = d > 0xffffffffu ? 0xffffffffu : (u32)d;
    return 1;
}

static u32 i2c_read(void *ctx, u32 off, int n)
{
    struct c3_i2c *k = (struct c3_i2c *)ctx;
    if ((off & ~3u) == 0x1Cu) { /* DATA: the RX FIFO */
        u32 b = reg(k->rxram[k->rrd & 31u]) & 0xffu;
        if (!k->rcnt) {
            raise(k, I_RXFIFO_UDF);
            return b;
        }
        k->rrd = (k->rrd + 1u) & 31u;
        k->rcnt--;
        fifo_status(k);
        return b;
    }
    return esk_rf_read(k->p, off, n);
}

static void i2c_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_i2c *k = (struct c3_i2c *)ctx;
    u32 w = off & ~3u;
    now_at(k, k->s->cycles);
    switch (w) {
    case 0x1Cu: /* DATA: the TX FIFO */
        if (k->tcnt >= 32u) {
            raise(k, I_TXFIFO_OVF);
            return;
        }
        esk_rf_hw(k->txram[k->twr & 31u], v & 0xffu);
        k->twr = (k->twr + 1u) & 31u;
        k->tcnt++;
        fifo_status(k);
        return;
    case 0x24u: /* INT_CLR */
        esk_rf_hw(k->raw, reg(k->raw) & ~v);
        update(k);
        return;
    case 0x08u:
    case 0x14u:
    case 0x2Cu:
        return; /* SR, FIFO_ST, INT_STATUS */
    default:
        break;
    }
    if (w >= 0x100u && w < 0x180u) { /* the TX RAM, directly */
        esk_rf_hw(k->txram[(w - 0x100u) / 4u], v & 0xffu);
        return;
    }
    if (w >= 0x180u && w < 0x200u)
        return; /* the RX RAM: read-only */
    esk_rf_write(k->p, off, n, v);
    if (w == 0x20u || w == 0x28u)
        update(k);
    if (w == esk_rf_off(k->fifo_conf)) {
        if (v & F_TX_RST)
            k->twr = k->trd = k->tcnt = 0;
        if (v & F_RX_RST)
            k->rwr = k->rrd = k->rcnt = 0;
        fifo_status(k);
    }
    if (w == esk_rf_off(k->stretch) && (v & ST_CLR) && k->holding) {
        k->holding = 0;
        esk_rf_hw(k->sr, reg(k->sr) | 3u << 14);
        lines(k, 1, k->sda_o);
    }
    if (w == esk_rf_off(k->ctr)) {
        if (v & C_CONF_UPGATE) {
            latch(k);
            if ((k->sp_l & SP_RST_SLV_EN) && (k->ctr_l & C_MS_MODE) && k->phase == P_IDLE) {
                k->clr_left = (int)((k->sp_l >> 1) & 31u);
                if (k->clr_left > 0) {
                    lines(k, 0, 1);
                    begin_pulse(k, K_CLR, 1);
                }
            }
            lines(k, k->scl_o, k->sda_o);
        }
        if (v & C_FSM_RST)
            fsm_reset(k);
        if ((v & C_TRANS_START) && (k->ctr_l & C_MS_MODE) && sclk_fs(k) > 0) {
            int held = k->phase == P_HELD;
            sr_set(k, SR_ARB_LOST, 0);
            k->ci = 0;
            if (held) { /* SCL is low: the next command's pulse from now */
                k->phase = P_LOW0;
                next_command(k);
            } else if (k->phase == P_IDLE || k->phase == P_STOP_HOLD) {
                k->phase = P_IDLE;
                next_command(k);
            }
        }
    }
    clocking(k);
}

static void i2c_reset(void *ctx)
{
    struct c3_i2c *k = (struct c3_i2c *)ctx;
    latch(k);
    k->twr = k->trd = k->tcnt = k->rwr = k->rrd = k->rcnt = 0;
    k->busy = 0;
    k->level = 0;
    esk_esp32c3_source(k->s, SOURCE, 0);
    k->clocked = 0;
    fsm_reset(k);
    fifo_status(k);
    update(k);
    clocking(k);
}

const struct esk_dev_ops esk_esp32c3_i2c_ops = {
    "esp32c3-i2c", i2c_read, i2c_write, i2c_reset, i2c_tick, i2c_next,
};

void *esk_esp32c3_i2c_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_i2c *k = (struct c3_i2c *)esk_mach_alloc(s, sizeof *k);
    static const struct {
        size_t at;
        const char *name;
    } regs[] = {
        { offsetof(struct c3_i2c, ctr), "CTR" },
        { offsetof(struct c3_i2c, sr), "SR" },
        { offsetof(struct c3_i2c, to), "TO" },
        { offsetof(struct c3_i2c, saddr), "SLAVE_ADDR" },
        { offsetof(struct c3_i2c, fifo_st), "FIFO_ST" },
        { offsetof(struct c3_i2c, fifo_conf), "FIFO_CONF" },
        { offsetof(struct c3_i2c, data), "DATA" },
        { offsetof(struct c3_i2c, raw), "INT_RAW" },
        { offsetof(struct c3_i2c, ena), "INT_ENA" },
        { offsetof(struct c3_i2c, st), "INT_STATUS" },
        { offsetof(struct c3_i2c, low), "SCL_LOW_PERIOD" },
        { offsetof(struct c3_i2c, high), "SCL_HIGH_PERIOD" },
        { offsetof(struct c3_i2c, hold), "SDA_HOLD" },
        { offsetof(struct c3_i2c, sample), "SDA_SAMPLE" },
        { offsetof(struct c3_i2c, sth), "SCL_START_HOLD" },
        { offsetof(struct c3_i2c, rss), "SCL_RSTART_SETUP" },
        { offsetof(struct c3_i2c, sph), "SCL_STOP_HOLD" },
        { offsetof(struct c3_i2c, sps), "SCL_STOP_SETUP" },
        { offsetof(struct c3_i2c, clk), "CLK_CONF" },
        { offsetof(struct c3_i2c, sp), "SCL_SP_CONF" },
        { offsetof(struct c3_i2c, stretch), "SCL_STRETCH_CONF" },
    };
    char name[24];
    size_t i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    for (i = 0; i < sizeof regs / sizeof regs[0]; i++) {
        struct esk_rf_reg *r = esk_rf_reg(p, regs[i].name);
        if (!r)
            return 0;
        memcpy((char *)k + regs[i].at, &r, sizeof r);
    }
    for (i = 0; i < 8; i++) {
        snprintf(name, sizeof name, "COMD%d", (int)i);
        if (!(k->comd[i] = esk_rf_reg(p, name)))
            return 0;
    }
    k->txram[0] = esk_rf_reg(p, "TXFIFO_START_ADDR");
    k->rxram[0] = esk_rf_reg(p, "RXFIFO_START_ADDR");
    for (i = 1; i < 32; i++) {
        snprintf(name, sizeof name, "TXRAM%d", (int)i - 1);
        k->txram[i] = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "RXRAM%d", (int)i - 1);
        k->rxram[i] = esk_rf_reg(p, name);
        if (!k->txram[i] || !k->rxram[i])
            return 0;
    }
    if (!k->txram[0] || !k->rxram[0])
        return 0;
    if (esk_esp32c3_signal_watch(s, watch, k) != 0)
        return 0;
    return k;
}
