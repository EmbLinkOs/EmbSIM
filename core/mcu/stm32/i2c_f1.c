/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* i2c_f1.c -- the STM32F1's I2C as a master (RM0008 26), over the register
 * file, on its pins: SCL and SDA open-drain, the circuit's pull-ups
 * bringing them high.
 *
 *   - CR1.START with PE makes a START (a repeated START after a byte):
 *     SDA falls while SCL is high, then SCL falls; SR1.SB, SR2.MSL and
 *     BUSY set and START clears. Writing DR with SB set sends the address
 *     byte; the slave's ACK sets ADDR (and SR2.TRA for a write), a NACK
 *     sets AF; reading SR2 after SR1 clears ADDR;
 *   - as a transmitter, TXE is set while DR is empty; a write of DR goes
 *     out MSB first, the slave's ACK clocked in after it; with DR empty at
 *     a byte's end BTF sets and SCL is held low until DR is written, STOP
 *     or START is asked for; a NACK sets AF and holds the bus likewise;
 *   - as a receiver, bytes are clocked in after ADDR clears, each
 *     acknowledged as CR1.ACK says when its ACK bit comes (with CR1.POS,
 *     as ACK said at the byte before: ST's two-byte reception); a byte goes
 *     to DR (RXNE) when DR is empty, else stays in the shift register with
 *     BTF set and SCL held low until DR is read; after a NACK the master
 *     clocks no more;
 *   - CR1.STOP makes the STOP after the byte under way, or at once when the
 *     bus is held: SDA rises while SCL is high; MSL and BUSY clear;
 *   - SR1's flags clear as RM0008 says: SB by a write of DR, ADDR by a read
 *     of SR2, BTF by an access to DR, RXNE by a read of it, TXE by a write;
 *     AF, ARLO, BERR and OVR are rc_w0. CR2's ITEVTEN and ITBUFEN make
 *     I2Cx_EV pending on SB, ADDR, BTF, TXE and RXNE, ITERREN I2Cx_ER on AF;
 *   - SCL's low and high times are CCR's in PCLK1 periods: equal in
 *     standard mode, 2:1 or (DUTY) 16:9 in fast mode (F/S);
 *   - the pins are AFIO's: I2C1 SCL PB6, SDA PB7 (remapped PB8, PB9), I2C2
 *     PB10, PB11; SDA is read from the circuit as SCL goes low, and
 *     changes a quarter of SCL's low time after it (the data hold time);
 *   - a step is a device event, at most one in an instruction.
 *
 * Not modelled: slave mode, 10-bit addresses, clock stretching by the
 * slave, arbitration, SMBus, PEC, DMA requests. */
#include <string.h>

#include "mcu/stm32/stm32.h"

#define CR1_PE (1u << 0)
#define CR1_START (1u << 8)
#define CR1_STOP (1u << 9)
#define CR1_ACK (1u << 10)
#define CR1_POS (1u << 11)
#define CR1_SWRST (1u << 15)
#define CR2_ITERREN (1u << 8)
#define CR2_ITEVTEN (1u << 9)
#define CR2_ITBUFEN (1u << 10)
#define SR1_SB (1u << 0)
#define SR1_ADDR (1u << 1)
#define SR1_BTF (1u << 2)
#define SR1_STOPF (1u << 4)
#define SR1_RXNE (1u << 6)
#define SR1_TXE (1u << 7)
#define SR1_BERR (1u << 8)
#define SR1_ARLO (1u << 9)
#define SR1_AF (1u << 10)
#define SR1_OVR (1u << 11)
#define SR1_CLEAR0 (SR1_BERR | SR1_ARLO | SR1_AF | SR1_OVR) /* rc_w0 */
#define SR2_MSL (1u << 0)
#define SR2_BUSY (1u << 1)
#define SR2_TRA (1u << 2)

/* what the master is doing */
enum {
    M_IDLE,   /* the bus is free */
    M_START,  /* making a START */
    M_ADDR,   /* sending the address byte */
    M_TX,     /* sending a data byte */
    M_RX,     /* receiving a data byte */
    M_HOLD,   /* SCL held low, waiting on the program */
    M_STOP,   /* making a STOP */
};

struct f1i2c {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr1, *cr2, *dr, *sr1, *sr2, *ccr;
    int unit, ev_irq, er_irq, running;
    int mode, step;   /* the activity and its step */
    int scl, sda;     /* 0: pulled low, 1: released */
    u32 shift;        /* the byte under way */
    int bit;
    int txmode;       /* the address said write */
    int dr_full;      /* DR holds a byte (to send, or received) */
    u32 dr_byte;
    int held;         /* a received byte waiting in the shift register */
    int ack_prev;     /* ACK at the last byte's ACK bit (POS) */
    int nacked;       /* the last received byte was NACKed: no more clocks */
    int acked;        /* the ACK bit just clocked */
    u64 left, since;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void set_sr1(struct f1i2c *k, u32 bits) { esk_rf_hw(k->sr1, k->sr1->value | bits); }
static void clr_sr1(struct f1i2c *k, u32 bits) { esk_rf_hw(k->sr1, k->sr1->value & ~(u64)bits); }
static void set_sr2(struct f1i2c *k, u32 bits) { esk_rf_hw(k->sr2, k->sr2->value | bits); }
static void clr_sr2(struct f1i2c *k, u32 bits) { esk_rf_hw(k->sr2, k->sr2->value & ~(u64)bits); }

static int pin_of(struct esk_mach *s, int unit, int sda)
{
    if (unit == 1)
        return 16 + ((esk_stm32f1_mapr(s) >> 1 & 1u) ? 8 : 6) + sda;
    if (unit == 2)
        return 16 + 10 + sda;
    return -1;
}

static const struct f1i2c *i2c_unit(struct esk_mach *s, int unit)
{
    char name[8] = "I2Cx";
    struct esk_rf_periph *p;
    name[3] = (char)('0' + unit);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_stm32f1_i2c_ops ? (const struct f1i2c *)p->ctx : 0;
}

int esk_stm32f1_i2c_pin(struct esk_mach *s, int port, int bit)
{
    int unit;
    for (unit = 1; unit <= 2; unit++) {
        const struct f1i2c *k = i2c_unit(s, unit);
        int at = port * 16 + bit;
        if (!k || !(reg(k->cr1) & CR1_PE))
            continue;
        if (at == pin_of(s, unit, 0))
            return k->scl;
        if (at == pin_of(s, unit, 1))
            return k->sda;
    }
    return -1;
}

static void lines(struct f1i2c *k, int scl, int sda)
{
    if (k->scl == scl && k->sda == sda)
        return;
    k->scl = scl;
    k->sda = sda;
    esk_stm32f1_port_changed(k->s, pin_of(k->s, k->unit, 0) / 16);
}

static int sda_in(struct f1i2c *k)
{
    struct esk_mach *s = k->s;
    int at = pin_of(s, k->unit, 1), level = 1;
    if (s->host.pin_in && s->host.pin_in(s->host.ctx, at / 16, at % 16, &level))
        return level != 0;
    return k->sda;
}

static void irq(struct f1i2c *k)
{
    u32 sr1 = reg(k->sr1), cr2 = reg(k->cr2);
    if ((cr2 & CR2_ITEVTEN) && ((sr1 & (SR1_SB | SR1_ADDR | SR1_BTF | SR1_STOPF)) ||
                               ((cr2 & CR2_ITBUFEN) && (sr1 & (SR1_TXE | SR1_RXNE)))))
        esk_rf_interrupt(k->p, k->ev_irq);
    if ((cr2 & CR2_ITERREN) && (sr1 & SR1_CLEAR0))
        esk_rf_interrupt(k->p, k->er_irq);
}

/* SCL's low or high time in core cycles */
static u64 period(const struct f1i2c *k, int high)
{
    u32 hclk = esk_stm32f1_hclk(k->s), pclk = esk_stm32f1_pclk(k->s, 1);
    u32 ratio = hclk && pclk ? hclk / pclk : 1, ccr = reg(k->ccr) & 0xfffu;
    u64 t;
    if (!ccr)
        ccr = 4;
    if (!(reg(k->ccr) & 0x8000u))
        t = ccr;                                  /* standard: Thigh = Tlow */
    else if (!(reg(k->ccr) & 0x4000u))
        t = high ? ccr : 2u * (u64)ccr;           /* fast, 2:1 */
    else
        t = high ? 9u * (u64)ccr : 16u * (u64)ccr; /* fast, 16:9 */
    return t * (ratio ? ratio : 1);
}

static void wait(struct f1i2c *k, int high)
{
    k->left = period(k, high);
    k->since = k->s->cycles;
    esk_rf_clock(k->p, &k->running, 1);
}

/* `quarters` of SCL's low time: its hold time and the rest of it */
static void wait_part(struct f1i2c *k, int quarters)
{
    k->left = period(k, 0) * (u64)quarters / 4u;
    if (!k->left)
        k->left = 1;
    k->since = k->s->cycles;
    esk_rf_clock(k->p, &k->running, 1);
}

static void idle_clock(struct f1i2c *k) { esk_rf_clock(k->p, &k->running, 0); }

static void begin_start(struct f1i2c *k)
{
    k->mode = M_START;
    k->step = 0;
    set_sr2(k, SR2_MSL | SR2_BUSY);
    wait(k, 0);
}

static void begin_stop(struct f1i2c *k)
{
    k->mode = M_STOP;
    k->step = 0;
    wait(k, 0);
}

static void begin_byte(struct f1i2c *k, int mode, u32 byte)
{
    k->mode = mode;
    k->shift = byte;
    k->bit = 0;
    k->step = 0;
    wait(k, 0);
}

/* the program's next request, with SCL held low */
static void hold(struct f1i2c *k)
{
    k->mode = M_HOLD;
    if (reg(k->cr1) & CR1_START) {
        begin_start(k);
        return;
    }
    if (reg(k->cr1) & CR1_STOP) {
        begin_stop(k);
        return;
    }
    idle_clock(k);
}

/* after a received byte: the next one, unless the program must act */
static void rx_next(struct f1i2c *k)
{
    if (k->nacked || k->held || (reg(k->cr1) & (CR1_START | CR1_STOP))) {
        hold(k);
        return;
    }
    begin_byte(k, M_RX, 0);
}

static void tx_next(struct f1i2c *k)
{
    if ((reg(k->sr1) & SR1_AF) || (reg(k->cr1) & (CR1_START | CR1_STOP)) || !k->dr_full) {
        if (!(reg(k->sr1) & SR1_AF) && !k->dr_full)
            set_sr1(k, SR1_BTF);
        irq(k);
        hold(k);
        return;
    }
    k->dr_full = 0;
    set_sr1(k, SR1_TXE);
    irq(k);
    begin_byte(k, M_TX, k->dr_byte);
}

/* a byte and its ACK bit are done */
static void byte_done(struct f1i2c *k, int acked)
{
    if (k->mode == M_ADDR) {
        if (acked) {
            set_sr1(k, SR1_ADDR);
            if (k->txmode)
                set_sr2(k, SR2_TRA);
            else
                clr_sr2(k, SR2_TRA);
        } else {
            set_sr1(k, SR1_AF);
        }
        irq(k);
        k->mode = M_HOLD;
        idle_clock(k); /* until ADDR clears */
        if (!acked)
            hold(k);
        return;
    }
    if (k->mode == M_TX) {
        if (!acked)
            set_sr1(k, SR1_AF);
        tx_next(k);
        return;
    }
    /* received */
    if (!k->dr_full) {
        k->dr_byte = k->shift;
        k->dr_full = 1;
        esk_rf_hw(k->dr, k->dr_byte);
        set_sr1(k, SR1_RXNE);
    } else {
        k->held = 1;
        set_sr1(k, SR1_BTF);
    }
    irq(k);
    rx_next(k);
}

/* SCL is low and the step's time has passed */
static void step(struct f1i2c *k)
{
    switch (k->mode) {
    case M_START:
        /* from a held bus: SDA released, SCL released, then SDA falls,
         * then SCL falls */
        if (k->step == 0) {
            lines(k, k->scl, 1);
            k->step = 1;
            wait(k, 0);
        } else if (k->step == 1) {
            lines(k, 1, 1);
            k->step = 2;
            wait(k, 1);
        } else if (k->step == 2) {
            lines(k, 1, 0);
            k->step = 3;
            wait(k, 1);
        } else {
            lines(k, 0, 0);
            esk_rf_hw(k->cr1, k->cr1->value & ~(u64)CR1_START);
            set_sr1(k, SR1_SB);
            k->mode = M_HOLD;
            irq(k);
            idle_clock(k); /* until DR is written */
        }
        return;
    case M_STOP:
        if (k->step == 0) {
            lines(k, 0, 0);
            k->step = 1;
            wait(k, 0);
        } else if (k->step == 1) {
            lines(k, 1, 0);
            k->step = 2;
            wait(k, 1);
        } else {
            lines(k, 1, 1);
            esk_rf_hw(k->cr1, k->cr1->value & ~(u64)CR1_STOP);
            clr_sr2(k, SR2_MSL | SR2_BUSY | SR2_TRA);
            clr_sr1(k, SR1_BTF | SR1_TXE);
            k->mode = M_IDLE;
            k->nacked = 0;
            idle_clock(k);
        }
        return;
    case M_ADDR:
    case M_TX:
    case M_RX: {
        /* each bit: SDA set with SCL low (step 0), SCL high (1), SDA read
         * and SCL low (2), a hold time before SDA changes again (3); bit 8
         * is the ACK */
        int rx = k->mode == M_RX;
        if (k->step == 0) {
            int sda;
            if (k->bit < 8)
                sda = rx ? 1 : (int)(k->shift >> (7 - k->bit) & 1u);
            else if (!rx)
                sda = 1;                                  /* the slave's ACK */
            else {
                u32 cr1 = reg(k->cr1);
                int ack = (cr1 & CR1_POS) ? k->ack_prev : (cr1 & CR1_ACK) != 0;
                k->ack_prev = (cr1 & CR1_ACK) != 0;
                k->nacked = !ack;
                sda = ack ? 0 : 1;
            }
            lines(k, 0, sda);
            k->step = 1;
            wait_part(k, 3);
            return;
        }
        if (k->step == 1) {
            lines(k, 1, k->sda);
            k->step = 2;
            wait(k, 1);
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
            wait_part(k, 1);
            return;
        }
        if (k->bit == 8) {
            if (rx)
                lines(k, 0, 1);
            byte_done(k, k->acked);
            return;
        }
        k->bit++;
        k->step = 0;
        step(k);
        return;
    }
    default:
        idle_clock(k);
        return;
    }
}

static void i2c_tick(void *ctx, u32 cycles)
{
    struct f1i2c *k = (struct f1i2c *)ctx;
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
    struct f1i2c *k = (struct f1i2c *)ctx;
    if (!k->running)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)(k->left ? k->left : 1);
    return 1;
}

static u32 i2c_read(void *ctx, u32 off, int n)
{
    struct f1i2c *k = (struct f1i2c *)ctx;
    u32 v = esk_rf_read(k->p, off, n);
    if (k->s->bus.debug)
        return v;
    if (off == esk_rf_off(k->sr2) && (reg(k->sr1) & SR1_ADDR)) {
        clr_sr1(k, SR1_ADDR);
        if (k->txmode) {
            if (k->dr_full) {
                tx_next(k);
            } else {
                set_sr1(k, SR1_TXE); /* waiting on DR, a STOP or a START */
                irq(k);
                hold(k);
            }
        } else {
            k->nacked = 0;
            k->ack_prev = 1;
            rx_next(k);
        }
    } else if (off == esk_rf_off(k->dr)) {
        clr_sr1(k, SR1_BTF);
        if (k->dr_full) {
            k->dr_full = 0;
            clr_sr1(k, SR1_RXNE);
            if (k->held) {
                /* the waiting byte moves to DR, and the bus goes on */
                k->held = 0;
                k->dr_byte = k->shift;
                k->dr_full = 1;
                esk_rf_hw(k->dr, k->dr_byte);
                set_sr1(k, SR1_RXNE);
                irq(k);
                if (k->mode == M_HOLD)
                    rx_next(k);
            }
        }
    }
    return v;
}

static void i2c_write(void *ctx, u32 off, int n, u32 v)
{
    struct f1i2c *k = (struct f1i2c *)ctx;
    if (off == esk_rf_off(k->sr1)) {
        /* rc_w0: the error flags written 0 clear */
        clr_sr1(k, ~v & SR1_CLEAR0);
        return;
    }
    if (off == esk_rf_off(k->dr)) {
        u32 b = v & 0xffu;
        clr_sr1(k, SR1_BTF);
        if (reg(k->sr1) & SR1_SB) {
            clr_sr1(k, SR1_SB);
            k->txmode = !(b & 1u);
            begin_byte(k, M_ADDR, b);
            return;
        }
        esk_rf_hw(k->dr, b);
        k->dr_byte = b;
        k->dr_full = 1;
        clr_sr1(k, SR1_TXE);
        if (k->mode == M_HOLD && k->txmode && !(reg(k->sr1) & (SR1_ADDR | SR1_AF)))
            tx_next(k);
        return;
    }
    esk_rf_write(k->p, off, n, v);
    if (off != esk_rf_off(k->cr1))
        return;
    if (reg(k->cr1) & CR1_SWRST) {
        k->mode = M_IDLE;
        esk_rf_hw(k->sr1, 0);
        esk_rf_hw(k->sr2, 0);
        idle_clock(k);
        lines(k, 1, 1);
        return;
    }
    if (!(reg(k->cr1) & CR1_PE)) {
        k->mode = M_IDLE;
        idle_clock(k);
        lines(k, 1, 1);
        esk_stm32f1_port_changed(k->s, pin_of(k->s, k->unit, 0) / 16);
        return;
    }
    esk_stm32f1_port_changed(k->s, pin_of(k->s, k->unit, 0) / 16);
    if (k->mode == M_IDLE && (reg(k->cr1) & CR1_START)) {
        k->dr_full = 0;
        k->held = 0;
        begin_start(k);
    } else if (k->mode == M_HOLD && !(reg(k->sr1) & (SR1_SB | SR1_ADDR)) && (reg(k->cr1) & (CR1_START | CR1_STOP))) {
        hold(k);
    } else if (k->mode == M_IDLE && (reg(k->cr1) & CR1_STOP)) {
        esk_rf_hw(k->cr1, k->cr1->value & ~(u64)CR1_STOP);
    }
}

static void i2c_reset(void *ctx)
{
    struct f1i2c *k = (struct f1i2c *)ctx;
    k->running = 0;
    k->mode = M_IDLE;
    k->scl = k->sda = 1;
    k->dr_full = k->held = k->nacked = 0;
    k->left = 0;
}

const struct esk_dev_ops esk_stm32f1_i2c_ops = {
    "stm32f1-i2c", i2c_read, i2c_write, i2c_reset, i2c_tick, i2c_next,
};

void *esk_stm32f1_i2c_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct f1i2c k, *c;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    k.cr1 = esk_rf_reg(p, "CR1");
    k.cr2 = esk_rf_reg(p, "CR2");
    k.dr = esk_rf_reg(p, "DR");
    k.sr1 = esk_rf_reg(p, "SR1");
    k.sr2 = esk_rf_reg(p, "SR2");
    k.ccr = esk_rf_reg(p, "CCR");
    if (!k.cr1 || !k.cr2 || !k.dr || !k.sr1 || !k.sr2 || !k.ccr || strncmp(p->name, "I2C", 3) ||
        p->name[3] < '1' || p->name[3] > '2')
        return 0;
    k.unit = p->name[3] - '0';
    k.ev_irq = esk_rf_irq(p, "_EV");
    k.er_irq = esk_rf_irq(p, "_ER");
    k.scl = k.sda = 1;
    c = (struct f1i2c *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    return c;
}
