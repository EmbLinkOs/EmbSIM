/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* gpio.c -- the ESP32-C3's GPIO matrix and IO MUX (ESP32-C3 TRM v1.4,
 * chapters 5 and 7) on its 22 pins, GPIO0-21 (the runtime's port n / 16,
 * bit n % 16).
 *
 * Each pin's IO_MUX_GPIOn_REG picks its function (MCU_SEL, table 5.12-1):
 * function 1 -- and function 0 where it is the pin's GPIO -- is the GPIO
 * matrix; GPIO21's function 0 is U0TXD and GPIO20's U0RXD, directly;
 * function 2 of GPIO2, 4-7 and 10 is FSPI's signal directly; the others
 * (the SPI flash's and JTAG's) leave the pin undriven.
 *   - Through the matrix, GPIO_FUNCn_OUT_SEL_CFG's OUT_SEL names the pin's
 *     output: 128 is GPIO_OUT's bit, with GPIO_ENABLE's as its enable; a
 *     peripheral signal (table 5.11-1) is that signal, its enable the
 *     peripheral's (1 for the UARTs' TXD, RTS and DTR, and for the others
 *     whose table entry is 1'd1) unless OEN_SEL takes GPIO_ENABLE's;
 *     INV_SEL and OEN_INV_SEL invert them. A peripheral's model sets its
 *     signals' levels and enables (esk_esp32c3_signal); a signal no model
 *     drives leaves the pin undriven.
 *   - GPIO_PINn's PAD_DRIVER makes the output open-drain; FUN_WPU and
 *     FUN_WPD pull the pin; FUN_IE lets its level into GPIO_IN, into the
 *     interrupt and into the peripherals' inputs.
 *   - The pin is its output where enabled; else what the board drives;
 *     else its pull; else 0.
 *   - A peripheral input (GPIO_FUNCy_IN_SEL_CFG) comes through the matrix
 *     from IN_SEL's pin (0x1E: 1, 0x1F: 0, IN_INV_SEL inverting) when SEL
 *     is set, else directly from the pin whose IO MUX function it is, else
 *     the table's default (1 for I2C's SCL and SDA and TWAI's RX, else 0);
 *     a model that follows its inputs is told when the pins' inputs change
 *     (esk_esp32c3_signal_watch).
 *   - Interrupts (GPIO_PINn's INT_TYPE: 1 rising, 2 falling, 3 any edge, 4
 *     low level, 5 high level; INT_ENA bit 0 the CPU's, bit 1 its NMI's)
 *     set GPIO_STATUS (W1TS and W1TC set and clear it; a level sets it
 *     again while it lasts); PCPU_INT and PCPU_NMI_INT are the enabled
 *     ones, matrix sources 16 and 17.
 *   - GPIO_STRAP latches GPIO9 (bit 3), GPIO8 (2), GPIO3 (1), GPIO2 (0) at
 *     a chip reset (7.2: the boot mode's pins, GPIO9's reset pull-up making
 *     it 1 when nothing drives it) and keeps them through every other.
 * IO MUX's pins come out of reset as table 5.12-1 has them (each pin's
 * IE, pull and drive), not with the SVD's one reset value for all; MTCK
 * pulled up (EFUSE_DIS_PAD_JTAG 0), GPIO19 pulled up by USB Serial/JTAG's
 * D+ pull-up.
 * Not yet: the peripherals' signals but the UARTs' (a pin routed to one
 * is not driven), the input filter and synchronisation, sleep mode (the
 * MCU_ pad bits, SLP_SEL), the drive strengths' effect, the sigma-delta
 * outputs, the power-up glitches (table 5.12-2) and the pins' wake-up. */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define NPIN 22
#define SIG_U0TXD 6
#define SIG_U0RTS 7
#define SIG_U0DTR 8
#define SIG_U1TXD 9
#define SIG_U1RTS 10
#define SIG_U1DTR 11
#define SIG_GPIO 128

#define MUX_FUN_WPD (1u << 7)
#define MUX_FUN_WPU (1u << 8)
#define MUX_FUN_IE (1u << 9)

/* table 5.12-1's reset column: 0 IE off; 1 IE; 2 IE and WPD; 3 IE and WPU;
 * 4 OE and WPU (U0TXD); and the DRV column */
static const u8 reset_kind[NPIN] = { 0, 0, 1, 1, 1, 1, 3, 1, 1, 3, 1, 0, 3, 3, 3, 3, 3, 3, 0, 5, 3, 4 };
static const u8 reset_drv[NPIN] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 2, 2 };

