/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* uart.c -- the ESP32-C3's UART0 and UART1 (ESP32-C3 TRM v1.4, chapter
 * 26), over the register file, at their baud rate.
 *
 *   - the core clock is CLK_CONF's source (SCLK_SEL: 1 APB_CLK, 2
 *     RC_FAST_CLK, 3 XTAL_CLK) over SCLK_DIV_NUM + 1 + SCLK_DIV_B /
 *     SCLK_DIV_A; a bit is CLKDIV + FRAG / 16 of its cycles;
 *   - a write of FIFO queues a byte in the TX FIFO (128 bytes times
 *     MEM_CONF's TX_SIZE; a write to a full FIFO is lost); while
 *     TX_SCLK_EN is set and nCTS lets it (TX_FLOW_EN) the transmitter sends
 *     a frame at a time: a start bit, 5 to 8 data bits (BIT_NUM), the
 *     parity (PARITY_EN, PARITY odd) and 1, 1.5 or 2 stop bits
 *     (STOP_BIT_NUM); frames in the FIFO go out back to back, and once it
 *     has emptied the line stays idle at least TX_IDLE_NUM bit times
 *     before the next transfer (TRM 26.4.4, register 26.16: "the duration
 *     time between transfers"; boot logs' timestamps show ESP-IDF's
 *     bootloader, which leaves the reset value 256, printing at the full
 *     rate, so the gap is not after every frame); TXD_BRK sends TX_BRK_NUM
 *     NULL characters once the FIFO is empty;
 *   - bytes arrive from the session's serial input or the RX pin into the
 *     RX FIFO (128 times RX_SIZE) while RX_SCLK_EN is set; a frame with no
 *     room is lost (RXFIFO_OVF); a read of FIFO takes the oldest byte;
 *     LOOPBACK sends the transmitter's frames to its own receiver;
 *   - INT_RAW's bits: RXFIFO_FULL while more than RXFIFO_FULL_THRHD bytes
 *     wait, TXFIFO_EMPTY while fewer than TXFIFO_EMPTY_THRHD do, PARITY_ERR
 *     and FRM_ERR for a received frame, RXFIFO_OVF, BRK_DET, RXFIFO_TOUT
 *     after RX_TOUT_THRHD bit times of idle line with bytes waiting
 *     (RX_TOUT_EN), TX_BRK_DONE, TX_BRK_IDLE_DONE, TX_DONE; INT_CLR clears
 *     them (a level condition sets its bit again while it lasts); INT_ST is
 *     INT_RAW and INT_ENA, and raises the matrix source (UART0 21, UART1
 *     22) while nonzero;
 *   - STATUS reads the FIFOs' counts and the lines; RXFIFO_RST and
 *     TXFIFO_RST empty the FIFOs while set; ID's REG_UPDATE, which the
 *     software sets to move its configuration to the core's clock domain,
 *     clears once that is done (at once here).
 * Not modelled: autobaud (LOWPULSE, HIGHPULSE, POSPULSE, NEGPULSE and
 * RXD_CNT keep their reset values), IrDA, RS485, the AT_CMD character,
 * software flow control (XON/XOFF), the RX glitch filter, light-sleep
 * wake-up, the APB-to-core synchronisation delay. UHCI (uhci.c) takes the RX
 * FIFO's bytes and fills the TX FIFO while it is attached. */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define BLOCK 128 /* bytes of FIFO RAM per block (TRM 26.4.2) */
#define RAW_RXFIFO_FULL (1u << 0)
#define RAW_TXFIFO_EMPTY (1u << 1)
#define RAW_PARITY_ERR (1u << 2)
#define RAW_FRM_ERR (1u << 3)
#define RAW_RXFIFO_OVF (1u << 4)
#define RAW_BRK_DET (1u << 7)
#define RAW_RXFIFO_TOUT (1u << 8)
#define RAW_TX_BRK_DONE (1u << 12)
#define RAW_TX_BRK_IDLE_DONE (1u << 13)
#define RAW_TX_DONE (1u << 14)
#define CONF0_PARITY (1u << 0)
#define CONF0_PARITY_EN (1u << 1)
#define CONF0_TXD_BRK (1u << 8)
#define CONF0_LOOPBACK (1u << 14)
#define CONF0_TX_FLOW_EN (1u << 15)
#define CONF0_RXFIFO_RST (1u << 17)
#define CONF0_TXFIFO_RST (1u << 18)
#define CONF1_RX_TOUT_EN (1u << 21)
#define CLK_TX_SCLK_EN (1u << 24)
#define CLK_RX_SCLK_EN (1u << 25)
#define CLK_RST_CORE (1u << 23)
#define ID_REG_UPDATE (1u << 31)

struct c3_uart {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *fifo, *raw, *st, *ena, *clr, *clkdiv, *status, *conf0, *conf1, *txbrk, *idle,
        *mem, *clk, *id;
    int unit, source, level;
    u8 tx[4 * BLOCK], rx[2 * BLOCK];
    int ntx, nrx;
    int busy;      /* a frame, a break or the idle gap is on the TX line */
    int clocked;   /* the device's clock runs (esk_rf_clock) */
    int phase;     /* 0 a frame, 1 the idle after it, 2 a break */
    int brk_left;  /* NULL characters of a break still to send */
    u64 left, since;
    u64 rx_idle;   /* bit times of idle RX line, for RXFIFO_TOUT */
    int rx_any;    /* a byte arrived since the FIFO was last empty */
    int cts;       /* nCTS high: the peer is not ready */
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static int tx_cap(const struct c3_uart *u)
{
    int n = (int)((reg(u->mem) >> 4) & 7u);
    return (n ? n : 1) * BLOCK;
}

static int rx_cap(const struct c3_uart *u)
{
    int n = (int)((reg(u->mem) >> 1) & 7u);
    n = n ? n : 1;
    return (n > 2 ? 2 : n) * BLOCK;
}

/* the UART core's clock, in Hz */
static double sclk_hz(const struct c3_uart *u)
{
    u32 c = reg(u->clk), sel = (c >> 20) & 3u, num = (c >> 12) & 0xffu, a = (c >> 6) & 0x3fu, b = c & 0x3fu;
    double src = sel == 1 ? (double)esk_esp32c3_apb_hz(u->s)
               : sel == 2 ? 17.5e6
                          : 40e6; /* 3: XTAL_CLK */
    double div = (double)num + 1.0 + (a ? (double)b / (double)a : 0.0);
    return src / div;
}

/* a bit's time in core cycles */
static u32 bit_cycles(const struct c3_uart *u)
{
    u32 d = reg(u->clkdiv), div = d & 0xfffu, frag = (d >> 20) & 15u;
    double per = (double)div + (double)frag / 16.0, baud, c;
    if (per < 1.0)
        per = 1.0;
    baud = sclk_hz(u) / per;
    c = (double)esk_mach_core_hz(u->s) / baud;
    return c < 1.0 ? 1u : (u32)(c + 0.5);
}

static int data_bits(const struct c3_uart *u) { return 5 + (int)((reg(u->conf0) >> 2) & 3u); }

/* STOP_BIT_NUM: 1 one bit, 2 one and a half, 3 two */
static int stop_halves(const struct c3_uart *u)
{
    u32 n = (reg(u->conf0) >> 4) & 3u;
    return n == 2 ? 3 : n == 3 ? 4 : 2;
}

static int nbits(const struct c3_uart *u) { return data_bits(u) + ((reg(u->conf0) & CONF0_PARITY_EN) ? 1 : 0); }

static u32 with_parity(const struct c3_uart *u, u32 c)
{
    int n = data_bits(u), ones = 0, i;
    c &= (1u << n) - 1u;
    if (!(reg(u->conf0) & CONF0_PARITY_EN))
        return c;
    for (i = 0; i < n; i++)
        ones += (int)((c >> i) & 1u);
    /* PARITY 0 even, 1 odd */
    return c | (u32)((ones & 1) ^ ((reg(u->conf0) & CONF0_PARITY) ? 1 : 0)) << n;
}

static int parity_ok(const struct c3_uart *u, u32 bits)
{
    int n = data_bits(u);
    if (!(reg(u->conf0) & CONF0_PARITY_EN))
        return 1;
    return with_parity(u, bits) == (bits & ((2u << n) - 1u));
}

/* the level conditions, INT_ST and the matrix source */
static void update(struct c3_uart *u)
{
    u32 raw = reg(u->raw), c1 = reg(u->conf1);
    if ((u32)u->nrx > (c1 & 0x1ffu))
        raw |= RAW_RXFIFO_FULL;
    if ((u32)u->ntx < ((c1 >> 9) & 0x1ffu))
        raw |= RAW_TXFIFO_EMPTY;
    esk_rf_hw(u->raw, raw);
    esk_rf_hw(u->st, raw & reg(u->ena));
    esk_rf_hw(u->status, (reg(u->status) & ~(0x3ffu | 0x3ffu << 16)) | (u32)u->nrx | (u32)u->ntx << 16);
    if ((reg(u->st) != 0) != u->level) {
        u->level = reg(u->st) != 0;
        esk_esp32c3_source(u->s, u->source, u->level);
    }
}

static void set_raw(struct c3_uart *u, u32 bits)
{
    esk_rf_hw(u->raw, reg(u->raw) | bits);
}

static void receive(struct c3_uart *u, u32 bits, int framing_error, int brk);

/* the clock runs while the line is busy or a receive timeout counts */
static void clocking(struct c3_uart *u)
{
    int on = u->busy || (u->rx_any && (reg(u->conf1) & CONF1_RX_TOUT_EN));
    if (on != u->clocked) {
        if (on)
            u->since = u->s->cycles;
        esk_rf_clock(u->p, &u->clocked, on);
    }
}

static void run(struct c3_uart *u, u64 cycles)
{
    u->left = cycles;
    u->since = u->s->cycles;
    u->busy = 1;
    clocking(u);
}

/* the next thing on the TX line: a frame, or a break once the FIFO is empty */
static void tx_next(struct c3_uart *u)
{
    struct esk_mach *s = u->s;
    u64 b;
    int n;
    if (u->busy || !(reg(u->clk) & CLK_TX_SCLK_EN) || (reg(u->clk) & CLK_RST_CORE))
        return;
    b = bit_cycles(u);
    n = nbits(u);
    if (u->ntx && !((reg(u->conf0) & CONF0_TX_FLOW_EN) && u->cts)) {
        u32 c = u->tx[0], bits = with_parity(u, c);
        memmove(u->tx, u->tx + 1, (size_t)(u->ntx - 1));
        u->ntx--;
        u->phase = 0;
        run(u, b * (u64)(1 + n) + (b * (u64)stop_halves(u) + 1) / 2);
        if (s->host.uart_frame)
            s->host.uart_frame(s->host.ctx, u->p->name, (int)(c & ((1u << data_bits(u)) - 1u)), bits, n,
                               stop_halves(u), (u32)b);
        else if (s->host.uart_tx)
            s->host.uart_tx(s->host.ctx, u->p->name, (int)c, (u32)b);
        if (reg(u->conf0) & CONF0_LOOPBACK)
            receive(u, bits, 0, 0);
        return;
    }
    if (!u->ntx && u->brk_left > 0) {
        u->phase = 2;
        u->brk_left--;
        run(u, b * (u64)(1 + n + 1));
        if (s->host.uart_frame)
            s->host.uart_frame(s->host.ctx, u->p->name, -1, 0, n + 1, 2, (u32)b);
    }
}

/* ---- the receiver ------------------------------------------------------------------ */

static void receive(struct c3_uart *u, u32 bits, int framing_error, int brk)
{
    int n = data_bits(u);
    if (!(reg(u->clk) & CLK_RX_SCLK_EN) || (reg(u->conf0) & CONF0_RXFIFO_RST))
        return;
    u->rx_idle = 0;
    if (brk) {
        set_raw(u, RAW_BRK_DET);
    } else {
        if (!parity_ok(u, bits))
            set_raw(u, RAW_PARITY_ERR);
        if (framing_error)
            set_raw(u, RAW_FRM_ERR);
        if (u->nrx < rx_cap(u)) {
            u->rx[u->nrx++] = (u8)(bits & ((1u << n) - 1u));
            u->rx_any = 1;
        } else {
            set_raw(u, RAW_RXFIFO_OVF);
        }
    }
    clocking(u); /* the timeout counts in ticks */
    update(u);
    esk_esp32c3_uhci_uart_event(u->s, u->unit, brk ? UHCI_EV_BREAK : UHCI_EV_RX);
}

static int ur_room(void *ctx)
{
    const struct c3_uart *u = (const struct c3_uart *)ctx;
    return (reg(u->clk) & CLK_RX_SCLK_EN) != 0;
}

static void ur_put(void *ctx, int c)
{
    struct c3_uart *u = (struct c3_uart *)ctx;
    receive(u, with_parity(u, (u32)c), 0, 0);
}

static u32 ur_format(void *ctx)
{
    const struct c3_uart *u = (const struct c3_uart *)ctx;
    return (u32)nbits(u) | (u32)stop_halves(u) << 8;
}

static void ur_put_frame(void *ctx, u32 bits, int framing_error)
{
    struct c3_uart *u = (struct c3_uart *)ctx;
    /* all zeros with a framing error: a break */
    receive(u, bits, framing_error, framing_error && !bits);
}

static void ur_started(void *ctx)
{
    struct c3_uart *u = (struct c3_uart *)ctx;
    u->rx_idle = 0;
}

static u32 ur_bit_cycles(void *ctx) { return bit_cycles((const struct c3_uart *)ctx); }

/* ---- the registers -------------------------------------------------------------------- */

static u32 ur_read(void *ctx, u32 off, int n)
{
    struct c3_uart *u = (struct c3_uart *)ctx;
    if (off == esk_rf_off(u->fifo)) {
        u32 v;
        if (!u->nrx)
            return 0;
        v = u->rx[0];
        if (!u->s->bus.debug) {
            memmove(u->rx, u->rx + 1, (size_t)(u->nrx - 1));
            u->nrx--;
            if (!u->nrx)
                u->rx_any = 0;
            update(u);
        }
        return v;
    }
    return esk_rf_read(u->p, off, n);
}

static void ur_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_uart *u = (struct c3_uart *)ctx;
    if (off == esk_rf_off(u->fifo)) {
        if (!(reg(u->conf0) & CONF0_TXFIFO_RST) && u->ntx < tx_cap(u))
            u->tx[u->ntx++] = (u8)v;
    } else if (off == esk_rf_off(u->clr)) {
        esk_rf_hw(u->raw, reg(u->raw) & ~v);
    } else if (off == esk_rf_off(u->raw) || off == esk_rf_off(u->st) || off == esk_rf_off(u->status)) {
        /* read only */
    } else {
        u32 was0 = reg(u->conf0);
        esk_rf_write(u->p, off, n, v);
        if (off == esk_rf_off(u->id))
            esk_rf_hw(u->id, reg(u->id) & ~ID_REG_UPDATE); /* synchronised */
        if (off == esk_rf_off(u->conf0)) {
            if (reg(u->conf0) & CONF0_TXFIFO_RST)
                u->ntx = 0;
            if (reg(u->conf0) & CONF0_RXFIFO_RST) {
                u->nrx = 0;
                u->rx_any = 0;
            }
            if ((reg(u->conf0) & CONF0_TXD_BRK) && !(was0 & CONF0_TXD_BRK))
                u->brk_left = (int)(reg(u->txbrk) & 0xffu);
        }
    }
    tx_next(u);
    update(u);
}

static void ur_tick(void *ctx, u32 cycles)
{
    struct c3_uart *u = (struct c3_uart *)ctx;
    u64 b = bit_cycles(u);
    cycles = esk_mach_after(u->s, cycles, u->since);
    if (u->rx_any && (reg(u->conf1) & CONF1_RX_TOUT_EN)) {
        u64 thr = (reg(u->mem) >> 16) & 0x3ffu;
        u->rx_idle += cycles;
        if (u->rx_idle >= thr * b && u->nrx) {
            set_raw(u, RAW_RXFIFO_TOUT);
            u->rx_any = 0;
            update(u);
            esk_esp32c3_uhci_uart_event(u->s, u->unit, UHCI_EV_IDLE);
        }
    }
    if (!u->busy) {
        clocking(u);
        return;
    }
    if (u->left > cycles) {
        u->left -= cycles;
        return;
    }
    u->left = 0;
    if (u->phase == 0) {
        /* the frame is out; the FIFO empty, the transfer is over: at least
         * TX_IDLE_NUM bit times of idle line before the next */
        u64 idle = (reg(u->idle) >> 10) & 0x3ffu;
        if (!u->ntx)
            set_raw(u, RAW_TX_DONE);
        if (idle && !u->ntx) {
            u->phase = 1;
            u->left = idle * b;
            u->since = u->s->cycles;
            update(u);
            return;
        }
    } else if (u->phase == 2) {
        if (u->brk_left <= 0)
            set_raw(u, RAW_TX_BRK_DONE | RAW_TX_BRK_IDLE_DONE);
    }
    u->busy = 0;
    tx_next(u);
    clocking(u);
    update(u);
}

static int ur_next(void *ctx, u32 *cycles)
{
    struct c3_uart *u = (struct c3_uart *)ctx;
    u64 c = u->busy ? u->left : 0;
    if (u->rx_any && (reg(u->conf1) & CONF1_RX_TOUT_EN)) {
        u64 thr = ((reg(u->mem) >> 16) & 0x3ffu) * bit_cycles(u);
        u64 t = thr > u->rx_idle ? thr - u->rx_idle : 1;
        if (!c || t < c)
            c = t;
    }
    if (!c)
        return 0;
    *cycles = c > 0xffffffffu ? 0xffffffffu : (u32)c;
    return 1;
}

static void ur_reset(void *ctx)
{
    struct c3_uart *u = (struct c3_uart *)ctx;
    u->ntx = u->nrx = 0;
    u->busy = 0;
    u->clocked = 0;
    u->left = 0;
    u->brk_left = 0;
    u->rx_any = 0;
    u->rx_idle = 0;
    u->level = 0;
    u->cts = 0;
    update(u);
}

/* ---- UHCI's side (uhci.c) ------------------------------------------------------- */

static struct c3_uart *uart_of(struct esk_mach *m, int unit)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_esp32c3_uart_ops && ((struct c3_uart *)p->ctx)->unit == unit)
            return (struct c3_uart *)p->ctx;
    }
    return NULL;
}

