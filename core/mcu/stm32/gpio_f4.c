/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* gpio_f4.c -- an STM32F2/F4/F7-style GPIO port (MODER, OTYPER, OSPEEDR,
 * PUPDR, IDR, ODR, BSRR, LCKR, AFRL, AFRH: RM0390 7), over the register
 * file.
 *
 *   - MODER gives each pin's mode: input (00), general-purpose output (01),
 *     alternate function (10) or analog (11). OTYPER makes an output or an
 *     alternate function push-pull (0) or open-drain (1); OSPEEDR's speed
 *     (low to very high) sets the pin's output resistance and edge rate
 *     from the description's io.electrical; PUPDR's pull-up (01) or
 *     pull-down (10) is on in every mode but analog (11 is reserved: none);
 *   - BSRR's low half sets ODR's bits and its high half resets them, the set
 *     winning when both are written; it reads 0;
 *   - IDR is what each pin reads through its Schmitt trigger, sampled as it
 *     is read: an output's level (an open-drain output's 1 lets go, and the
 *     pin reads what the circuit makes of it, else the pull, else 0,
 *     floating being EmbSIM's choice), an input's or alternate function's
 *     the circuit's, else the pull, else 0; analog mode reads 0;
 *   - an alternate function is the one AFRL or AFRH selects among those the
 *     description's io.alternate gives the pin; it drives the pin when its
 *     peripheral's model does (a timer's channel, tim.c; a USART's TX, RTS
 *     or CK, usart.c; an SPI's SCK, MOSI, MISO or NSS, spi.c; an I2C's SCL,
 *     SDA or SMBA, i2c.c, which an open-drain pin lets go for 1), else the
 *     pin is let go and reads what the circuit drives; the pin's level
 *     goes to the function's input (a timer's channel, ETR, BKIN, a
 *     USART's nCTS, an SPI's SCK and NSS, an I2C's lines); the debug
 *     port's (AF0 on PA13, PA14, PA15, PB3 and PB4, as the reset values of
 *     MODER and PUPDR select them) among them: with no debugger attached
 *     its pins are inputs with their pulls;
 *   - LCKR's lock sequence (write LCKK 1, 0, 1 with the same LCK bits, then
 *     read) sets LCKK until the next reset, and the locked pins' MODER,
 *     OTYPER, OSPEEDR, PUPDR and AFR bits keep their values; a write out
 *     of the sequence's order, or with other LCK bits, starts it again;
 *   - each pin's configuration as it changes goes to the MCU runtime
 *     (esk_mach_host.pin_cfg), what the circuit drives comes back into IDR
 *     (pin_in), and IDR's changes go to EXTI's line on the port SYSCFG's
 *     EXTICR picks.
 *
 *   - an enabled DAC channel drives PA4 or PA5 (dac_f4.c), the RTC's
 *     RTC_OUT drives PC13 (rtc_f4.c), whatever the pin's mode; PC13's level
 *     goes to the RTC's RTC_AF1 input;
 *   - PWR's (pwr_f4.c): an enabled WKUP pin (PA0, PC13) is an input with a
 *     pull-down whatever its mode, and its level goes to PWR; in Standby
 *     every pin lets go (an input without its pull) but those and RTC_OUT;
 *
 * Not modelled: EVENTOUT (AF15, the pulse SEV sends out), the I/O
 * compensation cell's effect on the edges (SYSCFG's CMPCR only reports it
 * ready), the 5 V tolerant pins' clamps. */
#include <string.h>

#include "mcu/stm32/stm32.h"

#define LCKK (1u << 16)

struct f4gpio {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *moder, *otyper, *ospeedr, *pupdr, *idr, *odr, *bsrr, *lckr, *afrl, *afrh;
    int port;             /* 0 for A */
    esk_mach_pin out[16]; /* what each pin was told to be */
    int af_in[16];        /* the level last given to a pin's AF inputs, -1 none */
    int known;
    int lck_step;         /* the lock sequence's writes so far (3: a read locks) */
    u32 lck_bits;
    int updating, again;  /* update() under way, and asked for again */
};

static u32 field(const struct esk_rf_reg *r, int pin, int width)
{
    return (u32)(r->value >> (width * pin)) & ((1u << width) - 1);
}

