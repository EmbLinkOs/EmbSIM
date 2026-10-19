/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* i2c.c -- an STM32 I2C of the SR1/SR2 layout (RM0008 26, RM0390 27: the
 * F1, F2 and F4's), master and slave, over the register file, on its pins:
 * SCL and SDA open-drain, the circuit's pull-ups bringing them high.
 *
 * The master:
 *   - CR1.START with PE makes a START (a repeated START after a byte),
 *     once the bus is free (it waits for another master's STOP): SDA falls
 *     while SCL is high, then SCL falls; SR1.SB and SR2.MSL set and START
 *     clears. Writing DR with SB set sends the address byte; the slave's
 *     ACK sets ADDR (and SR2.TRA for a write), a NACK sets AF; reading SR2
 *     after SR1 clears ADDR. A 10-bit header (11110xx0) sets ADD10 instead,
 *     and the address's second byte written to DR then sets ADDR; a read
 *     header (11110xx1) after a repeated START sets ADDR, receiving;
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
 *     bus is held: SDA rises while SCL is high; MSL clears;
 *   - SCL's low and high times are CCR's in PCLK1 periods: equal in
 *     standard mode, 2:1 or (DUTY) 16:9 in fast mode (F/S); the high time
 *     counts from SCL seen high, so a slave holding SCL low stretches the
 *     clock; SDA is read from the circuit as SCL goes low and changes a
 *     quarter of SCL's low time after it (the data hold time);
 *   - a 1 sent while SDA reads 0 (another master's 0) loses the
 *     arbitration: ARLO sets, MSL clears, the lines are let go, and the
 *     interface listens on as a slave;
 *   - a step is a device event, at most one in an instruction.
 *
 * The slave (PE, not the master):
 *   - it watches the bus on its pins: a START or repeated START, the bits
 *     on SCL's rising edges, a STOP; SR2.BUSY is set from a START to a
 *     STOP whoever makes them; a START or STOP inside a byte of a transfer
 *     it takes part in is BERR;
 *   - the address byte after a START is acknowledged (with CR1.ACK) when it
 *     is OAR1's 7-bit address, OAR2's with ENDUAL (DUALF), 0 with ENGC
 *     (GENCALL), the SMBus device default address 1100001 (SMBDEFAULT:
 *     SMBUS, ENARP, a device) or the host address 0001000 (SMBHOST: SMBUS,
 *     ENARP, a host), the alert response address 0001100 with ALERT set
 *     (SMBALERT), or OAR1's 10-bit address (ADDMODE: the header then the
 *     second byte; a read header after a repeated START once addressed);
 *     ADDR and TRA set after the ACK, and SCL is held low until ADDR clears
 *     (reading SR1 then SR2);
 *   - receiving, each byte is acknowledged as CR1.ACK says and goes to DR
 *     (RXNE) after its ACK bit; with DR still full the byte waits (BTF) and
 *     SCL is held low until DR is read;
 *   - transmitting, DR's byte goes out after ADDR clears (SCL held low
 *     until it is written); a byte acknowledged by the master is followed
 *     by DR's next (BTF and SCL held low while DR is empty), a NACK sets AF
 *     and lets SDA go;
 *   - with NOSTRETCH the clock is never held: a byte received while RXNE is
 *     set is lost, and a byte due while DR is empty is the last one again,
 *     both setting OVR;
 *   - a STOP after the slave was addressed sets STOPF (cleared by reading
 *     SR1 then writing CR1).
 *
 * Both:
 *   - SR1's flags clear as the manuals say: SB by a write of DR after SR1
 *     is read, ADDR by a read of SR2 after SR1, ADD10 by a write of DR,
 *     BTF by an access to DR, RXNE by a read of it, TXE by a write; BERR,
 *     ARLO, AF, OVR, PECERR, TIMEOUT and SMBALERT are rc_w0. CR2's ITEVTEN
 *     and ITBUFEN keep I2Cx_EV pending while SB, ADDR, ADD10, STOPF, BTF,
 *     TXE or RXNE is set, ITERREN I2Cx_ER while an error flag is;
 *   - DMAEN makes the DMA requests I2Cx_TX (while TXE) and I2Cx_RX (while
 *     RXNE); with LAST, the byte received after the DMA's next-to-last item
 *     (EOT-1) is NACKed;
 *   - ENPEC: the PEC (CRC-8, x^8 + x^2 + x + 1) of the bytes of a transfer
 *     the interface takes part in, addresses included, in SR2.PEC; CR1.PEC
 *     (or a DMA's end with PECEN) sends it after the last byte, or takes
 *     the next byte received as the PEC, which a mismatch NACKs with
 *     PECERR; it starts again after a STOP and after the PEC;
 *   - SMBus (SMBUS): SCL low for 25 ms sets TIMEOUT: a slave lets the bus
 *     go, a master makes a STOP; a host with ALERT sets SMBALERT on the
 *     SMBA pin's falling edge, a device with ALERT pulls SMBA low;
 *   - SWRST holds the interface in reset (its registers at their reset
 *     values, the lines let go, BUSY clear) until it is cleared; PE 0 lets
 *     the lines go and stops the interface;
 *   - the pins are the alternate functions the GPIO selects (the F4's AFR)
 *     or AFIO's (the F1: I2C1 SCL PB6, SDA PB7, remapped PB8, PB9, SMBA
 *     PB5; I2C2 PB10, PB11, SMBA PB12); the levels are the pins', the
 *     interface's own and the circuit's together.
 *
 * Not modelled: FLTR's analog and digital noise filters (the circuit's
 * edges are taken as they come: no spike to filter), the SMBus's
 * cumulative clock low extensions (TLOW:SEXT, TLOW:MEXT), clock
 * synchronization between two masters driving SCL together (one master's
 * clock wins), the master's rise time compensation (TRISE), CR2.FREQ
 * (the times come from the real PCLK1). */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

#define CR1_PE (1u << 0)
#define CR1_SMBUS (1u << 1)
#define CR1_SMBTYPE (1u << 3)
#define CR1_ENARP (1u << 4)
#define CR1_ENPEC (1u << 5)
#define CR1_ENGC (1u << 6)
#define CR1_NOSTRETCH (1u << 7)
#define CR1_START (1u << 8)
#define CR1_STOP (1u << 9)
#define CR1_ACK (1u << 10)
#define CR1_POS (1u << 11)
#define CR1_PEC (1u << 12)
#define CR1_ALERT (1u << 13)
#define CR1_SWRST (1u << 15)
#define CR2_ITERREN (1u << 8)
#define CR2_ITEVTEN (1u << 9)
#define CR2_ITBUFEN (1u << 10)
#define CR2_DMAEN (1u << 11)
#define CR2_LAST (1u << 12)
#define SR1_SB (1u << 0)
#define SR1_ADDR (1u << 1)
#define SR1_BTF (1u << 2)
#define SR1_ADD10 (1u << 3)
#define SR1_STOPF (1u << 4)
#define SR1_RXNE (1u << 6)
#define SR1_TXE (1u << 7)
#define SR1_BERR (1u << 8)
#define SR1_ARLO (1u << 9)
#define SR1_AF (1u << 10)
#define SR1_OVR (1u << 11)
#define SR1_PECERR (1u << 12)
#define SR1_TIMEOUT (1u << 14)
#define SR1_SMBALERT (1u << 15)
#define SR1_ERRORS (SR1_BERR | SR1_ARLO | SR1_AF | SR1_OVR | SR1_PECERR | SR1_TIMEOUT | SR1_SMBALERT)
#define SR2_MSL (1u << 0)
#define SR2_BUSY (1u << 1)
#define SR2_TRA (1u << 2)
#define SR2_GENCALL (1u << 4)
#define SR2_SMBDEFAULT (1u << 5)
#define SR2_SMBHOST (1u << 6)
#define SR2_DUALF (1u << 7)
#define SR2_SLAVE (SR2_TRA | SR2_GENCALL | SR2_SMBDEFAULT | SR2_SMBHOST | SR2_DUALF)
#define OAR1_ADDMODE (1u << 15)
#define OAR2_ENDUAL (1u << 0)

enum { F_SCL, F_SDA, F_SMBA };
static const char *const fn_names[3] = { "SCL", "SDA", "SMBA" };

/* what the master is doing */
enum {
    M_IDLE,   /* not the master */
    M_START,  /* making a START */
    M_ADDR,   /* sending an address byte */
    M_TX,     /* sending a data byte */
    M_RX,     /* receiving a data byte */
    M_HOLD,   /* SCL held low, waiting on the program */
    M_STOP,   /* making a STOP */
};

/* what the slave is doing */
enum {
    S_NONE,   /* not addressed */
    S_HDR10,  /* its 10-bit header acknowledged: the second byte next */
    S_RX,     /* receiving */
    S_TX,     /* transmitting */
    S_DONE,   /* the master NACKed: waiting for a STOP or START */
};

/* why the slave holds SCL low */
enum { H_NONE, H_ADDR, H_TXE, H_BTF };

struct i2c {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr1, *cr2, *oar1, *oar2, *dr, *sr1, *sr2, *ccr;
    int unit, ev_irq, er_irq, running;
    char sig_tx[16], sig_rx[16]; /* its DMA requests */
    /* the lines it drives: 1 let go, 0 low */
    int m_scl, m_sda, s_scl, s_sda;
    int sr1_read;        /* SR1 was read: the next step of a clearing sequence */
    /* DR, both roles */
    int dr_full;         /* DR holds a byte (to send, or received) */
    u32 dr_byte;
    int held;            /* a received byte waiting in the shift register */
    /* the master */
    int mode, step;
    u32 shift;           /* the byte under way */
    int bit;
    int txmode;          /* the address said write */
    int ten;             /* a 10-bit write header went out: ADD10 */
    int ack_prev;        /* ACK at the last byte's ACK bit (POS) */
    int nacked;          /* the last received byte was NACKed: no more clocks */
    int acked;           /* the ACK bit just clocked */
    int start_wait;      /* a START waiting for the bus to be free */
    int scl_wait;        /* SCL let go, a slave holding it low */
    int stepping;        /* `left` counts toward the master's next step */
    int pec_byte;        /* the master's byte under way is the PEC */
    u64 left, since;
    /* DMA and PEC */
    int last_nack;       /* LAST and the DMA's EOT-1: NACK the next byte */
    int pec_tx_end, pec_rx_end; /* the DMA's end with PECEN: the PEC next */
    u32 pec;
    /* the bus as its pins show it */
    int in_scl, in_sda, in_smba;
    int b_busy, b_n, b_first, b_ack;
    u32 b_shift;
    /* the slave */
    int sl, sl_hold, sl_ten, sl_addressed, sl_rw, sl_ack, sl_pec;
    u32 sl_shift, sl_flags;
    int sl_match;        /* the address byte matched: ADDR after its ACK */
    /* SMBus's timeout */
    int to_on;
    u64 to_at;
};

static u32 reg(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }

static void set_sr1(struct i2c *k, u32 bits) { esk_rf_hw(k->sr1, k->sr1->value | bits); }
static void clr_sr1(struct i2c *k, u32 bits) { esk_rf_hw(k->sr1, k->sr1->value & ~(u64)bits); }
static void set_sr2(struct i2c *k, u32 bits) { esk_rf_hw(k->sr2, k->sr2->value | bits); }
static void clr_sr2(struct i2c *k, u32 bits) { esk_rf_hw(k->sr2, k->sr2->value & ~(u64)bits); }
static int on(const struct i2c *k) { return (reg(k->cr1) & (CR1_PE | CR1_SWRST)) == CR1_PE; }
static int master(const struct i2c *k) { return (reg(k->sr2) & SR2_MSL) != 0; }

/* ---- the pins ------------------------------------------------------------------ */

/* pin `fn` of the unit: port * 16 + bit, or -1 */
static int pin_of(const struct i2c *k, int fn)
{
    struct esk_mach *s = k->s;
    if (s->rf && s->rf->naf) {
        char sig[24];
        int at;
        snprintf(sig, sizeof sig, "I2C%d_%s", k->unit, fn_names[fn]);
        at = esk_stm32f4_af_pin(s, sig);
        return at < 0 ? -1 : at / 32 * 16 + at % 32;
    }
    /* RM0008 table 27 and 9.3.9 */
    if (k->unit == 1)
        return fn == F_SMBA ? 16 + 5 : 16 + ((esk_stm32f1_mapr(s) >> 1 & 1u) ? 8 : 6) + fn;
    if (k->unit == 2)
        return fn == F_SMBA ? 16 + 12 : 16 + 10 + fn;
    return -1;
}

/* the level on pin `fn`, as its GPIO reads it: this interface's drive and
 * the circuit's together (no pull-up: low) */
static int line(const struct i2c *k, int fn)
{
    struct esk_mach *s = k->s;
    int at = pin_of(k, fn);
    char name[8] = "GPIOx";
    struct esk_rf_periph *p;
    struct esk_rf_reg *idr;
    if (at < 0)
        return 1;
    name[4] = (char)('A' + at / 16);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    idr = p ? esk_rf_reg(p, "IDR") : 0;
    return idr ? (int)(idr->value >> (at % 16) & 1u) : 1;
}

/* the level it drives on pin `fn`: 1 let go (open-drain), 0, or -1 off */
static int drive(const struct i2c *k, int fn)
{
    if (!on(k))
        return -1;
    if (fn == F_SCL)
        return k->m_scl && k->s_scl;
    if (fn == F_SDA)
        return k->m_sda && k->s_sda;
    /* SMBA: a device's alert, in SMBus mode only */
    if (!(reg(k->cr1) & CR1_SMBUS))
        return -1;
    return (reg(k->cr1) & (CR1_SMBTYPE | CR1_ALERT)) == CR1_ALERT ? 0 : 1;
}

static void pins(struct i2c *k)
{
    int done[3], n = 0, fn, i;
    for (fn = 0; fn < 3; fn++) {
        int at = pin_of(k, fn), port = at / 16, seen = 0;
        if (at < 0)
            continue;
        for (i = 0; i < n; i++)
            seen |= done[i] == port;
        if (seen)
            continue;
        done[n++] = port;
        if (k->s->rf && k->s->rf->naf)
            esk_stm32f4_port_changed(k->s, port);
        else
            esk_stm32f1_port_changed(k->s, port);
    }
}

static void lines(struct i2c *k, int scl, int sda)
{
    if (k->m_scl == scl && k->m_sda == sda)
        return;
    k->m_scl = scl;
    k->m_sda = sda;
    pins(k);
}

static void slave_lines(struct i2c *k, int scl, int sda)
{
    if (k->s_scl == scl && k->s_sda == sda)
        return;
    k->s_scl = scl;
    k->s_sda = sda;
    pins(k);
}

/* ---- flags, interrupts, DMA, PEC ---------------------------------------------- */

static void update(struct i2c *k)
{
    u32 sr1, cr2 = reg(k->cr2);
    int dma = on(k) && (cr2 & CR2_DMAEN);
    esk_stm32_dma_request(k->s, k->sig_tx, dma && (reg(k->sr1) & SR1_TXE));
    esk_stm32_dma_request(k->s, k->sig_rx, dma && (reg(k->sr1) & SR1_RXNE));
    sr1 = reg(k->sr1);
    if ((cr2 & CR2_ITEVTEN) && ((sr1 & (SR1_SB | SR1_ADDR | SR1_ADD10 | SR1_BTF | SR1_STOPF)) ||
                               ((cr2 & CR2_ITBUFEN) && (sr1 & (SR1_TXE | SR1_RXNE)))))
        esk_rf_interrupt(k->p, k->ev_irq);
    if ((cr2 & CR2_ITERREN) && (sr1 & SR1_ERRORS))
        esk_rf_interrupt(k->p, k->er_irq);
}

static void pec_add(struct i2c *k, u32 byte)
{
    int i;
    u32 c = (k->pec ^ byte) & 0xffu;
    for (i = 0; i < 8; i++)
        c = (c & 0x80u) ? ((c << 1) ^ 0x07u) & 0xffu : (c << 1) & 0xffu;
    k->pec = c;
    esk_rf_hw(k->sr2, (k->sr2->value & ~(u64)0xff00u) | (u64)c << 8);
}

static void pec_reset(struct i2c *k)
{
    k->pec = 0;
    k->pec_tx_end = k->pec_rx_end = 0;
    esk_rf_hw(k->sr2, k->sr2->value & ~(u64)0xff00u);
}

/* the PEC comes next: CR1.PEC, or a DMA's end with PECEN */
static int pec_due(const struct i2c *k, int rx)
{
    return (reg(k->cr1) & CR1_ENPEC) && ((reg(k->cr1) & CR1_PEC) || (rx ? k->pec_rx_end : k->pec_tx_end));
}

static void pec_done(struct i2c *k)
{
    esk_rf_hw(k->cr1, k->cr1->value & ~(u64)CR1_PEC);
    pec_reset(k);
}

/* ---- the master ---------------------------------------------------------------- */

/* SCL's low or high time in core cycles */
static u64 period(const struct i2c *k, int high)
{
    u32 core = esk_mach_core_hz(k->s), pclk = esk_stm32_pclk(k->s, 1);
    u32 ratio = core && pclk && core >= pclk ? core / pclk : 1, ccr = reg(k->ccr) & 0xfffu;
    u64 t;
    if (!ccr)
        ccr = 4;
    if (!(reg(k->ccr) & 0x8000u))
        t = ccr;                                   /* standard: Thigh = Tlow */
    else if (!(reg(k->ccr) & 0x4000u))
        t = high ? ccr : 2u * (u64)ccr;            /* fast, 2:1 */
    else
        t = high ? 9u * (u64)ccr : 16u * (u64)ccr; /* fast, 16:9 */
    return t * ratio;
}

static void clock_run(struct i2c *k) { esk_rf_clock(k->p, &k->running, k->stepping || k->to_on); }

static void wait_for(struct i2c *k, u64 cycles)
{
    k->left = cycles ? cycles : 1;
    k->since = k->s->cycles;
    k->stepping = 1;
    clock_run(k);
}

static void wait(struct i2c *k, int high) { wait_for(k, period(k, high)); }

/* `quarters` of SCL's low time: its hold time and the rest of it */
static void wait_part(struct i2c *k, int quarters) { wait_for(k, period(k, 0) * (u64)quarters / 4u); }

static void idle_clock(struct i2c *k)
{
    k->stepping = 0;
    clock_run(k);
}

/* SCL let go: its high time counts once it is high (a slave may hold it) */
static void wait_high(struct i2c *k)
{
    if (line(k, F_SCL)) {
        wait(k, 1);
        return;
    }
    k->scl_wait = 1;
    idle_clock(k);
}

static void begin_start(struct i2c *k)
{
    if (k->b_busy && !master(k)) {
        k->start_wait = 1; /* another master's transfer: after its STOP */
        return;
    }
    k->start_wait = 0;
    k->mode = M_START;
    k->step = 0;
    set_sr2(k, SR2_MSL);
    wait(k, 0);
}

static void begin_stop(struct i2c *k)
{
    k->mode = M_STOP;
    k->step = 0;
    wait(k, 0);
}

static void begin_byte(struct i2c *k, int mode, u32 byte)
{
    k->mode = mode;
    k->shift = byte;
    k->bit = 0;
    k->step = 0;
    wait(k, 0);
}

/* the program's next request, with SCL held low */
static void hold(struct i2c *k)
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
static void rx_next(struct i2c *k)
{
    if (k->nacked || k->held || (reg(k->cr1) & (CR1_START | CR1_STOP))) {
        hold(k);
        return;
    }
    begin_byte(k, M_RX, 0);
}

static void tx_next(struct i2c *k)
{
    if ((reg(k->sr1) & SR1_AF) || (reg(k->cr1) & (CR1_START | CR1_STOP))) {
        update(k);
        hold(k);
        return;
    }
    if (!k->dr_full) {
        if (pec_due(k, 0)) {
            k->pec_byte = 1;
            begin_byte(k, M_TX, k->pec);
            return;
        }
        set_sr1(k, SR1_BTF);
        update(k);
        hold(k);
        return;
    }
    k->dr_full = 0;
    set_sr1(k, SR1_TXE);
    begin_byte(k, M_TX, k->dr_byte);
    update(k);
}

/* the arbitration is lost: the lines let go, a slave from now on */
static void lost(struct i2c *k)
{
    set_sr1(k, SR1_ARLO);
    clr_sr2(k, SR2_MSL | SR2_TRA);
    k->mode = M_IDLE;
    k->scl_wait = 0;
    idle_clock(k);
    lines(k, 1, 1);
    update(k);
}

/* the master's byte received: to DR, or waiting in the shift register */
static void master_received(struct i2c *k)
{
    if (!k->dr_full) {
        k->dr_byte = k->shift;
        k->dr_full = 1;
        esk_rf_hw(k->dr, k->dr_byte);
        set_sr1(k, SR1_RXNE);
    } else {
        k->held = 1;
        set_sr1(k, SR1_BTF);
    }
}

/* a byte and its ACK bit are done */
static void byte_done(struct i2c *k, int acked)
{
    if (k->mode == M_ADDR) {
        int hdr = (k->shift & 0xf8u) == 0xf0u;
        if (!acked) {
            set_sr1(k, SR1_AF);
            k->ten = 0;
        } else if (hdr && !(k->shift & 1u)) {
            set_sr1(k, SR1_ADD10); /* the second byte next */
            k->ten = 1;
        } else {
            set_sr1(k, SR1_ADDR);
            if (k->txmode)
                set_sr2(k, SR2_TRA);
            else
                clr_sr2(k, SR2_TRA);
        }
        k->mode = M_HOLD;
        idle_clock(k); /* until ADDR clears or DR is written */
        update(k);
        if (!acked)
            hold(k);
        return;
    }
    if (k->mode == M_TX) {
        if (!acked)
            set_sr1(k, SR1_AF);
        if (k->pec_byte) {
            k->pec_byte = 0;
            pec_done(k);
        }
        tx_next(k);
        return;
    }
    master_received(k);
    update(k);
    rx_next(k);
}

/* SCL is low (or let go) and the step's time has passed */
static void step(struct i2c *k)
{
    switch (k->mode) {
    case M_START:
        /* from a held bus: SDA let go, SCL let go, then SDA falls, then
         * SCL falls */
        if (k->step == 0) {
            lines(k, k->m_scl, 1);
            k->step = 1;
            wait(k, 0);
        } else if (k->step == 1) {
            lines(k, 1, 1);
            k->step = 2;
            wait_high(k);
        } else if (k->step == 2) {
            lines(k, 1, 0);
            k->step = 3;
            wait(k, 1);
        } else {
            lines(k, 0, 0);
            esk_rf_hw(k->cr1, k->cr1->value & ~(u64)CR1_START);
            set_sr1(k, SR1_SB);
            k->mode = M_HOLD;
            idle_clock(k); /* until DR is written */
            update(k);
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
            wait_high(k);
        } else {
            lines(k, 1, 1);
            esk_rf_hw(k->cr1, k->cr1->value & ~(u64)CR1_STOP);
            clr_sr2(k, SR2_MSL | SR2_TRA);
            clr_sr1(k, SR1_BTF | SR1_TXE);
            k->mode = M_IDLE;
            k->nacked = 0;
            k->ten = 0;
            idle_clock(k);
            update(k);
        }
        return;
    case M_ADDR:
    case M_TX:
    case M_RX: {
        /* each bit: SDA set with SCL low (step 0), SCL let go (1), SDA
         * read and SCL low (2), a hold time before SDA changes again (3);
         * bit 8 is the ACK */
        int rx = k->mode == M_RX;
        if (k->step == 0) {
            int sda;
            if (k->bit < 8)
                sda = rx ? 1 : (int)(k->shift >> (7 - k->bit) & 1u);
            else if (!rx)
                sda = 1; /* the slave's ACK */
            else {
                u32 cr1 = reg(k->cr1);
                int ack = (cr1 & CR1_POS) ? k->ack_prev : (cr1 & CR1_ACK) != 0;
                k->ack_prev = (cr1 & CR1_ACK) != 0;
                if (k->last_nack)
                    ack = 0;
                if (pec_due(k, 1)) {
                    /* this byte is the PEC: a mismatch is NACKed */
                    if (k->shift != k->pec) {
                        set_sr1(k, SR1_PECERR);
                        ack = 0;
                    }
                    k->pec_byte = 1;
                } else if (reg(k->cr1) & CR1_ENPEC) {
                    pec_add(k, k->shift);
                }
                k->nacked = !ack;
                sda = ack ? 0 : 1;
            }
            lines(k, 0, sda);
            k->step = 1;
            wait_part(k, 3);
            return;
        }
        if (k->step == 1) {
            lines(k, 1, k->m_sda);
            k->step = 2;
            wait_high(k);
            return;
        }
        if (k->step == 2) {
            int v = line(k, F_SDA);
            if (k->bit < 8 && !rx && k->m_sda && !v) {
                lost(k); /* another master's 0 */
                return;
            }
            if (k->bit < 8 && rx)
                k->shift = (k->shift << 1 | (u32)v) & 0xffu;
            if (k->bit == 8)
                k->acked = rx ? 1 : v == 0;
            lines(k, 0, k->m_sda);
            k->step = 3;
            wait_part(k, 1);
            return;
        }
        if (k->bit == 7 && !rx && (reg(k->cr1) & CR1_ENPEC) && !k->pec_byte)
            pec_add(k, k->shift);
        if (k->bit == 8) {
            if (rx) {
                lines(k, 0, 1);
                if (k->pec_byte) {
                    k->pec_byte = 0;
                    pec_done(k);
                }
                if (k->last_nack && k->nacked)
                    k->last_nack = 0;
            }
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

/* ---- the slave ------------------------------------------------------------------ */

static void slave_release(struct i2c *k)
{
    k->sl_hold = H_NONE;
    slave_lines(k, 1, 1);
}

/* the slave's next byte to send: DR's, the PEC, or (NOSTRETCH, DR empty)
 * the last one again; 0, or 1 with SCL held while DR is empty */
static int slave_load(struct i2c *k, int after_byte)
{
    if (k->dr_full) {
        k->sl_shift = k->dr_byte;
        k->dr_full = 0;
        set_sr1(k, SR1_TXE);
        k->sl_pec = 0;
        return 0;
    }
    if (pec_due(k, 0)) {
        k->sl_shift = k->pec;
        k->sl_pec = 1;
        return 0;
    }
    if (reg(k->cr1) & CR1_NOSTRETCH) {
        set_sr1(k, SR1_OVR); /* an underrun: the same byte again */
        return 0;
    }
    set_sr1(k, after_byte ? SR1_TXE | SR1_BTF : SR1_TXE);
    k->sl_hold = H_TXE;
    return 1;
}

/* the slave's first bit, SCL as it is (low) */
static void slave_send(struct i2c *k)
{
    slave_lines(k, k->s_scl, (int)(k->sl_shift >> 7 & 1u));
}

/* the slave's received byte after its ACK bit */
static void slave_received(struct i2c *k)
{
    if (!k->dr_full) {
        k->dr_byte = k->sl_shift;
        k->dr_full = 1;
        esk_rf_hw(k->dr, k->dr_byte);
        set_sr1(k, SR1_RXNE);
    } else if (reg(k->cr1) & CR1_NOSTRETCH) {
        set_sr1(k, SR1_OVR); /* lost */
    } else {
        k->held = 1;
        set_sr1(k, SR1_BTF);
        k->sl_hold = H_BTF;
        slave_lines(k, 0, 1);
    }
}

/* the address byte after a START: matched (and acknowledged) or not */
static void slave_address(struct i2c *k, u32 b)
{
    u32 cr1 = reg(k->cr1), oar1 = reg(k->oar1), oar2 = reg(k->oar2), a = b >> 1;
    u32 flags = 0;
    int match = 0;
    k->sl_match = 0;
    if ((b & 0xf8u) == 0xf0u) { /* a 10-bit header */
        if (!(oar1 & OAR1_ADDMODE) || (b >> 1 & 3u) != (oar1 >> 8 & 3u))
            return;
        if (!(b & 1u)) {
            if (cr1 & CR1_ACK) {
                k->sl = S_HDR10;
                slave_lines(k, 1, 0);
            }
            return;
        }
        match = k->sl_ten; /* a read header after the address */
    } else {
        int smb = (cr1 & (CR1_SMBUS | CR1_ENARP)) == (CR1_SMBUS | CR1_ENARP);
        if (!(oar1 & OAR1_ADDMODE) && a == (oar1 >> 1 & 0x7fu))
            match = 1;
        else if ((oar2 & OAR2_ENDUAL) && a == (oar2 >> 1 & 0x7fu))
            match = 1, flags = SR2_DUALF;
        else if ((cr1 & CR1_ENGC) && b == 0)
            match = 1, flags = SR2_GENCALL;
        else if (smb && !(cr1 & CR1_SMBTYPE) && a == 0x61u)
            match = 1, flags = SR2_SMBDEFAULT;
        else if (smb && (cr1 & CR1_SMBTYPE) && a == 0x08u)
            match = 1, flags = SR2_SMBHOST;
        else if ((cr1 & (CR1_SMBUS | CR1_SMBTYPE | CR1_ALERT)) == (CR1_SMBUS | CR1_ALERT) && a == 0x0cu)
            match = 1, flags = 0x80000000u; /* the alert response address */
    }
    if (!match || !(cr1 & CR1_ACK))
        return;
    k->sl_match = 1;
    k->sl_rw = (int)(b & 1u);
    k->sl_flags = flags;
    slave_lines(k, 1, 0); /* ACK */
}

/* the 8th bit's SCL fell: the ACK bit comes */
static void bus_byte(struct i2c *k)
{
    u32 b = k->b_shift;
    int involved = master(k) || k->sl != S_NONE;
    if (k->b_first && !master(k)) {
        slave_address(k, b);
        involved = k->sl_match || k->sl == S_HDR10;
    } else if (k->sl == S_HDR10) {
        k->sl = S_NONE;
        if (b == (reg(k->oar1) & 0xffu) && (reg(k->cr1) & CR1_ACK)) {
            k->sl_match = 1;
            k->sl_rw = 0;
            k->sl_flags = 0;
            k->sl_ten = 1;
            slave_lines(k, 1, 0);
        }
        involved = 1;
    } else if (k->sl == S_RX) {
        int ack = (reg(k->cr1) & CR1_ACK) != 0;
        k->sl_shift = b;
        if (pec_due(k, 1)) {
            if (b != k->pec) {
                set_sr1(k, SR1_PECERR);
                ack = 0;
            }
            k->sl_pec = 1;
        }
        k->sl_ack = ack;
        slave_lines(k, k->s_scl, ack ? 0 : 1);
    } else if (k->sl == S_TX) {
        slave_lines(k, k->s_scl, 1); /* the master's ACK */
    }
    /* the PEC counts the transfer's bytes, the master's own included */
    if (involved && !master(k) && (reg(k->cr1) & CR1_ENPEC) && !k->sl_pec)
        pec_add(k, b);
}

/* the ACK bit's SCL fell: the byte is done */
static void bus_ack(struct i2c *k)
{
    int first = k->b_first;
    k->b_first = 0;
    if (master(k))
        return;
    if (k->sl_match) {
        u32 f = k->sl_flags;
        k->sl_match = 0;
        k->sl_addressed = 1;
        set_sr1(k, SR1_ADDR);
        clr_sr2(k, SR2_SLAVE);
        set_sr2(k, (f & 0xffu) | (k->sl_rw ? SR2_TRA : 0));
        if (f & 0x80000000u)
            set_sr1(k, SR1_SMBALERT);
        k->sl = k->sl_rw ? S_TX : S_RX;
        if (!(reg(k->cr1) & CR1_NOSTRETCH)) {
            k->sl_hold = H_ADDR;
            slave_lines(k, 0, 1);
        } else if (k->sl == S_TX) {
            (void)slave_load(k, 0);
            slave_send(k);
        } else {
            slave_lines(k, 1, 1);
        }
        update(k);
        return;
    }
    (void)first;
    if (k->sl == S_HDR10) {
        slave_lines(k, 1, 1); /* the header's ACK let go: the second byte */
        return;
    }
    if (k->sl == S_RX) {
        slave_lines(k, 1, 1);
        if (k->sl_pec) {
            k->sl_pec = 0;
            pec_done(k);
        }
        slave_received(k);
        update(k);
        return;
    }
    if (k->sl == S_TX) {
        if (k->sl_pec) {
            k->sl_pec = 0;
            pec_done(k);
        }
        if (!k->b_ack) {
            set_sr1(k, SR1_AF); /* the master's NACK: the last byte */
            k->sl = S_DONE;
            slave_lines(k, 1, 1);
            update(k);
            return;
        }
        if (slave_load(k, 1))
            slave_lines(k, 0, 1);
        else
            slave_send(k);
        update(k);
    }
}

/* the program did what the slave holds SCL for: ADDR cleared, DR written
 * or read */
static void slave_go(struct i2c *k)
{
    if (k->sl_hold == H_ADDR) {
        k->sl_hold = H_NONE;
        if (k->sl == S_TX) {
            if (slave_load(k, 0)) {
                update(k);
                return; /* SCL still held: DR first */
            }
            slave_send(k);
            slave_lines(k, 1, k->s_sda);
        } else {
            slave_lines(k, 1, 1);
        }
        update(k);
    } else if (k->sl_hold == H_TXE && k->dr_full) {
        k->sl_hold = H_NONE;
        (void)slave_load(k, 0);
        slave_send(k);
        slave_lines(k, 1, k->s_sda);
        update(k);
    } else if (k->sl_hold == H_BTF && !k->held) {
        k->sl_hold = H_NONE;
        slave_lines(k, 1, 1);
    }
}

/* ---- the bus ------------------------------------------------------------------- */

static void timeout_arm(struct i2c *k, int scl_low)
{
    int want = scl_low && (reg(k->cr1) & CR1_SMBUS) && k->b_busy;
    if (want && !k->to_on) {
        u32 hz = esk_mach_core_hz(k->s);
        k->to_at = k->s->cycles + (u64)(hz ? hz : 8000000u) / 40u; /* 25 ms */
    }
    k->to_on = want;
    clock_run(k);
}

/* a START or STOP inside a byte of a transfer it takes part in */
static void misplaced(struct i2c *k)
{
    /* (in a byte's first bit it is the repeated START or STOP it may be) */
    if (k->b_busy && k->b_n >= 2 && (master(k) || k->sl != S_NONE)) {
        set_sr1(k, SR1_BERR);
        if (!master(k)) {
            k->sl = S_NONE;
            slave_release(k);
        }
        update(k);
    }
}

static void bus_start(struct i2c *k)
{
    misplaced(k);
    k->b_busy = 1;
    set_sr2(k, SR2_BUSY);
    k->b_n = 0;
    k->b_first = 1;
    k->b_shift = 0;
    k->sl_match = 0;
    if (k->sl != S_NONE) {
        k->sl = S_NONE; /* a repeated START: the address comes again */
        slave_release(k);
    }
}

static void bus_stop(struct i2c *k)
{
    misplaced(k);
    k->b_busy = 0;
    k->b_n = 0;
    clr_sr2(k, SR2_BUSY);
    timeout_arm(k, 0);
    if (k->sl_addressed) {
        set_sr1(k, SR1_STOPF);
        k->sl_addressed = 0;
        k->sl_ten = 0;
        k->sl = S_NONE;
        slave_release(k);
        update(k);
    }
    if (reg(k->cr1) & CR1_ENPEC)
        pec_reset(k);
    if (k->start_wait && on(k))
        begin_start(k);
}

static void bus_rise(struct i2c *k)
{
    timeout_arm(k, 0);
    if (k->scl_wait) {
        /* the slave let SCL go: the master's high time from now */
        k->scl_wait = 0;
        wait(k, 1);
    }
    if (!k->b_busy)
        return;
    if (k->b_n < 8)
        k->b_shift = (k->b_shift << 1 | (u32)k->in_sda) & 0xffu;
    else if (k->b_n == 8)
        k->b_ack = !k->in_sda;
    if (k->b_n < 9)
        k->b_n++;
}

static void bus_fall(struct i2c *k)
{
    timeout_arm(k, 1);
    if (!k->b_busy)
        return;
    if (k->b_n == 8) {
        bus_byte(k);
    } else if (k->b_n == 9) {
        k->b_n = 0;
        bus_ack(k);
    } else if (k->sl == S_TX && k->sl_hold == H_NONE && k->b_n > 0) {
        slave_lines(k, 1, (int)(k->sl_shift >> (7 - k->b_n) & 1u));
    }
}

/* a line's level changed on its pin */
static void bus_input(struct i2c *k, int fn, int level)
{
    int scl = k->in_scl, sda = k->in_sda;
    if (fn == F_SCL)
        k->in_scl = level;
    else
        k->in_sda = level;
    if (!on(k) || scl < 0 || sda < 0)
        return;
    if (fn == F_SDA && scl) {
        if (!level)
            bus_start(k);
        else
            bus_stop(k);
    } else if (fn == F_SCL && level && !scl) {
        bus_rise(k);
    } else if (fn == F_SCL && !level && scl) {
        bus_fall(k);
    }
}

static void timeout(struct i2c *k)
{
    k->to_on = 0;
    set_sr1(k, SR1_TIMEOUT);
    if (master(k)) {
        k->scl_wait = 0;
        begin_stop(k);
    }
    if (k->sl != S_NONE) {
        k->sl = S_NONE;
        k->sl_addressed = 0;
        slave_release(k);
    }
    update(k);
}

static void i2c_tick(void *ctx, u32 cycles)
{
    struct i2c *k = (struct i2c *)ctx;
    if (!k->running)
        return;
    if (k->to_on && k->s->cycles >= k->to_at)
        timeout(k);
    if (!k->stepping)
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
        if (k->stepping && k->left) {
            u64 cut = over < k->left ? over : k->left - 1;
            k->left -= cut;
            k->since = 0;
        }
    }
}

static int i2c_next(void *ctx, u32 *cycles)
{
    struct i2c *k = (struct i2c *)ctx;
    u64 best = ~(u64)0;
    if (!k->running)
        return 0;
    if (k->stepping)
        best = k->left ? k->left : 1;
    if (k->to_on) {
        u64 d = k->to_at > k->s->cycles ? k->to_at - k->s->cycles : 1;
        if (d < best)
            best = d;
    }
    if (best == ~(u64)0)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

/* ---- the registers ---------------------------------------------------------------- */

/* DR read: RXNE clears, a waiting byte moves in and the bus goes on */
static void dr_read(struct i2c *k)
{
    clr_sr1(k, SR1_BTF);
    if (!k->dr_full)
        return;
    k->dr_full = 0;
    clr_sr1(k, SR1_RXNE);
    if (k->held) {
        k->held = 0;
        k->dr_byte = master(k) ? k->shift : k->sl_shift;
        k->dr_full = 1;
        esk_rf_hw(k->dr, k->dr_byte);
        set_sr1(k, SR1_RXNE);
        if (master(k) && k->mode == M_HOLD)
            rx_next(k);
        else if (!master(k))
            slave_go(k);
    }
    update(k);
}

static u32 i2c_read(void *ctx, u32 off, int n)
{
    struct i2c *k = (struct i2c *)ctx;
    u32 v = esk_rf_read(k->p, off, n);
    if (k->s->bus.debug)
        return v;
    if (off == esk_rf_off(k->sr1)) {
        k->sr1_read = 1;
    } else if (off == esk_rf_off(k->sr2)) {
        if ((reg(k->sr1) & SR1_ADDR) && k->sr1_read) {
            clr_sr1(k, SR1_ADDR);
            if (!master(k)) {
                slave_go(k);
            } else if (k->txmode) {
                if (k->dr_full) {
                    tx_next(k);
                } else {
                    set_sr1(k, SR1_TXE); /* waiting on DR, a STOP or a START */
                    hold(k);
                }
            } else {
                k->nacked = 0;
                k->ack_prev = 1;
                rx_next(k);
            }
            update(k);
        }
        k->sr1_read = 0;
    } else if (off == esk_rf_off(k->dr)) {
        k->sr1_read = 0;
        dr_read(k);
    }
    return v;
}

/* everything at its reset value, the lines let go */
static void quiet(struct i2c *k)
{
    k->mode = M_IDLE;
    k->sl = S_NONE;
    k->sl_hold = H_NONE;
    k->sl_addressed = k->sl_ten = k->sl_match = 0;
    k->b_busy = k->b_n = 0;
    k->dr_full = k->held = k->nacked = k->ten = 0;
    k->start_wait = k->scl_wait = k->last_nack = k->pec_byte = k->sl_pec = 0;
    k->to_on = 0;
    idle_clock(k);
    pec_reset(k);
    k->m_scl = k->m_sda = k->s_scl = k->s_sda = 1;
}

static void i2c_write(void *ctx, u32 off, int n, u32 v)
{
    struct i2c *k = (struct i2c *)ctx;
    u32 was = reg(k->cr1);
    if (off == esk_rf_off(k->sr1)) {
        clr_sr1(k, ~v & SR1_ERRORS); /* rc_w0 */
        update(k);
        return;
    }
    if (off == esk_rf_off(k->dr)) {
        u32 b = v & 0xffu;
        int sb = (reg(k->sr1) & SR1_SB) != 0, add10 = (reg(k->sr1) & SR1_ADD10) != 0;
        k->sr1_read = 0;
        clr_sr1(k, SR1_BTF);
        if (!on(k))
            return;
        if (master(k) && (sb || add10)) {
            clr_sr1(k, SR1_SB | SR1_ADD10);
            if (sb)
                k->txmode = !(b & 1u); /* a 10-bit read header: receiving */
            begin_byte(k, M_ADDR, b);
            update(k);
            return;
        }
        esk_rf_hw(k->dr, b);
        k->dr_byte = b;
        k->dr_full = 1;
        clr_sr1(k, SR1_TXE);
        if (master(k) && k->mode == M_HOLD && k->txmode && !(reg(k->sr1) & (SR1_ADDR | SR1_AF)))
            tx_next(k);
        else if (!master(k))
            slave_go(k);
        update(k);
        return;
    }
    if (off == esk_rf_off(k->cr1) && (v & CR1_SWRST)) {
        /* under reset: every register at its reset value */
        int i;
        for (i = 0; i < k->p->nreg; i++)
            esk_rf_hw(k->p->reg[i], k->p->reg[i]->reset);
        esk_rf_hw(k->cr1, CR1_SWRST);
        quiet(k);
        pins(k);
        return;
    }
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->cr1) && (was & CR1_SWRST))
        quiet(k); /* out of reset */
    if (off != esk_rf_off(k->cr1)) {
        if (off == esk_rf_off(k->cr2))
            update(k);
        return;
    }
    if (k->sr1_read && (reg(k->sr1) & SR1_STOPF))
        clr_sr1(k, SR1_STOPF); /* read SR1, then write CR1 */
    k->sr1_read = 0;
    if (!(reg(k->cr1) & CR1_PE)) {
        quiet(k);
        esk_rf_hw(k->sr1, 0);
        esk_rf_hw(k->sr2, 0);
        esk_rf_hw(k->cr1, k->cr1->value & ~(u64)(CR1_START | CR1_STOP | CR1_PEC | CR1_ACK));
        pins(k);
        update(k);
        return;
    }
    if (!(was & CR1_PE) || (was & CR1_SWRST)) {
        /* the bus as it is when the interface starts watching it */
        k->in_scl = line(k, F_SCL);
        k->in_sda = line(k, F_SDA);
        k->in_smba = line(k, F_SMBA);
    }
    if ((was & CR1_ENPEC) && !(reg(k->cr1) & CR1_ENPEC))
        pec_reset(k);
    pins(k);
    if (reg(k->cr1) & CR1_START) {
        if (k->mode == M_IDLE && !k->start_wait) {
            k->dr_full = 0;
            k->held = 0;
            begin_start(k);
        } else if (k->mode == M_HOLD && !(reg(k->sr1) & (SR1_SB | SR1_ADDR | SR1_ADD10))) {
            hold(k);
        }
    } else if (reg(k->cr1) & CR1_STOP) {
        if (k->mode == M_HOLD && !(reg(k->sr1) & (SR1_SB | SR1_ADDR)))
            hold(k);
        else if (k->mode == M_IDLE) {
            esk_rf_hw(k->cr1, k->cr1->value & ~(u64)CR1_STOP);
            if (k->sl != S_NONE) { /* a slave lets the bus go */
                k->sl = S_NONE;
                slave_release(k);
            }
        }
    }
    update(k);
}

static void i2c_reset(void *ctx)
{
    struct i2c *k = (struct i2c *)ctx;
    quiet(k);
    k->running = 0;
    k->left = 0;
    k->sr1_read = 0;
    k->in_scl = k->in_sda = k->in_smba = -1;
}

const struct esk_dev_ops esk_stm32_i2c_ops = {
    "stm32-i2c", i2c_read, i2c_write, i2c_reset, i2c_tick, i2c_next,
};

/* ---- the signals ----------------------------------------------------------------- */

static struct i2c *i2c_named(struct esk_mach *s, const char *name)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_stm32_i2c_ops && !esk_rf_gated(p) ? (struct i2c *)p->ctx : 0;
}

/* "I2C1_SCL" -> I2C1 and its function, or 0 (FMPI2C1_SCL is not this) */
static struct i2c *signal_of(struct esk_mach *s, const char *sig, int *fn)
{
    char name[8];
    const char *us = strchr(sig, '_');
    size_t l = us ? (size_t)(us - sig) : 0;
    int i;
    if (!us || l >= sizeof name || strncmp(sig, "I2C", 3))
        return 0;
    for (i = 0; i < 3; i++)
        if (!strcmp(us + 1, fn_names[i]))
            break;
    if (i == 3)
        return 0;
    memcpy(name, sig, l);
    name[l] = 0;
    *fn = i;
    return i2c_named(s, name);
}

int esk_stm32_i2c_signal(struct esk_mach *s, const char *sig)
{
    int fn;
    const struct i2c *k = signal_of(s, sig, &fn);
    return k ? drive(k, fn) : -1;
}

void esk_stm32_i2c_signal_input(struct esk_mach *s, const char *sig, int level)
{
    int fn;
    struct i2c *k = signal_of(s, sig, &fn);
    if (!k)
        return;
    level = level != 0;
    if (fn == F_SMBA) {
        int was = k->in_smba;
        k->in_smba = level;
        if (on(k) && was == 1 && !level &&
            (reg(k->cr1) & (CR1_SMBUS | CR1_SMBTYPE | CR1_ALERT)) == (CR1_SMBUS | CR1_SMBTYPE | CR1_ALERT)) {
            set_sr1(k, SR1_SMBALERT); /* a device's alert, at the host */
            update(k);
        }
        return;
    }
    if (level != (fn == F_SCL ? k->in_scl : k->in_sda))
        bus_input(k, fn, level);
}

void esk_stm32_i2c_dma_left(struct esk_mach *s, const char *sig, u32 left)
{
    char name[8];
    const char *us = strchr(sig, '_');
    size_t l = us ? (size_t)(us - sig) : 0;
    struct i2c *k;
    if (!us || l >= sizeof name || strncmp(sig, "I2C", 3))
        return;
    memcpy(name, sig, l);
    name[l] = 0;
    k = i2c_named(s, name);
    if (!k)
        return;
    if (!strcmp(us + 1, "TX") && left == 0 && (reg(k->cr1) & CR1_ENPEC))
        k->pec_tx_end = 1; /* the PEC after the last byte */
    else if (!strcmp(us + 1, "RX") && left == 1 && (reg(k->cr2) & CR2_LAST)) {
        /* EOT-1: the next byte is the last, NACKed (the PEC with PECEN) */
        k->last_nack = 1;
        if (reg(k->cr1) & CR1_ENPEC)
            k->pec_rx_end = 1;
    }
}

int esk_stm32f1_i2c_pin(struct esk_mach *s, int port, int bit)
{
    int unit, fn;
    for (unit = 1; unit <= 2; unit++) {
        char name[8] = "I2Cx";
        const struct i2c *k;
        name[3] = (char)('0' + unit);
        k = i2c_named(s, name);
        if (!k)
            continue;
        for (fn = 0; fn < 3; fn++)
            if (pin_of(k, fn) == port * 16 + bit) {
                int v = drive(k, fn);
                if (v >= 0)
                    return v;
            }
    }
    return -1;
}

void esk_stm32f1_i2c_input(struct esk_mach *s, int port, int bit, int level)
{
    int unit, fn;
    for (unit = 1; unit <= 2; unit++) {
        char name[8] = "I2Cx", sig[16];
        const struct i2c *k;
        name[3] = (char)('0' + unit);
        k = i2c_named(s, name);
        if (!k)
            continue;
        for (fn = 0; fn < 3; fn++)
            if (pin_of(k, fn) == port * 16 + bit) {
                snprintf(sig, sizeof sig, "%s_%s", name, fn_names[fn]);
                esk_stm32_i2c_signal_input(s, sig, level);
            }
    }
}

void *esk_stm32_i2c_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct i2c k, *c;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    k.cr1 = esk_rf_reg(p, "CR1");
    k.cr2 = esk_rf_reg(p, "CR2");
    k.oar1 = esk_rf_reg(p, "OAR1");
    k.oar2 = esk_rf_reg(p, "OAR2");
    k.dr = esk_rf_reg(p, "DR");
    k.sr1 = esk_rf_reg(p, "SR1");
    k.sr2 = esk_rf_reg(p, "SR2");
    k.ccr = esk_rf_reg(p, "CCR");
    if (!k.cr1 || !k.cr2 || !k.oar1 || !k.oar2 || !k.dr || !k.sr1 || !k.sr2 || !k.ccr ||
        strncmp(p->name, "I2C", 3) || p->name[3] < '1' || p->name[3] > '4' || p->name[4])
        return 0; /* not this layout: the registers alone */
    k.unit = p->name[3] - '0';
    k.ev_irq = esk_rf_irq(p, "_EV");
    k.er_irq = esk_rf_irq(p, "_ER");
    snprintf(k.sig_tx, sizeof k.sig_tx, "%s_TX", p->name);
    snprintf(k.sig_rx, sizeof k.sig_rx, "%s_RX", p->name);
    c = (struct i2c *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    i2c_reset(c);
    return c;
}
