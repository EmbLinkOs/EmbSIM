/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* stm32f1-gpio.c -- an STM32F1 GPIO port (CRL, CRH, IDR, ODR, BSRR, BRR,
 * LCKR: RM0008's "General-purpose and alternate-function I/Os"), over
 * the register file.
 *
 *   - BSRR's low half sets ODR's bits and its high half resets them (set
 *     wins); BRR resets them;
 *   - IDR is what each pin reads, from its configuration (CNFy, MODEy):
 *       input, analog (CNF 00): 0, its Schmitt trigger off;
 *       input, floating (01, out of reset): what the board drives, else 0
 *         (floating, EmbSim's choice);
 *       input, pull-up/pull-down (10): what the board drives, else the
 *         pull ODR's bit picks (1 up, 0 down);
 *       general-purpose output, push-pull (00): ODR's bit; open-drain
 *         (01): 0 when ODR's bit is 0, else let go (as floating);
 *       alternate-function output (10, 11): a USART's TX pin, the line
 *         idle, reads 1 (USART1 PA9, or PB6 remapped; USART2 PA2; USART3
 *         PB10, or PC10 partly remapped); a timer's enabled compare
 *         channel's pin is its output (tim.c), an SPI master's SCK, MOSI
 *         and NSS and an I2C master's SCL and SDA are theirs (spi_f1.c,
 *         i2c_f1.c; open-drain for I2C, CNF 11); any other is let go (its
 *         peripheral has no model to drive it) and reads what the circuit
 *         drives, else 0;
 *   - the debug port's pins, while AFIO's MAPR.SWJ_CFG keeps them (out of
 *     reset all five), are its, whatever CRL and CRH say: PA13 (JTMS)
 *     and PA15 (JTDI) pulled up, PA14 (JTCK) pulled down, PB4 (JNTRST)
 *     pulled up, PB3 (JTDO) 0. SWJ_CFG 001 gives PB4 back, 010 PA15, PB3
 *     and PB4, 100 all five;
 *   - each pin's configuration as it changes goes to the MCU runtime
 *     (esk_mach_host.pin_cfg: driven 0 or 1, open-drain, the weak pull, or
 *     analog), which drives the pin's net; what the rest of the circuit
 *     drives comes back into IDR (pin_in), and IDR's changes go to EXTI;
 *   - an output's MODE (2, 10 or 50 MHz) is its speed: the description's
 *     output resistance and edge rate for it;
 *   - LCKR is a plain register: the lock sequence is not modelled.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/stm32f1-gpio.c. Changes:
 * pins are reported as configurations (with their pulls) rather than
 * levels, and IDR's edges feed EXTI. */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

struct f1gpio {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *crl, *crh, *idr, *odr, *bsrr, *brr;
    int port;               /* 0 for A */
    esk_mach_pin out[16];   /* what each pin was told to be */
    int known;
};

static u32 cfg(const struct f1gpio *g, int pin)
{
    const struct esk_rf_reg *r = pin < 8 ? g->crl : g->crh;
    return (u32)(r->value >> 4 * (pin & 7)) & 15; /* CNF:MODE */
}

/* -1, or the level the debug port gives a pin it holds */
static int debug_pin(const struct f1gpio *g, int pin, u32 mp)
{
    u32 swj = mp >> 24 & 7;
    if (g->port == 0 && pin >= 13 && pin <= 15) {
        if (swj == 4 || (swj == 2 && pin == 15))
            return -1;
        return pin == 14 ? 0 : 1;
    }
    if (g->port == 1 && (pin == 3 || pin == 4)) {
        if (swj == 4 || swj == 2 || (swj == 1 && pin == 4))
            return -1;
        return pin == 4 ? 1 : 0;
    }
    return -1;
}

int esk_stm32f1_usart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit)
{
    u32 mp = esk_stm32f1_mapr(m);
    int u1 = mp >> 2 & 1, u2 = mp >> 3 & 1, u3 = (int)(mp >> 4 & 3);
    if (!strcmp(unit, "USART1")) {
        *port = u1 ? 1 : 0;
        *bit = u1 ? (tx ? 6 : 7) : (tx ? 9 : 10);
    } else if (!strcmp(unit, "USART2")) {
        *port = u2 ? 3 : 0;
        *bit = u2 ? (tx ? 5 : 6) : (tx ? 2 : 3);
    } else if (!strcmp(unit, "USART3")) {
        *port = u3 == 0 ? 1 : u3 == 1 ? 2 : 3;
        *bit = u3 == 0 ? (tx ? 10 : 11) : u3 == 1 ? (tx ? 10 : 11) : (tx ? 8 : 9);
    } else
        return -1;
    return 0;
}