int esk_esp32c3_uart_rx_take(struct esk_mach *m, int unit, u8 *c)
{
    struct c3_uart *u = uart_of(m, unit);
    if (!u || !u->nrx)
        return 0;
    *c = u->rx[0];
    memmove(u->rx, u->rx + 1, (size_t)(u->nrx - 1));
    u->nrx--;
    update(u);
    return 1;
}

int esk_esp32c3_uart_tx_room(struct esk_mach *m, int unit)
{
    struct c3_uart *u = uart_of(m, unit);
    if (!u || (reg(u->conf0) & CONF0_TXFIFO_RST))
        return 0;
    return tx_cap(u) - u->ntx;
}

void esk_esp32c3_uart_tx_put(struct esk_mach *m, int unit, u8 c)
{
    struct c3_uart *u = uart_of(m, unit);
    if (!u || u->ntx >= tx_cap(u))
        return;
    u->tx[u->ntx++] = c;
    tx_next(u);
    update(u);
}

u32 esk_esp32c3_uart_char_cycles(struct esk_mach *m, int unit)
{
    struct c3_uart *u = uart_of(m, unit);
    return u ? bit_cycles(u) * (u32)(2 + nbits(u)) : 0u;
}

const struct esk_dev_ops esk_esp32c3_uart_ops = {
    "esp32c3-uart", ur_read, ur_write, ur_reset, ur_tick, ur_next,
};

