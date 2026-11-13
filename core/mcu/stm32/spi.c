/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* spi.c -- an STM32 SPI of the SR/DR layout (RM0008 25, RM0390 26: the F1,
 * F2 and F4's), master or slave, over the register file, on its pins.
 *
 *   - a master (CR1.MSTR with SPE) drives SCK, idle at CPOL, and MOSI,
 *     holding its last bit; MISO is sampled. A write of DR fills the
 *     transmit buffer (TXE clears); with the shift register idle the buffer
 *     moves into it at once (TXE sets, BSY sets) and the frame starts: 8 or
 *     16 bits (DFF), MSB or LSB first (LSBFIRST), at PCLK divided by 2 to
 *     256 (BR), in the mode CPOL and CPHA give: with CPHA 0 the first bit is
 *     out before the leading edge and the input is sampled on it, with
 *     CPHA 1 the output changes on the leading edge and the input is
 *     sampled on the trailing one;
 *   - at a frame's end the received bits are DR's (RXNE sets; with RXNE
 *     still set OVR sets and the frame is lost, DR keeping the older data)
 *     and a waiting buffer starts the next frame at once, else BSY clears.
 *     Clearing SPE during a frame lets it finish, then stops. Reading DR
 *     clears RXNE; reading DR then SR clears OVR;
 *   - receive-only (RXONLY, or BIDIMODE with BIDIOE 0): the master clocks
 *     frames one after the other from SPE's setting until it clears, its
 *     output let go; bidirectional mode (BIDIMODE) moves the data on one
 *     line, the master's MOSI or the slave's MISO, out with BIDIOE 1 (and
 *     nothing received: no RXNE, no OVR), in with BIDIOE 0;
 *   - NSS: with SSM the internal NSS is SSI and the pin is free; else, a
 *     master with SSOE drives it low while enabled, and without SSOE reads
 *     it: low (or SSI 0 with SSM) is a mode fault: MODF sets, SPE and MSTR
 *     clear, and stay write-protected until MODF clears (reading SR, then
 *     writing CR1);
 *   - a slave (SPE without MSTR) is selected while NSS is low (SSI 0 with
 *     SSM); selected, it follows SCK's edges from its pin in the same modes,
 *     samples MOSI and drives MISO (let go while not selected). The shift
 *     register takes the transmit buffer while no frame is under way (TXE
 *     sets), else at the frame's end; with nothing written it sends what it
 *     holds, the frame it received last. NSS rising during a frame does not
 *     end it: the bit count carries on at the next selection, as the part's
 *     does, until SPE clears;
 *   - the TI mode (CR2.FRF, the F4's): SCK idles low, data change on the
 *     rising edge and are sampled on the falling one; the master pulses NSS
 *     high for one clock before a frame's first bit (during the last bit of
 *     the previous one when frames follow each other), the slave starts a
 *     frame on that pulse, sets FRE if it comes during a frame, and lets
 *     MISO go between frames;
 *   - CRC (CRCEN): TXCRCR and RXCRCR take every data frame sent and
 *     received, 8 or 16 bits as DFF, with CRCPR's polynomial, bit by bit in
 *     the order they travel; setting CRCEN clears them. With CRCNEXT set
 *     (or after a DMA stream's last item, DMA's end of transfer, for the
 *     transmit or the receive request) the frame after the buffered data is
 *     the CRC: TXCRCR goes out, CRCNEXT clears, and the frame received then
 *     is compared with RXCRCR: CRCERR (cleared by writing 0) when they
 *     differ; the received CRC is DR's as any frame;
 *   - CR2's TXEIE, RXNEIE and ERRIE (MODF, OVR, CRCERR, FRE) keep SPIx's
 *     interrupt pending while their flag is set; TXDMAEN and RXDMAEN make
 *     the DMA requests SPIx_TX (while TXE) and SPIx_RX (while RXNE);
 *   - the pins are the alternate functions the GPIO selects (the F4's
 *     AFR) or AFIO's (the F1: SPI1 NSS PA4, SCK PA5, MISO PA6, MOSI PA7,
 *     remapped PA15, PB3, PB4, PB5; SPI2 PB12 to PB15; SPI3 PA15, PB3, PB4,
 *     PB5); an input is the circuit's level on its pin;
 *   - a master's edge is a device event, at most one in an instruction: a
 *     clock faster than the instructions that wait on it is stretched to
 *     them. A slave's edges are its SCK pin's changes, as the circuit makes
 *     them.
 *
 * Not modelled: I2S (I2SCFGR's I2SMOD: the registers keep their values;
 * no part in the catalog speaks I2S yet), the multimaster arbitration a
 * MODF serves beyond the fault itself, the TI slave's MISO release half a
 * clock after the last bit (it is let go at the frame's end). */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

#define CR1_CPHA (1u << 0)
#define CR1_CPOL (1u << 1)
#define CR1_MSTR (1u << 2)
#define CR1_SPE (1u << 6)
#define CR1_LSBFIRST (1u << 7)
#define CR1_SSI (1u << 8)
#define CR1_SSM (1u << 9)
#define CR1_RXONLY (1u << 10)
#define CR1_DFF (1u << 11)
#define CR1_CRCL (1u << 11) /* the FIFO SPI's: the CRC's length */
#define CR1_CRCNEXT (1u << 12)
#define CR1_CRCEN (1u << 13)
#define CR1_BIDIOE (1u << 14)
#define CR1_BIDIMODE (1u << 15)
#define CR2_RXDMAEN (1u << 0)
#define CR2_TXDMAEN (1u << 1)
#define CR2_SSOE (1u << 2)
#define CR2_NSSP (1u << 3)
#define CR2_FRF (1u << 4)
#define CR2_ERRIE (1u << 5)
#define CR2_RXNEIE (1u << 6)
#define CR2_TXEIE (1u << 7)
#define CR2_FRXTH (1u << 12)
#define CR2_LDMA_RX (1u << 13)
#define CR2_LDMA_TX (1u << 14)
#define SR_RXNE (1u << 0)
#define SR_TXE (1u << 1)
#define SR_CRCERR (1u << 4)
#define SR_MODF (1u << 5)
#define SR_OVR (1u << 6)
#define SR_BSY (1u << 7)
#define SR_FRE (1u << 8)
#define SR_FRLVL (3u << 9)
#define SR_FTLVL (3u << 11)

enum { P_NSS, P_SCK, P_MISO, P_MOSI };
static const char *const fn_names[4] = { "NSS", "SCK", "MISO", "MOSI" };

struct spi {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr1, *cr2, *sr, *dr, *crcpr, *rxcrcr, *txcrcr;
    int unit, irq, apb, running, has_ti;
    char sig_tx[16], sig_rx[16]; /* its DMA requests */
    u32 tx_buf;
    int tx_full;
    u32 out, in;      /* the frame's shift registers */
    int bits, bit;    /* the frame's length and the bit under way (-1: TI's sync clock) */
    int leading;      /* a master's next edge is the bit's leading edge */
    int busy;         /* a frame is under way */
    int crc_frame;    /* the frame under way is the CRC */
    int fresh;        /* a slave's shift register holds data not yet sent */
    int crc_tx_end, crc_rx_end; /* DMA's end of transfer: the CRC follows */
    u64 left, since;  /* a master: core cycles to the next edge */
    u32 rx;           /* the receive buffer */
    int sck, mosi;    /* a master's levels */
    int miso;         /* a slave's output level, -1 let go */
    int nss_out;      /* a TI master's frame pulse */
    int synced;       /* a TI master pulsed NSS in the last bit: no sync clock */
    int armed;        /* a TI slave saw the pulse: a frame starts */
    int in_sck, in_nss; /* the levels last seen on the pins, -1 none yet */
    int ovr_dr, modf_sr; /* the clearing sequences' first steps */
    /* the FIFO SPI (RM0440 42, an SR with FRLVL): 32-bit transmit and
     * receive FIFOs of frames, each one byte (DS <= 8) or two */
    int fifo;
    u16 txq[4], rxq[4];
    int txn, rxn;
    int gap;            /* NSSP: half bits of NSS high left before the next frame */
    int crc_part;       /* a 16-bit CRC after 8-bit frames: its second frame is next */
    u32 crc_word, crc_in; /* the CRC going out, and coming in, in two frames */
    int ldma_tx, ldma_rx; /* LDMA: the DMA's last access carries one frame */
};

static u32 reg(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }

static int master(const struct spi *k) { return (reg(k->cr1) & CR1_MSTR) != 0; }
static int enabled(const struct spi *k) { return (reg(k->cr1) & CR1_SPE) != 0; }
/* driving its pins: enabled, or finishing the frame it was disabled in */
static int active(const struct spi *k) { return enabled(k) || k->busy; }
static int ti(const struct spi *k) { return k->has_ti && (reg(k->cr2) & CR2_FRF); }
static int cpol(const struct spi *k) { return ti(k) ? 0 : (reg(k->cr1) & CR1_CPOL) != 0; }
static int cpha(const struct spi *k) { return ti(k) ? 1 : (reg(k->cr1) & CR1_CPHA) != 0; }
static int nssp(const struct spi *k);

/* the data go out, and come in */
static int sending(const struct spi *k)
{
    u32 c1 = reg(k->cr1);
    return (c1 & CR1_BIDIMODE) ? (c1 & CR1_BIDIOE) != 0 : !(c1 & CR1_RXONLY);
}

static int receiving(const struct spi *k)
{
    u32 c1 = reg(k->cr1);
    return !((c1 & CR1_BIDIMODE) && (c1 & CR1_BIDIOE));
}

/* a master that clocks on its own, frame after frame */
static int receive_only(const struct spi *k) { return !sending(k); }

/* ---- the pins ------------------------------------------------------------------ */

/* pin `fn` of the unit: port * 16 + bit, or -1 */
static int pin_of(const struct spi *k, int fn)
{
    struct esk_mach *s = k->s;
    if (s->rf && s->rf->naf) {
        char sig[24];
        int at;
        snprintf(sig, sizeof sig, "SPI%d_%s", k->unit, fn_names[fn]);
        at = esk_stm32f4_af_pin(s, sig);
        return at < 0 ? -1 : at / 32 * 16 + at % 32;
    } else {
        /* RM0008 table 25 and 9.3.10 */
        static const int spi1[2][4] = { { 4, 5, 6, 7 }, { 15, 16 + 3, 16 + 4, 16 + 5 } };
        static const int spi3[4] = { 15, 16 + 3, 16 + 4, 16 + 5 };
        if (k->unit == 1)
            return spi1[esk_stm32f1_mapr(s) & 1u][fn];
        if (k->unit == 2)
            return 16 + 12 + fn;
        if (k->unit == 3)
            return spi3[fn];
    }
    return -1;
}

/* the level on pin `fn`: the circuit's, else what its GPIO reads (its pull) */
static int line(const struct spi *k, int fn)
{
    struct esk_mach *s = k->s;
    int at = pin_of(k, fn), level = 0;
    char name[8] = "GPIOx";
    struct esk_rf_periph *p;
    struct esk_rf_reg *idr;
    if (at < 0)
        return 0;
    if (s->host.pin_in && s->host.pin_in(s->host.ctx, at / 16, at % 16, &level))
        return level != 0;
    name[4] = (char)('A' + at / 16);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    idr = p ? esk_rf_reg(p, "IDR") : 0;
    return idr ? (int)(idr->value >> (at % 16) & 1u) : 0;
}

/* the slave is selected: SSI 0 with SSM, else NSS low (TI: always) */
static int selected(const struct spi *k)
{
    u32 c1 = reg(k->cr1);
    if (ti(k))
        return 1;
    if (c1 & CR1_SSM)
        return !(c1 & CR1_SSI);
    return !line(k, P_NSS);
}

/* the level the unit drives on pin `fn`, or -1 */
static int drive(const struct spi *k, int fn)
{
    u32 c1 = reg(k->cr1), c2 = reg(k->cr2);
    if (!active(k))
        return -1;
    if (master(k)) {
        if (fn == P_SCK)
            return k->sck;
        if (fn == P_MOSI)
            return sending(k) ? k->mosi : -1;
        if (fn == P_NSS) {
            if (ti(k) || nssp(k))
                return k->nss_out;
            return (c2 & CR2_SSOE) && !(c1 & CR1_SSM) ? 0 : -1;
        }
        return -1;
    }
    if (fn == P_MISO && sending(k) && selected(k))
        return k->miso;
    return -1;
}

static void pins(struct spi *k)
{
    int done[4], n = 0, fn, i;
    for (fn = 0; fn < 4; fn++) {
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

/* ---- flags, interrupt, DMA ------------------------------------------------ */

/* the DMA requests and the interrupt as the flags are now: a request the
 * DMA serves at once changes them (it writes DR, it reads it) */
static void levels(struct spi *k);

static void update(struct spi *k)
{
    u32 sr, c2 = reg(k->cr2);
    levels(k);
    esk_stm32_dma_request(k->s, k->sig_tx, enabled(k) && (c2 & CR2_TXDMAEN) && (reg(k->sr) & SR_TXE));
    esk_stm32_dma_request(k->s, k->sig_rx, enabled(k) && (c2 & CR2_RXDMAEN) && (reg(k->sr) & SR_RXNE));
    sr = reg(k->sr);
    if (((sr & SR_TXE) && (c2 & CR2_TXEIE)) || ((sr & SR_RXNE) && (c2 & CR2_RXNEIE)) ||
        ((sr & (SR_MODF | SR_OVR | SR_CRCERR | SR_FRE)) && (c2 & CR2_ERRIE)))
        esk_rf_interrupt(k->p, k->irq);
}

static void sr_set(struct spi *k, u32 set, u32 clear)
{
    esk_rf_hw(k->sr, (k->sr->value & ~(u64)clear) | set);
}

/* ---- CRC -------------------------------------------------------------------------- */

static int width(const struct spi *k)
{
    if (k->fifo) {
        u32 ds = reg(k->cr2) >> 8 & 15u;
        return ds < 3 ? 8 : (int)ds + 1; /* DS: 4 to 16 bits */
    }
    return (reg(k->cr1) & CR1_DFF) ? 16 : 8;
}

/* the CRC's length: the FIFO SPI's CRCL, else the frame's */
static int crc_len(const struct spi *k) { return k->fifo ? ((reg(k->cr1) & CR1_CRCL) ? 16 : 8) : width(k); }

/* ---- the FIFOs (RM0440 42.5.9) ------------------------------------------------------ */

/* a frame takes one byte of a 32-bit FIFO (DS <= 8) or two */
static int frame_bytes(const struct spi *k) { return width(k) > 8 ? 2 : 1; }
static u32 frame_mask(const struct spi *k) { return (1u << width(k)) - 1u; }

/* TXE while the transmit FIFO is at most half full; RXNE once the receive
 * FIFO holds FRXTH's quarter (8 bits) or half (16 bits), a quarter for the
 * DMA's last access with LDMA_RX; FTLVL and FRLVL their levels (3/4 and
 * full both read full) */
static void levels(struct spi *k)
{
    int tb, rb, thr;
    u32 sr;
    if (!k->fifo)
        return;
    tb = k->txn * frame_bytes(k);
    rb = k->rxn * frame_bytes(k);
    thr = (reg(k->cr2) & CR2_FRXTH) || k->ldma_rx ? 1 : 2;
    sr = reg(k->sr) & ~(SR_FRLVL | SR_FTLVL | SR_TXE | SR_RXNE);
    if (tb <= 2)
        sr |= SR_TXE;
    if (rb >= thr)
        sr |= SR_RXNE;
    sr |= (u32)(rb > 3 ? 3 : rb) << 9 | (u32)(tb > 3 ? 3 : tb) << 11;
    esk_rf_hw(k->sr, sr);
}

static void tx_push(struct spi *k, u32 v)
{
    if ((k->txn + 1) * frame_bytes(k) <= 4)
        k->txq[k->txn++] = (u16)(v & frame_mask(k));
}

static u32 rx_pop(struct spi *k)
{
    u32 v;
    if (!k->rxn)
        return 0;
    v = k->rxq[0];
    memmove(k->rxq, k->rxq + 1, (size_t)(--k->rxn) * sizeof k->rxq[0]);
    return v;
}

/* a frame waits to go out */
static int tx_ready(const struct spi *k) { return k->fifo ? k->txn > 0 : k->tx_full; }

/* the frame's bit i (0 first on the line) of a `bits`-bit word */
static int line_pos(const struct spi *k, int i) { return (reg(k->cr1) & CR1_LSBFIRST) ? i : k->bits - 1 - i; }

static u32 crc_step(const struct spi *k, u32 crc, u32 word)
{
    int w = crc_len(k), i;
    u32 mask = w == 16 ? 0xffffu : 0xffu, poly = reg(k->crcpr) & mask;
    for (i = 0; i < k->bits; i++) {
        u32 b = word >> line_pos(k, i) & 1u, fb = (crc >> (w - 1) & 1u) ^ b;
        crc = (crc << 1) & mask;
        if (fb)
            crc ^= poly;
    }
    return crc;
}

static int crc_due(const struct spi *k)
{
    return k->crc_part ||
           ((reg(k->cr1) & CR1_CRCEN) && ((reg(k->cr1) & CR1_CRCNEXT) || k->crc_tx_end || k->crc_rx_end));
}

/* a 16-bit CRC after 8-bit frames goes out as two frames, its first byte
 * on the line first */
static u32 crc_piece(const struct spi *k, int second)
{
    int lsb = (reg(k->cr1) & CR1_LSBFIRST) != 0;
    return (second ? !lsb : lsb) ? (k->crc_word & 0xffu) : (k->crc_word >> 8 & 0xffu);
}

/* ---- frames ------------------------------------------------------------------------ */

/* the next frame's shift register: the transmit buffer, else the CRC when
 * its phase is due, else what it holds (a slave sends back the frame it
 * received) */
static void load(struct spi *k)
{
    k->crc_frame = 0;
    k->bits = width(k);
    k->fresh = 1;
    if (k->crc_part) {
        k->out = crc_piece(k, 1);
        k->crc_part = 0;
        k->crc_frame = 2;
    } else if (tx_ready(k) && sending(k)) {
        if (k->fifo) {
            k->out = k->txq[0];
            memmove(k->txq, k->txq + 1, (size_t)(--k->txn) * sizeof k->txq[0]);
            levels(k);
        } else {
            k->out = k->tx_buf;
            k->tx_full = 0;
            sr_set(k, SR_TXE, 0);
        }
    } else if (crc_due(k)) {
        k->crc_word = reg(k->txcrcr);
        if (crc_len(k) > k->bits) {
            k->out = crc_piece(k, 0); /* the second follows */
            k->crc_part = 1;
            k->crc_frame = 1;
        } else {
            k->out = k->crc_word;
            k->crc_frame = 2;
        }
        k->crc_tx_end = k->crc_rx_end = 0;
        esk_rf_hw(k->cr1, k->cr1->value & ~(u64)CR1_CRCNEXT);
    } else {
        k->out = k->in;
        k->fresh = 0;
    }
    k->in = 0;
    k->bit = 0;
}

static int out_bit(const struct spi *k, int i) { return (int)(k->out >> line_pos(k, i) & 1u); }

static void in_bit(struct spi *k, int i, int v)
{
    if (v)
        k->in |= 1u << line_pos(k, i);
}

/* the received frame to DR, the CRCs */
static void received(struct spi *k)
{
    u32 mask = crc_len(k) == 16 ? 0xffffu : 0xffu;
    if (k->crc_frame == 1) {
        k->crc_in = k->in & 0xffu; /* the CRC's first frame */
    } else if (k->crc_frame) {
        u32 got = k->in;
        if (crc_len(k) > k->bits)
            got = (reg(k->cr1) & CR1_LSBFIRST) ? (k->in & 0xffu) << 8 | k->crc_in : k->crc_in << 8 | (k->in & 0xffu);
        if (receiving(k) && (got & mask) != (reg(k->rxcrcr) & mask))
            sr_set(k, SR_CRCERR, 0);
    } else if (reg(k->cr1) & CR1_CRCEN) {
        if (sending(k))
            esk_rf_hw(k->txcrcr, crc_step(k, reg(k->txcrcr), k->out));
        if (receiving(k))
            esk_rf_hw(k->rxcrcr, crc_step(k, reg(k->rxcrcr), k->in));
    }
    k->crc_frame = 0;
    if (!receiving(k))
        return;
    if (k->fifo) {
        /* a full receive FIFO: OVR, the frame lost */
        if ((k->rxn + 1) * frame_bytes(k) > 4)
            sr_set(k, SR_OVR, 0);
        else
            k->rxq[k->rxn++] = (u16)(k->in & frame_mask(k));
        levels(k);
        return;
    }
    if (k->sr->value & SR_RXNE) {
        sr_set(k, SR_OVR, 0);
    } else {
        k->rx = k->in;
        esk_rf_hw(k->dr, k->rx);
        sr_set(k, SR_RXNE, 0);
    }
}

/* ---- the master ---------------------------------------------------------------- */

/* core cycles per half bit */
static u32 half(const struct spi *k)
{
    u32 core = esk_mach_core_hz(k->s), pclk = esk_stm32_pclk(k->s, k->apb);
    u32 ratio = core && pclk && core >= pclk ? core / pclk : 1;
    u32 div = 2u << (reg(k->cr1) >> 3 & 7u);
    return ratio * div / 2u;
}

static void wait(struct spi *k)
{
    k->left = half(k);
    k->since = k->s->cycles;
    esk_rf_clock(k->p, &k->running, 1);
}

/* a frame follows the one under way */
static int next_waiting(const struct spi *k)
{
    return enabled(k) && master(k) && ((tx_ready(k) && sending(k)) || crc_due(k) || receive_only(k));
}

/* NSS pulse mode: a Motorola master with CPHA 0 driving NSS (RM0440 42.5.12) */
static int nssp(const struct spi *k)
{
    u32 c1 = reg(k->cr1), c2 = reg(k->cr2);
    return k->fifo && master(k) && (c2 & CR2_NSSP) && (c2 & CR2_SSOE) && !(c1 & CR1_SSM) && !ti(k) &&
           !(c1 & CR1_CPHA);
}

static void start(struct spi *k)
{
    if (nssp(k))
        k->nss_out = 0;
    load(k);
    k->leading = 1;
    k->busy = 1;
    sr_set(k, SR_BSY, 0);
    if (ti(k) && !k->synced)
        k->bit = -1; /* one clock with NSS high before the first bit */
    k->synced = 0;
    if (!cpha(k) && k->bit == 0)
        k->mosi = out_bit(k, 0);
    pins(k);
    update(k);
    wait(k);
}

static void stop(struct spi *k)
{
    k->busy = 0;
    sr_set(k, 0, SR_BSY);
    esk_rf_clock(k->p, &k->running, 0);
}

static void frame_end(struct spi *k)
{
    received(k);
    update(k); /* a DMA reading the last item says the CRC is next */
    if (next_waiting(k)) {
        if (nssp(k)) {
            /* NSS high for one clock between the frames, SCK idle */
            k->nss_out = 1;
            k->gap = 2;
            pins(k);
            wait(k);
            return;
        }
        start(k);
        return;
    }
    if (nssp(k))
        k->nss_out = 1; /* and high after the last one */
    stop(k);
    pins(k);
    update(k);
}

static void edge(struct spi *k)
{
    int ph = cpha(k), pol = cpol(k);
    if (k->gap) {
        if (--k->gap) {
            wait(k);
            return;
        }
        start(k);
        return;
    }
    if (k->leading) {
        k->sck = !pol;
        if (k->bit < 0) {
            k->nss_out = 1;
        } else {
            if (ti(k)) {
                /* frames that follow each other: the pulse in the last bit */
                k->synced = k->bit == k->bits - 1 && next_waiting(k);
                k->nss_out = k->synced;
            }
            if (ph)
                k->mosi = out_bit(k, k->bit);
            else
                in_bit(k, k->bit, line(k, (reg(k->cr1) & CR1_BIDIMODE) ? P_MOSI : P_MISO));
        }
        pins(k);
        k->leading = 0;
        wait(k);
        return;
    }
    k->sck = pol;
    if (k->bit < 0) {
        k->bit = 0; /* NSS falls with the first bit's rising edge */
        if (!ph)
            k->mosi = out_bit(k, 0);
        pins(k);
        k->leading = 1;
        wait(k);
        return;
    }
    if (ph)
        in_bit(k, k->bit, line(k, (reg(k->cr1) & CR1_BIDIMODE) ? P_MOSI : P_MISO));
    k->bit++;
    if (!ph && k->bit < k->bits)
        k->mosi = out_bit(k, k->bit);
    pins(k);
    k->leading = 1;
    if (k->bit < k->bits)
        wait(k);
    else
        frame_end(k);
}

/* a master without SSOE whose NSS is low, or with SSM and SSI 0: MODF */
static void mode_fault(struct spi *k)
{
    u32 c1 = reg(k->cr1), c2 = reg(k->cr2);
    int low;
    if (!master(k) || !enabled(k) || ti(k))
        return;
    if (c1 & CR1_SSM)
        low = !(c1 & CR1_SSI);
    else
        low = !(c2 & CR2_SSOE) && !line(k, P_NSS);
    if (!low)
        return;
    sr_set(k, SR_MODF, 0);
    esk_rf_hw(k->cr1, k->cr1->value & ~(u64)(CR1_SPE | CR1_MSTR));
    k->busy = 0;
    k->tx_full = 0;
    k->txn = 0;
    k->gap = k->crc_part = 0;
    sr_set(k, SR_TXE, SR_BSY);
    esk_rf_clock(k->p, &k->running, 0);
    pins(k);
    update(k);
}

static void spi_tick(void *ctx, u32 cycles)
{
    struct spi *k = (struct spi *)ctx;
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
    struct spi *k = (struct spi *)ctx;
    if (!k->running)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)(k->left ? k->left : 1);
    return 1;
}

/* ---- the slave ------------------------------------------------------------------ */

static void slave_out(struct spi *k, int i)
{
    k->miso = i < k->bits ? out_bit(k, i) : -1;
}

/* the shift register takes the buffer, the CRC or what it holds, and its
 * first bit goes out (with CPHA 0) */
static void slave_load(struct spi *k)
{
    load(k);
    if (!cpha(k))
        slave_out(k, 0);
    else if (ti(k))
        k->miso = -1;
    pins(k);
    update(k);
}

static void slave_edge(struct spi *k, int level)
{
    int ph = cpha(k), lead = level != cpol(k);
    int data = (reg(k->cr1) & CR1_BIDIMODE) ? P_MISO : P_MOSI;
    if (master(k) || !enabled(k) || !selected(k))
        return;
    if (ti(k) && !k->busy && !k->armed && lead)
        return; /* waits for the frame pulse */
    if (lead) {
        if (!k->busy) {
            k->busy = 1;
            k->armed = k->fresh = 0;
            sr_set(k, SR_BSY, 0);
        }
        if (ph)
            slave_out(k, k->bit);
        else
            in_bit(k, k->bit, line(k, data));
        pins(k);
        return;
    }
    if (k->busy) {
        if (ph)
            in_bit(k, k->bit, line(k, data));
        k->bit++;
        if (k->bit >= k->bits) {
            received(k);
            k->busy = 0;
            sr_set(k, 0, SR_BSY);
            load(k);
            if (ti(k))
                k->miso = -1;
            else if (!ph)
                slave_out(k, 0);
            update(k);
        } else if (!ph) {
            slave_out(k, k->bit);
        }
        pins(k);
    }
    if (ti(k) && line(k, P_NSS)) {
        /* the frame pulse, sampled on the falling edge */
        if (k->busy && k->bit > 0) {
            sr_set(k, SR_FRE, 0);
            k->busy = 0;
            sr_set(k, 0, SR_BSY);
            load(k);
            update(k);
        }
        k->armed = 1;
    }
}

/* ---- the registers ---------------------------------------------------------------- */

static u32 spi_read(void *ctx, u32 off, int n)
{
    struct spi *k = (struct spi *)ctx;
    u32 v = esk_rf_read(k->p, off, n);
    if (k->fifo && off == esk_rf_off(k->dr)) {
        /* the oldest frame, or two packed in a 16-bit access (DS <= 8) */
        if (k->s->bus.debug)
            return k->rxn ? k->rxq[0] : 0;
        k->ovr_dr = (k->sr->value & SR_OVR) != 0;
        v = rx_pop(k);
        if (n >= 2 && frame_bytes(k) == 1 && !k->ldma_rx)
            v |= rx_pop(k) << 8;
        k->ldma_rx = 0;
        esk_rf_hw(k->dr, v);
        update(k);
        return v;
    }
    if (k->s->bus.debug)
        return v;
    if (off == esk_rf_off(k->dr)) {
        k->ovr_dr = (k->sr->value & SR_OVR) != 0;
        sr_set(k, 0, SR_RXNE);
        update(k);
    } else if (off == esk_rf_off(k->sr)) {
        if (k->ovr_dr)
            sr_set(k, 0, SR_OVR);
        k->ovr_dr = 0;
        k->modf_sr = (v & SR_MODF) != 0;
        if (v & SR_FRE)
            sr_set(k, 0, SR_FRE);
    }
    return v;
}

static void spi_write(void *ctx, u32 off, int n, u32 v)
{
    struct spi *k = (struct spi *)ctx;
    if (off == esk_rf_off(k->dr)) {
        if (k->fifo) {
            /* one frame, or two packed in a 16-bit access (DS <= 8), but
             * the DMA's last with LDMA_TX */
            tx_push(k, v);
            if (n >= 2 && frame_bytes(k) == 1 && !k->ldma_tx)
                tx_push(k, v >> 8);
            k->ldma_tx = 0;
            levels(k);
        } else {
            k->tx_buf = v & ((reg(k->cr1) & CR1_DFF) ? 0xffffu : 0xffu);
            k->tx_full = 1;
            sr_set(k, 0, SR_TXE);
        }
        if (enabled(k) && master(k) && !k->busy)
            start(k);
        else if (enabled(k) && !master(k) && !k->busy && !k->fresh)
            slave_load(k); /* the shift register sent its data: the buffer goes in */
        update(k);
        return;
    }
    if (off == esk_rf_off(k->cr1)) {
        u32 was = reg(k->cr1);
        if (k->sr->value & SR_MODF) {
            if (k->modf_sr)
                sr_set(k, 0, SR_MODF);
            else /* SPE and MSTR stay as the fault left them */
                v = (v & ~(CR1_SPE | CR1_MSTR)) | (was & (CR1_SPE | CR1_MSTR));
        }
        k->modf_sr = 0;
        esk_rf_write(k->p, off, n, v);
        if ((reg(k->cr1) & CR1_CRCEN) && !(was & CR1_CRCEN)) {
            esk_rf_hw(k->txcrcr, 0);
            esk_rf_hw(k->rxcrcr, 0);
        }
        if ((was & CR1_SPE) && !enabled(k) && !master(k)) {
            /* a slave disabled: its bit count starts again */
            k->busy = k->armed = 0;
            k->bit = 0;
            sr_set(k, 0, SR_BSY);
        }
        if (!enabled(k) && !k->busy)
            esk_rf_clock(k->p, &k->running, 0);
        if (enabled(k) && master(k)) {
            if (!(was & (CR1_SPE)) || !(was & CR1_MSTR)) {
                k->sck = cpol(k);
                k->nss_out = 0;
                k->synced = 0;
            }
            mode_fault(k);
            if (enabled(k) && !k->busy && next_waiting(k))
                start(k);
        } else if (enabled(k) && !k->busy &&
                   (!(was & CR1_SPE) || (was & CR1_MSTR) || (((reg(k->cr1) & ~was) & CR1_CRCNEXT) && !k->fresh))) {
            slave_load(k);
        }
        pins(k);
        update(k);
        return;
    }
    if (off == esk_rf_off(k->sr)) {
        if (!(v & SR_CRCERR)) /* the only flag a write clears, with 0 */
            sr_set(k, 0, SR_CRCERR);
        update(k);
        return;
    }
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->cr2)) {
        if (k->fifo && (reg(k->cr2) >> 8 & 15u) < 3)
            esk_rf_hw(k->cr2, (k->cr2->value & ~(u64)0xF00u) | 0x700u); /* not used: 8 bits */
        if (enabled(k) && master(k) && !k->busy)
            k->sck = cpol(k);
        mode_fault(k);
        pins(k);
        update(k);
    }
}