int esk_stm32f1_usart_fn_pin(struct esk_mach *m, const char *unit, const char *fn)
{
    /* RM0008 tables 54-56: CK, CTS, RTS as port * 16 + bit */
    static const int u1[3] = { 8, 11, 12 };
    static const int u2[2][3] = { { 4, 0, 1 }, { 3 * 16 + 7, 3 * 16 + 3, 3 * 16 + 4 } };
    static const int u3[3][3] = { { 16 + 12, 16 + 13, 16 + 14 }, { 32 + 12, 16 + 13, 16 + 14 },
                                  { 48 + 10, 48 + 11, 48 + 12 } };
    u32 mp = esk_stm32f1_mapr(m);
    int i = !strcmp(fn, "CK") ? 0 : !strcmp(fn, "CTS") ? 1 : !strcmp(fn, "RTS") ? 2 : -1, r;
    if (i < 0)
        return -1;
    if (!strcmp(unit, "USART1"))
        return u1[i];
    if (!strcmp(unit, "USART2"))
        return u2[mp >> 3 & 1][i];
    if (!strcmp(unit, "USART3")) {
        r = (int)(mp >> 4 & 3);
        return u3[r == 3 ? 2 : r == 1 ? 1 : 0][i];
    }
    return -1;
}

/* a USART's RTS or CK output on the pin, or -1 */
static int usart_fn_out(const struct f1gpio *g, int pin)
{
    static const char *const units[3] = { "USART1", "USART2", "USART3" };
    static const char *const fns[2] = { "RTS", "CK" };
    char sig[16];
    int i, k, v;
    for (i = 0; i < 3; i++)
        for (k = 0; k < 2; k++)
            if (esk_stm32f1_usart_fn_pin(g->s, units[i], fns[k]) == g->port * 16 + pin) {
                snprintf(sig, sizeof sig, "%s_%s", units[i], fns[k]);
                if ((v = esk_stm32_usart_signal(g->s, sig)) >= 0)
                    return v;
            }
    return -1;
}

/* nCTS's level from its pin */
static void usart_cts_in(const struct f1gpio *g, int pin, int level)
{
    static const char *const units[3] = { "USART1", "USART2", "USART3" };
    char sig[16];
    int i;
    for (i = 0; i < 3; i++)
        if (esk_stm32f1_usart_fn_pin(g->s, units[i], "CTS") == g->port * 16 + pin) {
            snprintf(sig, sizeof sig, "%s_CTS", units[i]);
            esk_stm32_usart_signal_input(g->s, sig, level);
        }
}

/* a USART's TX pin, as AFIO's MAPR places it */
static int usart_tx(const struct f1gpio *g, int pin)
{
    static const char *const units[3] = { "USART1", "USART2", "USART3" };
    int i, port, bit;
    for (i = 0; i < 3; i++)
        if (esk_stm32f1_usart_pin(g->s, units[i], 1, &port, &bit) == 0 && port == g->port &&
            bit == pin)
            return 1;
    return 0;
}

