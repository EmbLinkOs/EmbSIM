/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* twi.c -- the ATmega328P's 2-wire serial interface as a master (datasheet
 * 22), over the register file, on its pins: SCL (PC5) and SDA (PC4)
 * open-drain while TWCR.TWEN is set, pulled low or let go to the pull-ups
 * (PORTC's own, when PORTC4 and PORTC5 are set, and the circuit's).
 *
 *   - the program drives the TWI by writing TWCR with TWINT set, which
 *     clears TWINT and starts the next step; TWINT sets when the step is
 *     done, with its status in TWSR's upper five bits, and while it is set
 *     the TWI holds SCL low. The steps are the datasheet's master
 *     transmitter and receiver tables:
 *       TWSTA: a START from a free bus (0x08) or a repeated START from a
 *         held one (0x10);
 *       after a START, TWDR goes out as SLA+R/W, ACKed or not: 0x18 or
 *         0x20 for a write, 0x40 or 0x48 for a read;
 *       in a write, TWDR goes out as data: 0x28 ACKed, 0x30 NACKed;
 *       in a read, a byte comes into TWDR and the master ACKs it when
 *         TWEA is set (0x50) or NACKs it (0x58);
 *       TWSTO: a STOP, after which TWSTO clears and TWINT does not set
 *         (TWSR 0xF8); with TWSTA too, a START follows it;
 *   - a write of TWDR while TWINT is clear is lost and sets TWWC; one while
 *     it is set clears TWWC;
 *   - the TWI vector is pending while TWINT and TWIE are set (entering it
 *     does not clear TWINT: the program does);
 *   - SCL's period is 16 + 2 * TWBR * 4^TWPS core cycles, half low and
 *     half high; SDA changes in the middle of SCL's low half and is read as
 *     SCL falls;
 *   - clearing TWEN ends what is under way and lets both lines go;
 *   - a step is a device event, at most one in an instruction.
 *
 * Not modelled: slave mode (TWAR, TWAMR and the slave statuses: the TWI
 * never answers an address), arbitration and its loss (0x38), bus errors
 * (0x00), clock stretching by a slave, the spike filter. */
#include <string.h>

#include "mcu/avr/avr.h"

#define TWIE 0x01u
#define TWEN 0x04u
#define TWWC 0x08u
#define TWSTO 0x10u
#define TWSTA 0x20u
#define TWEA 0x40u
#define TWINT 0x80u

#define PC 2
#define SDA 4
#define SCL 5

enum {
    T_IDLE,  /* the bus is free (to this TWI) */
    T_START, /* making a START or a repeated START */
    T_BYTE,  /* SLA+R/W, or a data byte, and its ACK bit */
    T_HOLD,  /* TWINT set: SCL held low */
    T_STOP,  /* making a STOP */
};