static void spi_reset(void *ctx)
{
    struct spi *k = (struct spi *)ctx;
    k->running = 0;
    k->busy = k->tx_full = k->crc_frame = k->crc_tx_end = k->crc_rx_end = k->fresh = 0;
    k->rx = k->out = k->in = 0;
    k->bit = 0;
    k->bits = 8;
    k->sck = k->mosi = 0;
    k->miso = -1;
    k->nss_out = k->synced = k->armed = 0;
    k->ovr_dr = k->modf_sr = 0;
    k->left = 0;
    k->txn = k->rxn = k->gap = k->crc_part = 0;
    k->ldma_tx = k->ldma_rx = 0;
    k->crc_word = k->crc_in = 0;
    esk_rf_hw(k->sr, k->sr->value | SR_TXE);
    levels(k);
}

const struct esk_dev_ops esk_stm32_spi_ops = {
    "stm32-spi", spi_read, spi_write, spi_reset, spi_tick, spi_next,
};

/* ---- the signals ----------------------------------------------------------------- */

static struct spi *spi_named(struct esk_mach *s, const char *name)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_stm32_spi_ops && !esk_rf_gated(p) ? (struct spi *)p->ctx : 0;
}

/* "SPI2_SCK" -> SPI2 and its function, or 0 */
static struct spi *signal_of(struct esk_mach *s, const char *sig, int *fn)
{
    char name[8];
    const char *us = strchr(sig, '_');
    size_t l = us ? (size_t)(us - sig) : 0;
    int i;
    if (!us || l >= sizeof name || strncmp(sig, "SPI", 3))
        return 0;
    for (i = 0; i < 4; i++)
        if (!strcmp(us + 1, fn_names[i]))
            break;
    if (i == 4)
        return 0;
    memcpy(name, sig, l);
    name[l] = 0;
    *fn = i;
    return spi_named(s, name);
}