static void update(struct f1gpio *g)
{
    struct esk_mach *s = g->s;
    u32 mp = esk_stm32f1_mapr(s), idr = 0, odr = (u32)g->odr->value;
    u32 old = (u32)g->idr->value & 0xffffu;
    int i;
    for (i = 0; i < 16; i++) {
        u32 c = cfg(g, i), mode = c & 3, cnf = c >> 2;
        int lvl, ext = 0;
        esk_mach_pin o;
        int dbg = debug_pin(g, i, mp);
        memset(&o, 0, sizeof o);
        o.drive = -1;
        if (dbg >= 0) {
            lvl = dbg;
            o.pull = g->port == 1 && i == 3 ? 0 : dbg ? 1 : 2;
        } else {
            if (mode) {
                if (cnf & 2) {
                    /* a USART's TX idles high; an alternate function with
                     * no model lets the pin go rather than fight the
                     * circuit (an I2C bus's pull-ups) */
                    o.drive = usart_tx(g, i) ? 1 : esk_stm32f1_tim_pin(s, g->port, i);
                    if (o.drive < 0)
                        o.drive = usart_fn_out(g, i);
                    if (o.drive < 0)
                        o.drive = esk_stm32f1_spi_pin(s, g->port, i);
                    if (o.drive < 0)
                        o.drive = esk_stm32f1_i2c_pin(s, g->port, i);
                    o.af = 1;
                }
                else if (!(cnf & 1) || !(odr >> i & 1))
                    o.drive = (int)(odr >> i & 1);
                o.open_drain = (cnf & 1) != 0;
                /* MODE 10 2 MHz, 01 10 MHz, 11 50 MHz: the description's
                 * rout and slew in that order */
                o.speed = mode == 2 ? 1 : mode == 1 ? 2 : 3;
            } else if (cnf == 0) {
                o.analog = 1;
            } else if (cnf == 2) {
                o.pull = odr >> i & 1 ? 1 : 2;
            }
            if (o.drive >= 0)
                lvl = o.drive;
            else if (o.analog)
                lvl = 0;
            else if (s->host.pin_in && s->host.pin_in(s->host.ctx, g->port, i, &ext))
                lvl = ext != 0;
            else if (o.pull)
                lvl = o.pull == 1;
            else
                lvl = 0;
        }
        if (!o.analog)
            idr |= (u32)lvl << i;
        if (s->host.pin_cfg && (!g->known || memcmp(&g->out[i], &o, sizeof o) != 0))
            s->host.pin_cfg(s->host.ctx, g->port, i, &o);
        g->out[i] = o;
    }
    g->known = 1;
    if (old != idr) {
        esk_rf_hw(g->idr, (g->idr->value & ~(u64)0xffffu) | idr);
        for (i = 0; i < 16; i++) {
            if (!((old ^ idr) >> i & 1))
                continue;
            if (esk_stm32f1_exti_port(s, i) == g->port)
                esk_stm32_exti_input(s, i, (int)(idr >> i & 1));
            esk_stm32f1_tim_input(s, g->port, i, (int)(idr >> i & 1)); /* input capture */
            usart_cts_in(g, i, (int)(idr >> i & 1));
        }
    }
}

static u32 f1gpio_read(void *ctx, u32 off, int n)
{
    struct f1gpio *g = (struct f1gpio *)ctx;
    if (!g->s->bus.debug)
        update(g);
    return esk_rf_read(g->p, off, n);
}

static void f1gpio_write(void *ctx, u32 off, int n, u32 v)
{
    struct f1gpio *g = (struct f1gpio *)ctx;
    u64 odr, nv;
    esk_rf_write(g->p, off, n, v);
    odr = nv = g->odr->value;
    if (off == esk_rf_off(g->bsrr))
        nv = (odr & ~(u64)(v >> 16 & 0xffff)) | (v & 0xffff);
    else if (g->brr && off == esk_rf_off(g->brr))
        nv = odr & ~(u64)(v & 0xffff);
    if (nv != odr)
        esk_rf_hw(g->odr, nv);
    update(g);
}

static void f1gpio_reset(void *ctx)
{
    struct f1gpio *g = (struct f1gpio *)ctx;
    g->known = 0;
    update(g);
}

const struct esk_dev_ops esk_stm32f1_gpio_ops = {
    "stm32f1-gpio", f1gpio_read, f1gpio_write, f1gpio_reset, 0, 0,
};

void esk_stm32f1_pins_changed(struct esk_mach *s)
{
    int i;
    for (i = 0; s->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(s->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_stm32f1_gpio_ops && !esk_rf_gated(p))
            update((struct f1gpio *)p->ctx);
    }
}

void esk_stm32f1_port_changed(struct esk_mach *s, int port)
{
    char name[8] = "GPIOx";
    struct esk_rf_periph *p;
    name[4] = (char)('A' + port);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    if (p && p->ops == &esk_stm32f1_gpio_ops && !esk_rf_gated(p))
        update((struct f1gpio *)p->ctx);
}

void *esk_stm32f1_gpio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct f1gpio g, *c;
    memset(&g, 0, sizeof g);
    g.s = s;
    g.p = p;
    g.crl = esk_rf_reg(p, "CRL");
    g.crh = esk_rf_reg(p, "CRH");
    g.idr = esk_rf_reg(p, "IDR");
    g.odr = esk_rf_reg(p, "ODR");
    g.bsrr = esk_rf_reg(p, "BSRR");
    g.brr = esk_rf_reg(p, "BRR");
    if (!g.crl || !g.crh || !g.idr || !g.odr || !g.bsrr || strncmp(p->name, "GPIO", 4) ||
        p->name[4] < 'A' || p->name[4] > 'K' || p->name[5])
        return 0; /* not this layout: the registers alone */
    g.port = p->name[4] - 'A';
    s->pins_changed = esk_stm32f1_pins_changed;
    s->uart_pin = esk_stm32_usart_pin;
    c = (struct f1gpio *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = g;
    return c;
}
