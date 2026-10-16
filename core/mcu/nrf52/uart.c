/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* uart.c -- the nRF52840's UART0/UARTE0 (one space, one model) and UARTE1
 * (Product Specification 6.33, 6.34), over the register file, at their
 * baud rate.
 *
 *   - ENABLE 4 is the UART: after STARTTX a byte written to TXD goes onto
 *     the line, and TXDRDY is set when its frame has gone (a byte written
 *     while one is on the line waits for it); after STARTRX the bytes that
 *     arrive come into RXD's six-byte FIFO, RXDRDY each; STOPRX gives RXTO;
 *   - ENABLE 8 is the UARTE: STARTTX (TXSTARTED) sends TXD.MAXCNT bytes
 *     from TXD.PTR, TXDRDY as each goes and ENDTX after the last, with
 *     TXD.AMOUNT; STOPTX gives TXSTOPPED once the frame on the line has
 *     gone; STARTRX (RXSTARTED) puts the bytes that arrive at RXD.PTR,
 *     RXDRDY each, ENDRX and RXD.AMOUNT at RXD.MAXCNT; STOPRX ends the
 *     buffer (ENDRX, RXTO); SHORTS' ENDRX_STARTRX and ENDRX_STOPRX. EasyDMA
 *     reaches Data RAM alone (the PS: elsewhere "may result in a HardFault
 *     or RAM corruption"): a buffer outside it is a warning, and nothing
 *     moves;
 *   - a frame: a start bit, 8 data bits, the parity bit with CONFIG.PARITY,
 *     one stop bit or two with CONFIG.STOP, each 2^32 / BAUDRATE cycles of
 *     the 16 MHz peripheral clock (BAUDRATE 0x01D7E000 is 115200 baud).
 *     Each frame goes to the MCU runtime with its bit time; PSEL.TXD's pin
 *     is driven (high between frames) and PSEL.RXD's is the receiver's
 *     input while the peripheral is enabled.
 * Not modelled: flow control (PSEL.RTS, PSEL.CTS), the error sources, the
 * break. */
#include <string.h>

#include "mcu/nrf52/nrf52.h"

#define RXFIFO 6

struct nuart {
    struct nrfp x;
    int tx_on, rx_on; /* UART: started */
    u8 fifo[RXFIFO];  /* UART's RXD FIFO */
    int nfifo;
    int pending;      /* UART: a byte waits for the line */
    u8 next;
    u32 rx_ptr, rx_max, rx_n; /* UARTE's buffer being filled */
    int rx_buf;
    u32 tx_ptr, tx_max, tx_n; /* UARTE's buffer being sent */
    int tx_buf, tx_stop;
    int busy;         /* a frame is on the line */
    u64 left, since;
    int running;
    u32 last_role;    /* ENABLE and the PSELs as the pins last had them */
};

static struct nuart *uart_named(struct esk_mach *s, const char *name)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_nrf_uart_ops ? (struct nuart *)p->ctx : 0;
}

static u32 enabled(const struct nuart *u) { return esk_nrfp_rd(&u->x, 0x500) & 15u; }

static u32 bit_cycles(const struct nuart *u)
{
    u32 b = esk_nrfp_rd(&u->x, 0x524);
    u64 c;
    if (!b)
        return 0;
    c = ((u64)1 << 34) / b; /* 64 MHz x 2^32 / (16 MHz x BAUDRATE) */
    return c ? (u32)c : 1u;
}

static int frame_bits(const struct nuart *u)
{
    u32 cfg = esk_nrfp_rd(&u->x, 0x56c);
    return 1 + 8 + ((cfg >> 1 & 7u) == 7u ? 1 : 0) + ((cfg >> 4 & 1u) ? 2 : 1);
}

void esk_nrf_uart_roles(struct esk_mach *s, signed char *role)
{
    static const char *const names[2] = { "UARTE0", "UARTE1" };
    int i;
    memset(role, 0, NRF_PINS);
    for (i = 0; i < 2; i++) {
        struct nuart *u = uart_named(s, names[i]);
        int tx, rx;
        if (!u || !enabled(u))
            continue;
        tx = esk_nrf_psel(esk_nrfp_rd(&u->x, 0x50c));
        rx = esk_nrf_psel(esk_nrfp_rd(&u->x, 0x514));
        if (tx >= 0 && tx < NRF_PINS)
            role[tx] = 1;
        if (rx >= 0 && rx < NRF_PINS)
            role[rx] = 2;
    }
}

int esk_nrf_uart_pin(struct esk_mach *s, const char *unit, int tx, int *port, int *bit)
{
    struct nuart *u = uart_named(s, unit);
    int pin;
    if (!u || !enabled(u))
        return -1;
    pin = esk_nrf_psel(esk_nrfp_rd(&u->x, tx ? 0x50c : 0x514));
    if (pin < 0 || pin >= NRF_PINS)
        return -1;
    *port = pin / 16;
    *bit = pin % 16;
    return 0;
}

