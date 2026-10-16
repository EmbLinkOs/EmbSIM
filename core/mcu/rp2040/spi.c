/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* spi.c -- the RP2040's SPI (ARM's PL022, its datasheet, 4.4) as a master,
 * over the register file, on its pins.
 *
 *   - SSPDR's writes go to an eight-entry transmit FIFO (lost when it is
 *     full), its reads come from an eight-entry receive FIFO (0 when it is
 *     empty); SSPSR's TFE, TNF, RNE, RFF and BSY follow them;
 *   - with SSPCR1.SSE (and MS 0: master), a frame starts whenever the
 *     transmit FIFO has one: DSS + 1 bits (4 to 16), MSB first, Motorola's
 *     format (FRF 0) in the mode SPO and SPH give: with SPH 0 the data is
 *     valid before the leading edge and sampled on it, with SPH 1 it
 *     changes on the leading edge and is sampled on the trailing one; SCK
 *     idles at SPO;
 *   - the bit rate is clk_peri / (CPSDVSR * (1 + SCR)); CSn (SSPFSSOUT) is
 *     low from half a bit before the frame to its end, and with SPH 0 high
 *     for half a bit between frames, with SPH 1 low across frames that
 *     follow each other;
 *   - at a frame's end the received bits go to the receive FIFO, or, when
 *     it is full, are lost and set RORRIS; LBM makes the receive input the
 *     transmit output;
 *   - SSPRIS: TXRIS while the transmit FIFO holds four or fewer, RXRIS
 *     while the receive FIFO holds four or more, RORRIS (cleared by
 *     SSPICR.RORIC); SSPMIS is SSPRIS & SSPIMSC, and raises SPIn_IRQ;
 *   - the pins are those whose FUNCSEL is SPI (1) for this unit (io.c):
 *     SCK, TX and CSn driven, RX (MISO) the pin's input;
 *   - an edge is a device event, at most one in an instruction: a clock
 *     faster than the instructions that wait on it is stretched to them.
 *
 * Not modelled: slave mode (MS), SOD, TI's synchronous serial and
 * National's Microwire formats (FRF 1, 2), the receive timeout (RTRIS),
 * DMA requests. */
#include <string.h>

#include "mcu/rp2040/rp2040.h"

#define CR0_SPO (1u << 6)
#define CR0_SPH (1u << 7)
#define CR1_LBM (1u << 0)
#define CR1_SSE (1u << 1)
#define CR1_MS (1u << 2)
#define SR_TFE (1u << 0)
#define SR_TNF (1u << 1)
#define SR_RNE (1u << 2)
#define SR_RFF (1u << 3)
#define SR_BSY (1u << 4)
#define RIS_ROR (1u << 0)
#define RIS_RX (1u << 2)
#define RIS_TX (1u << 3)
#define DEPTH 8

