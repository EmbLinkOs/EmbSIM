/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* usart.c -- the ATmega328P's USART0 in asynchronous mode (datasheet 24),
 * over the register file, at its baud rate.
 *
 *   - a bit is 16 (U2X0: 8) times UBRR0 + 1 core cycles; a frame is a start
 *     bit, 5 to 9 data bits (UCSZ0), a parity bit when UPM0 asks for one,
 *     and 1 or 2 stop bits (USBS0);
 *   - a byte written to UDR0 with TXEN0 set moves to the shift register if
 *     it is empty (UDRE0 stays set) or waits in the buffer (UDRE0 clears)
 *     until the frame ends; when the shift register empties with nothing
 *     waiting, TXC0 sets. Each frame goes to the MCU runtime
 *     (esk_mach_host.uart_tx) with its bit time, which writes the serial
 *     text and drives the TX pin's waveform;
 *   - TXEN0 takes PD1 over as the transmitter's output (idle high) and
 *     RXEN0 takes PD0 as its input, whatever DDRD says;
 *   - the receiver takes a byte from the runtime's serial input (or the RX
 *     pin's waveform) while RXEN0 is set and UDR0 holds none: RXC0 sets, a
 *     read of UDR0 takes the byte and clears it;
 *   - the vectors: receive complete (RXC0 with RXCIE0), data register
 *     empty (UDRE0 with UDRIE0, as long as it is empty) and transmit
 *     complete (TXC0 with TXCIE0; entering it clears TXC0).
 *
 * Not modelled: synchronous and master SPI modes (UMSEL0), the error flags
 * (a byte waits for room, so DOR0 never sets), the receive FIFO's second
 * byte, multi-processor mode.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/avr-usart.c, which
 * sent a byte in no time; the frame timing and the pins are EmbSIM's. */
#include <string.h>

#include "mcu/avr/avr.h"

#define RXC 0x80u
#define TXC 0x40u
#define UDRE 0x20u
#define U2X 0x02u
#define RXCIE 0x80u
#define TXCIE 0x40u
#define UDRIE 0x20u
#define RXEN 0x10u
#define TXEN 0x08u
#define UCSZ2 0x04u

struct avr_usart {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *a, *b, *c, *brrl, *brrh, *udr;
    int vec_rx, vec_udre, vec_tx;
    int running;
    u8 rx;
    int held;
    u8 hold;
    u64 left;  /* core cycles until the frame in the shift register ends */
    u64 since; /* the cycle it started: an instruction's own cycles before it do not count */
    int tx_on, rx_on;
};

static u32 bit_cycles(const struct avr_usart *u)
{
    u32 ubrr = (u32)((u->brrh->value & 0x0f) << 8 | (u->brrl->value & 0xff));
    return (u->a->value & U2X ? 8u : 16u) * (ubrr + 1u);
}

static u64 frame_cycles(const struct avr_usart *u)
{
    u32 c = (u32)u->c->value;
    u32 data = 5u + ((c >> 1) & 3u) + ((u->b->value & UCSZ2) ? 4u : 0u);
    u32 bits = 1u + (data > 9u ? 9u : data) + (((c >> 4) & 3u) ? 1u : 0u) + ((c & 0x08u) ? 2u : 1u);
    return (u64)bit_cycles(u) * bits;
}

static void start_frame(struct avr_usart *u, u8 v)
{
    struct esk_mach *s = u->s;
    u->left = frame_cycles(u);
    u->since = s->cycles;
    esk_rf_clock(u->p, &u->running, 1);
    if (s->host.uart_tx)
        s->host.uart_tx(s->host.ctx, "USART0", v, bit_cycles(u));
}

static void pins(struct avr_usart *u)
{
    int tx = (u->b->value & TXEN) != 0, rx = (u->b->value & RXEN) != 0;
    if (tx != u->tx_on) {
        u->tx_on = tx;
        esk_avr_pin_force(u->s, 3, 1, tx ? 1 : -1);
    }
    if (rx != u->rx_on) {
        u->rx_on = rx;
        esk_avr_pin_force(u->s, 3, 0, rx ? 2 : -1);
    }
}

static u32 us_bit_cycles(void *ctx) { return bit_cycles((const struct avr_usart *)ctx); }

static int us_room(void *ctx)
{
    struct avr_usart *u = (struct avr_usart *)ctx;
    return (u->b->value & RXEN) && !(u->a->value & RXC);
}

static void us_put(void *ctx, int c)
{
    struct avr_usart *u = (struct avr_usart *)ctx;
    u->rx = (u8)c;
    esk_rf_hw(u->a, u->a->value | RXC);
    esk_avr_irq_changed(u->s);
}

