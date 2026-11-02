/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* i2s.c -- the ESP32-C3's I2S controller (I2S0 at 0x6002D000, TRM v1.4
 * chapter 29) as a master in TDM mode, of which the standard (Philips, MSB,
 * PCM) formats are the two-slot case ESP-IDF configures: its transmitter on
 * I2SO_BCK, I2SO_WS and I2SO_SD (matrix signals 13, 14, 15), its receiver on
 * I2SI_BCK and I2SI_WS (16, 17) reading I2SI_SD (15), its data through the
 * GDMA (peripheral 3), its interrupt on matrix source 20.
 *
 *   - Clocks: MCLK is the source CLKM_CONF selects (CLK_SEL: 0 XTAL_CLK, 2
 *     the PLL's 160 MHz) over N + b/a, N being CLKM_DIV_NUM and a, b coded
 *     in CLKM_DIV_CONF's X, Y, Z and YN1 as ESP-IDF codes them (a = (X + 1)
 *     Z + Y; b = Z, or a - Z with YN1); BCK is MCLK over BCK_DIV_NUM + 1.
 *     The clock runs while CLK_ACTIVE is set and the side is started.
 *   - A frame is TDM_TOT_CHAN_NUM + 1 slots of TDM_CHAN_BITS + 1 bits. WS is
 *     WS_IDLE_POL for the frame's first TDM_WS_WIDTH + 1 bits and the
 *     opposite for the rest (so Philips's left slot with WS low, and PCM's
 *     one-bit pulse); data change on BCK's falling edge and are sampled on
 *     its rising edge, one bit late with MSB_SHIFT. A slot's BITS_MOD + 1
 *     data bits sit at its start with LEFT_ALIGN, else at its end, MSB
 *     first unless BIT_ORDER. A disabled channel (TDM_CHANn_EN 0) sends 0;
 *     with TX_TDM_SKIP_MSK_EN its sample is still taken from the buffer.
 *   - Samples are 1, 2, 3 (4 with 24_FILL_EN) or 4 bytes as the data bits
 *     need, little-endian unless BIG_ENDIAN; with MONO one sample serves
 *     every enabled slot of the transmitter's frame, and the receiver keeps
 *     only the first. The transmitter takes them from its GDMA channel (a
 *     buffer gone empty sends zeros and raises TX_HUNG); the receiver gives
 *     its channel each sample and an EOF every RXEOF_NUM bytes.
 *   - SIG_LOOPBACK (TX_CONF bit 27): the receiver uses the transmitter's BCK
 *     and WS, ESP-IDF's duplex mode; a data pin shared by DOUT and DIN loops
 *     the data back.
 *   - TX_DONE is raised when the GDMA's EOF sample has been sent; TX_UPDATE
 *     and RX_UPDATE clear themselves; INT_ST is INT_RAW and INT_ENA, INT_CLR
 *     clears.
 * Not yet: slave mode (BCK and WS from the pins), PDM transmission
 * (TX_PDM_EN, I2SO_SD1, the PCM-to-PDM converter), the MCLK output, the
 * FIFO's depth and the hung timeout (LC_HUNG_CONF), CONF_SIGLE_DATA,
 * RX_STOP_MODE, the timing delays (TX_TIMING, RX_TIMING). */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define SOURCE 20
#define PERI 3 /* the GDMA's I2S */
#define SIG_TBCK 13
#define SIG_TWS 14
#define SIG_SD 15
#define SIG_RBCK 16
#define SIG_RWS 17
#define XTAL_HZ 40000000.0
#define PLL_HZ 160000000.0
#define MAXF 512 /* bits in a frame: 16 slots of 32 */

#define I_RX_DONE 1u
#define I_TX_DONE 2u
#define I_RX_HUNG 4u
#define I_TX_HUNG 8u

#define C_RESET (1u << 0)
#define C_FIFO_RESET (1u << 1)
#define C_START (1u << 2)
#define C_SLAVE (1u << 3)
#define C_MONO (1u << 5)
#define C_BIG_ENDIAN (1u << 7)
#define C_UPDATE (1u << 8)
#define C_LEFT_ALIGN (1u << 15)
#define C_24_FILL (1u << 16)
#define C_WS_IDLE_POL (1u << 17)
#define C_BIT_ORDER (1u << 18)
#define C_LOOPBACK (1u << 27) /* TX_CONF only */

