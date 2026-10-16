/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* gpio.c -- a PIC24FJ's I/O ports A to G (the data sheet's I/O Ports
 * section), over the register file's GPIO peripheral: TRISx, PORTx, LATx,
 * ODCx, ANSx, the pulls (IOCPUx, IOCPDx) and the interrupt-on-change
 * (IOCPx, IOCNx, IOCFx, with PADCON and IOCSTAT); and the Peripheral Pin
 * Select's view of the pins. Port x pin n is the runtime's pin Rxn.
 *
 *   - Out of reset every pin is an input (TRISx all ones) and every pin
 *     with an analog function is analog (ANSx's reset): its digital input
 *     reads 0.
 *   - An output (TRISx 0) drives what LATx holds; with ODCx it is open
 *     drain and a 1 lets the pin go. A peripheral the PPS gives the pin
 *     overrides it, as the data sheet gives the peripherals priority: a
 *     UART's TX drives high between its frames (the frames on the line
 *     come from the MCU runtime), its RX is an input.
 *   - PORTx reads the pins: an output what it drives, an input what the
 *     board drives, else its pull (IOCPUx up as 1, IOCPDx down as 0). A
 *     write to PORTx writes LATx.
 *   - The interrupt-on-change: with PADCON.IOCON set, a pin whose level
 *     rises (IOCPx) or falls (IOCNx) sets its IOCFx bit, its port's
 *     IOCSTAT bit and the IOC interrupt's flag; the program clears IOCFx
 *     bits (writing a 1 changes nothing), and IOCSTAT follows.
 *   - The PPS: the pins table's RPn and RPIn name each remappable pin's
 *     port pin; an RPORx field (RPnR) selects the output function an RPn
 *     pin carries, an RPINRx field (U2RXR) the RP pin an input comes from.
 * Not modelled: the pulls' and the outputs' analog behaviour beyond the
 * runtime's pin model, the slew rate.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/pic32-gpio.c (its
 * PIC24 part). Changes for EmbSIM: the pins are the session's nets, told
 * through the runtime's pin hooks; the PPS's outputs are a UART's. */
#include <stdio.h>
#include <string.h>

#include "mcu/pic24/pic24.h"

#define NPORTS 7 /* A to G */
#define NRP 64

struct port {
    struct esk_rf_reg *tris, *port, *lat, *odc, *ans, *iocp, *iocn, *iocf, *iocpu, *iocpd;
    u32 pins;  /* the pins the port has */
    u32 level; /* the pins' levels, as last seen */
    esk_mach_pin out[16];
};

struct gpio {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct port pt[NPORTS];
    struct esk_rf_reg *padcon, *iocstat;
    int ioc_irq;
    int rp_pin[NRP];                  /* RPn's port pin, or -1 */
    struct esk_rf_reg *rpor[NRP];     /* the RPORx holding RPnR */
    int rpor_lsb[NRP];
    struct esk_rf_reg *rpinr[64];
    int nrpinr;
    int known, busy;
};

static struct gpio *gpio_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "GPIO") : 0;
    return p && p->ops == &esk_pic24_gpio_ops ? (struct gpio *)p->ctx : 0;
}

static u32 val(const struct esk_rf_reg *r)
{
    return r ? (u32)r->value & 0xffffu : 0;
}

/* each pin's configuration, told to the runtime when it changes, its level,
 * and the interrupt-on-change */
