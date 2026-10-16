/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* io.c -- the RP2040's user GPIOs (its datasheet, 2.19): IO_BANK0's
 * function select, overrides and interrupts, PADS_BANK0's pads, and the
 * pins they make (GPIOn on the runtime's pin GPIOn).
 *
 * Each of GPIO0-29:
 *   - its function (CTRL.FUNCSEL) gives the output and its enable: SIO
 *     (5), GPIO_OUT and GPIO_OE (sio.c); UART (2), a TX or RTS pin driven
 *     high (the line idle; the frames on it come from the MCU runtime) and
 *     RX and CTS pins inputs, by the function table's pattern (GPIO0-3
 *     UART0, 4-11 UART1, 12-19 UART0, 20-27 UART1, 28-29 UART0); any other
 *     function does not drive the pin (its peripheral is not modelled), nor
 *     does NULL (0x1f, out of reset);
 *   - OUTOVER and OEOVER then pass, invert, or force them low or high
 *     (OEOVER: disable or enable), and the pad's OD disables the output;
 *   - the pin is the output where enabled; else what the board drives on
 *     it; else its pad's pull (PUE up, PDE down; both: it keeps its last
 *     level, the bus keeper); else 0;
 *   - its input, while the pad's IE is set, is the pin (INOVER passes,
 *     inverts or forces it): what GPIO_IN reads, and the interrupt's;
 *   - the interrupts (INTR0-3): LEVEL_LOW and LEVEL_HIGH follow the input
 *     after IRQOVER, EDGE_LOW and EDGE_HIGH latch its falls and rises until
 *     written with 1; PROCn_INTS is (INTR | PROCn_INTF) & PROCn_INTE, and
 *     any bit of it raises IO_IRQ_BANK0 on core n, when it sets and when a
 *     write leaves it set;
 *   - GPIOn_STATUS shows each stage: OUTFROMPERI, OUTTOPAD, OEFROMPERI,
 *     OETOPAD, INFROMPAD, INTOPERI, IRQFROMPAD, IRQTOPROC.
 *
 * Not modelled: DORMANT_WAKE, the pads' drive
 * strength, slew and Schmitt trigger, the QSPI bank's pins and interrupts,
 * and the other functions' peripherals (SPI, I2C, PWM, PIO, the clock pins,
 * USB). */
#include <stdio.h>
#include <string.h>

#include "mcu/rp2040/rp2040.h"

#define NPIN 30
#define fld esk_rp_fld

enum { F_SPI = 1, F_UART, F_I2C, F_PWM, F_SIO, F_PIO0, F_PIO1, F_CLOCK, F_USB, F_NULL = 0x1f };

struct io {
    struct esk_mach *s;
    struct esk_rf_periph *p, *pads;
    struct esk_rf_reg *status[NPIN], *ctrl[NPIN], *pad[NPIN];
    struct esk_rf_reg *intr[4], *inte[2][4], *intf[2][4], *ints[2][4]; /* PROC0, PROC1 */
    u64 m_funcsel, m_outover, m_oeover, m_inover, m_irqover;
    u64 m_od, m_ie, m_pue, m_pde;
    u64 st_outperi, st_outpad, st_oeperi, st_oepad, st_inpad, st_inperi, st_irqpad, st_irqproc;
    int irq;
    u32 sio_out, sio_oe;
    u32 level;     /* each pin's level */
    u32 irq_level; /* each pin's interrupt input, after IRQOVER */
    int known;     /* the levels have been worked out */
    int busy;
    esk_mach_pin out[NPIN];
};

static struct io *io_of(struct esk_mach *s)
{
    return (struct io *)esk_rp_model(s, "IO_BANK0", &esk_rp2040_io_ops);
}

static int over(int v, int mode) { return mode == 0 ? v : mode == 1 ? !v : mode == 2 ? 0 : 1; }

/* the UART on pin n (0 or 1), and its signal there: 0 TX, 1 RX, 2 CTS, 3 RTS */
static int uart_unit(int n) { return ((n + 4) >> 3) & 1; }
static int uart_signal(int n) { return n & 3; }

