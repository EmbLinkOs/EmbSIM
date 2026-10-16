/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* gpio.c -- the FE310's GPIO controller (FE310-G002 manual, chapter 17),
 * over the register file: 32 pins, GPIO n on the runtime's pin GPIOn.
 *
 *   - output_en makes a pin drive output_val (inverted by out_xor);
 *     input_en makes input_val read it (0 otherwise); pue is the weak
 *     pull-up (about 50 k);
 *   - iof_en gives the pin to a hardware function, iof_sel picking IOF0 or
 *     IOF1: the UARTs' TX pins are driven by them (idle high, the frames on
 *     the line from the MCU runtime), their RX pins are inputs; a function
 *     with no model lets the pin go;
 *   - rise_ip, fall_ip, high_ip and low_ip latch the input's edges and
 *     levels (a one written clears a bit; a level bit sets again while the
 *     level lasts), and each pin with an enabled pending bit raises its
 *     PLIC source, 8 + n. */
#include <string.h>

#include "mcu/riscv/riscv.h"

#define NPINS 32

struct fe_gpio {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *in_val, *in_en, *out_en, *out_val, *pue, *rise_ie, *rise_ip, *fall_ie, *fall_ip,
        *high_ie, *high_ip, *low_ie, *low_ip, *iof_en, *iof_sel, *out_xor;
    int iof_level[NPINS]; /* a hardware function's drive, -1 when it lets go */
    esk_mach_pin out[NPINS];
    int known;
    u32 src; /* the PLIC sources raised */
};

static struct fe_gpio *gpio_of(struct esk_mach *m)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_fe310_gpio_ops)
            return (struct fe_gpio *)p->ctx;
    }
    return NULL;
}

static u32 v32(const struct esk_rf_reg *r) { return (u32)r->value; }

static void raise_sources(struct fe_gpio *g)
{
    u32 src = (v32(g->rise_ip) & v32(g->rise_ie)) | (v32(g->fall_ip) & v32(g->fall_ie)) |
              (v32(g->high_ip) & v32(g->high_ie)) | (v32(g->low_ip) & v32(g->low_ie)), ch = src ^ g->src;
    int i;
    g->src = src;
    for (i = 0; ch && i < NPINS; i++)
        if (ch >> i & 1)
            esk_fe310_plic_source(g->s, 8 + i, (int)(src >> i & 1));
}

static void update(struct fe_gpio *g)
{
    struct esk_mach *s = g->s;
    u32 oe = v32(g->out_en), ov = v32(g->out_val) ^ v32(g->out_xor), pue = v32(g->pue);
    u32 ie = v32(g->in_en), iof = v32(g->iof_en), old = v32(g->in_val), in = 0;
    int i;
    for (i = 0; i < NPINS; i++) {
        esk_mach_pin o;
        int lvl, ext = 0;
        memset(&o, 0, sizeof o);
        o.drive = -1;
        if (iof >> i & 1) {
            /* IOF0 on GPIO17 and 18 is UART0's and UART1's TX: idle high */
            int tx = !(v32(g->iof_sel) >> i & 1) && (i == 17 || i == 18);
            o.af = 1;
            o.drive = g->iof_level[i] >= 0 ? g->iof_level[i] : tx ? 1 : -1;
        } else if (oe >> i & 1) {
            o.drive = (int)(ov >> i & 1);
        }
        if (o.drive < 0 && (pue >> i & 1))
            o.pull = 1;
        if (o.drive >= 0)
            lvl = o.drive;
        else if (s->host.pin_in && s->host.pin_in(s->host.ctx, i / 16, i % 16, &ext))
            lvl = ext != 0;
        else
            lvl = o.pull == 1;
        if (ie >> i & 1)
            in |= (u32)lvl << i;
        if (s->host.pin_cfg && (!g->known || memcmp(&g->out[i], &o, sizeof o) != 0))
            s->host.pin_cfg(s->host.ctx, i / 16, i % 16, &o);
        g->out[i] = o;
    }
    g->known = 1;
    if (in != old)
        esk_rf_hw(g->in_val, in);
    esk_rf_hw(g->rise_ip, v32(g->rise_ip) | (in & ~old));
    esk_rf_hw(g->fall_ip, v32(g->fall_ip) | (~in & old & ie));
    esk_rf_hw(g->high_ip, v32(g->high_ip) | in);
    esk_rf_hw(g->low_ip, v32(g->low_ip) | (~in & ie));
    raise_sources(g);
}

