/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* spi.c -- the FE310's SPI controllers SPI1 and SPI2 (FE310-G002 manual,
 * chapter 19) in their single-wire programmed-I/O mode, over the register
 * file, on their pins.
 *
 *   - txdata queues a frame into an 8-entry FIFO (its full bit, 31, reads
 *     set when it holds 8 and a write then is lost); rxdata takes the
 *     oldest received frame, or reads its empty bit (31);
 *   - a frame is fmt.len bits (up to 8), MSB first unless fmt.endian, in
 *     the mode sckmode's pol and pha give, SCK at the bus clock divided by
 *     2 * (sckdiv + 1); MISO is read on the sampling edge, and the frame
 *     goes to the receive FIFO unless fmt.dir says transmit only (a full
 *     FIFO drops it);
 *   - csmode AUTO asserts CS csid (to the inverse of its csdef bit) before
 *     each frame and lets it go after; HOLD keeps it asserted from the
 *     first frame until csmode, csid or csdef is written; OFF leaves the CS
 *     pins at csdef. delay0's cssck and sckcs, and delay1's intercs and
 *     interxfr, are counted in SCK periods;
 *   - ip.txwm is set while the transmit FIFO holds fewer than txmark
 *     frames, ip.rxwm while the receive FIFO holds more than rxmark; with
 *     their ie bits they raise the PLIC source (SPI1 6, SPI2 7);
 *   - SPI1's pins (IOF0): CS0 GPIO 2, MOSI 3, MISO 4, SCK 5, CS1 8, CS2 9,
 *     CS3 10; SPI2 has none on the FE310-G002's package (its frames shift
 *     MISO in as 0);
 *   - an edge is a device event, at most one in an instruction.
 *
 * Not modelled: QSPI0 (the flash's controller, whose memory-mapped mode the
 * bus answers; its registers hold values), the dual and quad protocols,
 * fctrl and ffmt. */
#include <string.h>

#include "mcu/riscv/riscv.h"

#define DEPTH 8
#define FULL 0x80000000u

enum { ST_IDLE, ST_SETUP, ST_BITS, ST_HOLD, ST_GAP };

struct fe_spi {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *sckdiv, *sckmode, *csid, *csdef, *csmode, *delay0, *delay1, *fmt, *txdata, *rxdata,
        *txmark, *rxmark, *ie, *ip;
    int unit, source, running;
    u8 tx[DEPTH], rx[DEPTH];
    int ntx, nrx;
    int state;
    int cs_on;       /* a CS asserted */
    int cs_pin;      /* the pin asserted */
    int release;     /* let CS go once the frame under way ends (HOLD left) */
    u32 out, in;     /* the frame's shift registers */
    int bits, bit, leading;
    int sck, mosi;
    int irq;         /* the source raised */
    u64 left, since;
};

static u32 v32(const struct esk_rf_reg *r) { return (u32)r->value; }

static const int cs_pins[4] = { 2, 8, 9, 10 };
#define PIN_MOSI 3
#define PIN_MISO 4
#define PIN_SCK 5

static u64 half(const struct fe_spi *k) { return (u64)(v32(k->sckdiv) & 0xfffu) + 1u; }

static void drive(struct fe_spi *k, int pin, int level)
{
    if (k->unit == 1)
        esk_fe310_gpio_iof_level(k->s, 0, pin, level);
}

static void update_ip(struct fe_spi *k)
{
    u32 ip = 0;
    int on;
    if ((u32)k->ntx < (v32(k->txmark) & 7u))
        ip |= 1u;
    if ((u32)k->nrx > (v32(k->rxmark) & 7u))
        ip |= 2u;
    esk_rf_hw(k->ip, ip);
    on = (ip & v32(k->ie) & 3u) != 0;
    if (on != k->irq) {
        k->irq = on;
        if (k->source >= 0)
            esk_fe310_plic_source(k->s, k->source, on);
    }
}

/* the CS pins at rest: csdef's levels */
static void cs_rest(struct fe_spi *k)
{
    int i;
    for (i = 0; i < 4; i++)
        drive(k, cs_pins[i], (int)(v32(k->csdef) >> i & 1u));
}

static void cs_assert(struct fe_spi *k)
{
    int id = (int)(v32(k->csid) & 3u);
    k->cs_on = 1;
    k->cs_pin = id;
    drive(k, cs_pins[id], !(int)(v32(k->csdef) >> id & 1u));
}