/* the pins follow when ENABLE or a PSEL changed */
static void roles(struct nuart *u)
{
    u32 r = enabled(u) ^ esk_nrfp_rd(&u->x, 0x50c) * 3u ^ esk_nrfp_rd(&u->x, 0x514) * 5u;
    if (r != u->last_role) {
        u->last_role = r;
        esk_nrf_pins_changed(u->x.s);
    }
}

static int easydma_ok(struct nuart *u, u32 ptr, u32 n, const char *what)
{
    if (!n || (ptr >= NRF_DATA_RAM && ptr - NRF_DATA_RAM < NRF_DATA_RAM_SIZE &&
               NRF_DATA_RAM_SIZE - (ptr - NRF_DATA_RAM) >= n))
        return 1;
    esk_rf_diag(u->x.s, ES_SEV_WARNING, "mcu.easydma_range",
                "%s's %s 0x%08lx is not in Data RAM: EasyDMA reaches only that (the part may fault or corrupt RAM); nothing is moved",
                u->x.p->name, what, (unsigned long)ptr);
    return 0;
}

static void run(struct nuart *u)
{
    int was = u->running;
    esk_rf_clock(u->x.p, &u->running, u->busy);
    if (u->running && !was)
        u->since = u->x.s->cycles;
}

/* a byte onto the line */
static void send(struct nuart *u, u8 c)
{
    struct esk_mach *s = u->x.s;
    u32 bit = bit_cycles(u);
    u->busy = 1;
    u->left = (u64)(bit ? bit : 1u) * (u64)frame_bits(u);
    run(u);
    if (s->host.uart_tx)
        s->host.uart_tx(s->host.ctx, u->x.p->name, c, bit ? bit : 1u);
}

/* UARTE: the next byte of the buffer, or its end */
static void dma_next(struct nuart *u)
{
    if (!u->tx_buf)
        return;
    if (u->tx_n < u->tx_max && !u->tx_stop) {
        u32 b = 0;
        (void)esk_bus_debug_read(&u->x.s->bus, u->tx_ptr + u->tx_n, 1, &b);
        u->tx_n++;
        send(u, (u8)b);
        return;
    }
    u->tx_buf = 0;
    esk_nrfp_hw(&u->x, 0x54c, u->tx_n); /* TXD.AMOUNT */
    esk_nrfp_event(&u->x, 0x120);       /* ENDTX */
    if (u->tx_stop) {
        u->tx_stop = 0;
        esk_nrfp_event(&u->x, 0x158); /* TXSTOPPED */
    }
}

static void uart_task(struct nuart *u, u32 off)
{
    u32 en = enabled(u);
    switch (off) {
    case 0x000: /* STARTRX */
        if (en == 8) {
            u->rx_ptr = esk_nrfp_rd(&u->x, 0x534);
            u->rx_max = esk_nrfp_rd(&u->x, 0x538) & 0xffffu;
            u->rx_n = 0;
            u->rx_buf = easydma_ok(u, u->rx_ptr, u->rx_max, "RXD.PTR");
            esk_nrfp_event(&u->x, 0x14c); /* RXSTARTED */
        }
        u->rx_on = en == 4 || en == 8;
        break;
    case 0x004: /* STOPRX */
        if (u->rx_on) {
            u->rx_on = 0;
            if (en == 8 && u->rx_buf) {
                esk_nrfp_hw(&u->x, 0x53c, u->rx_n);
                u->rx_buf = 0;
                esk_nrfp_event(&u->x, 0x110); /* ENDRX */
            }
            esk_nrfp_event(&u->x, 0x144); /* RXTO */
        }
        break;
    case 0x008: /* STARTTX */
        if (en == 4)
            u->tx_on = 1;
        if (en == 8 && !u->tx_buf) {
            u->tx_ptr = esk_nrfp_rd(&u->x, 0x544);
            u->tx_max = esk_nrfp_rd(&u->x, 0x548) & 0xffffu;
            u->tx_n = 0;
            u->tx_stop = 0;
            esk_nrfp_event(&u->x, 0x150); /* TXSTARTED */
            u->tx_buf = 1;
            if (!easydma_ok(u, u->tx_ptr, u->tx_max, "TXD.PTR"))
                u->tx_max = 0;
            if (!u->busy)
                dma_next(u);
        }
        break;
    case 0x00c: /* STOPTX */
        u->tx_on = 0;
        if (en == 8) {
            if (u->tx_buf && u->busy)
                u->tx_stop = 1; /* after the frame on the line */
            else
                esk_nrfp_event(&u->x, 0x158); /* TXSTOPPED */
        }
        break;
    default:
        break;
    }
}

