/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* gpio.c -- the nRF52840's GPIO ports P0 (32 pins) and P1 (16), over the
 * register file (Product Specification 6.8): pin n of port p is the
 * runtime's pin GPIO(32p + n).
 *
 *   - OUT, OUTSET, OUTCLR; DIR, DIRSET, DIRCLR, and PIN_CNF[n]'s DIR, the
 *     same bits. An output drives what OUT holds -- unless DRIVE lets that
 *     level go (D0S1, D0H1 for 0: open drain; S0D1, H0D1 for 1);
 *   - a peripheral that takes the pin overrides it: a UART's TXD drives
 *     high between its frames (the frames on the line come from the MCU
 *     runtime), its RXD is an input; a GPIOTE channel in task mode drives
 *     its pin; SPIM's SCK, MOSI (and SPIM3's CSN) and PWM's outputs drive
 *     theirs, SPIM's MISO is an input; TWIM's SCL and SDA pull low or let
 *     go (PIN_CNF's pull then applies);
 *   - IN is each pin with its input buffer connected (PIN_CNF.INPUT 0, or a
 *     peripheral's input): an output what it drives, else what the board
 *     drives, else the pull (PULL up 1, down 0), else 0; a disconnected
 *     input reads 0;
 *   - SENSE (high or low) makes the pin's DETECT condition: LATCH latches
 *     each pin's (a one written clears it), and the port's DETECT signal is
 *     any pin's condition, or with DETECTMODE LDETECT any LATCH bit -- the
 *     signal GPIOTE's PORT event comes from (gpiote.c). */
#include <string.h>

#include "mcu/nrf52/nrf52.h"

struct ngpio {
    struct nrfp x;
    int port, npins;
    esk_mach_pin out[32];
    int known;
    u32 level, detect_bits;
    int busy; /* P0's: the ports are being worked out */
};

static struct ngpio *port_of(struct esk_mach *s, int port)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, port ? "P1" : "P0") : 0;
    return p && p->ops == &esk_nrf_gpio_ops ? (struct ngpio *)p->ctx : 0;
}

int esk_nrf_psel(u32 v)
{
    if (v >> 31 & 1)
        return -1; /* CONNECT: disconnected */
    return (int)((v >> 5 & 1) * 32u + (v & 31u));
}

/* DIR's bits are PIN_CNF's DIR bits */
static void gpio_dir(struct ngpio *g, u32 dir)
{
    int i;
    for (i = 0; i < g->npins; i++) {
        struct esk_rf_reg *r = esk_nrfp_reg(&g->x, 0x700u + 4u * (u32)i);
        if (r && (u32)(r->value & 1u) != (dir >> i & 1u))
            esk_rf_hw(r, (r->value & ~(u64)1) | (dir >> i & 1u));
    }
}

static void gpio_update(struct ngpio *g, const signed char *role, const signed char *te_drive, const u8 *te_watch,
                        const signed char *pd, const u8 *pod)
{
    struct esk_mach *s = g->x.s;
    u32 out = esk_nrfp_rd(&g->x, 0x504), in = 0, dir = 0, sense = 0;
    int i;
    for (i = 0; i < g->npins; i++) {
        struct esk_rf_reg *r = esk_nrfp_reg(&g->x, 0x700u + 4u * (u32)i);
        u32 c = r ? (u32)r->value : 0, drive = c >> 8 & 7u, pull = c >> 2 & 3u, sn = c >> 16 & 3u;
        int pin = 32 * g->port + i, uart = role[pin], te = te_drive[pin];
        int connected = !(c & 2u) || uart == 2 || te_watch[pin] || pd[pin] >= 0, lvl, ext = 0;
        esk_mach_pin o;
        memset(&o, 0, sizeof o);
        o.drive = -1;
        dir |= (c & 1u) << i;
        if (uart == 1) {
            o.drive = 1; /* TXD, idle high */
            o.af = 1;
        } else if (uart == 2) {
            o.af = 1;
        } else if (te >= 0) {
            o.drive = te;
            o.af = 1;
        } else if (pd[pin] >= 0) {
            o.af = 1;
            if (pd[pin] < 2) {
                o.drive = pd[pin];
                o.open_drain = pod[pin];
            }
        } else if (c & 1u) {
            int v = (int)(out >> i & 1u);
            int let_go = v ? (drive == 6 || drive == 7) : (drive == 4 || drive == 5);
            if (!let_go)
                o.drive = v;
            o.open_drain = drive == 4 || drive == 5;
        }
        if (o.drive < 0)
            o.pull = pull == 3 ? 1 : pull == 1 ? 2 : 0;
        if (o.drive >= 0)
            lvl = o.drive;
        else if (s->host.pin_in && s->host.pin_in(s->host.ctx, pin / 16, pin % 16, &ext))
            lvl = ext != 0;
        else
            lvl = pull == 3 ? 1 : 0;
        if (connected)
            in |= (u32)lvl << i;
        if ((sn == 2 && lvl) || (sn == 3 && !lvl))
            sense |= 1u << i;
        if (s->host.pin_cfg && (!g->known || memcmp(&g->out[i], &o, sizeof o) != 0))
            s->host.pin_cfg(s->host.ctx, pin / 16, pin % 16, &o);
        g->out[i] = o;
    }
    g->known = 1;
    g->level = in;
    esk_nrfp_hw(&g->x, 0x510, in);
    esk_nrfp_hw(&g->x, 0x514, dir);
    esk_nrfp_hw(&g->x, 0x518, dir);
    esk_nrfp_hw(&g->x, 0x51c, dir);
    esk_nrfp_hw(&g->x, 0x508, out);
    esk_nrfp_hw(&g->x, 0x50c, out);
    esk_nrfp_hw(&g->x, 0x520, esk_nrfp_rd(&g->x, 0x520) | sense); /* LATCH */
    g->detect_bits = (esk_nrfp_rd(&g->x, 0x524) & 1u) ? esk_nrfp_rd(&g->x, 0x520) : sense;
}

