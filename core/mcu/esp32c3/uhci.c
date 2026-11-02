/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* uhci.c -- the ESP32-C3's UART/HCI controller (UHCI0 at 0x60014000, TRM
 * v1.4 chapter 27): a UART's data to and from the GDMA (peripheral 2), as
 * ESP-IDF's HCI-over-UART transport for the Bluetooth controller uses it.
 *
 *   - CONF0's UART0_CE or UART1_CE attaches a UART (uart.c), while CLK_EN
 *     is set and TX_RST / RX_RST are clear.
 *   - Transmitting: the bytes of the GDMA's transmit list go into the
 *     UART's TX FIFO as it has room. With SEPER_EN a frame begins and ends
 *     (after an EOF descriptor's last byte) with the separator (ESC_CONF0's
 *     SEPER_CHAR) and TX_START is raised as it begins. ESCAPE_CONF's TX_*
 *     bits replace, in the data, the separator by SEPER_ESC_CHAR0/1, 0xDB
 *     (ESC_SEQ0) by its two characters, 0x11 (ESC_SEQ1) and 0x13 (ESC_SEQ2)
 *     by theirs.
 *   - Receiving: the bytes of the UART's RX FIFO go into the GDMA's receive
 *     list while it has one (they wait in the FIFO otherwise), the RX_*
 *     escapes undone. With SEPER_EN a frame starts at a separator (RX_START)
 *     and the next separator ends it; bytes outside a frame are dropped. A
 *     frame also ends (the descriptor written back with suc_eof) after
 *     PKT_THRS bytes (LEN_EOF_EN), on a break (UART_RX_BRK_EOF_EN) or when
 *     the UART's receive timeout fires (UART_IDLE_EOF_EN).
 *   - INT_RAW's bits, APP_CTRL0 and APP_CTRL1 set by software; INT_ST is
 *     INT_RAW and INT_ENA, on source 15; INT_CLR clears.
 *   - The work is done at once at the UART's events, and polled once a
 *     character time while attached (a transmit list started later).
 * Not yet: the HCI packet header (HEAD_EN: its sequence numbers,
 * acknowledgement, checksum and CRC, CONF1's checks, RX_HEAD, ACK_NUM), the
 * quick-sent register queues (QUICK_SENT, REG_Q*), the FIFO hang timeouts
 * (HUNG_CONF, RX_HUNG, TX_HUNG), STATE0/1, and UHCI1, which no GDMA channel
 * serves (its registers keep what is written). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define PERI_UHCI0 2
#define SRC_UHCI0 15

#define C0_TX_RST (1u << 0)
#define C0_RX_RST (1u << 1)
#define C0_UART0_CE (1u << 2)
#define C0_UART1_CE (1u << 3)
#define C0_SEPER_EN (1u << 5)
#define C0_IDLE_EOF (1u << 8)
#define C0_LEN_EOF (1u << 9)
#define C0_CLK_EN (1u << 11)
#define C0_BRK_EOF (1u << 12)

#define I_RX_START (1u << 0)
#define I_TX_START (1u << 1)
#define I_APP_CTRL (3u << 7)

struct c3_uhci {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *conf0, *raw, *st, *ena, *clr, *esc, *esc0, *esc1, *esc2, *esc3, *thres;
    int dma;     /* its GDMA peripheral, -1 for none */
    int source;  /* its interrupt source, -1 for none */
    int level, clocked;
    /* transmitting */
    u8 out[8];
    int nout;
    int tx_open;  /* a frame's separator is out */
    /* receiving */
    int rx_open;  /* inside a frame (SEPER_EN) */
    int rx_esc;   /* the first character of an escape, or -1 */
    u32 rx_count; /* bytes into the list since the last EOF */
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static struct c3_uhci *uhci_of(struct esk_mach *m, int unit)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        struct c3_uhci *k;
        u32 c;
        if (!p)
            break;
        if (p->ops != &esk_esp32c3_uhci_ops)
            continue;
        k = (struct c3_uhci *)p->ctx;
        c = reg(k->conf0);
        if (k->dma >= 0 && (c & C0_CLK_EN) && (c & (unit ? C0_UART1_CE : C0_UART0_CE)))
            return k;
    }
    return NULL;
}

/* the attached UART: 0, 1, or -1 */
static int unit_of(const struct c3_uhci *k)
{
    u32 c = reg(k->conf0);
    if (k->dma < 0 || !(c & C0_CLK_EN))
        return -1;
    return (c & C0_UART0_CE) ? 0 : (c & C0_UART1_CE) ? 1 : -1;
}

