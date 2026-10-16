/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* gpio.c -- a PIC32MX's I/O ports A to G (Family Reference Manual section
 * 12), over the register file's GPIO peripheral: TRISx, PORTx, LATx, ODCx,
 * and the change notices (CNCON, CNEN, CNPUE). Port x pin n is the
 * runtime's pin Rxn.
 *
 *   - Out of reset every pin is an input (TRISx all ones). Port B's pins
 *     are analog while their AD1PCFG bit (the ADC's register) is 0, as out
 *     of reset: their digital input reads 0.
 *   - An output (TRISx 0) drives what LATx holds; with ODCx it is open
 *     drain and a 1 lets the pin go. A peripheral that takes the pin
 *     overrides it: a UART's TX drives high between its frames (the frames
 *     on the line come from the MCU runtime), its RX is an input.
 *   - PORTx reads the pins: an output what it drives, an input what the
 *     board drives, else its pull-up (CNPUE, by the pin's change-notice
 *     number CNn) as 1, else 0. A write to PORTx writes LATx.
 *   - With CNCON.ON, a pin whose CNEN bit is set flags CHANGE_NOTICE when
 *     its level changes, either way (the part compares with the last read
 *     of PORTx: here, with the level last seen).
 * Not modelled: the pull-ups' and the outputs' analog behaviour beyond the
 * runtime's pin model, the slew rate, the CN pins' change during sleep. */
#include <stdio.h>
#include <string.h>

#include "mcu/pic32/pic32.h"

#define NPORTS 7 /* A to G */

struct port {
    struct esk_rf_reg *tris, *port, *lat, *odc;
    u32 pins;  /* the pins the port has */
    u32 level; /* the pins' levels, as last seen */
    esk_mach_pin out[16];
};

struct gpio {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct port pt[NPORTS];
    struct esk_rf_reg *ad1pcfg, *cncon, *cnen, *cnpue;
    signed char cn[NPORTS][16]; /* each pin's CN number, or -1 */
    int cn_irq;
    int known, busy;
};

static struct gpio *gpio_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "GPIO") : 0;
    return p && p->ops == &esk_pic32_gpio_ops ? (struct gpio *)p->ctx : 0;
}

/* CNPUE's or CNEN's bits on port i, by the pins' CN numbers */
static u32 cn_bits(const struct gpio *g, const struct esk_rf_reg *r, int i)
{
    u32 v = 0;
    int b;
    for (b = 0; r && b < 16; b++)
        if (g->cn[i][b] >= 0 && (r->value >> g->cn[i][b] & 1u))
            v |= 1u << b;
    return v;
}

static u32 analog(const struct gpio *g, int i)
{
    return i == 1 && g->ad1pcfg ? ~(u32)g->ad1pcfg->value & 0xffffu : 0;
}

/* each pin's configuration, told to the runtime when it changes, and its
 * level */
static void update(struct gpio *g)
{
    struct esk_mach *s = g->s;
    int i, b, cn_on = g->cncon && (g->cncon->value >> 15 & 1u), flag = 0;
    if (g->busy)
        return;
    g->busy = 1;
    for (i = 0; i < NPORTS; i++) {
        struct port *q = &g->pt[i];
        u32 tris, lat, od, an, up, lv = 0, was = q->level;
        if (!q->port)
            continue;
        tris = (u32)q->tris->value;
        lat = (u32)q->lat->value;
        od = q->odc ? (u32)q->odc->value : 0;
        an = analog(g, i);
        up = cn_bits(g, g->cnpue, i);
        for (b = 0; b < 16; b++) {
            u32 m = 1u << b;
            int role = esk_pic32_uart_role(s, i, b), level = 0, ext = 0;
            esk_mach_pin o;
            if (!(q->pins & m))
                continue;
            memset(&o, 0, sizeof o);
            o.drive = -1;
            if (role == 1) {
                o.drive = 1; /* a UART's TX, idle high */
                o.af = 1;
            } else if (role == 2) {
                o.af = 1;
            } else if (!(tris & m)) {
                o.open_drain = (od & m) != 0;
                if (!((od & m) && (lat & m)))
                    o.drive = (lat & m) != 0;
            }
            o.analog = (an & m) != 0 && role == 0 && (tris & m);
            if (o.drive < 0 && (up & m))
                o.pull = 1;
            if (o.drive >= 0)
                level = o.drive;
            else if (s->host.pin_in && s->host.pin_in(s->host.ctx, i, b, &ext))
                level = ext != 0;
            else
                level = (up & m) != 0;
            if (!(an & m) && level)
                lv |= m;
            if (s->host.pin_cfg && (!g->known || memcmp(&q->out[b], &o, sizeof o) != 0))
                s->host.pin_cfg(s->host.ctx, i, b, &o);
            q->out[b] = o;
        }
        q->level = lv;
        if (lv != (u32)(q->port->value & q->pins))
            esk_rf_hw(q->port, (q->port->value & ~(u64)q->pins) | lv);
        if (g->known && cn_on && ((lv ^ was) & cn_bits(g, g->cnen, i)))
            flag = 1;
    }
    g->known = 1;
    g->busy = 0;
    if (flag && g->cn_irq >= 0)
        esk_pic32_irq(s, g->cn_irq);
}