int esk_stm32_spi_signal(struct esk_mach *s, const char *sig)
{
    int fn;
    const struct spi *k = signal_of(s, sig, &fn);
    return k ? drive(k, fn) : -1;
}

void esk_stm32_spi_signal_input(struct esk_mach *s, const char *sig, int level)
{
    int fn;
    struct spi *k = signal_of(s, sig, &fn);
    if (!k)
        return;
    level = level != 0;
    if (fn == P_SCK) {
        int was = k->in_sck;
        k->in_sck = level;
        if (was >= 0 && was != level)
            slave_edge(k, level);
    } else if (fn == P_NSS && level != k->in_nss) {
        k->in_nss = level;
        if (master(k)) {
            mode_fault(k);
        } else if (enabled(k)) {
            if (!ti(k) && !level && !k->busy && !cpha(k))
                k->miso = out_bit(k, k->bit); /* selected: the first bit out */
            pins(k);
        }
    }
}

/* the DMA's items left for an SPI's request: with one left, LDMA_TX and
 * LDMA_RX make its last access carry one frame */
void esk_stm32_spi_dma_left(struct esk_mach *s, const char *sig, u32 left)
{
    char name[8];
    const char *us = strchr(sig, '_');
    size_t l = us ? (size_t)(us - sig) : 0;
    struct spi *k;
    if (left != 1 || !us || l >= sizeof name || strncmp(sig, "SPI", 3))
        return;
    memcpy(name, sig, l);
    name[l] = 0;
    k = spi_named(s, name);
    if (!k || !k->fifo || frame_bytes(k) != 1)
        return;
    if (!strcmp(us + 1, "TX") && (reg(k->cr2) & CR2_LDMA_TX))
        k->ldma_tx = 1;
    else if (!strcmp(us + 1, "RX") && (reg(k->cr2) & CR2_LDMA_RX)) {
        k->ldma_rx = 1;
        update(k);
    }
}