/* what the function gives the pin: its output and output enable */
static void peri(const struct io *x, int n, int *out, int *oe)
{
    *out = *oe = 0;
    switch (fld(x->ctrl[n], x->m_funcsel)) {
    case F_SIO:
        *out = (int)(x->sio_out >> n & 1);
        *oe = (int)(x->sio_oe >> n & 1);
        break;
    case F_UART:
        if (uart_signal(n) == 0 || uart_signal(n) == 3)
            *out = *oe = 1; /* TX idle, RTS not asserted */
        break;
    default:
        break;
    }
}

/* every pin's level, the stages GPIOn_STATUS shows, the interrupts */
static void settle(struct io *x)
{
    struct esk_mach *s = x->s;
    u32 lvl = 0, irq = 0, rise, fall;
    int n, r;
    if (x->busy)
        return;
    x->busy = 1;
    for (n = 0; n < NPIN; n++) {
        int out, oe, ext = 0, pout, poe, drive, pin, in, toperi, toproc;
        int pue = (int)fld(x->pad[n], x->m_pue), pde = (int)fld(x->pad[n], x->m_pde);
        u32 func = fld(x->ctrl[n], x->m_funcsel);
        esk_mach_pin o;
        u64 st;
        peri(x, n, &out, &oe);
        pout = over(out, (int)fld(x->ctrl[n], x->m_outover));
        poe = over(oe, (int)fld(x->ctrl[n], x->m_oeover));
        drive = poe && !fld(x->pad[n], x->m_od);
        memset(&o, 0, sizeof o);
        o.drive = drive ? pout : -1;
        o.pull = pue && !pde ? 1 : pde && !pue ? 2 : 0;
        o.af = func == F_UART; /* the peripherals modelled: the UARTs */
        o.analog = !fld(x->pad[n], x->m_ie) && !drive;
        if (s->host.pin_cfg && (!x->known || memcmp(&x->out[n], &o, sizeof o) != 0))
            s->host.pin_cfg(s->host.ctx, n / 16, n % 16, &o);
        x->out[n] = o;
        if (drive)
            pin = pout;
        else if (s->host.pin_in && s->host.pin_in(s->host.ctx, n / 16, n % 16, &ext))
            pin = ext != 0;
        else if (pue && pde)
            pin = (int)(x->level >> n & 1);
        else
            pin = pue ? 1 : 0;
        in = fld(x->pad[n], x->m_ie) ? pin : 0;
        toperi = over(in, (int)fld(x->ctrl[n], x->m_inover));
        toproc = over(in, (int)fld(x->ctrl[n], x->m_irqover));
        lvl |= (u32)pin << n;
        irq |= (u32)toproc << n;
        st = (out ? x->st_outperi : 0) | (pout ? x->st_outpad : 0) | (oe ? x->st_oeperi : 0) |
             (poe ? x->st_oepad : 0) | (in ? x->st_inpad | x->st_irqpad : 0) | (toperi ? x->st_inperi : 0) |
             (toproc ? x->st_irqproc : 0);
        esk_rf_hw(x->status[n], st);
    }
    rise = x->known ? irq & ~x->irq_level : 0;
    fall = x->known ? ~irq & x->irq_level : 0;
    x->level = lvl;
    x->irq_level = irq;
    x->known = 1;
    for (r = 0; r < 4; r++) {
        u64 v = x->intr[r]->value;
        int k;
        for (k = 0; k < 8 && 8 * r + k < NPIN; k++) {
            int i = 8 * r + k, b = 4 * k;
            v &= ~((u64)3 << b);
            v |= (u64)((irq >> i & 1) ? 2 : 1) << b; /* LEVEL_HIGH, LEVEL_LOW */
            if (fall >> i & 1)
                v |= (u64)4 << b;
            if (rise >> i & 1)
                v |= (u64)8 << b;
        }
        if (v != x->intr[r]->value)
            esk_rf_hw(x->intr[r], v);
    }
    x->busy = 0;
}

