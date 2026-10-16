/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* uart.c -- a PIC32MX's UARTs (Family Reference Manual section 21), over
 * the register file: UxMODE, UxSTA, UxTXREG, UxRXREG, UxBRG, at their
 * baud rate on their pins (the device pack's UxTX and UxRX).
 *
 *   - While UxMODE.ON and UxSTA.UTXEN are set, a write to UxTXREG goes into
 *     the transmit FIFO (8 deep; UTXBF says it is full, and a write then is
 *     lost), which drains a frame at a time: a start bit, 8 data bits (9
 *     with PDSEL 11), the parity bit with PDSEL 01 or 10, one stop bit or
 *     two with STSEL; each bit 16 (4 with BRGH) x (UxBRG + 1) PBCLK cycles.
 *     Each frame goes to the MCU runtime with its bit time in core cycles,
 *     and the TX pin carries the waveform. TRMT is set when the FIFO and
 *     the shift register are empty.
 *   - The receiver: bytes arrive from the session's serial input or the RX
 *     pin while ON and URXEN are set and the FIFO (8 deep) has room;
 *     URXDA says one waits, a read of UxRXREG takes it; a byte that finds
 *     the FIFO full is lost and sets OERR, which stops the receiver until
 *     the program clears it. RIDLE is set while nothing arrives.
 *   - The interrupts are persistent, as on the part: UARTn_TX while
 *     UTXISEL's condition holds (00 the FIFO has room, 10 it is empty, 01
 *     all is sent), UARTn_RX while URXISEL's does (00 a byte waits, 01 the
 *     FIFO is half full, 10 three quarters), and UARTn_ERR on an overrun.
 *     Their flags set again as soon as the program clears them while the
 *     condition holds.
 * Not modelled: the address detect mode, the handshake lines (UEN), IrDA,
 * loopback, auto-baud, break, the polarity bits; parity and framing errors
 * (the input is always well formed). */
#include <stdio.h>
#include <string.h>

#include "mcu/pic32/pic32.h"

#define FIFO 8

#define MODE_ON 0x8000u
#define MODE_BRGH 0x0008u
#define STA_URXDA 0x0001u
#define STA_OERR 0x0002u
#define STA_RIDLE 0x0010u
#define STA_TRMT 0x0100u
#define STA_UTXBF 0x0200u
#define STA_UTXEN 0x0400u
#define STA_URXEN 0x1000u

struct uart {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    int n;
    struct esk_rf_reg *mode, *sta, *txreg, *rxreg, *brg;
    int irq_tx, irq_rx, irq_err;
    int tx_pin, rx_pin; /* port * 32 + bit, or -1 */
    u16 rx[FIFO], tx[FIFO];
    int nrx, rxh, ntx;
    int busy; /* a frame is on the line */
    u64 left; /* its cycles to go */
    int running;
    u64 since;
};

static int on(const struct uart *u) { return (u->mode->value & MODE_ON) != 0; }
static int tx_on(const struct uart *u) { return on(u) && (u->sta->value & STA_UTXEN); }
static int rx_on(const struct uart *u) { return on(u) && (u->sta->value & STA_URXEN); }

static struct uart *uart_n(struct esk_mach *s, int n)
{
    char nm[16];
    struct esk_rf_periph *p;
    snprintf(nm, sizeof nm, "UART%d", n);
    p = s->rf ? esk_rf_periph(s->rf, nm) : 0;
    return p && p->ops == &esk_pic32_uart_ops ? (struct uart *)p->ctx : 0;
}

int esk_pic32_uart_role(struct esk_mach *s, int port, int bit)
{
    int n, pin = 32 * port + bit;
    for (n = 1; n <= 6; n++) {
        struct uart *u = uart_n(s, n);
        if (!u)
            continue;
        if (u->tx_pin == pin && tx_on(u))
            return 1;
        if (u->rx_pin == pin && rx_on(u))
            return 2;
    }
    return 0;
}

int esk_pic32_uart_pin(struct esk_mach *s, const char *unit, int tx, int *port, int *bit)
{
    int n = 0;
    struct uart *u;
    if (sscanf(unit, "UART%d", &n) != 1 || !(u = uart_n(s, n)))
        return -1;
    n = tx ? u->tx_pin : u->rx_pin;
    if (n < 0 || n % 32 > 15)
        return -1;
    *port = n / 32;
    *bit = n % 32;
    return 0;
}