void esk_nrf_pins_changed(struct esk_mach *s)
{
    struct ngpio *p0 = port_of(s, 0);
    u32 levels[2] = { 0, 0 };
    signed char role[NRF_PINS], te_drive[NRF_PINS], pd[NRF_PINS];
    u8 te_watch[NRF_PINS], pod[NRF_PINS];
    int k, detect = 0;
    if (!p0 || p0->busy)
        return;
    p0->busy = 1;
    esk_nrf_uart_roles(s, role);
    esk_nrf_gpiote_roles(s, te_drive, te_watch);
    memset(pd, -1, sizeof pd);
    memset(pod, 0, sizeof pod);
    esk_nrf_spim_roles(s, pd, pod);
    esk_nrf_pwm_roles(s, pd);
    for (k = 0; k < 2; k++) {
        struct ngpio *g = port_of(s, k);
        if (!g)
            continue;
        gpio_update(g, role, te_drive, te_watch, pd, pod);
        levels[k] = g->level;
        detect |= g->detect_bits != 0;
    }
    esk_nrf_gpiote_levels(s, levels, detect);
    p0->busy = 0;
}

int esk_nrf_pin_level(struct esk_mach *s, int pin)
{
    struct ngpio *g = port_of(s, pin / 32);
    int ext = 0;
    if (!g || pin % 32 >= g->npins)
        return 0;
    if (g->out[pin % 32].drive >= 0)
        return g->out[pin % 32].drive;
    if (s->host.pin_in && s->host.pin_in(s->host.ctx, pin / 16, pin % 16, &ext))
        return ext != 0;
    return g->out[pin % 32].pull == 1;
}

static u32 gpio_read(void *ctx, u32 off, int n)
{
    struct ngpio *g = (struct ngpio *)ctx;
    if (!g->x.s->bus.debug)
        esk_nrf_pins_changed(g->x.s);
    return esk_rf_read(g->x.p, off, n);
}

static void gpio_write(void *ctx, u32 off, int n, u32 v)
{
    struct ngpio *g = (struct ngpio *)ctx;
    u32 out = esk_nrfp_rd(&g->x, 0x504), dir = esk_nrfp_rd(&g->x, 0x514);
    esk_rf_write(g->x.p, off, n, v);
    switch (off) {
    case 0x504: out = v; break;
    case 0x508: out |= v; break;
    case 0x50c: out &= ~v; break;
    case 0x514: gpio_dir(g, v); break;
    case 0x518: gpio_dir(g, dir | v); break;
    case 0x51c: gpio_dir(g, dir & ~v); break;
    default: break;
    }
    esk_nrfp_hw(&g->x, 0x504, out);
    esk_nrf_pins_changed(g->x.s);
}

static void gpio_reset(void *ctx)
{
    struct ngpio *g = (struct ngpio *)ctx;
    g->known = 0;
    g->level = g->detect_bits = 0;
    esk_nrf_pins_changed(g->x.s);
}

const struct esk_dev_ops esk_nrf_gpio_ops = {
    "nrf-gpio", gpio_read, gpio_write, gpio_reset, 0, 0,
};

void *esk_nrf_gpio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct ngpio *g;
    if (strcmp(p->name, "P0") && strcmp(p->name, "P1"))
        return 0;
    g = (struct ngpio *)esk_mach_alloc(s, sizeof *g);
    if (!g)
        return 0;
    memset(g, 0, sizeof *g);
    if (esk_nrfp_init(&g->x, s, p))
        return 0;
    g->port = p->name[1] - '0';
    g->npins = g->port ? 16 : 32;
    s->pins_changed = esk_nrf_pins_changed;
    s->uart_pin = esk_nrf_uart_pin;
    return g;
}
