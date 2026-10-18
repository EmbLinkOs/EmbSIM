/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* spi.c -- the ATmega328P's SPI as a master (datasheet 19), over the
 * register file, on its pins.
 *
 *   - with SPCR.SPE and MSTR the SPI drives MOSI (PB3) and SCK (PB5) while
 *     their DDRB bits make them outputs, and makes MISO (PB4) an input
 *     whatever DDRB says (PORTB4 still gives it the pull-up); SCK idles at
 *     CPOL, MOSI holds its last bit;
 *   - SS (PB2) as an input held low takes the master down when SPCR or SPDR
 *     is written: MSTR clears and SPIF sets (the datasheet's multi-master
 *     guard); as an output it is the program's GPIO, as Arduino's SPI
 *     library leaves it;
 *   - a write of SPDR starts a byte at once, MSB or LSB first (DORD), at
 *     the core clock divided by 4, 16, 64 or 128 (SPR), halved by
 *     SPSR.SPI2X; with CPHA 0 MOSI is valid before the leading edge and
 *     MISO is sampled on it, with CPHA 1 MOSI changes on the leading edge
 *     and MISO is sampled on the trailing one. A write while a byte is
 *     under way is lost and sets WCOL;
 *   - at the byte's end SPIF sets and SPDR reads the byte received (the
 *     receive side is buffered: it holds until the next byte ends); SPIF
 *     and WCOL clear when SPDR is read or written after SPSR was read with
 *     either set, and SPIF as the core enters SPI_STC's vector (SPIE);
 *   - an edge is a device event, at most one in an instruction: a clock
 *     faster than the instructions that wait on it is stretched to them.
 *
 * Not modelled: slave mode, SS as an input while a byte is under way. */
#include <string.h>

#include "mcu/avr/avr.h"

#define SPCR_SPR 0x03u
#define SPCR_CPHA 0x04u
#define SPCR_CPOL 0x08u
#define SPCR_MSTR 0x10u
#define SPCR_DORD 0x20u
#define SPCR_SPE 0x40u
#define SPCR_SPIE 0x80u
#define SPSR_SPI2X 0x01u
#define SPSR_WCOL 0x40u
#define SPSR_SPIF 0x80u

#define PB 1
#define SS 2
#define MOSI 3
#define MISO 4
#define SCK 5

struct avr_spi {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *spcr, *spsr, *spdr;
    int vec, running;
    u8 out, in, rx;
    int bit, leading, busy;
    int armed;       /* SPSR read with SPIF or WCOL set */
    int sck, mosi;   /* the levels driven */
    int pins_on;     /* the SPI holds its pins */
    u64 left, since; /* core cycles to the next edge */
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static int master(const struct avr_spi *k)
{
    return (reg(k->spcr) & (SPCR_SPE | SPCR_MSTR)) == (SPCR_SPE | SPCR_MSTR);
}

static void pins(struct avr_spi *k)
{
    struct esk_mach *s = k->s;
    int on = master(k);
    if (on) {
        esk_avr_pin_override(s, PB, SCK, k->sck);
        esk_avr_pin_override(s, PB, MOSI, k->mosi);
        esk_avr_pin_force(s, PB, MISO, 2);
    } else if (k->pins_on) {
        esk_avr_pin_override(s, PB, SCK, -1);
        esk_avr_pin_override(s, PB, MOSI, -1);
        esk_avr_pin_force(s, PB, MISO, -1);
    }
    k->pins_on = on;
}

/* core cycles per half bit */
static u32 half(const struct avr_spi *k)
{
    static const u32 div[4] = { 4, 16, 64, 128 };
    u32 d = div[reg(k->spcr) & SPCR_SPR];
    if (reg(k->spsr) & SPSR_SPI2X)
        d /= 2u;
    return d / 2u;
}

static int out_bit(const struct avr_spi *k, int i)
{
    int pos = (reg(k->spcr) & SPCR_DORD) ? i : 7 - i;
    return k->out >> pos & 1;
}

static void in_bit(struct avr_spi *k, int i, int v)
{
    int pos = (reg(k->spcr) & SPCR_DORD) ? i : 7 - i;
    if (v)
        k->in = (u8)(k->in | 1u << pos);
}

static void flags(struct avr_spi *k, u32 set, u32 clr)
{
    esk_rf_hw(k->spsr, (reg(k->spsr) & ~clr) | set);
    esk_avr_irq_changed(k->s);
}

/* SS an input held low: the master steps down */
static int mode_fault(struct avr_spi *k)
{
    struct esk_rf_periph *port = k->s->rf ? esk_rf_periph(k->s->rf, "PORTB") : 0;
    struct esk_rf_reg *ddr = port ? esk_rf_reg(port, "DDRB") : 0;
    if (!master(k) || !ddr || (ddr->value >> SS & 1u) || esk_avr_pin_read(k->s, PB, SS))
        return 0;
    esk_rf_hw(k->spcr, reg(k->spcr) & ~(u64)SPCR_MSTR);
    k->busy = 0;
    esk_rf_clock(k->p, &k->running, 0);
    pins(k);
    flags(k, SPSR_SPIF, 0);
    return 1;
}

static void wait(struct avr_spi *k)
{
    k->left = half(k);
    k->since = k->s->cycles;
    esk_rf_clock(k->p, &k->running, 1);
}

static void start(struct avr_spi *k, u8 v)
{
    k->out = v;
    k->in = 0;
    k->bit = 0;
    k->leading = 1;
    k->busy = 1;
    if (!(reg(k->spcr) & SPCR_CPHA)) {
        k->mosi = out_bit(k, 0);
        pins(k);
    }
    wait(k);
}

static void edge(struct avr_spi *k)
{
    int cpha = (reg(k->spcr) & SPCR_CPHA) != 0, cpol = (reg(k->spcr) & SPCR_CPOL) != 0;
    if (k->leading) {
        k->sck = !cpol;
        if (cpha)
            k->mosi = out_bit(k, k->bit);
        pins(k);
        if (!cpha)
            in_bit(k, k->bit, esk_avr_pin_read(k->s, PB, MISO));
        k->leading = 0;
        wait(k);
        return;
    }
    if (cpha)
        in_bit(k, k->bit, esk_avr_pin_read(k->s, PB, MISO));
    k->sck = cpol;
    k->bit++;
    if (!cpha && k->bit < 8)
        k->mosi = out_bit(k, k->bit);
    pins(k);
    k->leading = 1;
    if (k->bit < 8) {
        wait(k);
        return;
    }
    k->busy = 0;
    k->rx = k->in;
    esk_rf_hw(k->spdr, k->rx);
    esk_rf_clock(k->p, &k->running, 0);
    flags(k, SPSR_SPIF, 0);
}

static void spi_tick(void *ctx, u32 cycles)
{
    struct avr_spi *k = (struct avr_spi *)ctx;
    if (!k->running)
        return;
    cycles = esk_mach_after(k->s, cycles, k->since);
    k->since = 0;
    if (k->left > cycles) {
        k->left -= cycles;
        return;
    }
    {
        /* one edge in an instruction; what the instruction ran past the
         * edge counts toward the next, up to a whole half bit */
        u64 over = cycles - k->left;
        k->left = 0;
        edge(k);
        if (k->running && k->left) {
            u64 cut = over < k->left ? over : k->left - 1;
            k->left -= cut;
            k->since = 0;
        }
    }
}

static int spi_next(void *ctx, u32 *cycles)
{
    struct avr_spi *k = (struct avr_spi *)ctx;
    if (!k->running)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)(k->left ? k->left : 1);
    return 1;
}