static u32 af_of(const struct f4gpio *g, int pin)
{
    return pin < 8 ? field(g->afrl, pin, 4) : field(g->afrh, pin - 8, 4);
}

/* the level a peripheral's model drives on its alternate function `af` of
 * the pin, or -1 */
static int af_level(const struct f4gpio *g, int pin, u32 af)
{
    const char *sig;
    int k, v;
    for (k = 0; (sig = esk_rf_af_signal(g->s->rf, g->port, pin, (int)af, k)) != 0; k++)
        if ((v = esk_stm32_tim_signal(g->s, sig)) >= 0 || (v = esk_stm32_usart_signal(g->s, sig)) >= 0 ||
            (v = esk_stm32_spi_signal(g->s, sig)) >= 0 || (v = esk_stm32_i2c_signal(g->s, sig)) >= 0)
            return v;
    return -1;
}

/* a pin in alternate-function mode gives its level to its signals' inputs
 * (a timer's channel, ETR or BKIN, a USART's nCTS, an SPI's SCK, NSS, an
 * I2C's SCL, SDA and SMBA) */
static void af_input(struct f4gpio *g, int pin, u32 af, int level)
{
    const char *sig;
    int k;
    for (k = 0; (sig = esk_rf_af_signal(g->s->rf, g->port, pin, (int)af, k)) != 0; k++) {
        esk_stm32_tim_signal_input(g->s, sig, level);
        esk_stm32_usart_signal_input(g->s, sig, level);
        esk_stm32_spi_signal_input(g->s, sig, level);
        esk_stm32_i2c_signal_input(g->s, sig, level);
    }
}

static void update_once(struct f4gpio *g)
{
    struct esk_mach *s = g->s;
    u32 idr = 0, odr = (u32)g->odr->value, old = (u32)g->idr->value & 0xffffu;
    int i, standby = esk_stm32f4_pwr_standby(s);
    for (i = 0; i < 16; i++) {
        u32 mode = field(g->moder, i, 2), pupd = field(g->pupdr, i, 2);
        int od = (int)field(g->otyper, i, 1), lvl, ext = 0;
        esk_mach_pin o;
        memset(&o, 0, sizeof o);
        o.drive = -1;
        if (standby || esk_stm32f4_pwr_wkup_pin(s, g->port, i)) {
            /* Standby lets the pins go; an enabled WKUP pin is an input
             * with a pull-down (pwr_f4.c) */
            o.pull = esk_stm32f4_pwr_wkup_pin(s, g->port, i) ? 2 : 0;
        } else if (mode == 3) {
            o.analog = 1;
        } else {
            o.pull = pupd == 1 ? 1 : pupd == 2 ? 2 : 0;
            if (mode == 1 || mode == 2) {
                int d = mode == 1 ? (int)(odr >> i & 1) : af_level(g, i, af_of(g, i));
                o.open_drain = od;
                o.af = mode == 2;
                o.speed = (int)field(g->ospeedr, i, 2) + 1;
                if (d == 0 || (d == 1 && !od))
                    o.drive = d;
            }
        }
        if (g->port == 2 && i == 13) {
            int rod, v = esk_stm32_rtc_out(s, &rod); /* RTC_OUT, whatever the mode */
            if (v >= 0) {
                o.analog = 0;
                o.open_drain = rod;
                o.drive = v == 0 || !rod ? v : -1;
            }
        }
        if (!standby && esk_stm32f4_dac_out(s, g->port, i, &o.volts, &o.aout_r)) {
            o.aout = 1; /* an enabled DAC channel takes its pin, whatever its mode */
            o.drive = -1;
        }
        if (o.analog)
            lvl = 0;
        else if (o.aout)
            lvl = o.volts > (s->vdd > 0.0 ? s->vdd : 3.3) / 2.0;
        else if (o.drive >= 0)
            lvl = o.drive;
        else if (s->host.pin_in && s->host.pin_in(s->host.ctx, g->port, i, &ext))
            lvl = ext != 0;
        else
            lvl = o.pull == 1;
        idr |= (u32)lvl << i;
        if (mode == 2 && lvl != g->af_in[i]) {
            g->af_in[i] = lvl;
            af_input(g, i, af_of(g, i), lvl);
        } else if (mode != 2) {
            g->af_in[i] = -1;
        }
        if (s->host.pin_cfg && (!g->known || memcmp(&g->out[i], &o, sizeof o) != 0))
            s->host.pin_cfg(s->host.ctx, g->port, i, &o);
        g->out[i] = o;
    }
    g->known = 1;
    if (old != idr) {
        esk_rf_hw(g->idr, (g->idr->value & ~(u64)0xffffu) | idr);
        for (i = 0; i < 16; i++)
            if (((old ^ idr) >> i & 1) && esk_stm32f4_exti_port(s, i) == g->port)
                esk_stm32_exti_input(s, i, (int)(idr >> i & 1));
        if ((old ^ idr) >> 13 & 1 && g->port == 2)
            esk_stm32_rtc_af1(s, (int)(idr >> 13 & 1)); /* RTC_AF1: timestamp, tamper */
        if ((g->port == 0 && ((old ^ idr) & 1u)) || (g->port == 2 && ((old ^ idr) >> 13 & 1)))
            esk_stm32f4_pwr_pin_edge(s, g->port, g->port ? 13 : 0, (int)(idr >> (g->port ? 13 : 0) & 1));
    }
}

