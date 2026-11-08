/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* gpio.c -- the ESP32's GPIO matrix and IO MUX (TRM v5 chapter 4,
 * "IO_MUX and GPIO Matrix"; ESP32 Series Datasheet's pin tables) on its 34
 * pads GPIO0-19, 21-23, 25-27 and 32-39 (the runtime's port n / 16, bit
 * n % 16). The ESP32-C3's model (mcu/esp32c3/gpio.c) on the ESP32's
 * registers.
 *
 * Each pad's IO_MUX register picks its function (MCU_SEL): function 2 is
 * the GPIO matrix on every pad, and so is function 0 on the pads whose
 * first function is their GPIO; GPIO1's function 0 is U0TXD and GPIO3's
 * U0RXD, directly; GPIO6-11 (the module's flash) and the JTAG pads
 * (GPIO12-15) leave the pad to their functions, which no model drives.
 *   - Through the matrix, GPIO_FUNCn_OUT_SEL_CFG's OUT_SEL names the pad's
 *     output: 256 is GPIO_OUT's (OUT1's) bit, with GPIO_ENABLE's
 *     (ENABLE1's) as its enable; a peripheral signal (TRM table 4-2) is
 *     that signal, its enable the peripheral's (1 for the UARTs' TXD, RTS
 *     and DTR) unless OEN_SEL takes GPIO_ENABLE's; INV_SEL and OEN_INV_SEL
 *     invert them. A peripheral's model sets its signals' levels and
 *     enables (esk_esp32_signal); a signal no model drives leaves the pad
 *     undriven. GPIO34-39 have no output driver.
 *   - GPIO_PINn's PAD_DRIVER makes the output open-drain; FUN_WPU and
 *     FUN_WPD pull the pad; FUN_IE lets its level into GPIO_IN (IN1), the
 *     interrupt and the peripherals' inputs.
 *   - The pad is its output where enabled; else what the board drives;
 *     else its pull; else 0.
 *   - A peripheral input (GPIO_FUNCy_IN_SEL_CFG) comes through the matrix
 *     from IN_SEL's pad (0x38: 1, 0x30: 0, IN_INV_SEL inverting) when SEL
 *     is set, else directly from the pad whose IO MUX function it is, else
 *     0 (1 for the I2C controllers' SCL and SDA). A model may watch the
 *     pads' input levels (esk_esp32_signal_watch).
 *   - Interrupts (GPIO_PINn's INT_TYPE: 1 rising, 2 falling, 3 any edge,
 *     4 low level, 5 high level; INT_ENA bit 0 the APP_CPU's, 1 its NMI's,
 *     2 the PRO_CPU's, 3 its NMI's) set GPIO_STATUS (STATUS1; W1TS and
 *     W1TC set and clear it; a level sets it again while it lasts);
 *     PCPU_INT, PCPU_NMI_INT, ACPU_INT and ACPU_NMI_INT (and their *1) are
 *     the enabled ones; the matrix sources GPIO (22) and GPIO_NMI (23)
 *     reach each CPU with that CPU's own.
 *   - GPIO_STRAP latches at a chip reset the boot-mode pins (TRM 4.? and
 *     the datasheet's strapping table): MTDI (GPIO12) bit 5, GPIO0 bit 4,
 *     GPIO2 bit 3, GPIO4 bit 2, MTDO (GPIO15) bit 1, GPIO5 bit 0, each
 *     its reset pull's level when nothing drives it (GPIO0, MTDO, GPIO5
 *     up; GPIO2, GPIO4, MTDI down): 0x13, SPI_FAST_FLASH_BOOT; GPIO0 held
 *     low gives 0x03, the download mode. It keeps them through every other
 *     reset.
 * IO MUX's pads come out of reset as the datasheet's table has them (each
 * pad's IE and pull), not with the SVD's one reset value for all.
 * Not yet: the RTC IO MUX (RTC_IO's pad functions, its pulls, the
 * hold), the input filter, sleep mode (the MCU_ pad bits, SLP_SEL), the
 * drive strengths' effect, the pads' wake-up, the SDIO interrupt. */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32/soc/esp32.h"

#define NPIN 40
#define SIG_U0TXD 14
#define SIG_U0RTS 15
#define SIG_U0DTR 16
#define SIG_U1TXD 17
#define SIG_U1RTS 18
#define SIG_U2TXD 198
#define SIG_U2RTS 199
#define SIG_GPIO 256
#define NSIG 256

#define MUX_FUN_WPD (1u << 7)
#define MUX_FUN_WPU (1u << 8)
#define MUX_FUN_IE (1u << 9)

/* the pads that exist */
#define PADS 0xFF0EFFFFFFull
/* the datasheet's reset column: 0 IE off; 1 IE; 2 IE and WPD; 3 IE and WPU */
static const u8 reset_kind[NPIN] = {
    3, 3, 2, 3, 2, 3, 3, 3, 3, 3, 3, 3, 2, 1, 1, 3, 1, 1, 1, 1,
    0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
/* function 0 is the pad's GPIO */
static const u8 f0_is_gpio[NPIN] = {
    1, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 };

struct e32_gpio {
    struct esk_mach *s;
    struct esk_rf_periph *p, *mux;
    struct esk_rf_reg *out, *out1, *enable, *enable1, *strap, *in, *in1, *status, *status1;
    struct esk_rf_reg *pcpu, *pnmi, *acpu, *anmi, *pcpu1, *pnmi1, *acpu1, *anmi1;
    struct esk_rf_reg *pin[NPIN], *fout[NPIN], *fin[NSIG], *iomux[NPIN];
    u32 latched;
    int have, known, busy, irq[2], irq_nmi[2];
    u64 level, input;
    esk_mach_pin cfg[NPIN];
    u8 sig_on[NSIG], sig_lv[NSIG], sig_oe[NSIG];
    int again;
    struct {
        void (*fn)(void *ctx);
        void *ctx;
    } watch[8];
    int nwatch;
};

static u32 reg(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }

static int pad(int n) { return n >= 0 && n < NPIN && ((PADS >> n) & 1u); }

static struct e32_gpio *gpio_of(struct esk_mach *m)
{
    return m->rf ? (struct e32_gpio *)esk_rf_ctx_of(m->rf, &esk_esp32_gpio_ops) : 0;
}

static u64 out64(const struct e32_gpio *k) { return (u64)reg(k->out) | (u64)reg(k->out1) << 32; }
static u64 en64(const struct e32_gpio *k) { return (u64)reg(k->enable) | (u64)reg(k->enable1) << 32; }

/* the pad's IO MUX function: 2 the matrix (also function 0 of the pads
 * whose function 0 is their GPIO), else its own */
static u32 mux_sel(const struct e32_gpio *k, int n)
{
    u32 sel = (reg(k->iomux[n]) >> 12) & 7u;
    return sel == 0 && f0_is_gpio[n] ? 2u : sel;
}

static int signal_out(const struct e32_gpio *k, int sig, int *level, int *oe)
{
    if (sig >= 0 && sig < NSIG && k->sig_on[sig]) {
        *level = k->sig_lv[sig];
        *oe = k->sig_oe[sig];
        return 1;
    }
    switch (sig) {
    case SIG_U0TXD: case SIG_U1TXD: case SIG_U2TXD: /* idle high: the frames are the runtime's */
    case SIG_U0RTS: case SIG_U0DTR: case SIG_U1RTS: case SIG_U2RTS:
        *level = 1;
        *oe = 1;
        return 1;
    default:
        return 0;
    }
}

static int pin_out(const struct e32_gpio *k, int n, int *level, int *oe, int *af)
{
    u32 sel = mux_sel(k, n);
    *af = 0;
    if (n >= 34)
        return 0; /* input only */
    if (sel == 2) {
        u32 f = reg(k->fout[n]), sig = f & 0x1ffu;
        int lv = 0, en = 0;
        if (sig == SIG_GPIO) {
            lv = (int)(out64(k) >> n & 1u);
            en = (int)(en64(k) >> n & 1u);
        } else {
            if (!signal_out(k, (int)sig, &lv, &en))
                return 0;
            *af = 1;
            if (f & (1u << 10))
                en = (int)(en64(k) >> n & 1u);
        }
        if (f & (1u << 9))
            lv = !lv;
        if (f & (1u << 11))
            en = !en;
        *level = lv;
        *oe = en;
        return 1;
    }
    if (sel == 0 && n == 1) { /* U0TXD */
        *level = 1;
        *oe = 1;
        *af = 1;
        return 1;
    }
    return 0;
}

static void irqs(struct e32_gpio *k, u64 rise, u64 fall)
{
    u64 st = (u64)reg(k->status) | (u64)reg(k->status1) << 32, ints[4] = { 0, 0, 0, 0 };
    int n, c;
    for (n = 0; n < NPIN; n++) {
        u32 pr, type, ena;
        int in, hit;
        if (!pad(n))
            continue;
        pr = reg(k->pin[n]);
        type = (pr >> 7) & 7u;
        ena = (pr >> 13) & 0x1fu;
        in = (int)(k->input >> n & 1u);
        hit = (type == 1 && (rise >> n & 1u)) || (type == 2 && (fall >> n & 1u)) ||
              (type == 3 && ((rise | fall) >> n & 1u)) || (type == 4 && !in) || (type == 5 && in);
        if (hit)
            st |= 1ull << n;
        if (type && (st >> n & 1u))
            for (c = 0; c < 4; c++)
                if (ena & (1u << c))
                    ints[c] |= 1ull << n;
    }
    esk_rf_hw(k->status, (u32)st);
    esk_rf_hw(k->status1, (u32)(st >> 32) & 0xffu);
    esk_rf_hw(k->acpu, (u32)ints[0]);
    esk_rf_hw(k->anmi, (u32)ints[1]);
    esk_rf_hw(k->pcpu, (u32)ints[2]);
    esk_rf_hw(k->pnmi, (u32)ints[3]);
    if (k->acpu1) {
        esk_rf_hw(k->acpu1, (u32)(ints[0] >> 32));
        esk_rf_hw(k->anmi1, (u32)(ints[1] >> 32));
        esk_rf_hw(k->pcpu1, (u32)(ints[2] >> 32));
        esk_rf_hw(k->pnmi1, (u32)(ints[3] >> 32));
    }
    for (c = 0; c < 2; c++) {
        int cpu_int = ints[c ? 0 : 2] != 0, cpu_nmi = ints[c ? 1 : 3] != 0;
        if (cpu_int != k->irq[c]) {
            k->irq[c] = cpu_int;
            esk_esp32_source_cpu(k->s, c, 22, cpu_int);
        }
        if (cpu_nmi != k->irq_nmi[c]) {
            k->irq_nmi[c] = cpu_nmi;
            esk_esp32_source_cpu(k->s, c, 23, cpu_nmi);
        }
    }
}

/* the pads the matrix gives a UART's RX: the runtime reads their frames */
static u64 uart_rx_pins(const struct e32_gpio *k)
{
    static const int sig[3] = { 14, 17, 198 };
    u64 m = 0;
    int i;
    for (i = 0; i < 3; i++) {
        u32 f = reg(k->fin[sig[i]]), n = f & 0x3fu;
        if ((f & (1u << 7)) && n < NPIN)
            m |= 1ull << n;
    }
    return m;
}

static void settle(struct e32_gpio *k)
{
    struct esk_mach *s = k->s;
    u64 lvl = 0, in = 0, rise, fall, rx;
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
        u32 mux;
        int level = 0, oe = 0, af = 0, drive, ext = 0, pin, wpu, wpd, od;
        esk_mach_pin o;
        if (!pad(n))
            continue;
        mux = reg(k->iomux[n]);
        wpu = (mux & MUX_FUN_WPU) != 0;
        wpd = (mux & MUX_FUN_WPD) != 0;
        od = (reg(k->pin[n]) & (1u << 2)) != 0;
        if (!pin_out(k, n, &level, &oe, &af))
            oe = 0;
        drive = oe && !(od && level);
        memset(&o, 0, sizeof o);
        o.drive = oe ? level : -1;
        o.open_drain = od;
        o.pull = wpu && !wpd ? 1 : wpd && !wpu ? 2 : 0;
        o.af = af || (mux_sel(k, n) == 0 && n == 3) /* U0RXD */ || (rx >> n & 1u);
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
        lvl |= (u64)pin << n;
        if (mux & MUX_FUN_IE)
            in |= (u64)pin << n;
    }
    rise = k->known ? in & ~k->input : 0;
    fall = k->known ? ~in & k->input : 0;
    k->level = lvl;
    k->input = in;
    k->known = 1;
    esk_rf_hw(k->in, (u32)in);
    esk_rf_hw(k->in1, (u32)(in >> 32) & 0xffu);
    irqs(k, rise, fall);
    if (rise | fall)
        for (n = 0; n < k->nwatch; n++)
            k->watch[n].fn(k->watch[n].ctx);
    if (k->again)
        goto again;
    k->busy = 0;
}

void esk_esp32_signal(struct esk_mach *m, int sig, int level, int oe)
{
    struct e32_gpio *k = gpio_of(m);
    u8 lv = (u8)(level != 0), en = (u8)(oe != 0);
    if (!k || sig < 0 || sig >= NSIG)
        return;
    if (k->sig_on[sig] && k->sig_lv[sig] == lv && k->sig_oe[sig] == en)
        return;
    k->sig_on[sig] = 1;
    k->sig_lv[sig] = lv;
    k->sig_oe[sig] = en;
    if (k->known)
        settle(k);
}

int esk_esp32_signal_in(struct esk_mach *m, int sig)
{
    struct e32_gpio *k = gpio_of(m);
    u32 f;
    if (!k || sig < 0 || sig >= NSIG)
        return 0;
    f = reg(k->fin[sig]);
    if (f & (1u << 7)) {
        u32 n = f & 0x3fu;
        int v = n == 0x38u ? 1 : n == 0x30u ? 0 : n < NPIN ? (int)(k->input >> n & 1u) : 0;
        return (f & (1u << 6)) ? !v : v;
    }
    if (sig == 14 && mux_sel(k, 3) == 0) /* U0RXD */
        return (int)(k->input >> 3 & 1u);
    /* the table's defaults: I2CEXT0/1's SCL and SDA idle high */
    return sig == 29 || sig == 30 || sig == 95 || sig == 96;
}

int esk_esp32_signal_watch(struct esk_mach *m, void (*fn)(void *ctx), void *ctx)
{
    struct e32_gpio *k = gpio_of(m);
    if (!k || k->nwatch >= (int)(sizeof k->watch / sizeof k->watch[0]))
        return -1;
    k->watch[k->nwatch].fn = fn;
    k->watch[k->nwatch].ctx = ctx;
    k->nwatch++;
    return 0;
}

void esk_esp32_pins_changed(struct esk_mach *m)
{
    struct e32_gpio *k = gpio_of(m);
    if (k)
        settle(k);
}

int esk_esp32_uart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit)
{
    static const int txs[3] = { SIG_U0TXD, SIG_U1TXD, SIG_U2TXD }, rxs[3] = { 14, 17, 198 };
    struct e32_gpio *k = gpio_of(m);
    int u, n;
    if (!k || strncmp(unit, "UART", 4) || unit[4] < '0' || unit[4] > '2' || unit[5])
        return -1;
    u = unit[4] - '0';
    if (tx) {
        for (n = 0; n < 34; n++) {
            u32 sel;
            if (!pad(n))
                continue;
            sel = mux_sel(k, n);
            if ((sel == 2 && (int)(reg(k->fout[n]) & 0x1ffu) == txs[u]) || (u == 0 && sel == 0 && n == 1)) {
                *port = n / 16;
                *bit = n % 16;
                return 0;
            }
        }
        return -1;
    }
    {
        u32 f = reg(k->fin[rxs[u]]);
        if (f & (1u << 7)) {
            n = (int)(f & 0x3fu);
            if (!pad(n))
                return -1;
        } else if (u == 0 && mux_sel(k, 3) == 0) {
            n = 3;
        } else {
            return -1;
        }
        *port = n / 16;
        *bit = n % 16;
        return 0;
    }
}

u32 esk_esp32_strap(struct esk_mach *m)
{
    struct e32_gpio *k = gpio_of(m);
    return k ? k->latched : 0x13u;
}

/* ---- the GPIO block ---------------------------------------------------- */

static u32 gp_read(void *ctx, u32 off, int n)
{
    struct e32_gpio *k = (struct e32_gpio *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void set_clr(struct esk_rf_reg *r, int set, u32 v, u32 mask)
{
    esk_rf_hw(r, set ? reg(r) | (v & mask) : reg(r) & ~v);
}

static void gp_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_gpio *k = (struct e32_gpio *)ctx;
    const u32 lo = (u32)(PADS & 0xffffffffull), hi = (u32)(PADS >> 32) & 0xffu;
    switch (off) {
    case 0x38: case 0x3C: case 0x40: case 0x60: case 0x64: case 0x68: case 0x6C:
        return; /* read-only */
    case 0x08: case 0x0C: set_clr(k->out, off == 0x08, v, lo); break;
    case 0x14: case 0x18: set_clr(k->out1, off == 0x14, v, hi); break;
    case 0x24: case 0x28: set_clr(k->enable, off == 0x24, v, lo); break;
    case 0x30: case 0x34: set_clr(k->enable1, off == 0x30, v, hi); break;
    case 0x48: case 0x4C: set_clr(k->status, off == 0x48, v, lo); break;
    case 0x54: case 0x58: set_clr(k->status1, off == 0x54, v, hi); break;
    default:
        esk_rf_write(k->p, off, n, v);
    }
    settle(k);
}

static int pin_level(struct e32_gpio *k, int gpio, int dflt)
{
    int level = dflt;
    if (k->s->host.pin_in && k->s->host.pin_in(k->s->host.ctx, gpio / 16, gpio % 16, &level))
        return level != 0;
    return dflt;
}

static void gp_reset(void *ctx)
{
    struct e32_gpio *k = (struct e32_gpio *)ctx;
    int c;
    if (!k->have || k->s->reset_cause == ESK_RESET_POWER || k->s->reset_cause == ESK_RESET_PIN) {
        k->latched = (u32)pin_level(k, 12, 0) << 5 | (u32)pin_level(k, 0, 1) << 4 |
                     (u32)pin_level(k, 2, 0) << 3 | (u32)pin_level(k, 4, 0) << 2 |
                     (u32)pin_level(k, 15, 1) << 1 | (u32)pin_level(k, 5, 1);
        k->have = 1;
    }
    esk_rf_hw(k->strap, k->latched);
    for (c = 0; c < 2; c++) {
        k->irq[c] = k->irq_nmi[c] = 0;
        esk_esp32_source_cpu(k->s, c, 22, 0);
        esk_esp32_source_cpu(k->s, c, 23, 0);
    }
    k->known = 0;
    settle(k);
}

const struct esk_dev_ops esk_esp32_gpio_ops = {
    "esp32-gpio", gp_read, gp_write, gp_reset, 0, 0,
};

/* ---- IO MUX ------------------------------------------------------------ */

static u32 mx_read(void *ctx, u32 off, int n)
{
    struct esk_rf_periph *p = (struct esk_rf_periph *)ctx;
    return esk_rf_read(p, off, n);
}

static void mx_write(void *ctx, u32 off, int n, u32 v)
{
    struct esk_rf_periph *p = (struct esk_rf_periph *)ctx;
    esk_rf_write(p, off, n, v);
    esk_esp32_pins_changed(p->sim);
}

static void mx_reset(void *ctx)
{
    struct esk_rf_periph *p = (struct esk_rf_periph *)ctx;
    char name[16];
    int i;
    for (i = 0; i < NPIN; i++) {
        struct esk_rf_reg *r;
        u32 v = 2u << 10; /* FUN_DRV 2 */
        snprintf(name, sizeof name, "GPIO%d", i);
        r = esk_rf_reg(p, name);
        if (!r || !pad(i))
            continue;
        switch (reset_kind[i]) {
        case 1: v |= MUX_FUN_IE; break;
        case 2: v |= MUX_FUN_IE | MUX_FUN_WPD; break;
        case 3: v |= MUX_FUN_IE | MUX_FUN_WPU; break;
        default: break;
        }
        esk_rf_hw(r, v);
    }
    esk_esp32_pins_changed(p->sim);
}

const struct esk_dev_ops esk_esp32_iomux_ops = {
    "esp32-iomux", mx_read, mx_write, mx_reset, 0, 0,
};

void *esk_esp32_iomux_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    ES_UNUSED(s);
    return esk_rf_reg(p, "GPIO39") ? p : 0;
}

void *esk_esp32_gpio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_gpio *k = (struct e32_gpio *)esk_mach_alloc(s, sizeof *k);
    char name[32];
    int i;
    if (!k)
        return 0;
    k->s = s;
    k->p = p;
    k->mux = esk_rf_periph(s->rf, "IO_MUX");
    k->out = esk_rf_reg(p, "OUT");
    k->out1 = esk_rf_reg(p, "OUT1");
    k->enable = esk_rf_reg(p, "ENABLE");
    k->enable1 = esk_rf_reg(p, "ENABLE1");
    k->strap = esk_rf_reg(p, "STRAP");
    k->in = esk_rf_reg(p, "IN");
    k->in1 = esk_rf_reg(p, "IN1");
    k->status = esk_rf_reg(p, "STATUS");
    k->status1 = esk_rf_reg(p, "STATUS1");
    k->acpu = esk_rf_reg(p, "ACPU_INT");
    k->anmi = esk_rf_reg(p, "ACPU_NMI_INT");
    k->pcpu = esk_rf_reg(p, "PCPU_INT");
    k->pnmi = esk_rf_reg(p, "PCPU_NMI_INT");
    k->acpu1 = esk_rf_reg(p, "ACPU_INT1");
    k->anmi1 = esk_rf_reg(p, "ACPU_NMI_INT1");
    k->pcpu1 = esk_rf_reg(p, "PCPU_INT1");
    k->pnmi1 = esk_rf_reg(p, "PCPU_NMI_INT1");
    if (!k->mux || !k->out || !k->out1 || !k->enable || !k->enable1 || !k->strap || !k->in || !k->in1 ||
        !k->status || !k->status1 || !k->acpu || !k->anmi || !k->pcpu || !k->pnmi)
        return 0;
    for (i = 0; i < NPIN; i++) {
        if (!pad(i))
            continue;
        snprintf(name, sizeof name, "PIN%d", i);
        k->pin[i] = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "FUNC%d_OUT_SEL_CFG", i);
        k->fout[i] = esk_rf_reg(p, name);
        snprintf(name, sizeof name, "GPIO%d", i);
        k->iomux[i] = esk_rf_reg(k->mux, name);
        if (!k->pin[i] || !k->fout[i] || !k->iomux[i])
            return 0;
    }
    for (i = 0; i < NSIG; i++) {
        snprintf(name, sizeof name, "FUNC%d_IN_SEL_CFG", i);
        k->fin[i] = esk_rf_reg(p, name);
    }
    s->pins_changed = esk_esp32_pins_changed;
    s->uart_pin = esk_esp32_uart_pin;
    return k;
}