static void uart_tick(void *ctx, u32 cycles)
{
    struct nuart *u = (struct nuart *)ctx;
    struct esk_mach *s = u->x.s;
    if (!u->busy)
        return;
    if (s->cycles - cycles < u->since)
        cycles = (u32)(s->cycles - u->since);
    if (u->left > cycles) {
        u->left -= cycles;
        return;
    }
    u->left = 0;
    u->busy = 0;
    if (enabled(u) == 8) {
        esk_nrfp_event(&u->x, 0x11c); /* TXDRDY */
        dma_next(u);
    } else {
        esk_nrfp_event(&u->x, 0x11c); /* TXDRDY */
        if (u->pending) {
            u->pending = 0;
            send(u, u->next);
        }
    }
    run(u);
}

static int uart_next(void *ctx, u32 *cycles)
{
    struct nuart *u = (struct nuart *)ctx;
    if (!u->busy)
        return 0;
    *cycles = u->left > 0xffffffffu ? 0xffffffffu : (u32)u->left;
    return 1;
}

static u32 uart_read(void *ctx, u32 off, int n)
{
    struct nuart *u = (struct nuart *)ctx;
    esk_nrfp_inten_read(&u->x, off);
    if (off == 0x518 && enabled(u) == 4) { /* RXD: the FIFO's head */
        u32 v = u->nfifo ? u->fifo[0] : 0;
        if (u->nfifo && !u->x.s->bus.debug) {
            memmove(u->fifo, u->fifo + 1, (size_t)--u->nfifo);
            if (u->nfifo) {
                esk_nrfp_hw(&u->x, 0x518, u->fifo[0]);
                esk_nrfp_event(&u->x, 0x108); /* RXDRDY: the next */
            }
        }
        return v;
    }
    return esk_rf_read(u->x.p, off, n);
}

static void uart_write(void *ctx, u32 off, int n, u32 v)
{
    struct nuart *u = (struct nuart *)ctx;
    int was = esk_nrfp_line(&u->x);
    esk_rf_write(u->x.p, off, n, v);
    esk_nrfp_inten_write(&u->x, off, v);
    if (off < 0x100 && (v & 1))
        uart_task(u, off);
    if (off == 0x51c && enabled(u) == 4 && u->tx_on) { /* TXD */
        if (!u->busy) {
            send(u, (u8)v);
        } else {
            u->pending = 1;
            u->next = (u8)v;
        }
    }
    if (off == 0x500 || off == 0x50c || off == 0x514)
        roles(u);
    esk_nrfp_irq(&u->x, was, 1);
}

static void uart_reset(void *ctx)
{
    struct nuart *u = (struct nuart *)ctx;
    u->tx_on = u->rx_on = u->nfifo = 0;
    u->rx_buf = u->tx_buf = u->tx_stop = u->pending = 0;
    u->busy = 0;
    u->running = 0;
    u->left = 0;
    u->last_role = 0;
    u->x.inten = 0;
}

const struct esk_dev_ops esk_nrf_uart_ops = {
    "nrf-uart", uart_read, uart_write, uart_reset, uart_tick, uart_next,
};

static int uart_room(void *ctx)
{
    struct nuart *u = (struct nuart *)ctx;
    u32 en = enabled(u);
    return u->rx_on && ((en == 4 && u->nfifo < RXFIFO) || (en == 8 && u->rx_buf));
}

static void uart_put(void *ctx, int c)
{
    struct nuart *u = (struct nuart *)ctx;
    struct esk_mach *s = u->x.s;
    u32 sh;
    if (enabled(u) == 4) {
        if (u->nfifo >= RXFIFO)
            return;
        u->fifo[u->nfifo++] = (u8)c;
        if (u->nfifo == 1) {
            esk_nrfp_hw(&u->x, 0x518, (u32)c & 0xffu);
            esk_nrfp_event(&u->x, 0x108);
        }
        return;
    }
    if (!u->rx_buf)
        return;
    (void)esk_bus_debug_write(&s->bus, u->rx_ptr + u->rx_n++, 1, (u32)c & 0xffu);
    esk_nrfp_event(&u->x, 0x108); /* RXDRDY */
    if (u->rx_n == u->rx_max) {
        esk_nrfp_hw(&u->x, 0x53c, u->rx_n);
        u->rx_buf = 0;
        esk_nrfp_event(&u->x, 0x110); /* ENDRX */
        sh = esk_nrfp_rd(&u->x, 0x200);
        if (sh & (1u << 5))
            uart_task(u, 0x000); /* ENDRX_STARTRX */
        if (sh & (1u << 6))
            uart_task(u, 0x004); /* ENDRX_STOPRX */
    }
}

static u32 uart_bit_cycles(void *ctx) { return bit_cycles((const struct nuart *)ctx); }

void *esk_nrf_uart_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct nuart *u;
    if (strcmp(p->name, "UARTE0") && strcmp(p->name, "UARTE1"))
        return 0;
    u = (struct nuart *)esk_mach_alloc(s, sizeof *u);
    if (!u)
        return 0;
    memset(u, 0, sizeof *u);
    if (esk_nrfp_init(&u->x, s, p))
        return 0;
    esk_mach_rx_port(s, p->name, uart_room, uart_put, uart_bit_cycles, u);
    return u;
}