/* a bit's time in core cycles */
static u32 bit_cycles(const struct uart *u)
{
    u64 c = (u64)((u->mode->value & MODE_BRGH) ? 4u : 16u) * ((u->brg->value & 0xffffu) + 1u) *
            esk_pic32_pb_div(u->s);
    return c > 0xffffffffu ? 0xffffffffu : (u32)c;
}

static int frame_bits(const struct uart *u)
{
    u32 pd = (u32)(u->mode->value >> 1 & 3u);
    return 1 + (pd == 3 ? 9 : 8) + (pd == 1 || pd == 2 ? 1 : 0) + ((u->mode->value & 1u) ? 2 : 1);
}

/* UxSTA's status bits */
static void status(struct uart *u)
{
    u64 v = u->sta->value & ~(u64)(STA_URXDA | STA_RIDLE | STA_TRMT | STA_UTXBF);
    if (u->nrx)
        v |= STA_URXDA;
    v |= STA_RIDLE;
    if (!u->ntx && !u->busy)
        v |= STA_TRMT;
    if (u->ntx >= FIFO)
        v |= STA_UTXBF;
    if (v != u->sta->value)
        esk_rf_hw(u->sta, v);
}

static int tx_cond(void *ctx)
{
    struct uart *u = (struct uart *)ctx;
    u32 sel = (u32)(u->sta->value >> 14 & 3u);
    if (!tx_on(u))
        return 0;
    if (sel == 0)
        return u->ntx < FIFO;
    if (sel == 1)
        return !u->ntx && !u->busy;
    return !u->ntx;
}

static int rx_cond(void *ctx)
{
    struct uart *u = (struct uart *)ctx;
    u32 sel = (u32)(u->sta->value >> 6 & 3u);
    if (!on(u))
        return 0;
    if (sel == 0)
        return u->nrx >= 1;
    if (sel == 1)
        return u->nrx >= FIFO / 2;
    return u->nrx >= FIFO * 3 / 4;
}

static void uart_clock(struct uart *u)
{
    int was = u->running;
    esk_rf_clock(u->p, &u->running, u->busy);
    if (u->running && !was)
        u->since = u->s->cycles;
}

/* the next frame goes onto the line, if one waits and it can */
static void start(struct uart *u)
{
    struct esk_mach *s = u->s;
    u32 bit;
    u16 c;
    if (u->busy || !u->ntx || !tx_on(u))
        return;
    bit = bit_cycles(u);
    c = u->tx[0];
    memmove(u->tx, u->tx + 1, (size_t)(FIFO - 1) * sizeof u->tx[0]);
    u->ntx--;
    u->busy = 1;
    u->left = (u64)bit * (u64)frame_bits(u);
    uart_clock(u);
    if (s->host.uart_tx)
        s->host.uart_tx(s->host.ctx, u->p->name, c & 0xff, bit);
}

static void after(struct uart *u)
{
    status(u);
    esk_pic32_irq_update(u->s);
}

static u32 uart_read(void *ctx, u32 off, int n)
{
    struct uart *u = (struct uart *)ctx;
    status(u);
    if (off == esk_rf_off(u->rxreg)) {
        u32 v = u->nrx ? u->rx[u->rxh] : 0;
        if (!u->s->bus.debug && u->nrx) {
            u->rxh = (u->rxh + 1) % FIFO;
            u->nrx--;
            after(u);
        }
        esk_rf_hw(u->rxreg, v);
    }
    return esk_rf_read(u->p, off, n);
}

static void uart_write(void *ctx, u32 off, int n, u32 v)
{
    struct uart *u = (struct uart *)ctx;
    u64 keep = u->sta->value & (STA_URXDA | STA_RIDLE | STA_TRMT | STA_UTXBF);
    int was_tx = tx_on(u), was_rx = rx_on(u);
    esk_rf_write(u->p, off, n, v);
    if (off == esk_rf_off(u->sta))
        u->sta->value = (u->sta->value & ~(u64)(STA_URXDA | STA_RIDLE | STA_TRMT | STA_UTXBF)) | keep;
    if (off == esk_rf_off(u->txreg) && tx_on(u) && u->ntx < FIFO)
        u->tx[u->ntx++] = (u16)(v & 0x1ffu);
    if (!on(u)) {
        /* off: the FIFOs are reset */
        u->ntx = u->nrx = u->rxh = 0;
        u->busy = 0;
        uart_clock(u);
    }
    start(u);
    after(u);
    if (tx_on(u) != was_tx || rx_on(u) != was_rx)
        esk_pic32_pins_changed(u->s); /* the pins are the UART's, or the port's again */
}