static void cs_release(struct fe_spi *k)
{
    k->cs_on = 0;
    k->release = 0;
    cs_rest(k);
}

static void wait_n(struct fe_spi *k, u64 n)
{
    k->left = n ? n : 1;
    k->since = k->s->cycles;
    esk_rf_clock(k->p, &k->running, 1);
}

static int out_bit(const struct fe_spi *k, int i)
{
    int pos = (v32(k->fmt) & 4u) ? i : k->bits - 1 - i;
    return (int)(k->out >> pos & 1u);
}

static void in_bit(struct fe_spi *k, int i, int v)
{
    int pos = (v32(k->fmt) & 4u) ? i : k->bits - 1 - i;
    if (v)
        k->in |= 1u << pos;
}

static int miso(struct fe_spi *k) { return k->unit == 1 ? esk_fe310_pin_read(k->s, PIN_MISO) : 0; }

static void start_frame(struct fe_spi *k)
{
    u32 len = v32(k->fmt) >> 16 & 0xfu;
    k->out = k->tx[0];
    k->ntx--;
    memmove(k->tx, k->tx + 1, (size_t)k->ntx);
    update_ip(k);
    k->bits = len ? (int)(len > 8 ? 8 : len) : 8;
    k->bit = 0;
    k->in = 0;
    k->leading = 1;
    k->state = ST_BITS;
    if (!(v32(k->sckmode) & 1u)) {
        k->mosi = out_bit(k, 0);
        drive(k, PIN_MOSI, k->mosi);
    }
    wait_n(k, half(k));
}

/* the next frame, or rest */
static void next(struct fe_spi *k)
{
    u32 mode = v32(k->csmode) & 3u;
    if (!k->ntx) {
        k->state = ST_IDLE;
        esk_rf_clock(k->p, &k->running, 0);
        return;
    }
    if (mode != 3u && !k->cs_on) {
        cs_assert(k);
        k->state = ST_SETUP;
        wait_n(k, 2u * half(k) * (v32(k->delay0) & 0xffu));
        return;
    }
    start_frame(k);
}

static void frame_done(struct fe_spi *k)
{
    u32 mode = v32(k->csmode) & 3u;
    if (!(v32(k->fmt) & 8u)) {
        if (k->nrx < DEPTH)
            k->rx[k->nrx++] = (u8)k->in;
    }
    update_ip(k);
    if (k->cs_on && (mode == 0u || k->release)) {
        k->state = ST_HOLD; /* sckcs, then CS goes, then intercs */
        wait_n(k, 2u * half(k) * (v32(k->delay0) >> 16 & 0xffu));
        return;
    }
    if (k->ntx && (v32(k->delay1) >> 16 & 0xffu)) {
        k->state = ST_GAP; /* interxfr with CS held */
        wait_n(k, 2u * half(k) * (v32(k->delay1) >> 16 & 0xffu));
        return;
    }
    next(k);
}

static void step(struct fe_spi *k)
{
    int cpha = (v32(k->sckmode) & 1u) != 0, cpol = (v32(k->sckmode) & 2u) != 0;
    switch (k->state) {
    case ST_SETUP:
        start_frame(k);
        return;
    case ST_BITS:
        if (k->leading) {
            k->sck = !cpol;
            drive(k, PIN_SCK, k->sck);
            if (cpha) {
                k->mosi = out_bit(k, k->bit);
                drive(k, PIN_MOSI, k->mosi);
            } else {
                in_bit(k, k->bit, miso(k));
            }
            k->leading = 0;
            wait_n(k, half(k));
            return;
        }
        if (cpha)
            in_bit(k, k->bit, miso(k));
        k->sck = cpol;
        drive(k, PIN_SCK, k->sck);
        k->bit++;
        if (!cpha && k->bit < k->bits) {
            k->mosi = out_bit(k, k->bit);
            drive(k, PIN_MOSI, k->mosi);
        }
        k->leading = 1;
        if (k->bit < k->bits)
            wait_n(k, half(k));
        else
            frame_done(k);
        return;
    case ST_HOLD:
        cs_release(k);
        k->state = ST_GAP;
        wait_n(k, 2u * half(k) * (v32(k->delay1) & 0xffu));
        return;
    case ST_GAP:
        next(k);
        return;
    default:
        esk_rf_clock(k->p, &k->running, 0);
        return;
    }
}

