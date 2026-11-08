/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* i2c.c -- the ESP32's I2C controllers I2C0 and I2C1 (TRM v5 chapter 11)
 * on the GPIO matrix's I2CEXT0_SCL and _SDA (signals 29 and 30) and
 * I2CEXT1_SCL and _SDA (95 and 96), master or slave (CTR's MS_MODE),
 * their interrupts on matrix sources 49 and 50. The ESP32-C3's model
 * (mcu/esp32c3/i2c.c) on the ESP32's registers, timing and commands.
 *
 *   - The controller runs from APB_CLK; its settings act as written (the
 *     ESP32 has no CONF_UPGATE).
 *   - A master runs COMD0-15 from TRANS_START: each command's byte_num
 *     (bits 7:0), ack_check_en (8), ack_exp (9), ack_value (10) and
 *     op_code (13:11: 0 RSTART, 1 WRITE, 2 READ, 3 STOP, 4 END), its
 *     COMMAND_DONE (bit 31) set when it is done. RSTART makes a START (once
 *     SCL is high) or a repeated START; WRITE sends byte_num bytes from the
 *     TX FIFO, the address among them, each with its ACK checked against
 *     ack_exp when ack_check_en is set (ACK_ERR, then a STOP and the
 *     commands stop); READ takes byte_num bytes into the RX FIFO,
 *     answering ack_value; STOP ends the transfer and the commands; END
 *     holds SCL low and stops (END_DETECT) until the next TRANS_START runs
 *     the commands from COMD0 again. Each byte a master sends or receives
 *     raises MASTER_TRAN_COMP and sets SR's BYTE_TRANS.
 *   - The bus's timing in APB_CLK cycles, as ESP-IDF's i2c_ll counts it:
 *     SCL low SCL_LOW_PERIOD + 1; let go, high from its rise for
 *     SCL_HIGH_PERIOD + 7, or + 8 with the SCL filter on at a threshold
 *     up to 2, + threshold + 6 above (a slave holding SCL low stretches
 *     the low time); SDA changes SDA_HOLD after SCL falls and is read
 *     SDA_SAMPLE after it rises; a START holds SDA low SCL_START_HOLD
 *     before SCL falls, a repeated START waits SCL_RSTART_SETUP with SCL
 *     high, a STOP SCL_STOP_SETUP before SDA rises and SCL_STOP_HOLD
 *     after it.
 *   - The lines are open-drain: with SCL_FORCE_OUT and SDA_FORCE_OUT the
 *     output is enabled throughout (the pad's PAD_DRIVER then makes it
 *     open-drain, as ESP-IDF configures it), without them the level and
 *     the enable are exchanged (low driven, high let go). A master letting
 *     SDA go that reads it low loses the bus (ARBITRATION_LOST, SR's
 *     ARB_LOST) and lets both lines go. SCL held low by someone else for
 *     more than TO's TIME_OUT_REG cycles ends the transfer (TIME_OUT, SR's
 *     TIME_OUT).
 *   - The TX and RX FIFOs (32 bytes each) are filled and emptied through
 *     DATA (ESP-IDF writes it through the AHB view, 0x6001301C and
 *     0x6002701C, and reads it at the DPORT address): RXFIFO_OVF receiving
 *     into a full one; TX_FIFO_RST and RX_FIFO_RST empty them; TXFIFO_EMPTY
 *     while the TX FIFO holds fewer than TXFIFO_EMPTY_THRHD bytes and
 *     RXFIFO_FULL while the RX FIFO holds more than RXFIFO_FULL_THRHD;
 *     RXFIFO_ST and SR's counts follow them.
 *   - A slave answers SLAVE_ADDR (7 bits, or 10 with ADDR_10BIT_EN: the
 *     first byte 0x78 | its two high bits, then the low eight): it ACKs,
 *     puts the bytes written into the RX FIFO (with FIFO_ADDR_CFG_EN the
 *     first is the RAM address the bytes go to and come from), sends the
 *     TX FIFO's bytes when read (a NACK from the master ends them), ACKs
 *     a full RX FIFO (RXFIFO_OVF). SR's SLAVE_ADDRESSED and SLAVE_RW say
 *     how it was addressed; a STOP raises SLAVE_TRAN_COMP.
 *   - Both see the bus: BUS_BUSY from a START to a STOP, TRANS_COMPLETE at
 *     a STOP; TRANS_START when a master makes a START; ACK_REC is the last
 *     ACK.
 *   - INT_STATUS is INT_RAW and INT_ENA; INT_CLR clears. DPORT's
 *     PERIP_RST_EN bits 7 and 18 reset I2C0 and I2C1 (dport.c).
 * Where it differs inside: the filters (SCL_ and SDA_FILTER_CFG) are not
 * applied (the nets' levels are clean) beyond their share of the high
 * time; SR's state fields read 0; a slave reacts at the edge it sees, not
 * at its SDA_SAMPLE point. Not yet: the non-FIFO mode (NONFIFO_EN, its
 * RX_REC_FULL and TX_SEND_EMPTY, the RAM read directly at 0x100),
 * SAMPLE_SCL_LEVEL (sampling while SCL is low), 10-bit addressing's R/W
 * check. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "mcu/esp32/soc/esp32.h"

#define FS_PER_S 1000000000000000ull
#define NCMD 16

/* CTR */
#define C_SDA_FORCE (1u << 0)
#define C_SCL_FORCE (1u << 1)
#define C_MS_MODE (1u << 4)
#define C_TRANS_START (1u << 5)
#define C_TX_LSB (1u << 6)
#define C_RX_LSB (1u << 7)
/* FIFO_CONF */
#define F_ADDR_CFG (1u << 11)
#define F_RX_RST (1u << 12)
#define F_TX_RST (1u << 13)
/* INT_RAW */
#define I_RXFIFO_FULL (1u << 0)
#define I_TXFIFO_EMPTY (1u << 1)
#define I_RXFIFO_OVF (1u << 2)
#define I_END_DETECT (1u << 3)
#define I_SLAVE_TRAN_COMP (1u << 4)
#define I_ARB_LOST (1u << 5)
#define I_MASTER_TRAN_COMP (1u << 6)
#define I_TRANS_COMPLETE (1u << 7)
#define I_TIME_OUT (1u << 8)
#define I_TRANS_START (1u << 9)
#define I_ACK_ERR (1u << 10)
/* SR */
#define SR_ACK_REC (1u << 0)
#define SR_SLAVE_RW (1u << 1)
#define SR_TIME_OUT (1u << 2)
#define SR_ARB_LOST (1u << 3)
#define SR_BUS_BUSY (1u << 4)
#define SR_ADDRESSED (1u << 5)
#define SR_BYTE_TRANS (1u << 6)

enum { OP_RSTART = 0, OP_WRITE = 1, OP_READ = 2, OP_STOP = 3, OP_END = 4 };

/* the master's step */
enum {
    P_IDLE,
    P_WAIT_BUS,   /* SCL low (someone holds it): a START waits for it */
    P_START,      /* SDA low: SCL falls after the START hold */
    P_LOW0,       /* SCL low: SDA changes after the hold */
    P_LOW1,       /* SCL low: let go of it at the low period's end */
    P_WAITHI,     /* SCL let go, waiting for it to rise */
    P_HIGH0,      /* SCL high: SDA read at the sample point */
    P_HIGH1,      /* SCL high: SCL falls at the high period's end */
    P_RS_SETUP,   /* SCL high: SDA falls for the repeated START */
    P_STOP_SETUP, /* SCL high: SDA rises for the STOP */
    P_STOP_HOLD,  /* the bus free time after a STOP */
    P_HELD        /* END: SCL held low */
};

/* what a clock pulse carries */
enum { K_DOUT, K_DIN, K_AIN, K_AOUT, K_RS, K_STOP };

/* the slave's state */
enum { S_IDLE, S_ADDR, S_ADDR2, S_RX, S_TX, S_WAIT };

struct e32_i2c {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *ctr, *sr, *to, *saddr, *fifo_st, *fifo_conf, *raw, *ena, *st;
    struct esk_rf_reg *low, *high, *hold, *sample, *sth, *rss, *sph, *sps, *sclf;
    struct esk_rf_reg *comd[NCMD];
    int source, sig_scl, sig_sda;
    /* the FIFOs' RAM */
    u8 txram[32], rxram[32];
    u32 twr, trd, tcnt, rwr, rrd, rcnt;
    /* the lines: what it drives (1 lets go), what it sees */
    int scl_o, sda_o, scl_i, sda_i;
    /* the next event: an absolute cycle and fs past it */
    int ev;
    u64 ev_c, ev_f, cur_c, cur_f;
    /* the master */
    int phase, kind, waiting_to, ci, op, bitn;
    u32 cmd, nbytes, byte;
    int sda_next;
    /* the slave */
    int sstate, sbit, srw, sfirst, spend, spend_sda;
    u32 sbyte;
    int clocked, level, in_step;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void update(struct e32_i2c *k)
{
    u32 st = reg(k->raw) & reg(k->ena) & 0x1fffu;
    int level = st != 0;
    esk_rf_hw(k->st, st);
    if (level != k->level) {
        k->level = level;
        esk_esp32_source(k->s, k->source, level);
    }
}

static void raise(struct e32_i2c *k, u32 bits)
{
    esk_rf_hw(k->raw, reg(k->raw) | bits);
    update(k);
}

static void sr_set(struct e32_i2c *k, u32 bits, int on)
{
    esk_rf_hw(k->sr, on ? reg(k->sr) | bits : reg(k->sr) & ~bits);
}

static u32 reverse8(u32 b)
{
    u32 r = 0;
    int i;
    for (i = 0; i < 8; i++)
        r |= ((b >> i) & 1u) << (7 - i);
    return r;
}

/* ---- time ------------------------------------------------------------------ */

static double apb_fs(const struct e32_i2c *k)
{
    u32 hz = esk_esp32_apb_hz(k->s);
    return hz ? (double)FS_PER_S / (double)hz : 0;
}

static u64 fs_per_cycle(const struct e32_i2c *k)
{
    u32 hz = esk_mach_core_hz(k->s);
    return hz ? FS_PER_S / hz : 0;
}

/* the event `n` APB_CLK cycles after the present */
static void after(struct e32_i2c *k, u32 n)
{
    u64 fpc = fs_per_cycle(k), d;
    double af = apb_fs(k);
    if (!fpc || af <= 0) {
        k->ev = 0;
        return;
    }
    d = (u64)((double)n * af + 0.5) + k->cur_f;
    k->ev_c = k->cur_c + d / fpc;
    k->ev_f = d % fpc;
    k->ev = 1;
    k->s->bus.touched = 1; /* the machine's next event may be this one */
}

/* the present: a register write's, an edge's or an event's time */
static void now_at(struct e32_i2c *k, u64 cycle)
{
    k->cur_c = cycle;
    k->cur_f = 0;
}

static void clocking(struct e32_i2c *k)
{
    esk_rf_clock(k->p, &k->clocked, k->ev);
}

/* ---- the lines ------------------------------------------------------------------ */

static void lines(struct e32_i2c *k, int scl, int sda)
{
    u32 c = reg(k->ctr);
    int force_scl = (c & C_SCL_FORCE) != 0, force_sda = (c & C_SDA_FORCE) != 0;
    k->scl_o = scl;
    k->sda_o = sda;
    esk_esp32_signal(k->s, k->sig_scl, scl, force_scl || !scl);
    esk_esp32_signal(k->s, k->sig_sda, sda, force_sda || !sda);
}

static int scl_in(struct e32_i2c *k) { return esk_esp32_signal_in(k->s, k->sig_scl); }
static int sda_in(struct e32_i2c *k) { return esk_esp32_signal_in(k->s, k->sig_sda); }

/* ---- the FIFOs ------------------------------------------------------------------- */

static void fifo_status(struct e32_i2c *k)
{
    u32 conf = reg(k->fifo_conf), raw = reg(k->raw);
    esk_rf_hw(k->sr, (reg(k->sr) & ~(0x3fu << 8 | 0x3fu << 18)) | (k->rcnt & 0x3fu) << 8 | (k->tcnt & 0x3fu) << 18);
    esk_rf_hw(k->fifo_st, (k->rrd & 31u) | (k->rwr & 31u) << 5 | (k->trd & 31u) << 10 | (k->twr & 31u) << 15);
    if (k->tcnt < ((conf >> 5) & 31u))
        raw |= I_TXFIFO_EMPTY;
    if (k->rcnt > (conf & 31u))
        raw |= I_RXFIFO_FULL;
    esk_rf_hw(k->raw, raw);
    update(k);
}

static u32 tx_pop(struct e32_i2c *k)
{
    u32 b = k->txram[k->trd & 31u];
    k->trd = (k->trd + 1u) & 31u;
    if (k->tcnt)
        k->tcnt--;
    fifo_status(k);
    return b;
}

static void rx_push(struct e32_i2c *k, u32 b)
{
    if (k->rcnt >= 32u) {
        raise(k, I_RXFIFO_OVF);
        return;
    }
    k->rxram[k->rwr & 31u] = (u8)(b & 0xffu);
    k->rwr = (k->rwr + 1u) & 31u;
    k->rcnt++;
    fifo_status(k);
}

/* ---- the master ------------------------------------------------------------------ */

static u32 t_low(const struct e32_i2c *k) { return (reg(k->low) & 0x3fffu) + 1u; }

static u32 t_high(const struct e32_i2c *k)
{
    u32 f = reg(k->sclf), thres = f & 7u, extra = 7u;
    if (f & 8u)
        extra = thres <= 2u ? 8u : thres + 6u;
    return (reg(k->high) & 0x3fffu) + extra;
}

static u32 at_least1(u32 v) { return v ? v : 1u; }
static u32 t_hold(const struct e32_i2c *k) { return at_least1(reg(k->hold) & 0x3ffu); }
static u32 t_sample(const struct e32_i2c *k) { return at_least1(reg(k->sample) & 0x3ffu); }

static void master_idle(struct e32_i2c *k)
{
    k->phase = P_IDLE;
    k->ev = 0;
    lines(k, 1, 1);
}

static void cmd_done(struct e32_i2c *k)
{
    if (k->ci < NCMD)
        esk_rf_hw(k->comd[k->ci], reg(k->comd[k->ci]) | 1u << 31);
}

/* a clock pulse from SCL's fall: SDA's level for it after the hold */
static void begin_pulse(struct e32_i2c *k, int kind, int sda)
{
    u32 hold = t_hold(k), low = t_low(k);
    k->kind = kind;
    k->sda_next = sda;
    k->phase = P_LOW0;
    after(k, hold < low ? hold : low - 1u);
}

static void begin_byte(struct e32_i2c *k)
{
    k->bitn = 0;
    sr_set(k, SR_BYTE_TRANS, 0);
    if (k->op == OP_WRITE) {
        u32 b = tx_pop(k);
        if (reg(k->ctr) & C_TX_LSB) /* the bits reversed: still sent from bit 7 */
            b = reverse8(b);
        k->byte = b;
        begin_pulse(k, K_DOUT, (int)((b >> 7) & 1u));
    } else {
        k->byte = 0;
        begin_pulse(k, K_DIN, 1);
    }
}

/* the next command, SCL low (or the bus idle for a first START) */
static void next_command(struct e32_i2c *k)
{
    for (;;) {
        u32 c;
        if (k->ci >= NCMD) {
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
                after(k, at_least1(reg(k->sth) & 0x3ffu));
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
static void pulse_done(struct e32_i2c *k)
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
        int ack = (reg(k->sr) & SR_ACK_REC) != 0;
        sr_set(k, SR_BYTE_TRANS, 1);
        raise(k, I_MASTER_TRAN_COMP);
        if ((k->cmd & (1u << 8)) && ack != (int)((k->cmd >> 9) & 1u)) {
            raise(k, I_ACK_ERR);
            k->ci = NCMD; /* the commands stop */
            begin_pulse(k, K_STOP, 0);
            return;
        }
        break;
    }
    case K_AOUT: {
        u32 b = k->byte;
        if (reg(k->ctr) & C_RX_LSB)
            b = reverse8(b);
        rx_push(k, b);
        sr_set(k, SR_BYTE_TRANS, 1);
        raise(k, I_MASTER_TRAN_COMP);
        break;
    }
    case K_RS:
        cmd_done(k);
        k->ci++;
        next_command(k);
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

/* SCL is high: the high time counts from its rise */
static void high_begins(struct e32_i2c *k)
{
    u32 s = t_sample(k), h = t_high(k);
    k->waiting_to = 0;
    switch (k->kind) {
    case K_RS:
        k->phase = P_RS_SETUP;
        after(k, at_least1(reg(k->rss) & 0x3ffu));
        return;
    case K_STOP:
        k->phase = P_STOP_SETUP;
        after(k, at_least1(reg(k->sps) & 0x3ffu));
        return;
    default:
        k->phase = P_HIGH0;
        after(k, s < h ? s : h - 1u);
        return;
    }
}

static void master_step(struct e32_i2c *k)
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
        lines(k, 1, k->sda_o);
        if (k->phase == P_WAITHI && scl_in(k)) {
            high_begins(k);
        } else if (k->phase == P_WAITHI) { /* SCL held: the timeout */
            k->waiting_to = 1;
            after(k, at_least1(reg(k->to) & 0xfffffu));
        }
        return;
    case P_WAITHI: /* the timeout came first */
        if (k->waiting_to) {
            sr_set(k, SR_TIME_OUT, 1);
            raise(k, I_TIME_OUT);
            master_idle(k);
        }
        return;
    case P_HIGH0: {
        int sda = sda_in(k);
        u32 h = t_high(k), s = t_sample(k);
        if (k->kind == K_DOUT && k->sda_o && !sda) {
            sr_set(k, SR_ARB_LOST, 1);
            raise(k, I_ARB_LOST);
            k->ci = NCMD;
            master_idle(k);
            return;
        }
        if (k->kind == K_DIN)
            k->byte = k->byte << 1 | (u32)sda;
        if (k->kind == K_AIN)
            sr_set(k, SR_ACK_REC, sda);
        k->phase = P_HIGH1;
        after(k, h > s ? h - s : 1u);
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
        after(k, at_least1(reg(k->sth) & 0x3ffu));
        return;
    case P_STOP_SETUP:
        lines(k, 1, 1);
        cmd_done(k);
        k->phase = P_STOP_HOLD;
        after(k, at_least1(reg(k->sph) & 0x3fffu));
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

/* SDA's next level, SDA_HOLD after SCL fell */
static void slave_sda(struct e32_i2c *k, int level)
{
    k->spend = 1;
    k->spend_sda = level;
    after(k, t_hold(k));
}

static u32 slave_tx_byte(struct e32_i2c *k)
{
    u32 b = k->txram[k->trd & 31u];
    k->trd = (k->trd + 1u) & 31u;
    if (k->tcnt)
        k->tcnt--;
    fifo_status(k);
    return b;
}

static void slave_rise(struct e32_i2c *k)
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
            sr_set(k, SR_ACK_REC, sda);
            if (sda) /* the master's NACK: the last byte */
                k->sstate = S_WAIT;
        }
        return;
    default:
        return;
    }
}

static void slave_fall(struct e32_i2c *k)
{
    u32 sa = reg(k->saddr);
    switch (k->sstate) {
    case S_ADDR:
        if (k->sbit == 8) {
            if ((k->sbyte >> 1) != (sa & 0x7fu)) {
                k->sstate = S_WAIT;
                return;
            }
            k->srw = (int)(k->sbyte & 1u);
            sr_set(k, SR_ADDRESSED, 1);
            sr_set(k, SR_SLAVE_RW, k->srw);
            k->sfirst = 1;
            slave_sda(k, 0);
            return;
        }
        if (k->sbit == 9) { /* after the address's ACK */
            k->sbit = 0;
            k->sbyte = 0;
            if ((sa & (1u << 31)) && !k->srw) {
                k->sstate = S_ADDR2;
                slave_sda(k, 1);
                return;
            }
            if (k->srw) {
                u32 b = slave_tx_byte(k);
                k->sstate = S_TX;
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
            if ((k->sbyte & 0xffu) != ((sa >> 7) & 0xffu)) {
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
            if (reg(k->ctr) & C_RX_LSB)
                b = reverse8(b);
            if (k->sfirst && (reg(k->fifo_conf) & F_ADDR_CFG)) /* the RAM address */
                k->rwr = k->trd = b & 31u;
            else
                rx_push(k, b);
            k->sfirst = 0;
            sr_set(k, SR_BYTE_TRANS, 1);
            slave_sda(k, 0);
            return;
        }
        if (k->sbit == 9) {
            k->sbit = 0;
            k->sbyte = 0;
            sr_set(k, SR_BYTE_TRANS, 0);
            slave_sda(k, 1);
        }
        return;
    case S_TX:
        if (k->sbit < 8) {
            slave_sda(k, (int)((k->sbyte >> (7 - k->sbit)) & 1u));
            return;
        }
        if (k->sbit == 8) { /* the master's ACK: let SDA go */
            sr_set(k, SR_BYTE_TRANS, 1);
            slave_sda(k, 1);
            return;
        }
        { /* the next byte */
            u32 b;
            k->sbit = 0;
            sr_set(k, SR_BYTE_TRANS, 0);
            b = slave_tx_byte(k);
            k->sbyte = b;
            slave_sda(k, (int)((b >> 7) & 1u));
        }
        return;
    default:
        return;
    }
}

static void slave_step(struct e32_i2c *k)
{
    if (k->spend) {
        k->spend = 0;
        lines(k, 1, k->spend_sda);
    }
}

/* ---- the bus as seen -------------------------------------------------------------- */

static void watch(void *ctx)
{
    struct e32_i2c *k = (struct e32_i2c *)ctx;
    struct esk_mach *s = k->s;
    int scl = scl_in(k), sda = sda_in(k), master = (reg(k->ctr) & C_MS_MODE) != 0;
    u64 lag = (u64)s->input_lead + s->output_lag;
    if (scl == k->scl_i && sda == k->sda_i)
        return;
    if (!k->in_step)
        now_at(k, s->cycles > lag ? s->cycles - lag : 0);
    /* START and STOP: SDA moving while SCL stays high */
    if (scl && k->scl_i && sda != k->sda_i) {
        k->sda_i = sda;
        if (!sda) {
            sr_set(k, SR_BUS_BUSY, 1);
            if (!master) {
                k->sstate = S_ADDR;
                k->sbit = 0;
                k->sbyte = 0;
                k->spend = 0;
                if (k->sda_o == 0 || k->scl_o == 0)
                    lines(k, 1, 1);
            }
        } else { /* a STOP */
            sr_set(k, SR_BUS_BUSY, 0);
            raise(k, I_TRANS_COMPLETE);
            if (!master) {
                k->sstate = S_IDLE;
                sr_set(k, SR_ADDRESSED, 0);
                raise(k, I_SLAVE_TRAN_COMP);
                if (k->sda_o == 0)
                    lines(k, 1, 1);
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

static void fsm_reset(struct e32_i2c *k)
{
    k->phase = P_IDLE;
    k->ev = 0;
    k->sstate = S_IDLE;
    k->spend = 0;
    k->waiting_to = 0;
    lines(k, 1, 1);
    k->scl_i = scl_in(k);
    k->sda_i = sda_in(k);
}

static void i2c_tick(void *ctx, u32 cycles)
{
    struct e32_i2c *k = (struct e32_i2c *)ctx;
    struct esk_mach *s = k->s;
    (void)cycles;
    while (k->ev && k->ev_c <= s->cycles) {
        k->ev = 0;
        k->cur_c = k->ev_c;
        k->cur_f = k->ev_f;
        s->output_lag = (u32)(s->cycles - k->ev_c);
        k->in_step = 1;
        if (reg(k->ctr) & C_MS_MODE)
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
    struct e32_i2c *k = (struct e32_i2c *)ctx;
    u64 d;
    if (!k->ev)
        return 0;
    d = k->ev_c > k->s->cycles ? k->ev_c - k->s->cycles : 1u;
    *cycles = d > 0xffffffffu ? 0xffffffffu : (u32)d;
    return 1;
}

static u32 i2c_read(void *ctx, u32 off, int n)
{
    struct e32_i2c *k = (struct e32_i2c *)ctx;
    if ((off & ~3u) == 0x1Cu) { /* DATA: the RX FIFO */
        u32 b = k->rxram[k->rrd & 31u];
        if (!k->rcnt)
            return b;
        k->rrd = (k->rrd + 1u) & 31u;
        k->rcnt--;
        fifo_status(k);
        return b;
    }
    return esk_rf_read(k->p, off, n);
}

static void i2c_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_i2c *k = (struct e32_i2c *)ctx;
    u32 w = off & ~3u, was = reg(k->ctr);
    now_at(k, k->s->cycles);
    switch (w) {
    case 0x1Cu: /* DATA: the TX FIFO */
        if (k->tcnt >= 32u)
            return; /* full: the byte is lost */
        k->txram[k->twr & 31u] = (u8)(v & 0xffu);
        k->twr = (k->twr + 1u) & 31u;
        k->tcnt++;
        fifo_status(k);
        return;
    case 0x24u: /* INT_CLR */
        esk_rf_hw(k->raw, reg(k->raw) & ~v);
        fifo_status(k); /* the FIFO levels' interrupts raise again */
        return;
    case 0x08u:
    case 0x14u:
    case 0x2Cu:
        return; /* SR, RXFIFO_ST, INT_STATUS */
    default:
        break;
    }
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
    if (w == esk_rf_off(k->ctr)) {
        u32 c = reg(k->ctr);
        if ((c ^ was) & (C_SCL_FORCE | C_SDA_FORCE | C_MS_MODE))
            lines(k, k->scl_o, k->sda_o);
        if ((c & C_TRANS_START) && (c & C_MS_MODE) && apb_fs(k) > 0) {
            int held = k->phase == P_HELD;
            sr_set(k, SR_ARB_LOST | SR_TIME_OUT, 0);
            k->ci = 0;
            if (held) { /* SCL is low: the next command's pulse from now */
                k->phase = P_LOW0;
                next_command(k);
            } else if (k->phase == P_IDLE || k->phase == P_STOP_HOLD) {
                k->phase = P_IDLE;
                next_command(k);
            }
        }
        esk_rf_hw(k->ctr, c & ~C_TRANS_START); /* it clears itself */
    }
    clocking(k);
}

static void i2c_reset(void *ctx)
{
    struct e32_i2c *k = (struct e32_i2c *)ctx;
    k->twr = k->trd = k->tcnt = k->rwr = k->rrd = k->rcnt = 0;
    k->level = 0;
    esk_esp32_source(k->s, k->source, 0);
    k->clocked = 0;
    fsm_reset(k);
    fifo_status(k);
    update(k);
    clocking(k);
}

const struct esk_dev_ops esk_esp32_i2c_ops = {
    "esp32-i2c", i2c_read, i2c_write, i2c_reset, i2c_tick, i2c_next,
};

void *esk_esp32_i2c_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_i2c *k = (struct e32_i2c *)esk_mach_alloc(s, sizeof *k);
    static const struct {
        size_t at;
        const char *name;
    } regs[] = {
        { offsetof(struct e32_i2c, ctr), "CTR" },
        { offsetof(struct e32_i2c, sr), "SR" },
        { offsetof(struct e32_i2c, to), "TO" },
        { offsetof(struct e32_i2c, saddr), "SLAVE_ADDR" },
        { offsetof(struct e32_i2c, fifo_st), "RXFIFO_ST" },
        { offsetof(struct e32_i2c, fifo_conf), "FIFO_CONF" },
        { offsetof(struct e32_i2c, raw), "INT_RAW" },
        { offsetof(struct e32_i2c, ena), "INT_ENA" },
        { offsetof(struct e32_i2c, st), "INT_STATUS" },
        { offsetof(struct e32_i2c, low), "SCL_LOW_PERIOD" },
        { offsetof(struct e32_i2c, high), "SCL_HIGH_PERIOD" },
        { offsetof(struct e32_i2c, hold), "SDA_HOLD" },
        { offsetof(struct e32_i2c, sample), "SDA_SAMPLE" },
        { offsetof(struct e32_i2c, sth), "SCL_START_HOLD" },
        { offsetof(struct e32_i2c, rss), "SCL_RSTART_SETUP" },
        { offsetof(struct e32_i2c, sph), "SCL_STOP_HOLD" },
        { offsetof(struct e32_i2c, sps), "SCL_STOP_SETUP" },
        { offsetof(struct e32_i2c, sclf), "SCL_FILTER_CFG" },
    };
    char name[24];
    size_t i;
    int unit;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    unit = strcmp(p->name, "I2C1") == 0;
    k->source = unit ? 50 : 49;
    k->sig_scl = unit ? 95 : 29;
    k->sig_sda = unit ? 96 : 30;
    for (i = 0; i < sizeof regs / sizeof regs[0]; i++) {
        struct esk_rf_reg *r = esk_rf_reg(p, regs[i].name);
        if (!r)
            return 0;
        memcpy((char *)k + regs[i].at, &r, sizeof r);
    }
    for (i = 0; i < NCMD; i++) {
        snprintf(name, sizeof name, "COMD%d", (int)i);
        if (!(k->comd[i] = esk_rf_reg(p, name)))
            return 0;
    }
    if (esk_esp32_signal_watch(s, watch, k) != 0)
        return 0;
    return k;
}