struct side {
    int tx;
    struct esk_rf_reg *conf, *conf1, *clkm, *div, *tdm;
    u64 next;      /* the next BCK edge, in core cycles */
    double half;   /* half a BCK period, in core cycles */
    double acc;    /* its fraction carried */
    int high;      /* BCK's level */
    u32 idx;       /* the bit of the frame */
    u8 bits[MAXF]; /* the transmitter's frame, or the receiver's */
    int last_bit;  /* the transmitter's previous frame's last bit */
    int eof_after; /* the transmitter's GDMA EOF in this frame */
    u32 rx_bytes;  /* the receiver's bytes since its last EOF */
};

struct c3_i2s {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *raw, *st, *ena, *clr, *eofn, *state;
    struct side tx, rx;
    int level, clocked;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void update(struct c3_i2s *k)
{
    u32 st = reg(k->raw) & reg(k->ena) & 0xfu;
    int level = st != 0;
    esk_rf_hw(k->st, st);
    if (level != k->level) {
        k->level = level;
        esk_esp32c3_source(k->s, SOURCE, level);
    }
}

static void raise(struct c3_i2s *k, u32 bits)
{
    esk_rf_hw(k->raw, reg(k->raw) | bits);
    update(k);
}

static u32 frame_bits(const struct side *d)
{
    u32 slots = ((reg(d->tdm) >> 16) & 0xfu) + 1u, w = ((reg(d->conf1) >> 24) & 0x1fu) + 1u;
    u32 n = slots * w;
    return n > MAXF ? MAXF : n;
}
static u32 slot_bits(const struct side *d) { return ((reg(d->conf1) >> 24) & 0x1fu) + 1u; }
static u32 data_bits(const struct side *d) { return ((reg(d->conf1) >> 13) & 0x1fu) + 1u; }
static u32 ws_width(const struct side *d) { return (reg(d->conf1) & 0x7fu) + 1u; }
static int shift(const struct side *d) { return (int)((reg(d->conf1) >> 29) & 1u); }
static u32 sample_bytes(const struct side *d)
{
    u32 b = data_bits(d);
    return b <= 8u ? 1u : b <= 16u ? 2u : b <= 24u ? ((reg(d->conf) & C_24_FILL) ? 4u : 3u) : 4u;
}

/* half a BCK period in core cycles, or 0 while the side's clock is off */
static double half_cycles(const struct c3_i2s *k, const struct side *d)
{
    u32 c = reg(d->clkm), dv = reg(d->div), n = c & 0xffu, sel = (c >> 27) & 3u;
    u32 z = dv & 0x1ffu, y = (dv >> 9) & 0x1ffu, x = (dv >> 18) & 0x1ffu, yn1 = (dv >> 27) & 1u;
    double src = sel == 0 ? XTAL_HZ : sel == 2 ? PLL_HZ : 0.0, div = n ? (double)n : 256.0, mclk, bck;
    u32 core = esk_mach_core_hz(k->s);
    if (!(c & (1u << 26)) || src <= 0.0 || !core || (reg(d->conf) & C_SLAVE))
        return 0;
    if (z) {
        double a = (double)((x + 1u) * z + y), b = yn1 ? a - (double)z : (double)z;
        if (a > 0.0)
            div += b / a;
    }
    mclk = src / div;
    bck = mclk / (double)(((reg(d->conf1) >> 7) & 0x3fu) + 1u);
    return (double)core / bck / 2.0;
}

/* the side runs: started, clocked, and (the receiver) not on the transmitter's clock */
static int runs(const struct c3_i2s *k, const struct side *d)
{
    if (!(reg(d->conf) & C_START))
        return 0;
    if (!d->tx && (reg(k->tx.conf) & C_LOOPBACK))
        return 0; /* the receiver follows the transmitter's edges */
    return half_cycles(k, d) > 0;
}

static int rx_rides(const struct c3_i2s *k)
{
    return (reg(k->rx.conf) & C_START) && (reg(k->tx.conf) & C_LOOPBACK) && (reg(k->tx.conf) & C_START);
}

static int ws_level(const struct side *d, u32 idx)
{
    int idle = (reg(d->conf) & C_WS_IDLE_POL) != 0;
    return idx < ws_width(d) ? idle : !idle;
}

/* the transmitter's next frame from its GDMA channel */
static void tx_frame(struct c3_i2s *k)
{
    struct side *d = &k->tx;
    u32 slots = ((reg(d->tdm) >> 16) & 0xfu) + 1u, w = slot_bits(d), nb = data_bits(d), bytes = sample_bytes(d);
    u32 en = reg(d->tdm) & 0xffffu, sk = (reg(d->tdm) >> 20) & 1u, s, i;
    int mono = (reg(d->conf) & C_MONO) != 0, have = 0, empty = 0;
    u32 sample = 0;
    memset(d->bits, 0, sizeof d->bits);
    d->eof_after = 0;
    for (s = 0; s < slots && s * w < MAXF; s++) {
        u32 v = 0, from;
        int on = (en >> s) & 1u;
        if (on || sk) {
            if (!(mono && have)) {
                u32 b;
                sample = 0;
                for (b = 0; b < bytes; b++) {
                    u8 byte = 0;
                    int eof = 0;
                    if (!esk_esp32c3_dma_out(k->s, PERI, &byte, &eof)) {
                        empty = 1;
                        byte = 0;
                    }
                    if (eof)
                        d->eof_after = 1;
                    sample |= (u32)byte << (8u * b);
                }
                if (reg(d->conf) & C_BIG_ENDIAN) {
                    u32 r = 0;
                    for (b = 0; b < bytes; b++)
                        r |= ((sample >> (8u * b)) & 0xffu) << (8u * (bytes - 1u - b));
                    sample = r;
                }
                have = 1;
            }
            v = on ? sample : 0u;
        }
        from = (reg(d->conf) & C_LEFT_ALIGN) ? 0u : (w > nb ? w - nb : 0u);
        for (i = 0; i < nb && i < w; i++) {
            u32 bit = (reg(d->conf) & C_BIT_ORDER) ? i : nb - 1u - i;
            d->bits[s * w + from + i] = (u8)((v >> bit) & 1u);
        }
    }
    if (empty)
        raise(k, I_TX_HUNG);
}

/* the receiver's frame to its GDMA channel */
static void rx_frame(struct c3_i2s *k)
{
    struct side *d = &k->rx;
    u32 slots = ((reg(d->tdm) >> 16) & 0xfu) + 1u, w = slot_bits(d), nb = data_bits(d), bytes = sample_bytes(d);
    u32 en = reg(d->tdm) & 0xffffu, s, i, eofn = reg(k->eofn) & 0xfffu;
    int mono = (reg(d->conf) & C_MONO) != 0, stored = 0;
    for (s = 0; s < slots && s * w < MAXF; s++) {
        u32 v = 0, from = (reg(d->conf) & C_LEFT_ALIGN) ? 0u : (w > nb ? w - nb : 0u), b;
        if (!((en >> s) & 1u) || (mono && stored))
            continue;
        for (i = 0; i < nb && i < w; i++) {
            u32 bit = (reg(d->conf) & C_BIT_ORDER) ? i : nb - 1u - i;
            v |= (u32)d->bits[s * w + from + i] << bit;
        }
        for (b = 0; b < bytes; b++) {
            u32 sh = (reg(d->conf) & C_BIG_ENDIAN) ? 8u * (bytes - 1u - b) : 8u * b;
            (void)esk_esp32c3_dma_in(k->s, PERI, (u8)((v >> sh) & 0xffu));
            if (eofn && ++d->rx_bytes >= eofn) {
                esk_esp32c3_dma_in_eof(k->s, PERI);
                d->rx_bytes = 0;
                raise(k, I_RX_DONE);
            }
        }
        stored = 1;
    }
}

/* a falling edge: the transmitter's next bit and WS */
static void tx_fall(struct c3_i2s *k)
{
    struct side *d = &k->tx;
    u32 n = frame_bits(d);
    int bit;
    d->idx = d->idx + 1u >= n ? 0u : d->idx + 1u;
    if (d->idx == 0u) {
        int done = d->eof_after;
        d->last_bit = d->bits[n - 1u];
        tx_frame(k);
        if (done)
            raise(k, I_TX_DONE);
    }
    if (shift(d))
        bit = d->idx == 0u ? d->last_bit : d->bits[d->idx - 1u];
    else
        bit = d->bits[d->idx];
    esk_esp32c3_signal(k->s, SIG_TWS, ws_level(d, d->idx), 1);
    esk_esp32c3_signal(k->s, SIG_SD, bit, 1);
}

/* a rising edge: the receiver samples (on its own clock or the transmitter's) */
static void rx_sample(struct c3_i2s *k, u32 idx)
{
    struct side *d = &k->rx;
    u32 n = frame_bits(d), pos;
    int din = esk_esp32c3_signal_in(k->s, SIG_SD);
    if (shift(d))
        pos = idx == 0u ? n - 1u : idx - 1u;
    else
        pos = idx;
    if (pos < MAXF)
        d->bits[pos] = (u8)din;
    if (pos == n - 1u)
        rx_frame(k);
}

static void edge(struct c3_i2s *k, struct side *d)
{
    double h = half_cycles(k, d), step;
    d->high = !d->high;
    if (d->tx) {
        esk_esp32c3_signal(k->s, SIG_TBCK, d->high, 1);
        if (!d->high)
            tx_fall(k);
        else if (rx_rides(k))
            rx_sample(k, d->idx);
    } else {
        u32 n = frame_bits(d);
        esk_esp32c3_signal(k->s, SIG_RBCK, d->high, 1);
        if (!d->high) {
            d->idx = d->idx + 1u >= n ? 0u : d->idx + 1u;
            esk_esp32c3_signal(k->s, SIG_RWS, ws_level(d, d->idx), 1);
        } else {
            rx_sample(k, d->idx);
        }
    }
    d->acc += h;
    step = (double)(u64)d->acc;
    d->acc -= step;
    d->next += step > 0 ? (u64)step : 1u;
}

static void i2s_tick(void *ctx, u32 cycles)
{
    struct c3_i2s *k = (struct c3_i2s *)ctx;
    struct esk_mach *s = k->s;
    u64 end = s->cycles;
    (void)cycles;
    for (;;) {
        struct side *d = NULL;
        if (runs(k, &k->tx) && k->tx.next <= end)
            d = &k->tx;
        if (runs(k, &k->rx) && k->rx.next <= end && (!d || k->rx.next < d->next))
            d = &k->rx;
        if (!d)
            break;
        s->output_lag = (u32)(end - d->next);
        edge(k, d);
    }
    s->output_lag = 0;
}

static int i2s_next(void *ctx, u32 *cycles)
{
    struct c3_i2s *k = (struct c3_i2s *)ctx;
    u64 now = k->s->cycles, best = 0;
    int any = 0, i;
    for (i = 0; i < 2; i++) {
        struct side *d = i ? &k->rx : &k->tx;
        u64 dd;
        if (!runs(k, d))
            continue;
        dd = d->next > now ? d->next - now : 1u;
        if (!any || dd < best)
            best = dd;
        any = 1;
    }
    if (!any)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static u32 i2s_read(void *ctx, u32 off, int n)
{
    struct c3_i2s *k = (struct c3_i2s *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void side_start(struct c3_i2s *k, struct side *d)
{
    d->idx = frame_bits(d) - 1u; /* the first falling edge starts frame 0 */
    d->high = 1;
    d->acc = 0;
    d->next = k->s->cycles + 1u;
    d->last_bit = 0;
    d->rx_bytes = 0;
    if (d->tx)
        memset(d->bits, 0, sizeof d->bits);
}

static void idle_pins(struct c3_i2s *k)
{
    if (!(reg(k->tx.conf) & C_START)) {
        esk_esp32c3_signal(k->s, SIG_TBCK, 0, 1);
        esk_esp32c3_signal(k->s, SIG_TWS, (reg(k->tx.conf) & C_WS_IDLE_POL) != 0, 1);
        esk_esp32c3_signal(k->s, SIG_SD, 0, 1);
    }
    if (!(reg(k->rx.conf) & C_START)) {
        esk_esp32c3_signal(k->s, SIG_RBCK, 0, 1);
        esk_esp32c3_signal(k->s, SIG_RWS, (reg(k->rx.conf) & C_WS_IDLE_POL) != 0, 1);
    }
}

static void i2s_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_i2s *k = (struct c3_i2s *)ctx;
    u32 w = off & ~3u, tx_was = reg(k->tx.conf), rx_was = reg(k->rx.conf);
    if (w == 0x18u) { /* INT_CLR */
        esk_rf_hw(k->raw, reg(k->raw) & ~v);
        update(k);
        return;
    }
    if (w == 0x10u || w == 0x6Cu)
        return; /* INT_ST, STATE */
    esk_rf_write(k->p, off, n, v);
    if (w == 0x24u || w == 0x20u) {
        struct side *d = w == 0x24u ? &k->tx : &k->rx;
        u32 was = w == 0x24u ? tx_was : rx_was;
        esk_rf_hw(d->conf, reg(d->conf) & ~C_UPDATE); /* the update is taken at once */
        if (!(was & C_START) && (reg(d->conf) & C_START))
            side_start(k, d);
        if (reg(d->conf) & C_RESET)
            d->idx = frame_bits(d) - 1u;
        idle_pins(k);
    }
    if (w == 0x0Cu || w == 0x14u)
        update(k);
    esk_rf_hw(k->state, (reg(k->tx.conf) & C_START) ? 0u : 1u); /* TX_IDLE */
    esk_rf_clock(k->p, &k->clocked, runs(k, &k->tx) || runs(k, &k->rx));
}

static void i2s_reset(void *ctx)
{
    struct c3_i2s *k = (struct c3_i2s *)ctx;
    memset(k->tx.bits, 0, sizeof k->tx.bits);
    memset(k->rx.bits, 0, sizeof k->rx.bits);
    k->level = 0;
    esk_esp32c3_source(k->s, SOURCE, 0);
    esk_rf_hw(k->state, 1u);
    idle_pins(k);
    k->clocked = 0;
    esk_rf_clock(k->p, &k->clocked, 0);
    update(k);
}

const struct esk_dev_ops esk_esp32c3_i2s_ops = {
    "esp32c3-i2s", i2s_read, i2s_write, i2s_reset, i2s_tick, i2s_next,
};

void *esk_esp32c3_i2s_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_i2s *k = (struct c3_i2s *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->raw = esk_rf_reg(p, "INT_RAW");
    k->st = esk_rf_reg(p, "INT_ST");
    k->ena = esk_rf_reg(p, "INT_ENA");
    k->clr = esk_rf_reg(p, "INT_CLR");
    k->eofn = esk_rf_reg(p, "RXEOF_NUM");
    k->state = esk_rf_reg(p, "STATE");
    k->tx.tx = 1;
    k->tx.conf = esk_rf_reg(p, "TX_CONF");
    k->tx.conf1 = esk_rf_reg(p, "TX_CONF1");
    k->tx.clkm = esk_rf_reg(p, "TX_CLKM_CONF");
    k->tx.div = esk_rf_reg(p, "TX_CLKM_DIV_CONF");
    k->tx.tdm = esk_rf_reg(p, "TX_TDM_CTRL");
    k->rx.conf = esk_rf_reg(p, "RX_CONF");
    k->rx.conf1 = esk_rf_reg(p, "RX_CONF1");
    k->rx.clkm = esk_rf_reg(p, "RX_CLKM_CONF");
    k->rx.div = esk_rf_reg(p, "RX_CLKM_DIV_CONF");
    k->rx.tdm = esk_rf_reg(p, "RX_TDM_CTRL");
    if (!k->raw || !k->st || !k->ena || !k->clr || !k->eofn || !k->state || !k->tx.conf || !k->tx.conf1 ||
        !k->tx.clkm || !k->tx.div || !k->tx.tdm || !k->rx.conf || !k->rx.conf1 || !k->rx.clkm || !k->rx.div ||
        !k->rx.tdm)
        return 0;
    return k;
}
