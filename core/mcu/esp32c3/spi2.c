/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* spi2.c -- the ESP32-C3's GP-SPI2 (TRM v1.4 chapter 27) as a master, on
 * the FSPI signals of the GPIO matrix (FSPICLK 63, FSPIQ 64, FSPID 65,
 * FSPIHD 66, FSPIWP 67, FSPICS0-5 68-73) or GPIO2, 4-7 and 10's IO MUX
 * function, its interrupt on matrix source 19.
 *
 *   - clk_spi_mst is PLL_F80M (CLK_GATE's MST_CLK_SEL 1) or XTAL_CLK (0),
 *     while MST_CLK_ACTIVE is set; SPI_CLK is clk_spi_mst with
 *     CLK_EQU_SYSCLK, else divided by (CLKCNT_N + 1)(CLKDIV_PRE + 1),
 *     its first half (CLKCNT_H + 1)(CLKDIV_PRE + 1) cycles long.
 *   - SPI_USR starts a transaction: the CS lines not disabled (MISC's
 *     CSn_DIS) go active (MASTER_CS_POL's bit high, else low); CMD
 *     (USR_COMMAND: USR_COMMAND_BITLEN + 1 bits of USR_COMMAND_VALUE, its
 *     low byte first), ADDR (USR_ADDR: USR_ADDR_BITLEN + 1 bits of
 *     USR_ADDR_VALUE from bit 31), DUMMY (USR_DUMMY: USR_DUMMY_CYCLELEN + 1
 *     clocks, the data lines let go unless DUMMY_OUT), then the data:
 *     MS_DATA_BITLEN + 1 bits out (USR_MOSI) and in (USR_MISO), at once on
 *     FSPID and FSPIQ with DOUTDIN (full duplex), else out then in (half
 *     duplex, FSPID both ways with SIO). Each phase on 1, 2 (FCMD_DUAL,
 *     FADDR_DUAL, FWRITE_DUAL, FREAD_DUAL) or 4 lines (the _QUAD bits, or
 *     QPI_MODE for all): a clock carries the bits highest on FSPIHD, FSPIWP,
 *     FSPIQ, FSPID. WR_BIT_ORDER and RD_BIT_ORDER send and receive each
 *     byte LSB first.
 *   - The data comes from and goes to SPI_W0-W15 (bytes from W0[7:0];
 *     USR_MOSI_HIGHPART and USR_MISO_HIGHPART from W8; past the buffer's
 *     end the address wraps), or with DMA_TX_ENA and DMA_RX_ENA from and to
 *     the GDMA channels connected to SPI2: a TX list shorter than the data
 *     repeats its last byte (DMA_OUTFIFO_EMPTY_ERR), an RX list too short
 *     loses the rest (DMA_INFIFO_FULL_ERR); the transaction's end is the RX
 *     list's EOF.
 *   - The clock modes: CK_IDLE_EDGE the idle level, CK_OUT_EDGE with it the
 *     phase (table 27.7-1): data out before the first edge and read on the
 *     leading edges (modes 0 and 2), or out on the leading and read on the
 *     trailing (1 and 3). CS is set up (CS_SETUP_TIME + 1.5) SPI_CLK cycles
 *     before the first latching edge with CS_SETUP, half a cycle without,
 *     and held (CS_HOLD_TIME + 1.5) cycles after the last with CS_HOLD,
 *     half without; CS_KEEP_ACTIVE keeps it active after; CK_DIS keeps
 *     SPI_CLK at rest.
 *   - At the end TRANS_DONE, and SPI_USR reads 0 again. SPI_UPDATE, the
 *     settings' synchronisation, is done at once (it reads 0).
 *   - DMA_INT_ST is DMA_INT_RAW and DMA_INT_ENA; DMA_INT_CLR clears;
 *     DMA_INT_SET sets.
 * Where it differs inside: the settings are used as written (SPI_UPDATE
 * takes no time); the timing compensation (DIN_MODE, DIN_NUM, DOUT_MODE)
 * is not applied; WP and HD idle at WP_POL and HOLD_POL. Not yet: slave
 * mode (SLAVE's MODE), the configurable segmented transfer (USR_CONF and
 * CONF_BITLEN), CLK_MODE's clock after CS, the AFIFO errors. */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define SOURCE 19
#define SIG_CLK 63
#define SIG_Q 64
#define SIG_D 65
#define SIG_HD 66
#define SIG_WP 67
#define SIG_CS0 68
#define PERI_SPI2 0
#define FS_PER_S 1000000000000000ull

/* CTRL */
#define C_DUMMY_OUT (1u << 3)
#define C_FADDR_DUAL (1u << 5)
#define C_FADDR_QUAD (1u << 6)
#define C_FCMD_DUAL (1u << 8)
#define C_FCMD_QUAD (1u << 9)
#define C_FREAD_DUAL (1u << 14)
#define C_FREAD_QUAD (1u << 15)
#define C_Q_POL (1u << 18)
#define C_D_POL (1u << 19)
#define C_HOLD_POL (1u << 20)
#define C_WP_POL (1u << 21)
#define C_RD_LSB (1u << 25)
#define C_WR_LSB (1u << 26)
/* USER */
#define U_DOUTDIN (1u << 0)
#define U_QPI (1u << 3)
#define U_CS_HOLD (1u << 6)
#define U_CS_SETUP (1u << 7)
#define U_CK_OUT_EDGE (1u << 9)
#define U_FWRITE_DUAL (1u << 12)
#define U_FWRITE_QUAD (1u << 13)
#define U_SIO (1u << 17)
#define U_MISO_HIGH (1u << 24)
#define U_MOSI_HIGH (1u << 25)
#define U_MOSI (1u << 27)
#define U_MISO (1u << 28)
#define U_DUMMY (1u << 29)
#define U_ADDR (1u << 30)
#define U_COMMAND (1u << 31)
/* MISC */
#define M_CK_DIS (1u << 6)
#define M_CK_IDLE (1u << 29)
#define M_CS_KEEP (1u << 30)
/* DMA_CONF */
#define D_RX_ENA (1u << 27)
#define D_TX_ENA (1u << 28)
/* DMA_INT */
#define I_INFIFO_FULL_ERR (1u << 0)
#define I_OUTFIFO_EMPTY_ERR (1u << 1)
#define I_TRANS_DONE (1u << 12)

enum { PH_CMD, PH_ADDR, PH_DUMMY, PH_DOUT, PH_DIN, PH_DUPLEX, PH_END };
enum { ST_IDLE, ST_LEAD, ST_TRAIL, ST_HOLD };

struct c3_spi2 {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cmd, *addr, *ctrl, *clock, *user, *user1, *user2, *dlen, *misc, *dma_conf, *raw, *ena, *st,
        *slave, *clk_gate, *w[16];
    int clocked, level;
    /* the transaction */
    int state, phase, lines, cpha;
    u32 nbits, pos;        /* the phase's bits, those done */
    u32 out_byte, in_byte; /* the data byte going out, coming in */
    u32 txa, rxa;          /* the buffer's byte addresses */
    u8 txbuf[64];          /* the buffer as it was when the transaction started */
    int tx_dma_last;
    u8 tx_last;
    int rx_any;
    /* time: an absolute cycle and fs past it */
    int ev;
    u64 ev_c, ev_f, cur_c, cur_f;
    double lead_fs, trail_fs, period_fs;
    u64 latch_c, latch_f; /* the last latching edge */
    /* what the lines carry now */
    int out_lv[4], out_oe[4]; /* D, Q, WP, HD */
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void update(struct c3_spi2 *k)
{
    u32 st = reg(k->raw) & reg(k->ena) & 0x7ffffu;
    int level = st != 0;
    esk_rf_hw(k->st, st);
    if (level != k->level) {
        k->level = level;
        esk_esp32c3_source(k->s, SOURCE, level);
    }
}

static void raise(struct c3_spi2 *k, u32 bits)
{
    esk_rf_hw(k->raw, reg(k->raw) | bits);
    update(k);
}

/* ---- time ---------------------------------------------------------------------- */

static u64 fs_per_cycle(const struct c3_spi2 *k)
{
    u32 hz = esk_mach_core_hz(k->s);
    return hz ? FS_PER_S / hz : 0;
}

/* SPI_CLK's period and its halves, in fs: 0 when it does not run */
static int clocks(struct c3_spi2 *k)
{
    u32 g = reg(k->clk_gate), c = reg(k->clock), hz;
    double src;
    if (!(g & 2u))
        return 0;
    hz = (g & 4u) ? 80000000u : 40000000u;
    src = (double)FS_PER_S / (double)hz;
    if (c & (1u << 31)) {
        k->period_fs = src;
        k->lead_fs = src / 2.0;
    } else {
        u32 n = (c >> 12) & 0x3fu, h = (c >> 6) & 0x3fu, pre = (c >> 18) & 0xfu;
        k->period_fs = (double)(n + 1u) * (double)(pre + 1u) * src;
        k->lead_fs = (double)((h < n ? h : n) + 1u) * (double)(pre + 1u) * src;
        if (k->lead_fs >= k->period_fs)
            k->lead_fs = k->period_fs / 2.0;
    }
    k->trail_fs = k->period_fs - k->lead_fs;
    return 1;
}

static void after_fs(struct c3_spi2 *k, double fs)
{
    u64 fpc = fs_per_cycle(k), d;
    if (!fpc) {
        k->ev = 0;
        return;
    }
    d = (u64)(fs < 1.0 ? 1.0 : fs + 0.5) + k->cur_f;
    k->ev_c = k->cur_c + d / fpc;
    k->ev_f = d % fpc;
    k->ev = 1;
    k->s->bus.touched = 1;
}

static void clocking(struct c3_spi2 *k) { esk_rf_clock(k->p, &k->clocked, k->ev); }

/* ---- the lines -------------------------------------------------------------------- */

static void cs_lines(struct c3_spi2 *k, int active)
{
    u32 misc = reg(k->misc), pol = (misc >> 7) & 0x3fu;
    int i;
    for (i = 0; i < 6; i++) {
        int on = active && !(misc >> i & 1u), act = (int)(pol >> i & 1u);
        esk_esp32c3_signal(k->s, SIG_CS0 + i, on ? act : !act, 1);
    }
}

static void clk_line(struct c3_spi2 *k, int lead)
{
    int idle = (reg(k->misc) & M_CK_IDLE) != 0;
    if (reg(k->misc) & M_CK_DIS)
        lead = 0;
    esk_esp32c3_signal(k->s, SIG_CLK, lead ? !idle : idle, 1);
}

static void data_lines(struct c3_spi2 *k)
{
    static const int sig[4] = { SIG_D, SIG_Q, SIG_WP, SIG_HD };
    int i;
    for (i = 0; i < 4; i++)
        esk_esp32c3_signal(k->s, sig[i], k->out_lv[i], k->out_oe[i]);
}

/* at rest: D, WP and HD at their idle levels, Q an input */
static void idle_lines(struct c3_spi2 *k)
{
    u32 c = reg(k->ctrl);
    k->out_lv[0] = (c & C_D_POL) != 0;
    k->out_oe[0] = 1;
    k->out_lv[1] = (c & C_Q_POL) != 0;
    k->out_oe[1] = 0;
    k->out_lv[2] = (c & C_WP_POL) != 0;
    k->out_oe[2] = 1;
    k->out_lv[3] = (c & C_HOLD_POL) != 0;
    k->out_oe[3] = 1;
    data_lines(k);
}

static int line_in(struct c3_spi2 *k, int i)
{
    static const int sig[4] = { SIG_D, SIG_Q, SIG_WP, SIG_HD };
    return esk_esp32c3_signal_in(k->s, sig[i]);
}

/* ---- the bit streams ------------------------------------------------------------- */

static u32 tx_data_byte(struct c3_spi2 *k)
{
    u8 b = 0;
    int eof = 0;
    if (reg(k->dma_conf) & D_TX_ENA) {
        if (esk_esp32c3_dma_out(k->s, PERI_SPI2, &b, &eof)) {
            k->tx_last = b;
            return b;
        }
        raise(k, I_OUTFIFO_EMPTY_ERR);
        return k->tx_last;
    }
    b = k->txbuf[k->txa & 63u];
    k->txa++;
    return b;
}

static void rx_data_byte(struct c3_spi2 *k, u32 b)
{
    if (reg(k->dma_conf) & D_RX_ENA) {
        if (!esk_esp32c3_dma_in(k->s, PERI_SPI2, (u8)b))
            raise(k, I_INFIFO_FULL_ERR);
        k->rx_any = 1;
        return;
    }
    {
        u32 a = k->rxa & 63u, sh = 8u * (a & 3u);
        struct esk_rf_reg *r = k->w[a / 4u];
        esk_rf_hw(r, (reg(r) & ~(0xffu << sh)) | (b & 0xffu) << sh);
        k->rxa++;
    }
}

/* the bit `i` of the phase going out */
static int out_bit(struct c3_spi2 *k, u32 i)
{
    int lsb = (reg(k->ctrl) & C_WR_LSB) != 0;
    u32 byte = i / 8u, bit = i % 8u;
    switch (k->phase) {
    case PH_CMD: { /* the low byte first */
        u32 v = (reg(k->user2) & 0xffffu) >> (8u * byte) & 0xffu;
        return (int)((v >> (lsb ? bit : 7u - bit)) & 1u);
    }
    case PH_ADDR: { /* from the top byte down */
        u32 v = reg(k->addr) >> (8u * (3u - (byte & 3u))) & 0xffu;
        return (int)((v >> (lsb ? bit : 7u - bit)) & 1u);
    }
    case PH_DOUT:
    case PH_DUPLEX:
        if (bit == 0)
            k->out_byte = tx_data_byte(k);
        return (int)((k->out_byte >> (lsb ? bit : 7u - bit)) & 1u);
    default:
        return 0;
    }
}

static void in_bit(struct c3_spi2 *k, u32 i, int v)
{
    int lsb = (reg(k->ctrl) & C_RD_LSB) != 0;
    u32 bit = i % 8u;
    if (bit == 0)
        k->in_byte = 0;
    k->in_byte |= (u32)(v != 0) << (lsb ? bit : 7u - bit);
    if (bit == 7 || i + 1u == k->nbits)
        rx_data_byte(k, k->in_byte);
}

/* ---- the phases ------------------------------------------------------------------ */

static int phase_on(struct c3_spi2 *k, int ph)
{
    u32 u = reg(k->user);
    switch (ph) {
    case PH_CMD: return (u & U_COMMAND) != 0;
    case PH_ADDR: return (u & U_ADDR) != 0;
    case PH_DUMMY: return (u & U_DUMMY) != 0;
    case PH_DUPLEX: return (u & U_DOUTDIN) && (u & (U_MOSI | U_MISO));
    case PH_DOUT: return !(u & U_DOUTDIN) && (u & U_MOSI);
    case PH_DIN: return !(u & U_DOUTDIN) && (u & U_MISO);
    default: return 0;
    }
}

/* the first phase from `ph` that is on: its bits and lines */
static void enter_phase(struct c3_spi2 *k, int ph)
{
    static const int order[] = { PH_CMD, PH_ADDR, PH_DUMMY, PH_DUPLEX, PH_DOUT, PH_DIN, PH_END };
    u32 u = reg(k->user), c = reg(k->ctrl), u1 = reg(k->user1), u2 = reg(k->user2);
    int qpi = (u & U_QPI) != 0, i = 0;
    while (order[i] != ph)
        i++;
    for (; order[i] != PH_END && !phase_on(k, order[i]); i++)
        ;
    k->phase = order[i];
    k->pos = 0;
    switch (k->phase) {
    case PH_CMD:
        k->nbits = (u2 >> 28) + 1u;
        k->lines = qpi || (c & C_FCMD_QUAD) ? 4 : (c & C_FCMD_DUAL) ? 2 : 1;
        break;
    case PH_ADDR:
        k->nbits = (u1 >> 27) + 1u;
        k->lines = qpi || (c & C_FADDR_QUAD) ? 4 : (c & C_FADDR_DUAL) ? 2 : 1;
        break;
    case PH_DUMMY:
        k->nbits = (u1 & 0xffu) + 1u; /* clocks */
        k->lines = 1;
        break;
    case PH_DOUT:
    case PH_DUPLEX:
        k->nbits = (reg(k->dlen) & 0x3ffffu) + 1u;
        k->lines = k->phase == PH_DUPLEX ? 1 : qpi || (u & U_FWRITE_QUAD) ? 4 : (u & U_FWRITE_DUAL) ? 2 : 1;
        break;
    case PH_DIN:
        k->nbits = (reg(k->dlen) & 0x3ffffu) + 1u;
        k->lines = qpi || (c & C_FREAD_QUAD) ? 4 : (c & C_FREAD_DUAL) ? 2 : 1;
        break;
    default:
        k->nbits = 0;
        break;
    }
}

/* this clock's outputs: the data bits going out on their lines, the lines
 * reading let go */
static void present(struct c3_spi2 *k)
{
    u32 u = reg(k->user);
    int i, n = k->lines;
    for (i = 0; i < 4; i++) {
        k->out_oe[i] = 0;
    }
    k->out_oe[0] = 1; /* D */
    k->out_oe[2] = k->out_oe[3] = 1;
    switch (k->phase) {
    case PH_DUMMY:
        if (!(reg(k->ctrl) & C_DUMMY_OUT))
            k->out_oe[0] = k->out_oe[2] = k->out_oe[3] = 0;
        break;
    case PH_DIN:
        if (n > 1)
            for (i = 0; i < n; i++)
                k->out_oe[i] = 0;
        else if (u & U_SIO)
            k->out_oe[0] = 0;
        break;
    case PH_CMD:
    case PH_ADDR:
    case PH_DOUT:
    case PH_DUPLEX: {
        /* the clock's bits: the earliest goes to the highest line in MSB
         * order (B7 on HD), to the lowest in LSB order -- the higher bit
         * number on the higher line either way */
        static const int line_of[4][4] = { { 0 }, { 0, 1 }, { 0 }, { 0, 1, 2, 3 } };
        int lsb = (reg(k->ctrl) & C_WR_LSB) != 0, j;
        for (j = 0; j < n && k->pos + (u32)j < k->nbits; j++) {
            int b = out_bit(k, k->pos + (u32)j), ln = lsb ? j : n - 1 - j;
            if (n == 1)
                ln = 0;
            k->out_lv[line_of[n - 1][ln]] = b;
            k->out_oe[line_of[n - 1][ln]] = 1;
        }
        break;
    }
    default:
        break;
    }
    data_lines(k);
}

/* this clock's inputs, at its latching edge */
static void sample(struct c3_spi2 *k)
{
    u32 u = reg(k->user);
    int n = k->lines, j;
    if (k->phase == PH_DUPLEX) {
        in_bit(k, k->pos, line_in(k, 1));
        return;
    }
    if (k->phase != PH_DIN)
        return;
    if (n == 1) {
        in_bit(k, k->pos, line_in(k, (u & U_SIO) ? 0 : 1));
        return;
    }
    {
        int lsb = (reg(k->ctrl) & C_RD_LSB) != 0;
        for (j = 0; j < n && k->pos + (u32)j < k->nbits; j++)
            in_bit(k, k->pos + (u32)j, line_in(k, lsb ? j : n - 1 - j));
    }
}

/* on to the next clock: 0 when the transaction has no more */
static int next_clock(struct c3_spi2 *k)
{
    k->pos += k->phase == PH_DUMMY ? 1u : (u32)k->lines;
    if (k->pos >= k->nbits) {
        static const int after_ph[] = { PH_ADDR, PH_DUMMY, PH_DUPLEX, PH_DIN, PH_END, PH_END };
        int ph = k->phase;
        if (ph == PH_DUPLEX)
            ph = PH_DOUT; /* the duplex data stands for both */
        enter_phase(k, after_ph[ph]);
    }
    return k->phase != PH_END;
}

/* ---- the transaction ---------------------------------------------------------------- */

static void finish(struct c3_spi2 *k)
{
    k->state = ST_IDLE;
    k->ev = 0;
    if (!(reg(k->misc) & M_CS_KEEP))
        cs_lines(k, 0);
    idle_lines(k);
    if ((reg(k->dma_conf) & D_RX_ENA) && k->rx_any)
        esk_esp32c3_dma_in_eof(k->s, PERI_SPI2);
    esk_rf_hw(k->cmd, reg(k->cmd) & ~(1u << 24)); /* SPI_USR */
    raise(k, I_TRANS_DONE);
}

static void start(struct c3_spi2 *k)
{
    u32 u = reg(k->user), u1 = reg(k->user1);
    double setup;
    int i;
    if (k->state != ST_IDLE || (reg(k->slave) & (1u << 26)) || !clocks(k))
        return;
    for (i = 0; i < 16; i++) {
        u32 v = reg(k->w[i]);
        k->txbuf[4 * i] = (u8)(v & 0xffu);
        k->txbuf[4 * i + 1] = (u8)((v >> 8) & 0xffu);
        k->txbuf[4 * i + 2] = (u8)((v >> 16) & 0xffu);
        k->txbuf[4 * i + 3] = (u8)((v >> 24) & 0xffu);
    }
    k->txa = (u & U_MOSI_HIGH) ? 32u : 0u;
    k->rxa = (u & U_MISO_HIGH) ? 32u : 0u;
    k->rx_any = 0;
    k->cpha = ((reg(k->misc) & M_CK_IDLE) != 0) != ((u & U_CK_OUT_EDGE) != 0);
    enter_phase(k, PH_CMD);
    cs_lines(k, 1);
    clk_line(k, 0);
    setup = (u & U_CS_SETUP) ? ((double)((u1 >> 17) & 0x1fu) + 1.5) * k->period_fs : 0.5 * k->period_fs;
    if (k->phase == PH_END) { /* nothing to clock: CS for its setup and hold */
        k->state = ST_HOLD;
        after_fs(k, setup);
        return;
    }
    if (!k->cpha)
        present(k);
    k->state = ST_LEAD;
    /* the first latching edge after the setup time: the leading edge in
     * modes 0 and 2, the trailing one in 1 and 3 */
    after_fs(k, k->cpha ? (setup > k->lead_fs ? setup - k->lead_fs : 1.0) : setup);
}

static void step(struct c3_spi2 *k)
{
    u32 u = reg(k->user), u1 = reg(k->user1);
    switch (k->state) {
    case ST_LEAD:
        clk_line(k, 1);
        if (k->cpha) {
            present(k);
        } else {
            sample(k);
            k->latch_c = k->cur_c;
            k->latch_f = k->cur_f;
        }
        k->state = ST_TRAIL;
        after_fs(k, k->lead_fs);
        return;
    case ST_TRAIL: {
        int more;
        clk_line(k, 0);
        if (k->cpha) {
            sample(k);
            k->latch_c = k->cur_c;
            k->latch_f = k->cur_f;
        }
        more = next_clock(k);
        if (more) {
            if (!k->cpha)
                present(k);
            k->state = ST_LEAD;
            after_fs(k, k->trail_fs);
            return;
        }
        { /* CS held after the last latching edge */
            double hold = (u & U_CS_HOLD) ? ((double)((u1 >> 22) & 0x1fu) + 1.5) * k->period_fs : 0.5 * k->period_fs;
            double since = ((double)(k->cur_c - k->latch_c) * (double)fs_per_cycle(k) + (double)k->cur_f -
                            (double)k->latch_f);
            k->state = ST_HOLD;
            after_fs(k, hold > since ? hold - since : 1.0);
        }
        return;
    }
    case ST_HOLD:
        finish(k);
        return;
    default:
        return;
    }
}

static void sp_tick(void *ctx, u32 cycles)
{
    struct c3_spi2 *k = (struct c3_spi2 *)ctx;
    struct esk_mach *s = k->s;
    (void)cycles;
    while (k->ev && k->ev_c <= s->cycles) {
        k->ev = 0;
        k->cur_c = k->ev_c;
        k->cur_f = k->ev_f;
        s->output_lag = (u32)(s->cycles - k->ev_c);
        step(k);
        s->output_lag = 0;
    }
    clocking(k);
}

static int sp_next(void *ctx, u32 *cycles)
{
    struct c3_spi2 *k = (struct c3_spi2 *)ctx;
    u64 d;
    if (!k->ev)
        return 0;
    d = k->ev_c > k->s->cycles ? k->ev_c - k->s->cycles : 1u;
    *cycles = d > 0xffffffffu ? 0xffffffffu : (u32)d;
    return 1;
}

static u32 sp_read(void *ctx, u32 off, int n)
{
    struct c3_spi2 *k = (struct c3_spi2 *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void sp_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_spi2 *k = (struct c3_spi2 *)ctx;
    u32 w = off & ~3u;
    if (w == 0x38u) { /* DMA_INT_CLR */
        esk_rf_hw(k->raw, reg(k->raw) & ~v);
        update(k);
        return;
    }
    if (w == 0x40u)
        return; /* DMA_INT_ST */
    if (w == 0x44u) { /* DMA_INT_SET */
        esk_rf_hw(k->raw, reg(k->raw) | (v & 0x7ffffu));
        update(k);
        return;
    }
    esk_rf_write(k->p, off, n, v);
    if (w == 0x3Cu || w == 0x34u)
        update(k);
    if (w == 0x00u) {
        esk_rf_hw(k->cmd, reg(k->cmd) & ~(1u << 23)); /* SPI_UPDATE: done */
        if (v & (1u << 24)) {
            k->cur_c = k->s->cycles;
            k->cur_f = 0;
            start(k);
            if (k->state == ST_IDLE) /* not started: USR reads 0 */
                esk_rf_hw(k->cmd, reg(k->cmd) & ~(1u << 24));
        }
    }
    if (w == esk_rf_off(k->misc) && k->state == ST_IDLE) { /* CS_KEEP_ACTIVE let go, the polarities */
        if (!(reg(k->misc) & M_CS_KEEP))
            cs_lines(k, 0);
        clk_line(k, 0);
    }
    if (w == esk_rf_off(k->ctrl) && k->state == ST_IDLE)
        idle_lines(k);
    clocking(k);
}

static void sp_reset(void *ctx)
{
    struct c3_spi2 *k = (struct c3_spi2 *)ctx;
    k->state = ST_IDLE;
    k->ev = 0;
    k->level = 0;
    esk_esp32c3_source(k->s, SOURCE, 0);
    k->clocked = 0;
    cs_lines(k, 0);
    clk_line(k, 0);
    idle_lines(k);
    update(k);
}

const struct esk_dev_ops esk_esp32c3_spi2_ops = {
    "esp32c3-spi2", sp_read, sp_write, sp_reset, sp_tick, sp_next,
};

void *esk_esp32c3_spi2_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_spi2 *k = (struct c3_spi2 *)esk_mach_alloc(s, sizeof *k);
    char name[8];
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->cmd = esk_rf_reg(p, "CMD");
    k->addr = esk_rf_reg(p, "ADDR");
    k->ctrl = esk_rf_reg(p, "CTRL");
    k->clock = esk_rf_reg(p, "CLOCK");
    k->user = esk_rf_reg(p, "USER");
    k->user1 = esk_rf_reg(p, "USER1");
    k->user2 = esk_rf_reg(p, "USER2");
    k->dlen = esk_rf_reg(p, "MS_DLEN");
    k->misc = esk_rf_reg(p, "MISC");
    k->dma_conf = esk_rf_reg(p, "DMA_CONF");
    k->raw = esk_rf_reg(p, "DMA_INT_RAW");
    k->ena = esk_rf_reg(p, "DMA_INT_ENA");
    k->st = esk_rf_reg(p, "DMA_INT_ST");
    k->slave = esk_rf_reg(p, "SLAVE");
    k->clk_gate = esk_rf_reg(p, "CLK_GATE");
    if (!k->cmd || !k->addr || !k->ctrl || !k->clock || !k->user || !k->user1 || !k->user2 || !k->dlen ||
        !k->misc || !k->dma_conf || !k->raw || !k->ena || !k->st || !k->slave || !k->clk_gate)
        return 0;
    for (i = 0; i < 16; i++) {
        snprintf(name, sizeof name, "W%d", i);
        if (!(k->w[i] = esk_rf_reg(p, name)))
            return 0;
    }
    return k;
}