struct avr_twi {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *twbr, *twsr, *twdr, *twcr;
    int vec, running;
    int mode, step, bit;
    int scl, sda;     /* 0: pulled low, 1: let go */
    int pins_on;
    int active;       /* between our START and our STOP */
    int after_start;  /* the next byte is SLA+R/W */
    int reading;      /* SLA+R was ACKed */
    int sla_byte;     /* the byte under way is SLA+R/W */
    int ack;          /* a read byte's ACK (TWEA) */
    int start_after;  /* TWSTO with TWSTA: a START after the STOP */
    u8 out, shift;    /* the byte going out, and the bus's bits coming in */
    u64 left, since;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static int enabled(const struct avr_twi *k) { return (reg(k->twcr) & TWEN) != 0; }

static void pins(struct avr_twi *k)
{
    struct esk_mach *s = k->s;
    if (enabled(k)) {
        esk_avr_pin_force(s, PC, SCL, k->scl ? 2 : 0);
        esk_avr_pin_force(s, PC, SDA, k->sda ? 2 : 0);
        k->pins_on = 1;
    } else if (k->pins_on) {
        k->pins_on = 0;
        esk_avr_pin_force(s, PC, SCL, -1);
        esk_avr_pin_force(s, PC, SDA, -1);
    }
}

static void lines(struct avr_twi *k, int scl, int sda)
{
    if (k->scl == scl && k->sda == sda)
        return;
    k->scl = scl;
    k->sda = sda;
    pins(k);
}

static int sda_in(struct avr_twi *k) { return esk_avr_pin_read(k->s, PC, SDA); }

/* core cycles of SCL's low (0) or high (1) half */
static u64 phase(const struct avr_twi *k, int high)
{
    u64 t = 16u + 2u * (u64)(reg(k->twbr) & 0xffu) * ((u64)1 << (2u * (reg(k->twsr) & 3u)));
    return high ? t - t / 2u : t / 2u;
}

static u64 hold(const struct avr_twi *k)
{
    u64 h = phase(k, 0) / 2u;
    return h ? h : 1;
}

static void wait_n(struct avr_twi *k, u64 n)
{
    k->left = n ? n : 1;
    k->since = k->s->cycles;
    esk_rf_clock(k->p, &k->running, 1);
}

static void stop_clock(struct avr_twi *k) { esk_rf_clock(k->p, &k->running, 0); }

static void status(struct avr_twi *k, u32 code)
{
    esk_rf_hw(k->twsr, (code & 0xf8u) | (reg(k->twsr) & 3u));
}

/* a step done: TWINT sets with its status, and SCL stays low */
static void done(struct avr_twi *k, u32 code)
{
    status(k, code);
    esk_rf_hw(k->twcr, reg(k->twcr) | TWINT);
    k->mode = T_HOLD;
    stop_clock(k);
    esk_avr_irq_changed(k->s);
}

static void begin_start(struct avr_twi *k)
{
    k->mode = T_START;
    if (k->active) {
        k->step = 0; /* from a held bus: SDA up, SCL up, then the START */
        wait_n(k, hold(k));
    } else {
        k->step = 2; /* from a free bus: the START itself */
        wait_n(k, phase(k, 1));
    }
}

static void begin_stop(struct avr_twi *k)
{
    k->mode = T_STOP;
    k->step = 0;
    wait_n(k, hold(k));
}

static void begin_byte(struct avr_twi *k, int sla, u8 byte)
{
    k->mode = T_BYTE;
    k->sla_byte = sla;
    k->out = byte;
    k->shift = 0;
    k->bit = 0;
    k->step = 0;
    wait_n(k, hold(k));
}

/* the bus free again, or never taken: TWSTO clears */
static void freed(struct avr_twi *k)
{
    k->active = 0;
    k->after_start = 0;
    k->reading = 0;
    k->mode = T_IDLE;
    stop_clock(k);
    esk_rf_hw(k->twcr, reg(k->twcr) & ~(u64)TWSTO);
    status(k, 0xf8);
}

static void byte_done(struct avr_twi *k, int acked)
{
    esk_rf_hw(k->twdr, k->shift);
    if (k->sla_byte) {
        k->reading = k->out & 1u; /* the direction sent: no arbitration */
        if (k->reading)
            done(k, acked ? 0x40 : 0x48);
        else
            done(k, acked ? 0x18 : 0x20);
        if (!acked)
            k->reading = 0;
        return;
    }
    if (k->reading)
        done(k, k->ack ? 0x50 : 0x58);
    else
        done(k, acked ? 0x28 : 0x30);
}

static void step(struct avr_twi *k)
{
    switch (k->mode) {
    case T_START:
        if (k->step == 0) {
            lines(k, 0, 1);
            k->step = 1;
            wait_n(k, phase(k, 0) - hold(k));
        } else if (k->step == 1) {
            lines(k, 1, 1);
            k->step = 2;
            wait_n(k, phase(k, 1));
        } else if (k->step == 2) {
            lines(k, 1, 0);
            k->step = 3;
            wait_n(k, phase(k, 1));
        } else {
            int rep = k->active;
            lines(k, 0, 0);
            k->active = 1;
            k->after_start = 1;
            k->reading = 0;
            done(k, rep ? 0x10 : 0x08);
        }
        return;
    case T_STOP:
        if (k->step == 0) {
            lines(k, 0, 0);
            k->step = 1;
            wait_n(k, phase(k, 0) - hold(k));
        } else if (k->step == 1) {
            lines(k, 1, 0);
            k->step = 2;
            wait_n(k, phase(k, 1));
        } else {
            lines(k, 1, 1);
            freed(k);
            if (k->start_after) {
                k->start_after = 0;
                begin_start(k);
            }
        }
        return;
    case T_BYTE: {
        /* each bit: SDA set in SCL's low half (step 0), SCL high (1), SDA
         * read and SCL low (2); bit 8 is the ACK */
        int rx = k->reading && !k->sla_byte;
        if (k->step == 0) {
            int sda;
            if (k->bit < 8)
                sda = rx ? 1 : k->out >> (7 - k->bit) & 1;
            else
                sda = rx ? !k->ack : 1;
            lines(k, 0, sda);
            k->step = 1;
            wait_n(k, phase(k, 0) - hold(k));
            return;
        }
        if (k->step == 1) {
            lines(k, 1, k->sda);
            k->step = 2;
            wait_n(k, phase(k, 1));
            return;
        }
        {
            int v = sda_in(k);
            lines(k, 0, k->sda);
            if (k->bit < 8) {
                k->shift = (u8)(k->shift << 1 | (u8)v);
                k->bit++;
                k->step = 0;
                wait_n(k, hold(k));
                return;
            }
            byte_done(k, v == 0);
        }
        return;
    }
    default:
        stop_clock(k);
        return;
    }
}

/* TWCR written with TWINT: the next step */
static void go(struct avr_twi *k)
{
    u32 cr = reg(k->twcr);
    u32 st = reg(k->twsr) & 0xf8u;
    if (cr & TWSTO) {
        k->start_after = (cr & TWSTA) != 0;
        if (k->active) {
            begin_stop(k);
            return;
        }
        freed(k);
        if (k->start_after) {
            k->start_after = 0;
            begin_start(k);
        }
        return;
    }
    if (cr & TWSTA) {
        begin_start(k);
        return;
    }
    if (!k->active)
        return;
    if (k->after_start) {
        k->after_start = 0;
        begin_byte(k, 1, (u8)reg(k->twdr));
        return;
    }
    if (st == 0x18 || st == 0x20 || st == 0x28 || st == 0x30) {
        begin_byte(k, 0, (u8)reg(k->twdr));
    } else if (st == 0x40 || st == 0x50) {
        k->ack = (cr & TWEA) != 0;
        begin_byte(k, 0, 0xff);
    }
    /* 0x48 and 0x58 wait for a START or a STOP */
}

static void twi_tick(void *ctx, u32 cycles)
{
    struct avr_twi *k = (struct avr_twi *)ctx;
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

static int twi_next(void *ctx, u32 *cycles)
{
    struct avr_twi *k = (struct avr_twi *)ctx;
    if (!k->running)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)(k->left ? k->left : 1);
    return 1;
}

static u32 twi_read(void *ctx, u32 off, int n)
{
    struct avr_twi *k = (struct avr_twi *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void twi_write(void *ctx, u32 off, int n, u32 v)
{
    struct avr_twi *k = (struct avr_twi *)ctx;
    u32 cr = reg(k->twcr);
    if (off == esk_rf_off(k->twdr)) {
        if (cr & TWINT) {
            esk_rf_hw(k->twdr, v & 0xffu);
            esk_rf_hw(k->twcr, cr & ~(u64)TWWC);
        } else {
            esk_rf_hw(k->twcr, cr | TWWC);
        }
        return;
    }
    if (off == esk_rf_off(k->twsr)) {
        esk_rf_hw(k->twsr, (reg(k->twsr) & 0xf8u) | (v & 3u));
        return;
    }
    if (off != esk_rf_off(k->twcr)) {
        esk_rf_write(k->p, off, n, v);
        return;
    }
    {
        /* TWINT is cleared by a one; TWWC is the hardware's; TWSTO, once
         * set, holds until its STOP is made */
        u32 now = (v & (TWEA | TWSTA | TWEN | TWIE)) | (cr & TWWC) | ((cr | v) & TWSTO);
        if (!(v & TWINT))
            now |= cr & TWINT;
        esk_rf_hw(k->twcr, now);
    }
    if (!enabled(k)) {
        /* off: whatever was under way ends, the lines go */
        k->scl = k->sda = 1;
        k->start_after = 0;
        freed(k);
        pins(k);
        esk_avr_irq_changed(k->s);
        return;
    }
    pins(k);
    if ((v & TWINT) && (k->mode == T_IDLE || k->mode == T_HOLD))
        go(k);
    esk_avr_irq_changed(k->s);
}

static void twi_reset(void *ctx)
{
    struct avr_twi *k = (struct avr_twi *)ctx;
    k->running = 0;
    k->mode = T_IDLE;
    k->scl = k->sda = 1;
    k->active = k->after_start = k->reading = k->start_after = 0;
    k->left = 0;
    esk_rf_hw(k->twsr, 0xf8);
    if (k->pins_on) {
        k->pins_on = 0;
        esk_avr_pin_force(k->s, PC, SCL, -1);
        esk_avr_pin_force(k->s, PC, SDA, -1);
    }
}

static int twi_pending(void *ctx, int vec)
{
    struct avr_twi *k = (struct avr_twi *)ctx;
    ES_UNUSED(vec);
    return (reg(k->twcr) & (TWINT | TWIE)) == (TWINT | TWIE);
}

const struct esk_dev_ops esk_avr_twi_ops = {
    "avr-twi", twi_read, twi_write, twi_reset, twi_tick, twi_next,
};

void *esk_avr_twi_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct avr_twi k, *c;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    k.twbr = esk_rf_reg(p, "TWBR");
    k.twsr = esk_rf_reg(p, "TWSR");
    k.twdr = esk_rf_reg(p, "TWDR");
    k.twcr = esk_rf_reg(p, "TWCR");
    if (!k.twbr || !k.twsr || !k.twdr || !k.twcr)
        return 0;
    k.vec = esk_rf_irq(p, "TWI");
    k.scl = k.sda = 1;
    esk_rf_set_reset(k.twsr, 0xf8);
    esk_rf_set_reset(k.twdr, 0xff);
    c = (struct avr_twi *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    if (c->vec >= 0)
        esk_avr_irq_source(s, c->vec, twi_pending, 0, c);
    return c;
}