static void update(struct c3_uhci *k)
{
    esk_rf_hw(k->st, reg(k->raw) & reg(k->ena));
    if (k->source >= 0 && (reg(k->st) != 0) != k->level) {
        k->level = reg(k->st) != 0;
        esk_esp32c3_source(k->s, k->source, k->level);
    }
}

static void raise_int(struct c3_uhci *k, u32 bits)
{
    esk_rf_hw(k->raw, reg(k->raw) | bits);
    update(k);
}

/* the escape sequences: [i] = { original, first, second, ESCAPE_CONF's TX
 * bit } for the separator, 0xDB, 0x11 and 0x13 */
static void escapes(const struct c3_uhci *k, u32 e[4][4])
{
    const struct esk_rf_reg *r[4];
    int i;
    r[0] = k->esc0;
    r[1] = k->esc1;
    r[2] = k->esc2;
    r[3] = k->esc3;
    for (i = 0; i < 4; i++) {
        u32 v = reg(r[i]);
        e[i][0] = v & 0xffu;
        e[i][1] = (v >> 8) & 0xffu;
        e[i][2] = (v >> 16) & 0xffu;
        e[i][3] = 1u << i;
    }
}

/* ---- transmitting ---------------------------------------------------------- */

static void encode(struct c3_uhci *k, u8 b)
{
    u32 e[4][4], tx = reg(k->esc) & 0xfu;
    int i;
    escapes(k, e);
    for (i = 0; i < 4; i++)
        if ((tx & e[i][3]) && b == e[i][0]) {
            k->out[k->nout++] = (u8)e[i][1];
            k->out[k->nout++] = (u8)e[i][2];
            return;
        }
    k->out[k->nout++] = b;
}

static void transmit(struct c3_uhci *k, int unit)
{
    u32 c = reg(k->conf0), sep = reg(k->esc0) & 0xffu;
    int guard = 0;
    if (c & C0_TX_RST)
        return;
    while (guard++ < 4096 && esk_esp32c3_uart_tx_room(k->s, unit) > 0) {
        u8 b;
        int eof = 0;
        if (k->nout) {
            esk_esp32c3_uart_tx_put(k->s, unit, k->out[0]);
            memmove(k->out, k->out + 1, (size_t)(k->nout - 1));
            k->nout--;
            continue;
        }
        if (!esk_esp32c3_dma_out(k->s, k->dma, &b, &eof))
            break;
        if ((c & C0_SEPER_EN) && !k->tx_open) {
            k->out[k->nout++] = (u8)sep;
            k->tx_open = 1;
            raise_int(k, I_TX_START);
        }
        encode(k, b);
        if (eof && (c & C0_SEPER_EN)) {
            k->out[k->nout++] = (u8)sep;
            k->tx_open = 0;
        }
    }
}

/* ---- receiving ------------------------------------------------------------- */

static void rx_eof(struct c3_uhci *k)
{
    if (!k->rx_count)
        return;
    esk_esp32c3_dma_in_eof(k->s, k->dma);
    k->rx_count = 0;
}

static void rx_store(struct c3_uhci *k, u8 b)
{
    u32 thr = reg(k->thres) & 0x1fffu;
    esk_esp32c3_dma_in(k->s, k->dma, b);
    k->rx_count++;
    if ((reg(k->conf0) & C0_LEN_EOF) && thr && k->rx_count >= thr)
        rx_eof(k);
}

static void decode(struct c3_uhci *k, u8 c)
{
    u32 e[4][4], rx = (reg(k->esc) >> 4) & 0xfu, conf = reg(k->conf0);
    int i;
    if ((conf & C0_SEPER_EN) && c == (reg(k->esc0) & 0xffu)) {
        k->rx_esc = -1;
        if (k->rx_open)
            rx_eof(k);
        k->rx_open = 1;
        raise_int(k, I_RX_START);
        return;
    }
    if ((conf & C0_SEPER_EN) && !k->rx_open)
        return;
    escapes(k, e);
    if (k->rx_esc >= 0) {
        int first = k->rx_esc;
        k->rx_esc = -1;
        for (i = 0; i < 4; i++)
            if ((rx & e[i][3]) && (u32)first == e[i][1] && c == e[i][2]) {
                rx_store(k, (u8)e[i][0]);
                return;
            }
        rx_store(k, (u8)first);
    }
    for (i = 0; i < 4; i++)
        if ((rx & e[i][3]) && c == e[i][1]) {
            k->rx_esc = c;
            return;
        }
    rx_store(k, c);
}