struct rpspi {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr0, *cr1, *dr, *sr, *cpsr, *imsc, *ris, *mis, *icr;
    int unit, irq, running;
    u16 tx[DEPTH], rx[DEPTH];
    int ntx, nrx;
    u32 out, in;     /* the frame's shift registers */
    int bits, bit;   /* the frame's length and the bit under way */
    int phase;       /* 0: idle, 1: the leading edge next, 2: the trailing, 3: the gap between frames */
    int busy;
    u64 left, since; /* core cycles to the next step */
    int sck, mosi, csn; /* the levels driven */
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static int on(const struct rpspi *k) { return (reg(k->cr1) & (CR1_SSE | CR1_MS)) == CR1_SSE; }

static const struct rpspi *spi_unit(struct esk_mach *s, int unit)
{
    char name[8] = "SPIx";
    struct esk_rf_periph *p;
    name[3] = (char)('0' + unit);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_rp2040_spi_ops ? (const struct rpspi *)p->ctx : 0;
}

int esk_rp2040_spi_out(struct esk_mach *s, int unit, int signal, int *out)
{
    const struct rpspi *k = spi_unit(s, unit);
    if (!k || esk_rf_gated(k->p))
        return 0;
    switch (signal) {
    case RP_SPI_CSN:
        *out = on(k) ? k->csn : 1;
        return 1;
    case RP_SPI_SCK:
        *out = on(k) ? k->sck : (reg(k->cr0) & CR0_SPO) != 0;
        return 1;
    case RP_SPI_TX:
        *out = k->mosi;
        return 1;
    default:
        return 0; /* RX is an input */
    }
}

static void pins(struct rpspi *k) { esk_rp2040_pins_changed(k->s); }

static int miso(struct rpspi *k)
{
    int level = 0;
    if (reg(k->cr1) & CR1_LBM)
        return k->mosi;
    if (esk_rp2040_func_in(k->s, RP_FUNC_SPI, k->unit, RP_SPI_RX, &level))
        return level;
    return 0;
}

/* core cycles per half bit */
static u64 half(const struct rpspi *k)
{
    u32 sys = esk_rp2040_hz(k->s, RP_CLK_SYS), peri = esk_rp2040_hz(k->s, RP_CLK_PERI);
    u32 cpsdvsr = reg(k->cpsr) & 0xfeu, scr = reg(k->cr0) >> 8 & 0xffu;
    u64 n;
    if (!peri)
        return (u64)1 << 62; /* clk_peri stopped: the frame does not go on */
    if (!sys)
        sys = esk_mach_core_hz(k->s);
    if (cpsdvsr < 2)
        cpsdvsr = 2;
    n = ((u64)cpsdvsr * (1u + scr) * sys + (u64)peri - 1u) / ((u64)peri * 2u);
    return n ? n : 1;
}

static void irq(struct rpspi *k)
{
    u32 ris = reg(k->ris) & RIS_ROR;
    u32 was = reg(k->mis), mis;
    if (k->ntx <= DEPTH / 2)
        ris |= RIS_TX;
    if (k->nrx >= DEPTH / 2)
        ris |= RIS_RX;
    esk_rf_hw(k->ris, ris);
    mis = ris & reg(k->imsc) & 15u;
    esk_rf_hw(k->mis, mis);
    if (mis & ~was)
        esk_rf_interrupt(k->p, k->irq);
}

static void status(struct rpspi *k)
{
    u32 sr = 0;
    if (!k->ntx)
        sr |= SR_TFE;
    if (k->ntx < DEPTH)
        sr |= SR_TNF;
    if (k->nrx)
        sr |= SR_RNE;
    if (k->nrx == DEPTH)
        sr |= SR_RFF;
    if (k->busy || (k->ntx && on(k)))
        sr |= SR_BSY;
    esk_rf_hw(k->sr, sr);
    irq(k);
}

static void wait(struct rpspi *k)
{
    k->left = half(k);
    k->since = k->s->cycles;
    esk_rf_clock(k->p, &k->running, 1);
}

static int out_bit(const struct rpspi *k, int i) { return (int)(k->out >> (k->bits - 1 - i) & 1u); }

/* the next frame from the transmit FIFO: CSn low, half a bit before SCK */
static void start(struct rpspi *k)
{
    k->out = k->tx[0];
    k->ntx--;
    memmove(k->tx, k->tx + 1, (size_t)k->ntx * sizeof k->tx[0]);
    k->in = 0;
    k->bits = (int)(reg(k->cr0) & 15u) + 1;
    if (k->bits < 4)
        k->bits = 4;
    k->out &= (1u << k->bits) - 1u;
    k->bit = 0;
    k->busy = 1;
    k->csn = 0;
    k->sck = (reg(k->cr0) & CR0_SPO) != 0;
    if (!(reg(k->cr0) & CR0_SPH))
        k->mosi = out_bit(k, 0);
    pins(k);
    status(k);
    k->phase = 1; /* the leading edge, half a bit on */
    wait(k);
}

static void frame_end(struct rpspi *k)
{
    if (k->nrx < DEPTH)
        k->rx[k->nrx++] = (u16)k->in;
    else
        esk_rf_hw(k->ris, reg(k->ris) | RIS_ROR);
    if (k->ntx && on(k)) {
        if (reg(k->cr0) & CR0_SPH) {
            start(k); /* CSn stays low into the next frame */
            return;
        }
        k->csn = 1; /* SPH 0: CSn pulses high between frames */
        k->phase = 3;
        pins(k);
        status(k);
        wait(k);
        return;
    }
    k->busy = 0;
    k->csn = 1;
    k->phase = 0;
    pins(k);
    status(k);
    esk_rf_clock(k->p, &k->running, 0);
}

static void edge(struct rpspi *k)
{
    int sph = (reg(k->cr0) & CR0_SPH) != 0, spo = (reg(k->cr0) & CR0_SPO) != 0;
    if (k->phase == 3) {
        k->phase = 0;
        start(k);
        return;
    }
    if (k->phase == 1) {
        k->sck = !spo;
        if (sph)
            k->mosi = out_bit(k, k->bit);
        pins(k);
        if (!sph && miso(k))
            k->in |= 1u << (k->bits - 1 - k->bit);
        k->phase = 2;
        wait(k);
        return;
    }
    if (sph && miso(k))
        k->in |= 1u << (k->bits - 1 - k->bit);
    k->sck = spo;
    k->bit++;
    if (!sph && k->bit < k->bits)
        k->mosi = out_bit(k, k->bit);
    pins(k);
    if (k->bit < k->bits) {
        k->phase = 1;
        wait(k);
    } else {
        frame_end(k);
    }
}

static void spi_tick(void *ctx, u32 cycles)
{
    struct rpspi *k = (struct rpspi *)ctx;
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
    struct rpspi *k = (struct rpspi *)ctx;
    if (!k->running)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)(k->left ? k->left : 1);
    return 1;
}