/* again while a peripheral that heard a pin changed its own pins on this
 * port (a slave SPI's MISO after its SCK's edge) */
static void update(struct f4gpio *g)
{
    int round;
    if (g->updating) {
        g->again = 1;
        return;
    }
    g->updating = 1;
    for (round = 0; round < 8; round++) {
        g->again = 0;
        update_once(g);
        if (!g->again)
            break;
    }
    g->updating = 0;
}

/* the bits of `r` the locked pins own, `width` bits a pin from `first` */
static u64 locked_mask(u32 locked, int width, int first, int count)
{
    u64 m = 0;
    int i;
    for (i = 0; i < count; i++)
        if (locked >> (first + i) & 1)
            m |= (u64)((1u << width) - 1) << (width * i);
    return m;
}

static void keep_locked(struct f4gpio *g, struct esk_rf_reg *r, u64 was)
{
    u32 locked = (g->lckr->value & LCKK) ? (u32)g->lckr->value & 0xffffu : 0;
    u64 m;
    if (!locked)
        return;
    if (r == g->otyper)
        m = locked_mask(locked, 1, 0, 16);
    else if (r == g->afrl)
        m = locked_mask(locked, 4, 0, 8);
    else if (r == g->afrh)
        m = locked_mask(locked, 4, 8, 8);
    else
        m = locked_mask(locked, 2, 0, 16);
    if ((r->value ^ was) & m)
        esk_rf_hw(r, (r->value & ~m) | (was & m));
}

/* LCKR: the lock key sequence (RM0390 7.4.8) */
static void lckr_write(struct f4gpio *g, u32 v)
{
    u32 bits = v & 0xffffu;
    int key = (v & LCKK) != 0;
    if (g->lckr->value & LCKK)
        return; /* locked until reset */
    if (g->lck_step == 0 || (g->lck_step == 3 && key) || bits != g->lck_bits) {
        g->lck_step = key ? 1 : 0;
    } else if (g->lck_step == 1 && !key) {
        g->lck_step = 2;
    } else if (g->lck_step == 2 && key) {
        g->lck_step = 3;
    } else {
        g->lck_step = key ? 1 : 0;
    }
    g->lck_bits = bits;
    esk_rf_hw(g->lckr, bits);
}

static u32 f4gpio_read(void *ctx, u32 off, int n)
{
    struct f4gpio *g = (struct f4gpio *)ctx;
    if (g->s->bus.debug)
        return esk_rf_read(g->p, off, n);
    if (off == esk_rf_off(g->lckr) && g->lck_step == 3) {
        esk_rf_hw(g->lckr, (u64)g->lck_bits | LCKK);
        g->lck_step = 0;
    }
    update(g);
    return esk_rf_read(g->p, off, n);
}