static u32 us_read(void *ctx, u32 off, int n)
{
    struct avr_usart *u = (struct avr_usart *)ctx;
    if (off == esk_rf_off(u->udr)) {
        u8 v = u->rx;
        if (!u->s->bus.debug && (u->a->value & RXC)) {
            esk_rf_hw(u->a, u->a->value & ~(u64)RXC);
            esk_avr_irq_changed(u->s);
        }
        return v;
    }
    return esk_rf_read(u->p, off, n);
}

static void us_write(void *ctx, u32 off, int n, u32 v)
{
    struct avr_usart *u = (struct avr_usart *)ctx;
    if (off == esk_rf_off(u->udr)) {
        if (u->b->value & TXEN) {
            if (!u->running) {
                start_frame(u, (u8)v);
            } else if (!u->held) {
                u->held = 1;
                u->hold = (u8)v;
                esk_rf_hw(u->a, u->a->value & ~(u64)UDRE);
            }
        }
    } else {
        esk_rf_write(u->p, off, n, v);
    }
    if (!(u->b->value & TXEN)) {
        /* the transmitter off: nothing waits */
        u->held = 0;
        esk_rf_hw(u->a, u->a->value | UDRE);
    }
    pins(u);
    esk_avr_irq_changed(u->s);
}

static void us_tick(void *ctx, u32 cycles)
{
    struct avr_usart *u = (struct avr_usart *)ctx;
    if (!u->running)
        return;
    cycles = esk_mach_after(u->s, cycles, u->since);
    if (u->left > cycles) {
        u->left -= cycles;
        return;
    }
    if (u->held) {
        u->held = 0;
        esk_rf_hw(u->a, u->a->value | UDRE);
        start_frame(u, u->hold);
    } else {
        u->left = 0;
        esk_rf_clock(u->p, &u->running, 0);
        esk_rf_hw(u->a, u->a->value | TXC);
    }
    esk_avr_irq_changed(u->s);
}

static int us_next(void *ctx, u32 *cycles)
{
    struct avr_usart *u = (struct avr_usart *)ctx;
    if (!u->running)
        return 0;
    *cycles = u->left > 0xffffffffu ? 0xffffffffu : (u32)u->left;
    return 1;
}

static void us_reset(void *ctx)
{
    struct avr_usart *u = (struct avr_usart *)ctx;
    u->held = 0;
    u->left = 0;
    u->running = 0;
    u->rx = 0;
    if (u->tx_on || u->rx_on) {
        u->tx_on = u->rx_on = 0;
        esk_avr_pin_force(u->s, 3, 1, -1);
        esk_avr_pin_force(u->s, 3, 0, -1);
    }
}

static int us_pending(void *ctx, int vec)
{
    struct avr_usart *u = (struct avr_usart *)ctx;
    u64 a = u->a->value, b = u->b->value;
    if (vec == u->vec_rx)
        return (a & RXC) && (b & RXCIE);
    if (vec == u->vec_udre)
        return (a & UDRE) && (b & UDRIE);
    if (vec == u->vec_tx)
        return (a & TXC) && (b & TXCIE);
    return 0;
}

static void us_ack(void *ctx, int vec)
{
    struct avr_usart *u = (struct avr_usart *)ctx;
    if (vec == u->vec_tx)
        esk_rf_hw(u->a, u->a->value & ~(u64)TXC);
}

const struct esk_dev_ops esk_avr_usart_ops = {
    "avr-usart", us_read, us_write, us_reset, us_tick, us_next,
};

int esk_avr_usart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit)
{
    ES_UNUSED(m);
    if (strcmp(unit, "USART0"))
        return -1;
    *port = 3;
    *bit = tx ? 1 : 0;
    return 0;
}

void *esk_avr_usart_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct avr_usart u, *c;
    memset(&u, 0, sizeof u);
    if (strcmp(p->name, "USART0"))
        return 0;
    u.s = s;
    u.p = p;
    u.a = esk_rf_reg(p, "UCSR0A");
    u.b = esk_rf_reg(p, "UCSR0B");
    u.c = esk_rf_reg(p, "UCSR0C");
    u.brrl = esk_rf_reg(p, "UBRR0L");
    u.brrh = esk_rf_reg(p, "UBRR0H");
    u.udr = esk_rf_reg(p, "UDR0");
    if (!u.a || !u.b || !u.c || !u.brrl || !u.brrh || !u.udr)
        return 0;
    u.vec_rx = esk_rf_irq(p, "USART_RX");
    u.vec_udre = esk_rf_irq(p, "USART_UDRE");
    u.vec_tx = esk_rf_irq(p, "USART_TX");
    c = (struct avr_usart *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = u;
    esk_avr_irq_source(s, c->vec_rx, us_pending, 0, c);
    esk_avr_irq_source(s, c->vec_udre, us_pending, 0, c);
    esk_avr_irq_source(s, c->vec_tx, us_pending, us_ack, c);
    esk_mach_rx_port(s, "USART0", us_room, us_put, us_bit_cycles, c);
    return c;
}