struct c3_gpio {
    struct esk_mach *s;
    struct esk_rf_periph *p, *mux;
    struct esk_rf_reg *out, *enable, *strap, *in, *status, *pcpu, *nmi;
    struct esk_rf_reg *pin[NPIN], *fout[NPIN], *fin[128], *iomux[NPIN];
    u32 latched;
    int have, known, busy, irq, irq_nmi;
    u32 level;  /* each pin's level */
    u32 input;  /* each pin's input (its level where IE is set) */
    esk_mach_pin cfg[NPIN];
    u8 sig_on[128], sig_lv[128], sig_oe[128]; /* the peripherals' outputs */
    int again; /* a change while the pins settle: settle again */
    struct {
        void (*fn)(void *ctx);
        void *ctx;
    } watch[8];
    int nwatch;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static struct c3_gpio *gpio_of(struct esk_mach *m)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_esp32c3_gpio_ops)
            return (struct c3_gpio *)p->ctx;
    }
    return NULL;
}

/* the pin's IO MUX function: 1 the matrix (also function 0 of the pins
 * whose function 0 is their GPIO), else its own */
static u32 mux_sel(const struct c3_gpio *k, int n)
{
    u32 sel = (reg(k->iomux[n]) >> 12) & 7u;
    static const u8 f0_is_gpio[NPIN] = { 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 };
    return sel == 0 && f0_is_gpio[n] ? 1u : sel;
}

/* the FSPI signal of pin n's IO MUX function 2, or -1 (table 5.12-1:
 * FSPIQ on GPIO2, FSPIHD 4, FSPIWP 5, FSPICLK 6, FSPID 7, FSPICS0 10) */
static int fspi_sig(int n)
{
    switch (n) {
    case 2: return 64;
    case 4: return 66;
    case 5: return 67;
    case 6: return 63;
    case 7: return 65;
    case 10: return 68;
    default: return -1;
    }
}

/* a peripheral output signal's level and enable: 0 when it is not one
 * EmbSIM drives */
static int signal_out(const struct c3_gpio *k, int sig, int *level, int *oe)
{
    if (sig >= 0 && sig < 128 && k->sig_on[sig]) {
        *level = k->sig_lv[sig];
        *oe = k->sig_oe[sig];
        return 1;
    }
    switch (sig) {
    case SIG_U0TXD:
    case SIG_U1TXD: /* idle high: the frames are the runtime's */
    case SIG_U0RTS:
    case SIG_U0DTR:
    case SIG_U1RTS:
    case SIG_U1DTR:
        *level = 1;
        *oe = 1;
        return 1;
    default:
        return 0;
    }
}

/* the pin's output: 1 and its level and enable when something drives it;
 * whether it is a peripheral's (af) */
static int pin_out(const struct c3_gpio *k, int n, int *level, int *oe, int *af)
{
    u32 sel = mux_sel(k, n);
    *af = 0;
    if (sel == 1) {
        u32 f = reg(k->fout[n]), sig = f & 0xffu;
        int lv = 0, en = 0;
        if (sig == SIG_GPIO) {
            lv = (int)(reg(k->out) >> n & 1u);
            en = (int)(reg(k->enable) >> n & 1u);
        } else {
            if (!signal_out(k, (int)sig, &lv, &en))
                return 0;
            *af = 1;
            if (f & (1u << 9)) /* OEN_SEL: GPIO_ENABLE's */
                en = (int)(reg(k->enable) >> n & 1u);
        }
        if (f & (1u << 8))
            lv = !lv;
        if (f & (1u << 10))
            en = !en;
        *level = lv;
        *oe = en;
        return 1;
    }
    if (sel == 0 && n == 21) { /* U0TXD */
        *level = 1;
        *oe = 1;
        *af = 1;
        return 1;
    }
    if (sel == 2 && fspi_sig(n) >= 0) { /* FSPI's, through the IO MUX */
        *af = 1;
        return signal_out(k, fspi_sig(n), level, oe);
    }
    return 0;
}