static void uart_tick(void *ctx, u32 cycles)
{
    struct uart *u = (struct uart *)ctx;
    struct esk_mach *s = u->s;
    cycles = esk_mach_after(s, cycles, u->since); /* the instruction that started it */
    if (!u->busy)
        return;
    if (u->left > cycles) {
        u->left -= cycles;
        return;
    }
    u->left = 0;
    u->busy = 0;
    start(u);
    uart_clock(u);
    after(u);
}

static int uart_next(void *ctx, u32 *cycles)
{
    struct uart *u = (struct uart *)ctx;
    if (!u->busy)
        return 0;
    *cycles = u->left > 0xffffffffu ? 0xffffffffu : (u32)u->left;
    return 1;
}

static void uart_reset(void *ctx)
{
    struct uart *u = (struct uart *)ctx;
    u->nrx = u->rxh = u->ntx = 0;
    u->busy = 0;
    u->running = 0;
    u->left = 0;
    status(u);
}

const struct esk_dev_ops esk_pic32_uart_ops = {
    "pic32-uart", uart_read, uart_write, uart_reset, uart_tick, uart_next,
};

static int uart_room(void *ctx)
{
    struct uart *u = (struct uart *)ctx;
    return rx_on(u) && !(u->sta->value & STA_OERR);
}

static void uart_put(void *ctx, int c)
{
    struct uart *u = (struct uart *)ctx;
    if (u->nrx >= FIFO) {
        esk_rf_hw(u->sta, u->sta->value | STA_OERR);
        esk_pic32_irq(u->s, u->irq_err);
        return;
    }
    u->rx[(u->rxh + u->nrx++) % FIFO] = (u16)(c & 0xff);
    after(u);
}

static u32 uart_bit_cycles(void *ctx) { return bit_cycles((const struct uart *)ctx); }

void *esk_pic32_uart_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    char nm[16];
    int n = 0;
    struct uart *u;
    if (sscanf(p->name, "UART%d", &n) != 1 || n < 1 || n > 6)
        return 0;
    u = (struct uart *)esk_mach_alloc(s, sizeof *u);
    if (!u)
        return 0;
    memset(u, 0, sizeof *u);
    u->s = s;
    u->p = p;
    u->n = n;
#define REG(f, fmt)                                                                                \
    snprintf(nm, sizeof nm, fmt, n);                                                               \
    u->f = esk_rf_reg(p, nm)
    REG(mode, "U%dMODE");
    REG(sta, "U%dSTA");
    REG(txreg, "U%dTXREG");
    REG(rxreg, "U%dRXREG");
    REG(brg, "U%dBRG");
#undef REG
    if (!u->mode || !u->sta || !u->txreg || !u->rxreg || !u->brg)
        return 0;
    snprintf(nm, sizeof nm, "UART%d_TX", n);
    u->irq_tx = esk_rf_irq_named(s->rf, nm);
    snprintf(nm, sizeof nm, "UART%d_RX", n);
    u->irq_rx = esk_rf_irq_named(s->rf, nm);
    snprintf(nm, sizeof nm, "UART%d_ERR", n);
    u->irq_err = esk_rf_irq_named(s->rf, nm);
    snprintf(nm, sizeof nm, "U%dTX", n);
    u->tx_pin = esk_rf_pin(s->rf, nm);
    snprintf(nm, sizeof nm, "U%dRX", n);
    u->rx_pin = esk_rf_pin(s->rf, nm);
    esk_rf_set_reset(u->sta, STA_TRMT | STA_RIDLE);
    esk_pic32_irq_level(s, u->irq_tx, tx_cond, u);
    esk_pic32_irq_level(s, u->irq_rx, rx_cond, u);
    esk_mach_rx_port(s, p->name, uart_room, uart_put, uart_bit_cycles, u);
    return u;
}