/* SPDR touched after SPSR was read with a flag set: the flags clear */
static void touch(struct avr_spi *k)
{
    if (k->armed) {
        k->armed = 0;
        flags(k, 0, SPSR_SPIF | SPSR_WCOL);
    }
}

static u32 spi_read(void *ctx, u32 off, int n)
{
    struct avr_spi *k = (struct avr_spi *)ctx;
    u32 v = esk_rf_read(k->p, off, n);
    if (k->s->bus.debug)
        return v;
    if (off == esk_rf_off(k->spsr) && (v & (SPSR_SPIF | SPSR_WCOL)))
        k->armed = 1;
    else if (off == esk_rf_off(k->spdr))
        touch(k);
    return v;
}

static void spi_write(void *ctx, u32 off, int n, u32 v)
{
    struct avr_spi *k = (struct avr_spi *)ctx;
    if (off == esk_rf_off(k->spdr)) {
        touch(k);
        if (!master(k) || mode_fault(k))
            return;
        if (k->busy) {
            flags(k, SPSR_WCOL, 0);
            return;
        }
        start(k, (u8)v);
        return;
    }
    if (off == esk_rf_off(k->spsr)) {
        /* SPIF and WCOL are the hardware's; SPI2X the program's */
        esk_rf_hw(k->spsr, (reg(k->spsr) & ~(u64)SPSR_SPI2X) | (v & SPSR_SPI2X));
        return;
    }
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->spcr)) {
        if (!master(k)) {
            k->busy = 0;
            esk_rf_clock(k->p, &k->running, 0);
        } else if (!k->busy) {
            k->sck = (reg(k->spcr) & SPCR_CPOL) != 0;
        }
        pins(k);
        mode_fault(k);
        esk_avr_irq_changed(k->s);
    }
}

static void spi_reset(void *ctx)
{
    struct avr_spi *k = (struct avr_spi *)ctx;
    k->running = 0;
    k->busy = 0;
    k->armed = 0;
    k->rx = 0;
    k->sck = k->mosi = 0;
    k->left = 0;
    if (k->pins_on) {
        k->pins_on = 0;
        esk_avr_pin_override(k->s, PB, SCK, -1);
        esk_avr_pin_override(k->s, PB, MOSI, -1);
        esk_avr_pin_force(k->s, PB, MISO, -1);
    }
}

static int spi_pending(void *ctx, int vec)
{
    struct avr_spi *k = (struct avr_spi *)ctx;
    ES_UNUSED(vec);
    return (reg(k->spsr) & SPSR_SPIF) && (reg(k->spcr) & SPCR_SPIE) && (reg(k->spcr) & SPCR_SPE);
}

static void spi_ack(void *ctx, int vec)
{
    struct avr_spi *k = (struct avr_spi *)ctx;
    ES_UNUSED(vec);
    esk_rf_hw(k->spsr, reg(k->spsr) & ~(u64)SPSR_SPIF);
}

const struct esk_dev_ops esk_avr_spi_ops = {
    "avr-spi", spi_read, spi_write, spi_reset, spi_tick, spi_next,
};

void *esk_avr_spi_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct avr_spi k, *c;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    k.spcr = esk_rf_reg(p, "SPCR");
    k.spsr = esk_rf_reg(p, "SPSR");
    k.spdr = esk_rf_reg(p, "SPDR");
    if (!k.spcr || !k.spsr || !k.spdr)
        return 0;
    k.vec = esk_rf_irq(p, "SPI_STC");
    c = (struct avr_spi *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    if (c->vec >= 0)
        esk_avr_irq_source(s, c->vec, spi_pending, spi_ack, c);
    return c;
}