static void irqs(struct c3_gpio *k, u32 rise, u32 fall)
{
    u32 st = reg(k->status), cpu = 0, nmi = 0;
    int n;
    for (n = 0; n < NPIN; n++) {
        u32 pr = reg(k->pin[n]), type = (pr >> 7) & 7u, ena = (pr >> 13) & 0x1fu;
        int in = (int)(k->input >> n & 1u), hit;
        hit = (type == 1 && (rise >> n & 1u)) || (type == 2 && (fall >> n & 1u)) ||
              (type == 3 && ((rise | fall) >> n & 1u)) || (type == 4 && !in) || (type == 5 && in);
        if (hit)
            st |= 1u << n;
        if (type && (st >> n & 1u)) {
            if (ena & 1u)
                cpu |= 1u << n;
            if (ena & 2u)
                nmi |= 1u << n;
        }
    }
    esk_rf_hw(k->status, st);
    esk_rf_hw(k->pcpu, cpu);
    esk_rf_hw(k->nmi, nmi);
    if ((cpu != 0) != k->irq) {
        k->irq = cpu != 0;
        esk_esp32c3_source(k->s, 16, k->irq);
    }
    if ((nmi != 0) != k->irq_nmi) {
        k->irq_nmi = nmi != 0;
        esk_esp32c3_source(k->s, 17, k->irq_nmi);
    }
}

/* every pin's level, GPIO_IN, the interrupts */
/* the pins the matrix gives a UART's RX (U0RXD, U1RXD): the runtime reads
 * their frames, as it does GPIO20's through the IO MUX */
static u32 uart_rx_pins(const struct c3_gpio *k)
{
    static const int sig[2] = { 6, 9 };
    u32 m = 0;
    int i;
    for (i = 0; i < 2; i++) {
        u32 f = k->fin[sig[i]] ? reg(k->fin[sig[i]]) : 0, n = f & 0x1fu;
        if ((f & (1u << 6)) && n < NPIN)
            m |= 1u << n;
    }
    return m;
}

static void settle(struct c3_gpio *k)
{
    struct esk_mach *s = k->s;
    u32 lvl = 0, in = 0, rise, fall, rx;
    int n;
    if (k->busy) {
        k->again = 1;
        return;
    }
    k->busy = 1;
again:
    k->again = 0;
    rx = uart_rx_pins(k);
    for (n = 0; n < NPIN; n++) {
        u32 mux = reg(k->iomux[n]);
        int level = 0, oe = 0, af = 0, drive, ext = 0, pin;
        int wpu = (mux & MUX_FUN_WPU) != 0, wpd = (mux & MUX_FUN_WPD) != 0;
        int od = (reg(k->pin[n]) & (1u << 2)) != 0;
        esk_mach_pin o;
        if (!pin_out(k, n, &level, &oe, &af))
            oe = 0;
        drive = oe && !(od && level);
        memset(&o, 0, sizeof o);
        o.drive = oe ? level : -1;
        o.open_drain = od;
        o.pull = wpu && !wpd ? 1 : wpd && !wpu ? 2 : 0;
        o.af = af || (mux_sel(k, n) == 0 && n == 20) /* U0RXD */ || (rx >> n & 1u);
        o.analog = !(mux & MUX_FUN_IE) && !oe;
        if (s->host.pin_cfg && (!k->known || memcmp(&k->cfg[n], &o, sizeof o) != 0))
            s->host.pin_cfg(s->host.ctx, n / 16, n % 16, &o);
        k->cfg[n] = o;
        if (drive)
            pin = level;
        else if (s->host.pin_in && s->host.pin_in(s->host.ctx, n / 16, n % 16, &ext))
            pin = ext != 0;
        else
            pin = wpu ? 1 : 0;
        lvl |= (u32)pin << n;
        if (mux & MUX_FUN_IE)
            in |= (u32)pin << n;
    }
    rise = k->known ? in & ~k->input : 0;
    fall = k->known ? ~in & k->input : 0;
    k->level = lvl;
    k->input = in;
    k->known = 1;
    esk_rf_hw(k->in, in);
    irqs(k, rise, fall);
    if (rise | fall)
        for (n = 0; n < k->nwatch; n++)
            k->watch[n].fn(k->watch[n].ctx);
    if (k->again)
        goto again;
    k->busy = 0;
}

void esk_esp32c3_signal(struct esk_mach *m, int sig, int level, int oe)
{
    struct c3_gpio *k = gpio_of(m);
    u8 lv = (u8)(level != 0), en = (u8)(oe != 0);
    if (!k || sig < 0 || sig >= 128)
        return;
    if (k->sig_on[sig] && k->sig_lv[sig] == lv && k->sig_oe[sig] == en)
        return;
    k->sig_on[sig] = 1;
    k->sig_lv[sig] = lv;
    k->sig_oe[sig] = en;
    if (k->known)
        settle(k);
}

