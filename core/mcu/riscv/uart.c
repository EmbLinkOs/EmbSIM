/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* uart.c -- the FE310's UART (FE310-G002 manual, chapter 18), over the
 * register file, at its baud rate.
 *
 *   - txdata queues a byte in the 8-entry transmit FIFO (its bit 31 reads
 *     1 while the FIFO is full, and a write then is dropped); with txen
 *     the FIFO drains a frame at a time: a start bit, 8 data bits and 1 or
 *     2 stop bits (nstop), each div + 1 cycles of the bus clock, which on
 *     the FE310 is the core clock. Each frame goes to the MCU runtime with
 *     its bit time; the TX pin (UART0 GPIO17, UART1 GPIO18, IOF0) carries
 *     the waveform while its IOF is selected;
 *   - rxdata takes a byte from the 8-entry receive FIFO (bit 31 set when it
 *     is empty); bytes arrive from the session's serial input or the RX
 *     pin (UART0 GPIO16, UART1 GPIO23) while rxen is set;
 *   - ip's txwm is set while the transmit FIFO holds fewer than txcnt
 *     entries, rxwm while the receive FIFO holds more than rxcnt; with ie
 *     they raise the UART's PLIC source (UART0 3, UART1 4). */
#include <string.h>

#include "mcu/riscv/riscv.h"

#define FIFO 8

struct fe_uart {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *txdata, *rxdata, *txctrl, *rxctrl, *ie, *ip, *div;
    int unit, source;
    u8 tx[FIFO], rx[FIFO];
    int ntx, nrx;
    int running;
    u64 left, since;
    int src_level;
};

static u32 bit_cycles(const struct fe_uart *u) { return (u32)(u->div->value & 0xffffu) + 1u; }

static void irq(struct fe_uart *u)
{
    u32 txcnt = (u32)(u->txctrl->value >> 16) & 7, rxcnt = (u32)(u->rxctrl->value >> 16) & 7;
    u32 ip = ((u32)u->ntx < txcnt ? 1u : 0u) | ((u32)u->nrx > rxcnt ? 2u : 0u);
    int level = (ip & (u32)u->ie->value & 3u) != 0;
    esk_rf_hw(u->ip, ip);
    if (level != u->src_level) {
        u->src_level = level;
        esk_fe310_plic_source(u->s, u->source, level);
    }
}

static void start(struct fe_uart *u)
{
    struct esk_mach *s = u->s;
    u8 c = u->tx[0];
    u32 stop = (u->txctrl->value & 2) ? 2u : 1u;
    memmove(u->tx, u->tx + 1, (size_t)(FIFO - 1));
    u->ntx--;
    u->left = (u64)bit_cycles(u) * (1u + 8u + stop);
    u->since = s->cycles;
    esk_rf_clock(u->p, &u->running, 1);
    if (s->host.uart_tx)
        s->host.uart_tx(s->host.ctx, u->unit ? "UART1" : "UART0", c, bit_cycles(u));
}

static u32 ua_read(void *ctx, u32 off, int n)
{
    struct fe_uart *u = (struct fe_uart *)ctx;
    if (off == esk_rf_off(u->txdata))
        return u->ntx >= FIFO ? 0x80000000u : 0u;
    if (off == esk_rf_off(u->rxdata)) {
        u32 v;
        if (!u->nrx)
            return 0x80000000u;
        v = u->rx[0];
        if (!u->s->bus.debug) {
            memmove(u->rx, u->rx + 1, (size_t)(FIFO - 1));
            u->nrx--;
            irq(u);
        }
        return v;
    }
    if (off == esk_rf_off(u->ip))
        irq(u);
    return esk_rf_read(u->p, off, n);
}

static void ua_write(void *ctx, u32 off, int n, u32 v)
{
    struct fe_uart *u = (struct fe_uart *)ctx;
    if (off == esk_rf_off(u->txdata)) {
        if (u->ntx < FIFO)
            u->tx[u->ntx++] = (u8)v;
    } else if (off != esk_rf_off(u->rxdata)) {
        esk_rf_write(u->p, off, n, v);
    }
    if ((u->txctrl->value & 1) && u->ntx && !u->running)
        start(u);
    irq(u);
}

static void ua_tick(void *ctx, u32 cycles)
{
    struct fe_uart *u = (struct fe_uart *)ctx;
    if (!u->running)
        return;
    if (u->s->cycles - cycles < u->since)
        cycles = (u32)(u->s->cycles - u->since);
    if (u->left > cycles) {
        u->left -= cycles;
        return;
    }
    u->left = 0;
    esk_rf_clock(u->p, &u->running, 0);
    if ((u->txctrl->value & 1) && u->ntx)
        start(u);
    irq(u);
}

static int ua_next(void *ctx, u32 *cycles)
{
    struct fe_uart *u = (struct fe_uart *)ctx;
    if (!u->running)
        return 0;
    *cycles = u->left > 0xffffffffu ? 0xffffffffu : (u32)u->left;
    return 1;
}

static void ua_reset(void *ctx)
{
    struct fe_uart *u = (struct fe_uart *)ctx;
    u->ntx = u->nrx = 0;
    u->running = 0;
    u->left = 0;
    u->src_level = 0;
}

static int ua_room(void *ctx)
{
    struct fe_uart *u = (struct fe_uart *)ctx;
    return (u->rxctrl->value & 1) && u->nrx < FIFO;
}

static void ua_put(void *ctx, int c)
{
    struct fe_uart *u = (struct fe_uart *)ctx;
    if (u->nrx < FIFO)
        u->rx[u->nrx++] = (u8)c;
    irq(u);
}

static u32 ua_bit_cycles(void *ctx) { return bit_cycles((const struct fe_uart *)ctx); }

const struct esk_dev_ops esk_fe310_uart_ops = {
    "fe310-uart", ua_read, ua_write, ua_reset, ua_tick, ua_next,
};

int esk_fe310_uart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit)
{
    int pin;
    ES_UNUSED(m);
    if (!strcmp(unit, "UART0"))
        pin = tx ? 17 : 16;
    else if (!strcmp(unit, "UART1"))
        pin = tx ? 18 : 23;
    else
        return -1;
    *port = pin / 16;
    *bit = pin % 16;
    return 0;
}

void *esk_fe310_uart_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct fe_uart u, *c;
    memset(&u, 0, sizeof u);
    if (strcmp(p->name, "UART0") && strcmp(p->name, "UART1"))
        return 0;
    u.s = s;
    u.p = p;
    u.unit = p->name[4] - '0';
    u.source = u.unit ? 4 : 3;
    u.txdata = esk_rf_reg(p, "TXDATA");
    u.rxdata = esk_rf_reg(p, "RXDATA");
    u.txctrl = esk_rf_reg(p, "TXCTRL");
    u.rxctrl = esk_rf_reg(p, "RXCTRL");
    u.ie = esk_rf_reg(p, "IE");
    u.ip = esk_rf_reg(p, "IP");
    u.div = esk_rf_reg(p, "DIV");
    if (!u.txdata || !u.rxdata || !u.txctrl || !u.rxctrl || !u.ie || !u.ip || !u.div)
        return 0;
    c = (struct fe_uart *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = u;
    esk_mach_rx_port(s, p->name, ua_room, ua_put, ua_bit_cycles, c);
    return c;
}