static void update(struct gpio *g)
{
    struct esk_mach *s = g->s;
    int i, b, ioc_on = g->padcon && (g->padcon->value >> 15 & 1u), flag = 0;
    if (g->busy)
        return;
    g->busy = 1;
    for (i = 0; i < NPORTS; i++) {
        struct port *q = &g->pt[i];
        u32 tris, lat, od, an, up, dn, lv = 0, was = q->level, ch;
        if (!q->port)
            continue;
        tris = val(q->tris);
        lat = val(q->lat);
        od = val(q->odc);
        an = val(q->ans);
        up = val(q->iocpu);
        dn = val(q->iocpd);
        for (b = 0; b < 16; b++) {
            u32 m = 1u << b;
            int role = esk_pic24_uart_role(s, i, b), level = 0, ext = 0;
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
            if (o.drive < 0)
                o.pull = (up & m) ? 1 : (dn & m) ? 2 : 0;
            if (o.drive >= 0)
                level = o.drive;
            else if (s->host.pin_in && s->host.pin_in(s->host.ctx, i, b, &ext))
                level = ext != 0;
            else
                level = (up & m) != 0;
            if (!o.analog && level)
                lv |= m;
            if (s->host.pin_cfg && (!g->known || memcmp(&q->out[b], &o, sizeof o) != 0))
                s->host.pin_cfg(s->host.ctx, i, b, &o);
            q->out[b] = o;
        }
        q->level = lv;
        if (lv != (u32)(q->port->value & q->pins))
            esk_rf_hw(q->port, (q->port->value & ~(u64)q->pins) | lv);
        if (!g->known || !ioc_on || !q->iocf)
            continue;
        ch = (lv & ~was & val(q->iocp)) | (~lv & was & val(q->iocn));
        if (!ch)
            continue;
        esk_rf_hw(q->iocf, q->iocf->value | ch);
        flag = 1;
    }
    if (g->iocstat) {
        u64 st = 0;
        for (i = 0; i < NPORTS; i++)
            if (g->pt[i].iocf && val(g->pt[i].iocf))
                st |= (u64)1 << i;
        if (st != g->iocstat->value)
            esk_rf_hw(g->iocstat, st);
    }
    g->known = 1;
    g->busy = 0;
    if (flag && g->ioc_irq >= 0)
        esk_pic24_irq(s, g->ioc_irq);
}

void esk_pic24_pins_changed(struct esk_mach *s)
{
    struct gpio *g = gpio_of(s);
    if (g)
        update(g);
}

void esk_pic24_pps_changed(struct esk_mach *s)
{
    esk_pic24_uart_repin(s);
    esk_pic24_pins_changed(s);
}

int esk_pic24_rp_pin(struct esk_mach *s, int rp)
{
    struct gpio *g = gpio_of(s);
    return g && rp >= 0 && rp < NRP ? g->rp_pin[rp] : -1;
}

int esk_pic24_pps_out_rp(struct esk_mach *s, int fn)
{
    struct gpio *g = gpio_of(s);
    int rp;
    for (rp = 0; g && fn > 0 && rp < NRP; rp++)
        if (g->rpor[rp] && (int)(g->rpor[rp]->value >> g->rpor_lsb[rp] & 0x7fu) == fn)
            return rp;
    return -1;
}

int esk_pic24_pps_in_rp(struct esk_mach *s, const char *field)
{
    struct gpio *g = gpio_of(s);
    int k;
    for (k = 0; g && k < g->nrpinr; k++) {
        u64 m = esk_rf_mask(g->rpinr[k], field), v;
        if (!m)
            continue;
        v = g->rpinr[k]->value & m;
        while (!(m & 1u)) {
            m >>= 1;
            v >>= 1;
        }
        return v < NRP && v != m ? (int)v : -1; /* all ones: no pin */
    }
    return -1;
}

static int port_of(const struct gpio *g, u32 off)
{
    int i;
    for (i = 0; i < NPORTS; i++)
        if (g->pt[i].port && off == esk_rf_off(g->pt[i].port))
            return i;
    return -1;
}

static int iocf_of(const struct gpio *g, u32 off)
{
    int i;
    for (i = 0; i < NPORTS; i++)
        if (g->pt[i].iocf && off == esk_rf_off(g->pt[i].iocf))
            return i;
    return -1;
}

static u32 gpio_read(void *ctx, u32 off, int n)
{
    struct gpio *g = (struct gpio *)ctx;
    if (!g->s->bus.debug && port_of(g, off & ~1u) >= 0)
        update(g);
    return esk_rf_read(g->p, off, n);
}