static u32 spi_read(void *ctx, u32 off, int n)
{
    struct rpspi *k = (struct rpspi *)ctx;
    if (off == esk_rf_off(k->dr)) {
        u32 v = k->nrx ? k->rx[0] : 0;
        if (!k->s->bus.debug && k->nrx) {
            k->nrx--;
            memmove(k->rx, k->rx + 1, (size_t)k->nrx * sizeof k->rx[0]);
            status(k);
        }
        return v;
    }
    return esk_rf_read(k->p, off, n);
}

static void spi_write(void *ctx, u32 off, int n, u32 v)
{
    struct rpspi *k = (struct rpspi *)ctx;
    if (off == esk_rf_off(k->dr)) {
        if (k->ntx < DEPTH)
            k->tx[k->ntx++] = (u16)v;
        if (on(k) && !k->busy)
            start(k);
        else
            status(k);
        return;
    }
    if (off == esk_rf_off(k->icr)) {
        if (v & RIS_ROR)
            esk_rf_hw(k->ris, reg(k->ris) & ~(u64)RIS_ROR);
        status(k);
        return;
    }
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->cr0) || off == esk_rf_off(k->cr1)) {
        if (!on(k)) {
            /* disabled: the frame under way stops, the FIFOs keep their
             * contents */
            k->busy = 0;
            k->phase = 0;
            k->csn = 1;
            esk_rf_clock(k->p, &k->running, 0);
        } else if (!k->busy) {
            k->sck = (reg(k->cr0) & CR0_SPO) != 0;
            if (k->ntx)
                start(k);
        }
        pins(k);
    }
    status(k);
}

static void spi_reset(void *ctx)
{
    struct rpspi *k = (struct rpspi *)ctx;
    k->running = 0;
    k->busy = 0;
    k->phase = 0;
    k->ntx = k->nrx = 0;
    k->sck = k->mosi = 0;
    k->csn = 1;
    k->left = 0;
    status(k);
}

const struct esk_dev_ops esk_rp2040_spi_ops = {
    "rp2040-spi", spi_read, spi_write, spi_reset, spi_tick, spi_next,
};

void *esk_rp2040_spi_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct rpspi k, *c;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    k.cr0 = esk_rf_reg(p, "SSPCR0");
    k.cr1 = esk_rf_reg(p, "SSPCR1");
    k.dr = esk_rf_reg(p, "SSPDR");
    k.sr = esk_rf_reg(p, "SSPSR");
    k.cpsr = esk_rf_reg(p, "SSPCPSR");
    k.imsc = esk_rf_reg(p, "SSPIMSC");
    k.ris = esk_rf_reg(p, "SSPRIS");
    k.mis = esk_rf_reg(p, "SSPMIS");
    k.icr = esk_rf_reg(p, "SSPICR");
    if (!k.cr0 || !k.cr1 || !k.dr || !k.sr || !k.cpsr || !k.imsc || !k.ris || !k.mis || !k.icr ||
        strncmp(p->name, "SPI", 3) || p->name[3] < '0' || p->name[3] > '1')
        return 0;
    k.unit = p->name[3] - '0';
    k.irq = esk_rf_irq(p, "SPI");
    k.csn = 1;
    c = (struct rpspi *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    return c;
}
