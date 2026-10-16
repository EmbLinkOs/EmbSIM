/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* uart.c -- a PIC24FJ's UARTs (the data sheet's UART section), over the
 * register file: UxMODE, UxSTA, UxTXREG, UxRXREG, UxBRG, at their baud rate
 * on the pins the Peripheral Pin Select gives them (UART1 to UART4: an RPn
 * pin whose RPORx field selects UxTX, the RP pin RPINRx's UxRXR names) or
 * their own (UART5 and UART6: the pins table's UxTX and UxRX).
 *
 *   - While UxMODE.UARTEN and UxSTA.UTXEN are set, a write to UxTXREG goes
 *     into the transmit buffer (4 deep; UTXBF says it is full, and a write
 *     then is lost), which drains a frame at a time: a start bit, 8 data
 *     bits (9 with PDSEL 11), the parity bit with PDSEL 01 or 10, one stop
 *     bit or two with STSEL; each bit 16 (4 with BRGH) x (UxBRG + 1)
 *     instruction cycles (Fcy / (16 x (UxBRG + 1)) baud). Each frame goes to
 *     the MCU runtime with its bit time, and the TX pin carries the
 *     waveform. TRMT is set when the buffer and the shift register are
 *     empty.
 *   - The receiver: bytes arrive from the session's serial input or the RX
 *     pin while UARTEN (and URXEN, where the part has it) is set and the
 *     buffer (4 deep) has room; URXDA says one waits, a read of UxRXREG
 *     takes it; a byte that finds the buffer full is lost and sets OERR,
 *     which stops the receiver until the program clears it. RIDLE is set
 *     while nothing arrives.
 *   - The interrupts, as their flags are set on the part: UxTX when
 *     UTXISEL's condition comes about (00 a byte moved to the shift
 *     register, so the buffer has room; 01 all is sent; 10 the buffer is
 *     empty), and when the transmitter is enabled; UxRX when URXISEL's
 *     does (0x a byte arrived, 10 the buffer is three-quarters full, 11
 *     full); UxErr on an overrun.
 * Not modelled: the address detect mode, the handshake lines (UEN), IrDA,
 * loopback, auto-baud, break, the polarity bits; parity and framing errors
 * (the input is always well formed).
 *
 * Provenance: written for EmbSIM after EmbLinkOs/EmbCC aa4f10a
 * tools/embsim/pic24-uart.c (which sends a byte at once) and EmbSIM's
 * PIC32MX UART (core/mcu/pic32/uart.c). */
#include <stdio.h>
#include <string.h>

#include "mcu/pic24/pic24.h"

#define FIFO 4

#define MODE_ON 0x8000u
#define MODE_BRGH 0x0008u
#define STA_URXDA 0x0001u
#define STA_OERR 0x0002u
#define STA_RIDLE 0x0010u
#define STA_TRMT 0x0100u
#define STA_UTXBF 0x0200u
#define STA_UTXEN 0x0400u
#define STA_URXEN 0x1000u
#define STA_HW (STA_URXDA | STA_RIDLE | STA_TRMT | STA_UTXBF)

struct uart {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    int n;
    struct esk_rf_reg *mode, *sta, *txreg, *rxreg, *brg;
    int irq_tx, irq_rx, irq_err;
    int tx_fn;          /* its TX's PPS output function, or -1 */
    int tx_pin, rx_pin; /* port * 32 + bit, or -1 */
    int has_urxen;
    u16 rx[FIFO], tx[FIFO];
    int nrx, rxh, ntx;
    int busy; /* a frame is on the line */
    u64 left; /* its cycles to go */
    int running;
    u64 since;
    int tx_was; /* UTXISEL's condition, as last seen */
};

static int on(const struct uart *u) { return (u->mode->value & MODE_ON) != 0; }
static int tx_on(const struct uart *u) { return on(u) && (u->sta->value & STA_UTXEN); }
static int rx_on(const struct uart *u)
{
    return on(u) && (!u->has_urxen || (u->sta->value & STA_URXEN));
}

static struct uart *uart_n(struct esk_mach *s, int n)
{
    char nm[8];
    struct esk_rf_periph *p;
    snprintf(nm, sizeof nm, "UART%d", n);
    p = s->rf ? esk_rf_periph(s->rf, nm) : 0;
    return p && p->ops == &esk_pic24_uart_ops ? (struct uart *)p->ctx : 0;
}

/* where the PPS (or the part) puts its pins now */
static void repin(struct uart *u)
{
    char nm[16];
    struct esk_mach *s = u->s;
    int rp;
    if (u->tx_fn > 0) {
        rp = esk_pic24_pps_out_rp(s, u->tx_fn);
        u->tx_pin = rp >= 0 ? esk_pic24_rp_pin(s, rp) : -1;
        snprintf(nm, sizeof nm, "U%dRXR", u->n);
        rp = esk_pic24_pps_in_rp(s, nm);
        u->rx_pin = rp >= 0 ? esk_pic24_rp_pin(s, rp) : -1;
    } else {
        snprintf(nm, sizeof nm, "U%dTX", u->n);
        u->tx_pin = esk_rf_pin(s->rf, nm);
        snprintf(nm, sizeof nm, "U%dRX", u->n);
        u->rx_pin = esk_rf_pin(s->rf, nm);
    }
}

void esk_pic24_uart_repin(struct esk_mach *s)
{
    int n;
    for (n = 1; n <= 6; n++) {
        struct uart *u = uart_n(s, n);
        if (u)
            repin(u);
    }
}

int esk_pic24_uart_role(struct esk_mach *s, int port, int bit)
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

int esk_pic24_uart_pin(struct esk_mach *s, const char *unit, int tx, int *port, int *bit)
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

/* a bit's time in instruction cycles */
static u32 bit_cycles(const struct uart *u)
{
    u64 c = (u64)((u->mode->value & MODE_BRGH) ? 4u : 16u) * ((u->brg->value & 0xffffu) + 1u);
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
    u64 v = u->sta->value & ~(u64)STA_HW;
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

/* UTXISEL<1:0> (bits 15 and 13)'s condition */
static int tx_cond(const struct uart *u)
{
    u32 sel = (u32)((u->sta->value >> 14 & 2u) | (u->sta->value >> 13 & 1u));
    if (!tx_on(u))
        return 0;
    if (sel == 1)
        return !u->ntx && !u->busy;
    if (sel == 2)
        return !u->ntx;
    return u->ntx < FIFO;
}

/* the transmit interrupt as its condition comes about */
static void tx_edge(struct uart *u)
{
    int c = tx_cond(u);
    if (c && !u->tx_was)
        esk_pic24_irq(u->s, u->irq_tx);
    u->tx_was = c;
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
    /* UTXISEL 00: a byte moved to the shift register */
    if (!(u->sta->value & 0xa000u))
        esk_pic24_irq(s, u->irq_tx);
}

static u32 uart_read(void *ctx, u32 off, int n)
{
    struct uart *u = (struct uart *)ctx;
    status(u);
    if ((off & ~1u) == esk_rf_off(u->rxreg)) {
        u32 v = u->nrx ? u->rx[u->rxh] : 0;
        if (!u->s->bus.debug && u->nrx && !(off & 1u)) {
            u->rxh = (u->rxh + 1) % FIFO;
            u->nrx--;
            status(u);
        }
        esk_rf_hw(u->rxreg, v);
    }
    return esk_rf_read(u->p, off, n);
}

static void uart_write(void *ctx, u32 off, int n, u32 v)
{
    struct uart *u = (struct uart *)ctx;
    u64 keep = u->sta->value & STA_HW;
    int was_tx = tx_on(u), was_rx = rx_on(u);
    esk_rf_write(u->p, off, n, v);
    if ((off & ~1u) == esk_rf_off(u->sta))
        u->sta->value = (u->sta->value & ~(u64)STA_HW) | keep;
    if (off == esk_rf_off(u->txreg) && tx_on(u) && u->ntx < FIFO)
        u->tx[u->ntx++] = (u16)(v & 0x1ffu);
    if (!on(u)) {
        /* off: the buffers are reset */
        u->ntx = u->nrx = u->rxh = 0;
        u->busy = 0;
        uart_clock(u);
    }
    start(u);
    status(u);
    if (!was_tx && tx_on(u))
        u->tx_was = 0; /* enabling the transmitter flags its interrupt */
    tx_edge(u);
    if (tx_on(u) != was_tx || rx_on(u) != was_rx)
        esk_pic24_pins_changed(u->s); /* the pins are the UART's, or the port's again */
}

static void uart_tick(void *ctx, u32 cycles)
{
    struct uart *u = (struct uart *)ctx;
    struct esk_mach *s = u->s;
    if (s->cycles - cycles < u->since)
        cycles = (u32)(s->cycles - u->since); /* the instruction that started it */
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
    status(u);
    tx_edge(u);
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
    u->tx_was = 0;
    repin(u);
    status(u);
}

const struct esk_dev_ops esk_pic24_uart_ops = {
    "pic24-uart", uart_read, uart_write, uart_reset, uart_tick, uart_next,
};

static int uart_room(void *ctx)
{
    struct uart *u = (struct uart *)ctx;
    return rx_on(u) && !(u->sta->value & STA_OERR);
}

static void uart_put(void *ctx, int c)
{
    struct uart *u = (struct uart *)ctx;
    u32 sel;
    if (u->nrx >= FIFO) {
        esk_rf_hw(u->sta, u->sta->value | STA_OERR);
        esk_pic24_irq(u->s, u->irq_err);
        return;
    }
    u->rx[(u->rxh + u->nrx++) % FIFO] = (u16)(c & 0xff);
    status(u);
    sel = (u32)(u->sta->value >> 6 & 3u);
    if (sel < 2 || (sel == 2 && u->nrx == FIFO - 1) || (sel == 3 && u->nrx == FIFO))
        esk_pic24_irq(u->s, u->irq_rx);
}

static u32 uart_bit_cycles(void *ctx) { return bit_cycles((const struct uart *)ctx); }

void *esk_pic24_uart_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    char nm[16];
    int n = 0;
    struct uart *u;
    if (!esk_pic24_is(s->cpu) || sscanf(p->name, "UART%d", &n) != 1 || n < 1 || n > 6)
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
    u->has_urxen = esk_rf_mask(u->sta, "URXEN") != 0;
    snprintf(nm, sizeof nm, "U%dTX", n);
    u->irq_tx = esk_rf_irq_named(s->rf, nm);
    u->tx_fn = esk_rf_pps_out(s->rf, nm);
    snprintf(nm, sizeof nm, "U%dRX", n);
    u->irq_rx = esk_rf_irq_named(s->rf, nm);
    snprintf(nm, sizeof nm, "U%dErr", n);
    u->irq_err = esk_rf_irq_named(s->rf, nm);
    u->tx_pin = u->rx_pin = -1;
    esk_rf_set_reset(u->sta, STA_TRMT | STA_RIDLE);
    esk_mach_rx_port(s, p->name, uart_room, uart_put, uart_bit_cycles, u);
    return u;
}