static void irqs(struct io *x, int again)
{
    int c, r;
    for (c = 0; c < 2; c++) {
        int any = 0, was = 0;
        if (!x->inte[c][0])
            continue;
        for (r = 0; r < 4; r++) {
            u64 v = (x->intr[r]->value | x->intf[c][r]->value) & x->inte[c][r]->value;
            was |= x->ints[c][r]->value != 0;
            if (v != x->ints[c][r]->value)
                esk_rf_hw(x->ints[c][r], v);
            any |= v != 0;
        }
        if (any && (!was || again) && x->irq >= 0)
            esk_cortexm_core_interrupt(x->s, c, 16 + x->irq);
    }
}

void esk_rp2040_sio_pins(struct esk_mach *s, u32 out, u32 oe)
{
    struct io *x = io_of(s);
    if (!x)
        return;
    x->sio_out = out;
    x->sio_oe = oe;
    if (esk_rf_gated(x->p) || esk_rf_gated(x->pads))
        return;
    settle(x);
    irqs(x, 0);
}

u32 esk_rp2040_gpio_in(struct esk_mach *s)
{
    struct io *x = io_of(s);
    u32 v = 0;
    int n;
    if (!x || esk_rf_gated(x->p))
        return 0;
    settle(x);
    for (n = 0; n < NPIN; n++)
        if (x->status[n]->value & x->st_inperi)
            v |= 1u << n;
    return v;
}

int esk_rp2040_uart_pin(struct esk_mach *s, const char *unit, int tx, int *port, int *bit)
{
    struct io *x = io_of(s);
    int u, n;
    if (!strcmp(unit, "UART0"))
        u = 0;
    else if (!strcmp(unit, "UART1"))
        u = 1;
    else
        return -1;
    for (n = 0; x && n < NPIN; n++)
        if (fld(x->ctrl[n], x->m_funcsel) == F_UART && uart_unit(n) == u &&
            uart_signal(n) == (tx ? 0 : 1)) {
            *port = n / 16;
            *bit = n % 16;
            return 0;
        }
    return -1;
}

static u32 io_read(void *ctx, u32 off, int n)
{
    struct io *x = (struct io *)ctx;
    if (!x->s->bus.debug)
        settle(x);
    return esk_rf_read(x->p, off, n);
}

static void io_write(void *ctx, u32 off, int n, u32 v)
{
    struct io *x = (struct io *)ctx;
    esk_rf_write(x->p, off, n, v);
    settle(x);
    irqs(x, 1);
}

static void io_reset(void *ctx)
{
    struct io *x = (struct io *)ctx;
    x->known = 0;
    settle(x);
    irqs(x, 0);
}

const struct esk_dev_ops esk_rp2040_io_ops = {
    "rp2040-io", io_read, io_write, io_reset, 0, 0,
};

/* the pads, and the QSPI bank's controls: written, the pins follow */
static u32 pads_read(void *ctx, u32 off, int n) { return esk_rf_read((struct esk_rf_periph *)ctx, off, n); }

static void pads_write(void *ctx, u32 off, int n, u32 v)
{
    struct esk_rf_periph *p = (struct esk_rf_periph *)ctx;
    esk_rf_write(p, off, n, v);
    esk_rp2040_pins_changed(p->sim);
    if (!strcmp(p->name, "IO_QSPI"))
        esk_rp2040_ssi_select(p->sim); /* its chip select may be let go */
}

static void pads_reset(void *ctx)
{
    struct esk_rf_periph *p = (struct esk_rf_periph *)ctx;
    esk_rp2040_pins_changed(p->sim);
}

const struct esk_dev_ops esk_rp2040_pads_ops = {
    "rp2040-pads", pads_read, pads_write, pads_reset, 0, 0,
};

void *esk_rp2040_pads_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    ES_UNUSED(s);
    return p;
}