static void receive(struct c3_uhci *k, int unit)
{
    int guard = 0;
    u8 c;
    if (reg(k->conf0) & C0_RX_RST)
        return;
    while (guard++ < 4096 && esk_esp32c3_dma_in_ready(k->s, k->dma) &&
           esk_esp32c3_uart_rx_take(k->s, unit, &c))
        decode(k, c);
}

static void pump(struct c3_uhci *k)
{
    int unit = unit_of(k);
    if (unit < 0)
        return;
    receive(k, unit);
    transmit(k, unit);
}

void esk_esp32c3_uhci_uart_event(struct esk_mach *m, int unit, int ev)
{
    struct c3_uhci *k = uhci_of(m, unit);
    u32 c;
    if (!k)
        return;
    receive(k, unit);
    c = reg(k->conf0);
    if ((ev == UHCI_EV_BREAK && (c & C0_BRK_EOF)) || (ev == UHCI_EV_IDLE && (c & C0_IDLE_EOF)))
        rx_eof(k);
}

/* ---- the registers ----------------------------------------------------------- */

static void clocking(struct c3_uhci *k)
{
    esk_rf_clock(k->p, &k->clocked, unit_of(k) >= 0);
}

static void restart(struct c3_uhci *k, int tx, int rx)
{
    if (tx) {
        k->nout = 0;
        k->tx_open = 0;
    }
    if (rx) {
        k->rx_open = 0;
        k->rx_esc = -1;
        k->rx_count = 0;
    }
}

static u32 uh_read(void *ctx, u32 off, int n)
{
    struct c3_uhci *k = (struct c3_uhci *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void uh_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_uhci *k = (struct c3_uhci *)ctx;
    if (off == esk_rf_off(k->clr)) {
        esk_rf_hw(k->raw, reg(k->raw) & ~v);
        update(k);
        return;
    }
    if (off == esk_rf_off(k->raw)) {
        /* only APP_CTRL0 and APP_CTRL1 are software's */
        esk_rf_hw(k->raw, (reg(k->raw) & ~I_APP_CTRL) | (v & I_APP_CTRL));
        update(k);
        return;
    }
    if (off == esk_rf_off(k->st))
        return;
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->conf0))
        restart(k, (reg(k->conf0) & C0_TX_RST) != 0, (reg(k->conf0) & C0_RX_RST) != 0);
    update(k);
    clocking(k);
    pump(k);
}

static void uh_tick(void *ctx, u32 cycles)
{
    struct c3_uhci *k = (struct c3_uhci *)ctx;
    (void)cycles;
    pump(k);
    clocking(k);
}

static int uh_next(void *ctx, u32 *cycles)
{
    struct c3_uhci *k = (struct c3_uhci *)ctx;
    int unit = unit_of(k);
    u32 c;
    if (unit < 0)
        return 0;
    c = esk_esp32c3_uart_char_cycles(k->s, unit);
    *cycles = c ? c : 1u;
    return 1;
}

static void uh_reset(void *ctx)
{
    struct c3_uhci *k = (struct c3_uhci *)ctx;
    restart(k, 1, 1);
    k->level = 0;
    esk_rf_clock(k->p, &k->clocked, 0);
    update(k);
}

const struct esk_dev_ops esk_esp32c3_uhci_ops = {
    "esp32c3-uhci", uh_read, uh_write, uh_reset, uh_tick, uh_next,
};

void *esk_esp32c3_uhci_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_uhci *k = (struct c3_uhci *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->dma = strcmp(p->name, "UHCI0") == 0 ? PERI_UHCI0 : -1;
    k->source = k->dma >= 0 ? SRC_UHCI0 : -1;
    k->rx_esc = -1;
    k->conf0 = esk_rf_reg(p, "CONF0");
    k->raw = esk_rf_reg(p, "INT_RAW");
    k->st = esk_rf_reg(p, "INT_ST");
    k->ena = esk_rf_reg(p, "INT_ENA");
    k->clr = esk_rf_reg(p, "INT_CLR");
    k->esc = esk_rf_reg(p, "ESCAPE_CONF");
    k->esc0 = esk_rf_reg(p, "ESC_CONF0");
    k->esc1 = esk_rf_reg(p, "ESC_CONF1");
    k->esc2 = esk_rf_reg(p, "ESC_CONF2");
    k->esc3 = esk_rf_reg(p, "ESC_CONF3");
    k->thres = esk_rf_reg(p, "PKT_THRES");
    if (!k->conf0 || !k->raw || !k->st || !k->ena || !k->clr || !k->esc || !k->esc0 || !k->esc1 ||
        !k->esc2 || !k->esc3 || !k->thres)
        return 0;
    return k;
}