int esk_esp32c3_signal_in(struct esk_mach *m, int sig)
{
    struct c3_gpio *k = gpio_of(m);
    u32 f;
    int n;
    if (!k || sig < 0 || sig >= 128)
        return 0;
    f = k->fin[sig] ? reg(k->fin[sig]) : 0;
    if (f & (1u << 6)) { /* through the matrix */
        u32 pin = f & 0x1fu;
        int v = pin == 0x1Eu ? 1 : pin == 0x1Fu ? 0 : pin < NPIN ? (int)(k->input >> pin & 1u) : 0;
        return (f & (1u << 5)) ? !v : v;
    }
    if (sig == 6 && mux_sel(k, 20) == 0) /* U0RXD */
        return (int)(k->input >> 20 & 1u);
    for (n = 0; n < NPIN; n++)
        if (fspi_sig(n) == sig && mux_sel(k, n) == 2)
            return (int)(k->input >> n & 1u);
    return sig == 53 || sig == 54 || sig == 74; /* the table's defaults */
}

int esk_esp32c3_signal_watch(struct esk_mach *m, void (*fn)(void *ctx), void *ctx)
{
    struct c3_gpio *k = gpio_of(m);
    if (!k || k->nwatch >= (int)(sizeof k->watch / sizeof k->watch[0]))
        return -1;
    k->watch[k->nwatch].fn = fn;
    k->watch[k->nwatch].ctx = ctx;
    k->nwatch++;
    return 0;
}

void esk_esp32c3_pins_changed(struct esk_mach *m)
{
    struct c3_gpio *k = gpio_of(m);
    if (k)
        settle(k);
}

/* the pin a UART's TXD goes out on, or its RXD comes in from */
int esk_esp32c3_uart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit)
{
    struct c3_gpio *k = gpio_of(m);
    int u, n;
    if (!k)
        return -1;
    if (!strcmp(unit, "UART0"))
        u = 0;
    else if (!strcmp(unit, "UART1"))
        u = 1;
    else
        return -1;
    if (tx) {
        for (n = 0; n < NPIN; n++) {
            u32 sel = mux_sel(k, n);
            if ((sel == 1 && (reg(k->fout[n]) & 0xffu) == (u32)(u ? SIG_U1TXD : SIG_U0TXD)) ||
                (u == 0 && sel == 0 && n == 21)) {
                *port = n / 16;
                *bit = n % 16;
                return 0;
            }
        }
        return -1;
    }
    {
        u32 f = reg(k->fin[u ? 9 : 6]);
        if (f & (1u << 6)) { /* through the matrix */
            n = (int)(f & 0x1fu);
            if (n >= NPIN)
                return -1;
        } else if (u == 0 && mux_sel(k, 20) == 0) {
            n = 20; /* U0RXD, directly */
        } else {
            return -1;
        }
        *port = n / 16;
        *bit = n % 16;
        return 0;
    }
}

/* ---- the GPIO block -------------------------------------------------------------- */