void *esk_rp2040_io_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct io *x = (struct io *)esk_mach_alloc(s, sizeof *x);
    char nm[32];
    int n, r, c;
    if (!x)
        return 0;
    memset(x, 0, sizeof *x);
    x->s = s;
    x->p = p;
    x->pads = esk_rf_periph(s->rf, "PADS_BANK0");
    if (!x->pads)
        return 0;
    for (n = 0; n < NPIN; n++) {
        (void)snprintf(nm, sizeof nm, "GPIO%d_STATUS", n);
        x->status[n] = esk_rf_reg(p, nm);
        (void)snprintf(nm, sizeof nm, "GPIO%d_CTRL", n);
        x->ctrl[n] = esk_rf_reg(p, nm);
        (void)snprintf(nm, sizeof nm, "GPIO%d", n);
        x->pad[n] = esk_rf_reg(x->pads, nm);
        if (!x->status[n] || !x->ctrl[n] || !x->pad[n])
            return 0;
    }
    for (r = 0; r < 4; r++) {
        (void)snprintf(nm, sizeof nm, "INTR%d", r);
        x->intr[r] = esk_rf_reg(p, nm);
        for (c = 0; c < 2; c++) {
            (void)snprintf(nm, sizeof nm, "PROC%d_INTE%d", c, r);
            x->inte[c][r] = esk_rf_reg(p, nm);
            (void)snprintf(nm, sizeof nm, "PROC%d_INTF%d", c, r);
            x->intf[c][r] = esk_rf_reg(p, nm);
            (void)snprintf(nm, sizeof nm, "PROC%d_INTS%d", c, r);
            x->ints[c][r] = esk_rf_reg(p, nm);
            if (!x->inte[c][r] || !x->intf[c][r] || !x->ints[c][r])
                x->inte[c][r] = x->intf[c][r] = x->ints[c][r] = 0;
        }
        if (!x->intr[r] || !x->inte[0][r])
            return 0;
        if (!x->inte[1][r])
            x->inte[1][0] = 0; /* PROC1's are all there, or none is used */
    }
    x->m_funcsel = esk_rf_mask(x->ctrl[0], "FUNCSEL");
    x->m_outover = esk_rf_mask(x->ctrl[0], "OUTOVER");
    x->m_oeover = esk_rf_mask(x->ctrl[0], "OEOVER");
    x->m_inover = esk_rf_mask(x->ctrl[0], "INOVER");
    x->m_irqover = esk_rf_mask(x->ctrl[0], "IRQOVER");
    x->m_od = esk_rf_mask(x->pad[0], "OD");
    x->m_ie = esk_rf_mask(x->pad[0], "IE");
    x->m_pue = esk_rf_mask(x->pad[0], "PUE");
    x->m_pde = esk_rf_mask(x->pad[0], "PDE");
    x->st_outperi = esk_rf_mask(x->status[0], "OUTFROMPERI");
    x->st_outpad = esk_rf_mask(x->status[0], "OUTTOPAD");
    x->st_oeperi = esk_rf_mask(x->status[0], "OEFROMPERI");
    x->st_oepad = esk_rf_mask(x->status[0], "OETOPAD");
    x->st_inpad = esk_rf_mask(x->status[0], "INFROMPAD");
    x->st_inperi = esk_rf_mask(x->status[0], "INTOPERI");
    x->st_irqpad = esk_rf_mask(x->status[0], "IRQFROMPAD");
    x->st_irqproc = esk_rf_mask(x->status[0], "IRQTOPROC");
    x->irq = esk_rf_irq(p, "IO_IRQ_BANK0");
    s->pins_changed = esk_rp2040_pins_changed;
    s->uart_pin = esk_rp2040_uart_pin;
    return x;
}

/* the board drove a pin, or let it go: the levels again */
void esk_rp2040_pins_changed(struct esk_mach *s)
{
    struct io *x = io_of(s);
    if (x && !esk_rf_gated(x->p) && !esk_rf_gated(x->pads)) {
        settle(x);
        irqs(x, 0);
    }
}