static u32 gp_read(void *ctx, u32 off, int n)
{
    struct fe_gpio *g = (struct fe_gpio *)ctx;
    if (!g->s->bus.debug)
        update(g);
    return esk_rf_read(g->p, off, n);
}

static void gp_write(void *ctx, u32 off, int n, u32 v)
{
    struct fe_gpio *g = (struct fe_gpio *)ctx;
    esk_rf_write(g->p, off, n, v); /* the ip registers are rc_w1 */
    update(g);
}

static void gp_reset(void *ctx)
{
    struct fe_gpio *g = (struct fe_gpio *)ctx;
    int i;
    for (i = 0; i < NPINS; i++)
        g->iof_level[i] = -1;
    g->known = 0;
    g->src = 0;
    update(g);
}

const struct esk_dev_ops esk_fe310_gpio_ops = {
    "fe310-gpio", gp_read, gp_write, gp_reset, 0, 0,
};

void esk_fe310_pins_changed(struct esk_mach *m)
{
    struct fe_gpio *g = gpio_of(m);
    if (g)
        update(g);
}

int esk_fe310_gpio_iof(struct esk_mach *m, int pin, int *sel)
{
    struct fe_gpio *g = gpio_of(m);
    if (!g || pin < 0 || pin >= NPINS)
        return 0;
    *sel = (int)(v32(g->iof_sel) >> pin & 1);
    return (int)(v32(g->iof_en) >> pin & 1);
}

void esk_fe310_gpio_iof_level(struct esk_mach *m, int pin, int level)
{
    struct fe_gpio *g = gpio_of(m);
    if (!g || pin < 0 || pin >= NPINS || g->iof_level[pin] == level)
        return;
    g->iof_level[pin] = level;
    update(g);
}

void *esk_fe310_gpio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct fe_gpio g, *c;
    int i;
    memset(&g, 0, sizeof g);
    g.s = s;
    g.p = p;
    g.in_val = esk_rf_reg(p, "INPUT_VAL");
    g.in_en = esk_rf_reg(p, "INPUT_EN");
    g.out_en = esk_rf_reg(p, "OUTPUT_EN");
    g.out_val = esk_rf_reg(p, "OUTPUT_VAL");
    g.pue = esk_rf_reg(p, "PUE");
    g.rise_ie = esk_rf_reg(p, "RISE_IE");
    g.rise_ip = esk_rf_reg(p, "RISE_IP");
    g.fall_ie = esk_rf_reg(p, "FALL_IE");
    g.fall_ip = esk_rf_reg(p, "FALL_IP");
    g.high_ie = esk_rf_reg(p, "HIGH_IE");
    g.high_ip = esk_rf_reg(p, "HIGH_IP");
    g.low_ie = esk_rf_reg(p, "LOW_IE");
    g.low_ip = esk_rf_reg(p, "LOW_IP");
    g.iof_en = esk_rf_reg(p, "IOF_EN");
    g.iof_sel = esk_rf_reg(p, "IOF_SEL");
    g.out_xor = esk_rf_reg(p, "OUT_XOR");
    if (!g.in_val || !g.in_en || !g.out_en || !g.out_val || !g.pue || !g.rise_ie || !g.rise_ip ||
        !g.fall_ie || !g.fall_ip || !g.high_ie || !g.high_ip || !g.low_ie || !g.low_ip || !g.iof_en ||
        !g.iof_sel || !g.out_xor)
        return 0;
    for (i = 0; i < NPINS; i++)
        g.iof_level[i] = -1;
    s->pins_changed = esk_fe310_pins_changed;
    s->uart_pin = esk_fe310_uart_pin;
    c = (struct fe_gpio *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = g;
    return c;
}