void esk_stm32_spi_dma_end(struct esk_mach *s, const char *sig)
{
    char name[8];
    const char *us = strchr(sig, '_');
    size_t l = us ? (size_t)(us - sig) : 0;
    struct spi *k;
    if (!us || l >= sizeof name || strncmp(sig, "SPI", 3))
        return;
    memcpy(name, sig, l);
    name[l] = 0;
    k = spi_named(s, name);
    if (!k || !(reg(k->cr1) & CR1_CRCEN))
        return;
    if (!strcmp(us + 1, "TX"))
        k->crc_tx_end = 1;
    else if (!strcmp(us + 1, "RX") && !(k->busy && k->crc_frame))
        k->crc_rx_end = 1; /* (unless the transmitter's CRC is already on its way) */
    if (enabled(k) && master(k) && !k->busy && next_waiting(k))
        start(k);
}

int esk_stm32f1_spi_pin(struct esk_mach *s, int port, int bit)
{
    int unit, fn;
    for (unit = 1; unit <= 3; unit++) {
        char name[8] = "SPIx";
        const struct spi *k;
        name[3] = (char)('0' + unit);
        k = spi_named(s, name);
        if (!k)
            continue;
        for (fn = 0; fn < 4; fn++)
            if (pin_of(k, fn) == port * 16 + bit) {
                int v = drive(k, fn);
                if (v >= 0)
                    return v;
            }
    }
    return -1;
}