static u32 gp_read(void *ctx, u32 off, int n)
{
    struct c3_gpio *k = (struct c3_gpio *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void gp_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_gpio *k = (struct c3_gpio *)ctx;
    const u32 mask = (1u << NPIN) - 1u;
    if (off == esk_rf_off(k->strap) || off == esk_rf_off(k->in))
        return; /* read-only */
    if (off == 0x08u || off == 0x0Cu) { /* OUT_W1TS, OUT_W1TC */
        esk_rf_hw(k->out, off == 0x08u ? reg(k->out) | (v & mask) : reg(k->out) & ~v);
    } else if (off == 0x24u || off == 0x28u) { /* ENABLE_W1TS, ENABLE_W1TC */
        esk_rf_hw(k->enable, off == 0x24u ? reg(k->enable) | (v & mask) : reg(k->enable) & ~v);
    } else if (off == 0x48u || off == 0x4Cu) { /* STATUS_W1TS, STATUS_W1TC */
        esk_rf_hw(k->status, off == 0x48u ? reg(k->status) | (v & mask) : reg(k->status) & ~v);
    } else {
        esk_rf_write(k->p, off, n, v);
    }
    settle(k);
}

static int pin_level(struct c3_gpio *k, int gpio, int dflt)
{
    int level = dflt;
    if (k->s->host.pin_in && k->s->host.pin_in(k->s->host.ctx, gpio / 16, gpio % 16, &level))
        return level != 0;
    return dflt;
}

static void gp_reset(void *ctx)
{
    struct c3_gpio *k = (struct c3_gpio *)ctx;
    if (!k->have || k->s->reset_cause == ESK_RESET_POWER || k->s->reset_cause == ESK_RESET_PIN) {
        k->latched = (u32)pin_level(k, 9, 1) << 3 | (u32)pin_level(k, 8, 0) << 2 | (u32)pin_level(k, 3, 0) << 1 |
                     (u32)pin_level(k, 2, 0);
        k->have = 1;
    }
    esk_rf_hw(k->strap, k->latched);
    k->irq = k->irq_nmi = 0;
    esk_esp32c3_source(k->s, 16, 0);
    esk_esp32c3_source(k->s, 17, 0);
    k->known = 0;
    settle(k);
}

const struct esk_dev_ops esk_esp32c3_gpio_ops = {
    "esp32c3-gpio", gp_read, gp_write, gp_reset, 0, 0,
};

/* ---- IO MUX ------------------------------------------------------------------- */

static u32 mx_read(void *ctx, u32 off, int n)
{
    struct esk_rf_periph *p = (struct esk_rf_periph *)ctx;
    return esk_rf_read(p, off, n);
}

static void mx_write(void *ctx, u32 off, int n, u32 v)
{
    struct esk_rf_periph *p = (struct esk_rf_periph *)ctx;
    esk_rf_write(p, off, n, v);
    esk_esp32c3_pins_changed(p->sim);
}

/* each pin as table 5.12-1 has it out of reset */
static void mx_reset(void *ctx)
{
    struct esk_rf_periph *p = (struct esk_rf_periph *)ctx;
    char name[16];
    int i;
    for (i = 0; i < NPIN; i++) {
        struct esk_rf_reg *r;
        u32 v = (u32)reset_drv[i] << 10;
        snprintf(name, sizeof name, "GPIO%d", i);
        r = esk_rf_reg(p, name);
        if (!r)
            continue;
        switch (reset_kind[i]) {
        case 1:
            v |= MUX_FUN_IE;
            break;
        case 2:
            v |= MUX_FUN_IE | MUX_FUN_WPD;
            break;
        case 3:
            v |= MUX_FUN_IE | MUX_FUN_WPU;
            break;
        case 4: /* U0TXD's output and pull-up */
        case 5: /* GPIO19: USB Serial/JTAG's D+ pull-up */
            v |= MUX_FUN_WPU;
            break;
        default:
            break;
        }
        esk_rf_hw(r, v);
    }
    esk_esp32c3_pins_changed(p->sim);
}

const struct esk_dev_ops esk_esp32c3_iomux_ops = {
    "esp32c3-iomux", mx_read, mx_write, mx_reset, 0, 0,
};

void *esk_esp32c3_iomux_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    (void)s;
    return esk_rf_reg(p, "GPIO21") ? p : 0;
}

void *esk_esp32c3_gpio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_gpio *k = (struct c3_gpio *)esk_mach_alloc(s, sizeof *k);
    char name[32];
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->mux = esk_rf_periph(s->rf, "IO_MUX");
    k->out = esk_rf_reg(p, "OUT");
    k->enable = esk_rf_reg(p, "ENABLE");
    k->strap = esk_rf_reg(p, "STRAP");
    k->in = esk_rf_reg(p, "IN");
    k->status = esk_rf_reg(p, "STATUS");
    k->pcpu = esk_rf_reg(p, "PCPU_INT");
    k->nmi = esk_rf_reg(p, "PCPU_NMI_INT");
    if (!k->mux || !k->out || !k->enable || !k->strap || !k->in || !k->status || !k->pcpu || !k->nmi)
        return 0;
    for (i = 0; i < NPIN; i++) {
        snprintf(name, sizeof name, "PIN%d", i);
        k->pin[i] = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "FUNC%d_OUT_SEL_CFG", i);
        k->fout[i] = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "GPIO%d", i);
        k->iomux[i] = esk_rf_reg(k->mux, name);
        if (!k->pin[i] || !k->fout[i] || !k->iomux[i])
            return 0;
    }
    for (i = 0; i < 128; i++) {
        snprintf(name, sizeof name, "FUNC%d_IN_SEL_CFG", i);
        k->fin[i] = esk_rf_reg(p, name);
    }
    if (!k->fin[6] || !k->fin[9])
        return 0;
    s->pins_changed = esk_esp32c3_pins_changed;
    s->uart_pin = esk_esp32c3_uart_pin;
    return k;
}
