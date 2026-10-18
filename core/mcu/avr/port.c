/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* port.c -- an ATmega328P I/O port (PORTB, PORTC, PORTD: datasheet 18),
 * over the register file.
 *
 *   - DDxn 1 makes the pin an output driving PORTxn; DDxn 0 an input, with
 *     the internal pull-up while PORTxn is 1 and MCUCR.PUD is 0;
 *   - PINxn reads the pin: an output's own level, an input's level from the
 *     circuit, else the pull-up's 1, else 0 (a floating input: EmbSIM's
 *     choice, as for the STM32's ports); a one written to PINxn toggles
 *     PORTxn;
 *   - a peripheral can take a pin over: USART0's transmitter drives PD1 and
 *     its receiver makes PD0 an input whatever DDRD says, the SPI master
 *     makes MISO (PB4) an input, the TWI pulls SDA (PC4) and SCL (PC5) low
 *     or lets them go; a timer's output compare unit, or the SPI master's
 *     MOSI (PB3) and SCK (PB5), drives its pin while the pin's DDxn is 1;
 *   - each pin's configuration as it changes goes to the MCU runtime
 *     (esk_mach_host.pin_cfg), what the circuit drives comes back
 *     (pin_in), and PINx's changes go to the external interrupts.
 *
 * Not modelled: the input synchroniser's latency (a change is seen by the
 * next instruction), the digital input disable registers (DIDR0 and DIDR1
 * leave PINx reading the level). */
#include <string.h>

#include "mcu/avr/avr.h"

#define FORCE_NONE (-1)
#define FORCE_INPUT 2

struct avr_port {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *pin, *ddr, *port;
    int index;  /* 1 for B: the runtime numbers ports from A */
    int npins;
    int force[8]; /* a peripheral's: -1 none, 0 or 1 driven, 2 an input */
    int oc[8];    /* an output compare unit's level, -1 none */
    esk_mach_pin out[8];
    int known;
};

static struct avr_port *port_of(struct esk_mach *m, int index)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_avr_port_ops && ((struct avr_port *)p->ctx)->index == index)
            return (struct avr_port *)p->ctx;
    }
    return NULL;
}

static void update(struct avr_port *g)
{
    struct esk_mach *s = g->s;
    u32 ddr = (u32)g->ddr->value, port = (u32)g->port->value, old = (u32)g->pin->value, pin = 0;
    int pud = esk_avr_pud(s), i;
    for (i = 0; i < g->npins; i++) {
        esk_mach_pin o;
        int lvl, ext = 0, out_dir = (int)(ddr >> i & 1);
        memset(&o, 0, sizeof o);
        o.drive = -1;
        if (g->force[i] == 0 || g->force[i] == 1) {
            o.drive = g->force[i];
            o.af = 1;
        } else if (g->force[i] == FORCE_INPUT) {
            if ((port >> i & 1) && !pud)
                o.pull = 1;
        } else if (out_dir && g->oc[i] >= 0) {
            o.drive = g->oc[i];
            o.af = 1;
        } else if (out_dir) {
            o.drive = (int)(port >> i & 1);
        } else if ((port >> i & 1) && !pud) {
            o.pull = 1;
        }
        if (o.drive >= 0)
            lvl = o.drive;
        else if (s->host.pin_in && s->host.pin_in(s->host.ctx, g->index, i, &ext))
            lvl = ext != 0;
        else
            lvl = o.pull == 1;
        pin |= (u32)lvl << i;
        if (s->host.pin_cfg && (!g->known || memcmp(&g->out[i], &o, sizeof o) != 0))
            s->host.pin_cfg(s->host.ctx, g->index, i, &o);
        g->out[i] = o;
    }
    g->known = 1;
    if (pin != old) {
        esk_rf_hw(g->pin, pin);
        for (i = 0; i < g->npins; i++)
            if ((old ^ pin) >> i & 1)
                esk_avr_exint_pin(s, g->index, i, (int)(pin >> i & 1));
    }
}

static u32 port_read(void *ctx, u32 off, int n)
{
    struct avr_port *g = (struct avr_port *)ctx;
    if (!g->s->bus.debug)
        update(g);
    return esk_rf_read(g->p, off, n);
}

static void port_write(void *ctx, u32 off, int n, u32 v)
{
    struct avr_port *g = (struct avr_port *)ctx;
    if (off == esk_rf_off(g->pin)) {
        /* a one toggles PORTxn; PINx itself is the pins */
        esk_rf_hw(g->port, (g->port->value ^ (v & 0xffu)) & (((u64)1 << g->npins) - 1u));
    } else {
        esk_rf_write(g->p, off, n, v);
    }
    update(g);
}

static void port_reset(void *ctx)
{
    struct avr_port *g = (struct avr_port *)ctx;
    int i;
    for (i = 0; i < 8; i++) {
        g->force[i] = FORCE_NONE;
        g->oc[i] = -1;
    }
    g->known = 0;
    update(g);
}

const struct esk_dev_ops esk_avr_port_ops = {
    "avr-port", port_read, port_write, port_reset, 0, 0,
};

void esk_avr_pins_changed(struct esk_mach *m)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_avr_port_ops)
            update((struct avr_port *)p->ctx);
    }
    esk_avr_ac_pins(m);
}

int esk_avr_pin_level(struct esk_mach *m, int port, int bit)
{
    struct avr_port *g = port_of(m, port);
    return g && bit < g->npins ? (int)(g->pin->value >> bit & 1) : 0;
}

int esk_avr_pin_read(struct esk_mach *m, int port, int bit)
{
    struct avr_port *g = port_of(m, port);
    if (!g || bit < 0 || bit >= g->npins)
        return 0;
    update(g);
    return (int)(g->pin->value >> bit & 1);
}

void esk_avr_pin_override(struct esk_mach *m, int port, int bit, int level)
{
    struct avr_port *g = port_of(m, port);
    if (!g || bit < 0 || bit >= g->npins || g->oc[bit] == level)
        return;
    g->oc[bit] = level;
    update(g);
}

/* a peripheral's hold on a pin: the USART's on PD1 and PD0 (usart.c), the
 * SPI's on MISO (spi.c), the TWI's on SDA and SCL (twi.c) */
void esk_avr_pin_force(struct esk_mach *m, int port, int bit, int how)
{
    struct avr_port *g = port_of(m, port);
    if (!g || bit < 0 || bit >= g->npins || g->force[bit] == how)
        return;
    g->force[bit] = how;
    update(g);
}

void *esk_avr_port_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct avr_port g, *c;
    char want[8];
    int i;
    memset(&g, 0, sizeof g);
    if (strncmp(p->name, "PORT", 4) || p->name[4] < 'A' || p->name[4] > 'L' || p->name[5])
        return 0;
    g.s = s;
    g.p = p;
    want[0] = 'P';
    want[1] = 'I';
    want[2] = 'N';
    want[3] = p->name[4];
    want[4] = 0;
    g.pin = esk_rf_reg(p, want);
    want[0] = 'D';
    want[1] = 'D';
    want[2] = 'R';
    g.ddr = esk_rf_reg(p, want);
    g.port = esk_rf_reg(p, p->name);
    if (!g.pin || !g.ddr || !g.port)
        return 0;
    g.index = p->name[4] - 'A';
    g.npins = g.pin->nf > 1 ? g.pin->nf : 8;
    for (i = 0; i < 8; i++) {
        g.force[i] = FORCE_NONE;
        g.oc[i] = -1;
    }
    s->pins_changed = esk_avr_pins_changed;
    s->uart_pin = esk_avr_usart_pin;
    c = (struct avr_port *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = g;
    return c;
}