void esk_pic32_pins_changed(struct esk_mach *s)
{
    struct gpio *g = gpio_of(s);
    if (g)
        update(g);
}

static int port_of(const struct gpio *g, u32 off)
{
    int i;
    for (i = 0; i < NPORTS; i++)
        if (g->pt[i].port && off == esk_rf_off(g->pt[i].port))
            return i;
    return -1;
}

static u32 gpio_read(void *ctx, u32 off, int n)
{
    struct gpio *g = (struct gpio *)ctx;
    if (!g->s->bus.debug && port_of(g, off) >= 0)
        update(g);
    return esk_rf_read(g->p, off, n);
}

static void gpio_write(void *ctx, u32 off, int n, u32 v)
{
    struct gpio *g = (struct gpio *)ctx;
    int i = port_of(g, off);
    if (i >= 0)
        esk_rf_write(g->p, esk_rf_off(g->pt[i].lat), n, v); /* PORTx written is LATx */
    else
        esk_rf_write(g->p, off, n, v);
    update(g);
}

static void gpio_reset(void *ctx)
{
    struct gpio *g = (struct gpio *)ctx;
    g->known = 0;
    update(g);
}

const struct esk_dev_ops esk_pic32_gpio_ops = {
    "pic32-gpio", gpio_read, gpio_write, gpio_reset, 0, 0,
};

/* the ADC's AD1PCFG changed port B's analog pins */
static void kick(struct esk_mach *s) { esk_pic32_pins_changed(s); }

void *esk_pic32_gpio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct gpio *g;
    struct esk_rf_periph *adc;
    char nm[16];
    int i, k, any = 0;
    g = (struct gpio *)esk_mach_alloc(s, sizeof *g);
    if (!g)
        return 0;
    memset(g, 0, sizeof *g);
    g->s = s;
    g->p = p;
    for (i = 0; i < NPORTS; i++) {
        struct port *q = &g->pt[i];
        char l = (char)('A' + i);
        snprintf(nm, sizeof nm, "TRIS%c", l);
        q->tris = esk_rf_reg(p, nm);
        snprintf(nm, sizeof nm, "PORT%c", l);
        q->port = esk_rf_reg(p, nm);
        snprintf(nm, sizeof nm, "LAT%c", l);
        q->lat = esk_rf_reg(p, nm);
        snprintf(nm, sizeof nm, "ODC%c", l);
        q->odc = esk_rf_reg(p, nm);
        if (!q->port || !q->tris || !q->lat) {
            q->port = 0;
            continue;
        }
        any = 1;
        q->pins = (u32)esk_rf_fields(q->port) & 0xffffu;
        esk_rf_set_reset(q->tris, esk_rf_fields(q->tris));
        esk_rf_set_acc(q->port, q->pins, ESK_ACC_RO); /* the pins; a write goes to LATx */
    }
    if (!any)
        return 0;
    adc = s->rf ? esk_rf_periph(s->rf, "ADC10") : 0;
    g->ad1pcfg = adc ? esk_rf_reg(adc, "AD1PCFG") : 0;
    g->cncon = esk_rf_reg(p, "CNCON");
    g->cnen = esk_rf_reg(p, "CNEN");
    g->cnpue = esk_rf_reg(p, "CNPUE");
    memset(g->cn, -1, sizeof g->cn);
    for (k = 0; k < 32; k++) {
        int pin;
        snprintf(nm, sizeof nm, "CN%d", k);
        pin = esk_rf_pin(s->rf, nm);
        if (pin >= 0 && pin / 32 < NPORTS && pin % 32 < 16)
            g->cn[pin / 32][pin % 32] = (signed char)k;
    }
    g->cn_irq = esk_rf_irq_named(s->rf, "CHANGE_NOTICE");
    s->pins_changed = kick;
    s->uart_pin = esk_pic32_uart_pin;
    return g;
}