void *esk_esp32c3_uart_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_uart *c;
    if (strcmp(p->name, "UART0") && strcmp(p->name, "UART1"))
        return 0;
    c = (struct c3_uart *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    memset(c, 0, sizeof *c);
    c->s = s;
    c->p = p;
    c->unit = p->name[4] - '0';
    c->source = c->unit ? 22 : 21;
    c->fifo = esk_rf_reg(p, "FIFO");
    c->raw = esk_rf_reg(p, "INT_RAW");
    c->st = esk_rf_reg(p, "INT_ST");
    c->ena = esk_rf_reg(p, "INT_ENA");
    c->clr = esk_rf_reg(p, "INT_CLR");
    c->clkdiv = esk_rf_reg(p, "CLKDIV");
    c->status = esk_rf_reg(p, "STATUS");
    c->conf0 = esk_rf_reg(p, "CONF0");
    c->conf1 = esk_rf_reg(p, "CONF1");
    c->txbrk = esk_rf_reg(p, "TXBRK_CONF");
    c->idle = esk_rf_reg(p, "IDLE_CONF");
    c->mem = esk_rf_reg(p, "MEM_CONF");
    c->clk = esk_rf_reg(p, "CLK_CONF");
    c->id = esk_rf_reg(p, "ID");
    if (!c->fifo || !c->raw || !c->st || !c->ena || !c->clr || !c->clkdiv || !c->status || !c->conf0 ||
        !c->conf1 || !c->txbrk || !c->idle || !c->mem || !c->clk || !c->id)
        return 0;
    esk_mach_rx_port(s, p->name, ur_room, ur_put, ur_bit_cycles, c);
    esk_mach_rx_frames(s, ur_format, ur_put_frame, ur_started);
    return c;
}