static void gpio_write(void *ctx, u32 off, int n, u32 v)
{
    struct gpio *g = (struct gpio *)ctx;
    int i = port_of(g, off & ~1u), f = iocf_of(g, off & ~1u);
    if (i >= 0) {
        esk_rf_write(g->p, esk_rf_off(g->pt[i].lat) + (off & 1u), n, v); /* PORTx written is LATx */
    } else if (f >= 0 && !g->s->bus.debug) {
        /* IOCFx: the program clears bits, it cannot set them */
        u64 before = g->pt[f].iocf->value;
        esk_rf_write(g->p, off, n, v);
        esk_rf_hw(g->pt[f].iocf, g->pt[f].iocf->value & before);
    } else {
        esk_rf_write(g->p, off, n, v);
    }
    update(g);
}

static void gpio_reset(void *ctx)
{
    struct gpio *g = (struct gpio *)ctx;
    g->known = 0;
    update(g);
}

const struct esk_dev_ops esk_pic24_gpio_ops = {
    "pic24-gpio", gpio_read, gpio_write, gpio_reset, 0, 0,
};

static void kick(struct esk_mach *s) { esk_pic24_pins_changed(s); }

void *esk_pic24_gpio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct gpio *g;
    struct esk_rf_periph *pad, *pps;
    char nm[16];
    int i, k, any = 0;
    if (!esk_pic24_is(s->cpu))
        return 0;
    g = (struct gpio *)esk_mach_alloc(s, sizeof *g);
    if (!g)
        return 0;
    memset(g, 0, sizeof *g);
    g->s = s;
    g->p = p;
    for (i = 0; i < NPORTS; i++) {
        struct port *q = &g->pt[i];
        char l = (char)('A' + i);
#define REG(f, fmt)                                                                                \
    snprintf(nm, sizeof nm, fmt, l);                                                               \
    q->f = esk_rf_reg(p, nm)
        REG(tris, "TRIS%c");
        REG(port, "PORT%c");
        REG(lat, "LAT%c");
        REG(odc, "ODC%c");
        REG(ans, "ANS%c");
        REG(iocp, "IOCP%c");
        REG(iocn, "IOCN%c");
        REG(iocf, "IOCF%c");
        REG(iocpu, "IOCPU%c");
        REG(iocpd, "IOCPD%c");
#undef REG
        if (!q->port || !q->tris || !q->lat) {
            q->port = 0;
            continue;
        }
        any = 1;
        q->pins = (u32)esk_rf_fields(q->port) & 0xffffu;
        if (!q->pins)
            q->pins = (u32)q->tris->reset & 0xffffu;
        esk_rf_set_acc(q->port, q->pins, ESK_ACC_RO); /* the pins; a write goes to LATx */
    }
    if (!any)
        return 0;
    pad = s->rf ? esk_rf_periph(s->rf, "PAD") : 0;
    g->padcon = pad ? esk_rf_reg(pad, "PADCON") : 0;
    g->iocstat = pad ? esk_rf_reg(pad, "IOCSTAT") : 0;
    g->ioc_irq = esk_rf_irq_named(s->rf, "IOC");
    for (k = 0; k < NRP; k++) {
        snprintf(nm, sizeof nm, k < 32 ? "RP%d" : "RPI%d", k);
        g->rp_pin[k] = esk_rf_pin(s->rf, nm);
    }
    pps = s->rf ? esk_rf_periph(s->rf, "PPS") : 0;
    for (k = 0; pps && k < pps->nreg; k++) {
        struct esk_rf_reg *r = pps->reg[k];
        int f;
        if (!strncmp(r->name, "RPINR", 5) && g->nrpinr < 64)
            g->rpinr[g->nrpinr++] = r;
        if (strncmp(r->name, "RPOR", 4))
            continue;
        for (f = 0; f < r->nf; f++) {
            int rp = -1;
            if (r->f[f].name && sscanf(r->f[f].name, "RP%dR", &rp) == 1 && rp >= 0 && rp < NRP) {
                g->rpor[rp] = r;
                g->rpor_lsb[rp] = r->f[f].lsb;
            }
        }
    }
    s->pins_changed = kick;
    s->uart_pin = esk_pic24_uart_pin;
    return g;
}