static void spi_tick(void *ctx, u32 cycles)
{
    struct fe_spi *k = (struct fe_spi *)ctx;
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

static int spi_next(void *ctx, u32 *cycles)
{
    struct fe_spi *k = (struct fe_spi *)ctx;
    if (!k->running)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)(k->left ? k->left : 1);
    return 1;
}

static u32 spi_read(void *ctx, u32 off, int n)
{
    struct fe_spi *k = (struct fe_spi *)ctx;
    if (off == esk_rf_off(k->txdata))
        return k->ntx >= DEPTH ? FULL : 0u;
    if (off == esk_rf_off(k->rxdata)) {
        u32 v;
        if (!k->nrx)
            return FULL;
        v = k->rx[0];
        if (!k->s->bus.debug) {
            k->nrx--;
            memmove(k->rx, k->rx + 1, (size_t)k->nrx);
            update_ip(k);
        }
        return v;
    }
    return esk_rf_read(k->p, off, n);
}

static void spi_write(void *ctx, u32 off, int n, u32 v)
{
    struct fe_spi *k = (struct fe_spi *)ctx;
    if (off == esk_rf_off(k->txdata)) {
        if (k->ntx < DEPTH)
            k->tx[k->ntx++] = (u8)v;
        update_ip(k);
        if (k->state == ST_IDLE)
            next(k);
        return;
    }
    if (off == esk_rf_off(k->rxdata) || off == esk_rf_off(k->ip))
        return; /* read only */
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->csmode) || off == esk_rf_off(k->csid) || off == esk_rf_off(k->csdef)) {
        /* HOLD ends: CS goes now, or after the frame under way */
        if (k->cs_on && k->state == ST_IDLE)
            cs_release(k);
        else if (k->cs_on)
            k->release = 1;
        else
            cs_rest(k);
    }
    if (off == esk_rf_off(k->sckmode) && k->state == ST_IDLE) {
        k->sck = (v32(k->sckmode) & 2u) != 0;
        drive(k, PIN_SCK, k->sck);
    }
    update_ip(k);
}

static void spi_reset(void *ctx)
{
    struct fe_spi *k = (struct fe_spi *)ctx;
    k->running = 0;
    k->ntx = k->nrx = 0;
    k->state = ST_IDLE;
    k->cs_on = 0;
    k->release = 0;
    k->left = 0;
    k->sck = 0;
    k->mosi = 0;
    k->irq = 0;
    drive(k, PIN_SCK, 0);
    drive(k, PIN_MOSI, 0);
    drive(k, PIN_MISO, -1);
    {
        int i;
        for (i = 0; i < 4; i++)
            drive(k, cs_pins[i], 1); /* csdef's reset: inactive high */
    }
}

const struct esk_dev_ops esk_fe310_spi_ops = {
    "fe310-spi", spi_read, spi_write, spi_reset, spi_tick, spi_next,
};

void *esk_fe310_spi_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct fe_spi k, *c;
    memset(&k, 0, sizeof k);
    if (strncmp(p->name, "SPI", 3) || (p->name[3] != '1' && p->name[3] != '2') || p->name[4])
        return 0; /* QSPI0, the flash's, keeps its registers */
    k.s = s;
    k.p = p;
    k.unit = p->name[3] - '0';
#define REG(f, name)                                                                                           \
    k.f = esk_rf_reg(p, name);                                                                                 \
    if (!k.f)                                                                                                  \
        return 0;
    REG(sckdiv, "SCKDIV")
    REG(sckmode, "SCKMODE")
    REG(csid, "CSID")
    REG(csdef, "CSDEF")
    REG(csmode, "CSMODE")
    REG(delay0, "DELAY0")
    REG(delay1, "DELAY1")
    REG(fmt, "FMT")
    REG(txdata, "TXDATA")
    REG(rxdata, "RXDATA")
    REG(txmark, "TXMARK")
    REG(rxmark, "RXMARK")
    REG(ie, "IE")
    REG(ip, "IP")
#undef REG
    k.source = esk_rf_irq(p, p->name);
    c = (struct fe_spi *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = k;
    return c;
}