static void f4gpio_write(void *ctx, u32 off, int n, u32 v)
{
    struct f4gpio *g = (struct f4gpio *)ctx;
    struct esk_rf_reg *cfg[6];
    u64 was[6], odr;
    int i;
    if (off - esk_rf_off(g->lckr) < 4) {
        if (n == 4)
            lckr_write(g, v);
        else
            g->lck_step = 0; /* the sequence's writes are words */
        return;
    }
    cfg[0] = g->moder;
    cfg[1] = g->otyper;
    cfg[2] = g->ospeedr;
    cfg[3] = g->pupdr;
    cfg[4] = g->afrl;
    cfg[5] = g->afrh;
    for (i = 0; i < 6; i++)
        was[i] = cfg[i]->value;
    esk_rf_write(g->p, off, n, v);
    for (i = 0; i < 6; i++)
        keep_locked(g, cfg[i], was[i]);
    if (off - esk_rf_off(g->bsrr) < 4) {
        /* a halfword or byte write sets or resets its own lanes' bits (the
         * older HAL's BSRRL and BSRRH) */
        u32 w = (n < 4 ? v & ((1u << (8 * n)) - 1) : v) << (8 * (off - esk_rf_off(g->bsrr)));
        u32 set = w & 0xffffu, reset = w >> 16;
        odr = g->odr->value;
        odr = (odr & ~(u64)reset) | set; /* a set wins over a reset */
        if (odr != g->odr->value)
            esk_rf_hw(g->odr, odr);
        esk_rf_hw(g->bsrr, 0);
    }
    update(g);
}

static void f4gpio_reset(void *ctx)
{
    struct f4gpio *g = (struct f4gpio *)ctx;
    int i;
    g->known = 0;
    g->lck_step = 0;
    for (i = 0; i < 16; i++)
        g->af_in[i] = -1;
    update(g);
}

const struct esk_dev_ops esk_stm32f4_gpio_ops = {
    "stm32f4-gpio", f4gpio_read, f4gpio_write, f4gpio_reset, 0, 0,
};

int esk_stm32f4_af_pin(struct esk_mach *s, const char *sig)
{
    int k, at, af;
    for (k = 0; (at = esk_rf_af_pin(s->rf, sig, k, &af)) >= 0; k++) {
        char name[8] = "GPIOx";
        struct esk_rf_periph *p;
        const struct f4gpio *g;
        int pin = at % 32;
        name[4] = (char)('A' + at / 32);
        p = esk_rf_periph(s->rf, name);
        if (!p || p->ops != &esk_stm32f4_gpio_ops || esk_rf_gated(p))
            continue;
        g = (const struct f4gpio *)p->ctx;
        if (field(g->moder, pin, 2) == 2 && af_of(g, pin) == (u32)af)
            return at;
    }
    return -1;
}

void esk_stm32f4_pins_changed(struct esk_mach *s)
{
    int i;
    for (i = 0; s->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(s->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_stm32f4_gpio_ops)
            update((struct f4gpio *)p->ctx); /* its clock off, the pins keep their setting */
    }
    esk_stm32f4_pwr_pins_settled(s);
}

void esk_stm32f4_port_changed(struct esk_mach *s, int port)
{
    char name[8] = "GPIOx";
    struct esk_rf_periph *p;
    name[4] = (char)('A' + port);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    if (p && p->ops == &esk_stm32f4_gpio_ops)
        update((struct f4gpio *)p->ctx);
}

void *esk_stm32f4_gpio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct f4gpio g, *c;
    int i;
    memset(&g, 0, sizeof g);
    g.s = s;
    g.p = p;
    g.moder = esk_rf_reg(p, "MODER");
    g.otyper = esk_rf_reg(p, "OTYPER");
    g.ospeedr = esk_rf_reg(p, "OSPEEDR");
    g.pupdr = esk_rf_reg(p, "PUPDR");
    g.idr = esk_rf_reg(p, "IDR");
    g.odr = esk_rf_reg(p, "ODR");
    g.bsrr = esk_rf_reg(p, "BSRR");
    g.lckr = esk_rf_reg(p, "LCKR");
    g.afrl = esk_rf_reg(p, "AFRL");
    g.afrh = esk_rf_reg(p, "AFRH");
    if (!g.moder || !g.otyper || !g.ospeedr || !g.pupdr || !g.idr || !g.odr || !g.bsrr || !g.lckr ||
        !g.afrl || !g.afrh || strncmp(p->name, "GPIO", 4) || p->name[4] < 'A' || p->name[4] > 'K' ||
        p->name[5])
        return 0; /* not this layout: the registers alone */
    g.port = p->name[4] - 'A';
    for (i = 0; i < 16; i++)
        g.af_in[i] = -1;
    s->pins_changed = esk_stm32f4_pins_changed;
    c = (struct f4gpio *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = g;
    return c;
}