void esk_stm32f1_spi_input(struct esk_mach *s, int port, int bit, int level)
{
    int unit, fn;
    for (unit = 1; unit <= 3; unit++) {
        char name[8] = "SPIx", sig[16];
        const struct spi *k;
        name[3] = (char)('0' + unit);
        k = spi_named(s, name);
        if (!k)
            continue;
        for (fn = 0; fn < 4; fn++)
            if (pin_of(k, fn) == port * 16 + bit) {
                snprintf(sig, sizeof sig, "%s_%s", name, fn_names[fn]);
                esk_stm32_spi_signal_input(s, sig, level);
            }
    }
}

void *esk_stm32_spi_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct spi k, *c;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    k.cr1 = esk_rf_reg(p, "CR1");
    k.cr2 = esk_rf_reg(p, "CR2");
    k.sr = esk_rf_reg(p, "SR");
    k.dr = esk_rf_reg(p, "DR");
    k.crcpr = esk_rf_reg(p, "CRCPR");
    k.rxcrcr = esk_rf_reg(p, "RXCRCR");
    k.txcrcr = esk_rf_reg(p, "TXCRCR");
    if (!k.cr1 || !k.cr2 || !k.sr || !k.dr || !k.crcpr || !k.rxcrcr || !k.txcrcr ||
        strncmp(p->name, "SPI", 3) || p->name[3] < '1' || p->name[3] > '6' || p->name[4])
        return 0; /* not this layout: the registers alone */
    k.unit = p->name[3] - '0';
    k.apb = p->gate_name && strstr(p->gate_name, "APB2") ? 2 : 1;
    k.irq = esk_rf_irq(p, "SPI");
    k.has_ti = esk_rf_mask(k.cr2, "FRF") != 0;
    k.fifo = esk_rf_mask(k.sr, "FRLVL") != 0;
    k.in_sck = k.in_nss = -1;
    esk_rf_set_reset(k.sr, k.sr->reset | SR_TXE);
    snprintf(k.sig_tx, sizeof k.sig_tx, "%s_TX", p->name);
    snprintf(k.sig_rx, sizeof k.sig_rx, "%s_RX", p->name);
    c = (struct spi *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    spi_reset(c);
    return c;
}
