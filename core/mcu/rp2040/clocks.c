/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* clocks.c -- the RP2040's resets, its clocks, its power-on state machine
 * and its watchdog, over the register file (RP2040 datasheet, 2.14-2.18).
 *
 * RESETS. RESET's bits hold their peripherals in reset (each field is
 * named for its peripheral; USBCTRL is USB's and USB_DPRAM's): while held,
 * the peripheral's registers read their reset values and ignore writes,
 * with a warning (mcu.held_in_reset); a peripheral put into reset has its
 * registers and its model reset. RESET_DONE is ~RESET at once.
 *
 * The clocks. XOSC is the board's crystal (the Pico's 12 MHz): enabled
 * (CTRL.ENABLE anything but DISABLE, 0xd1e), it is STABLE once
 * STARTUP.DELAY x 256 (x 4 with X4) of its cycles have passed; COUNT
 * counts down at its frequency. ROSC runs at 6.5 MHz. A PLL runs when
 * PWR's PD and VCOPD are clear, at XOSC / REFDIV x FBDIV / (POSTDIV1 x
 * POSTDIV2) once POSTDIVPD is clear too, and is LOCKed at once when its
 * feedback divider is in range (16-320). The clock generators: clk_ref and
 * clk_sys switch glitchlessly (SELECTED is one-hot of SRC at once), the
 * others run while ENABLE is set; each divides by DIV (INT and FRAC, INT 0
 * for 2^16), and FC0 measures any of them at once. clk_sys is the core's
 * clock.
 *
 * The watchdog. TICK's generator divides clk_ref by CYCLES into the
 * microsecond tick the timer counts (timer.c); its counter, loaded by LOAD,
 * goes down by two each tick while CTRL.ENABLE (the RP2040-E1 erratum), and
 * at zero -- or on CTRL.TRIGGER -- resets the chip (the runtime's system
 * reset: the registers, REASON and SCRATCH0-7 among them, start again).
 *
 * And where RP2040.svd's reset values are not the part's: SYSINFO's
 * CHIP_ID (0x20002927, a B2) and PLATFORM (ASIC); XOSC's CTRL (DISABLE,
 * 1-15 MHz); ROSC's STATUS (running); PSM's DONE (every domain powered,
 * less those FRCE_OFF holds off); CHIP_RESET's HAD_POR.
 *
 * Not modelled: the ROSC's frequency controls (FREQA, FREQB, DIV), the
 * PLLs' lock time, the clock generators' stop and start cycles, GPOUT0-3,
 * GPIN, the resus, DORMANT and SLEEP, the watchdog's PSM and RESETS
 * selections (WDSEL). */
#include <stdio.h>
#include <string.h>

#include "mcu/rp2040/rp2040.h"

u32 esk_rp_fld(const struct esk_rf_reg *r, u64 mask)
{
    int sh = 0;
    if (!r || !mask)
        return 0;
    while (!(mask >> sh & 1))
        sh++;
    return (u32)((r->value & mask) >> sh);
}

u64 esk_rp_put(u64 v, u64 mask, u32 x)
{
    int sh = 0;
    if (!mask)
        return v;
    while (!(mask >> sh & 1))
        sh++;
    return (v & ~mask) | (((u64)x << sh) & mask);
}

void *esk_rp_model(struct esk_mach *m, const char *name, const struct esk_dev_ops *ops)
{
    struct esk_rf_periph *p = m->rf ? esk_rf_periph(m->rf, name) : 0;
    return p && p->ops == ops ? p->ctx : 0;
}

#define fld esk_rp_fld

u32 esk_rp2040_xtal_hz(struct esk_mach *m) { return m->hse_hz ? m->hse_hz : RP_XOSC_HZ; }

/* ---- XOSC ------------------------------------------------------------------ */

#define XOSC_DISABLE 0xd1eu
#define XOSC_ENABLE 0xfabu

struct xosc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *ctrl, *status, *startup, *count;
    u64 m_enable, m_stable, m_enabled, m_badwrite, m_delay, m_x4;
    u64 stable_at; /* the cycle STABLE sets, once enabled */
    u64 count_at;  /* COUNT was written at this cycle */
    u32 count_from;
};

static int xosc_on(const struct xosc *x) { return fld(x->ctrl, x->m_enable) != XOSC_DISABLE; }

static int xosc_stable(const struct xosc *x) { return xosc_on(x) && x->s->cycles >= x->stable_at; }

/* crystal cycles to core cycles, at the clk_sys now */
static u64 xtal_to_cycles(struct esk_mach *s, u64 n)
{
    u64 hz = esk_rp2040_hz(s, RP_CLK_SYS), x = esk_rp2040_xtal_hz(s);
    return (n * hz + x - 1) / x;
}

static void xosc_status(struct xosc *x)
{
    u64 v = x->status->value & ~(x->m_stable | x->m_enabled);
    if (xosc_on(x))
        v |= x->m_enabled;
    if (xosc_stable(x))
        v |= x->m_stable;
    if (v != x->status->value)
        esk_rf_hw(x->status, v);
}

static u32 xosc_read(void *ctx, u32 off, int n)
{
    struct xosc *x = (struct xosc *)ctx;
    if (off == esk_rf_off(x->status))
        xosc_status(x);
    if (off == esk_rf_off(x->count) && !x->s->bus.debug) {
        /* it counts down at the crystal's frequency */
        u64 hz = esk_rp2040_hz(x->s, RP_CLK_SYS), gone = 0;
        if (hz && xosc_stable(x))
            gone = (x->s->cycles - x->count_at) * esk_rp2040_xtal_hz(x->s) / hz;
        esk_rf_hw(x->count, gone >= x->count_from ? 0 : x->count_from - gone);
    }
    return esk_rf_read(x->p, off, n);
}

static void xosc_write(void *ctx, u32 off, int n, u32 v)
{
    struct xosc *x = (struct xosc *)ctx;
    int was = xosc_on(x);
    esk_rf_write(x->p, off, n, v);
    if (off == esk_rf_off(x->ctrl)) {
        u32 e = fld(x->ctrl, x->m_enable);
        if (e != XOSC_ENABLE && e != XOSC_DISABLE)
            esk_rf_hw(x->status, x->status->value | x->m_badwrite);
        /* the range resets to 1-15 MHz and cannot be changed */
        esk_rf_hw(x->ctrl, (x->ctrl->value & x->m_enable) | 0xaa0u);
        if (!was && xosc_on(x)) {
            u64 d = (u64)fld(x->startup, x->m_delay) * 256u;
            if (fld(x->startup, x->m_x4))
                d *= 4u;
            x->stable_at = x->s->cycles + xtal_to_cycles(x->s, d);
        }
        esk_rp2040_clock_change(x->s);
    }
    if (off == esk_rf_off(x->count)) {
        x->count_at = x->s->cycles;
        x->count_from = (u32)x->count->value;
    }
    xosc_status(x);
}

static void xosc_reset(void *ctx)
{
    struct xosc *x = (struct xosc *)ctx;
    x->stable_at = 0;
    x->count_from = 0;
    xosc_status(x);
}

const struct esk_dev_ops esk_rp2040_xosc_ops = {
    "rp2040-xosc", xosc_read, xosc_write, xosc_reset, 0, 0,
};

void *esk_rp2040_xosc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct xosc *x = (struct xosc *)esk_mach_alloc(s, sizeof *x);
    if (!x)
        return 0;
    memset(x, 0, sizeof *x);
    x->s = s;
    x->p = p;
    x->ctrl = esk_rf_reg(p, "CTRL");
    x->status = esk_rf_reg(p, "STATUS");
    x->startup = esk_rf_reg(p, "STARTUP");
    x->count = esk_rf_reg(p, "COUNT");
    if (!x->ctrl || !x->status || !x->startup || !x->count)
        return 0;
    x->m_enable = esk_rf_mask(x->ctrl, "ENABLE");
    x->m_stable = esk_rf_mask(x->status, "STABLE");
    x->m_enabled = esk_rf_mask(x->status, "ENABLED");
    x->m_badwrite = esk_rf_mask(x->status, "BADWRITE");
    x->m_delay = esk_rf_mask(x->startup, "DELAY");
    x->m_x4 = esk_rf_mask(x->startup, "X4");
    /* CTRL out of reset: DISABLE, 1-15 MHz (the datasheet's; the SVD says 0) */
    esk_rf_set_reset(x->ctrl, esk_rp_put(0xaa0u, x->m_enable, XOSC_DISABLE));
    return x;
}

static u32 xosc_hz(struct esk_mach *s)
{
    struct xosc *x = (struct xosc *)esk_rp_model(s, "XOSC", &esk_rp2040_xosc_ops);
    return x && xosc_stable(x) ? esk_rp2040_xtal_hz(s) : 0;
}

/* ---- ROSC: running at its nominal frequency while enabled ---------------- */

struct rosc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *ctrl, *status;
    u64 m_enable, m_stable, m_enabled;
};

static u32 rosc_hz(struct esk_mach *s)
{
    struct rosc *r = (struct rosc *)esk_rp_model(s, "ROSC", &esk_rp2040_rosc_ops);
    if (r && fld(r->ctrl, r->m_enable) == XOSC_DISABLE)
        return 0;
    return RP_ROSC_HZ;
}

static void rosc_status(struct rosc *r)
{
    u64 v = r->status->value & ~(r->m_stable | r->m_enabled);
    if (fld(r->ctrl, r->m_enable) != XOSC_DISABLE)
        v |= r->m_stable | r->m_enabled;
    if (v != r->status->value)
        esk_rf_hw(r->status, v);
}

static u32 rosc_read(void *ctx, u32 off, int n)
{
    struct rosc *r = (struct rosc *)ctx;
    rosc_status(r);
    return esk_rf_read(r->p, off, n);
}

static void rosc_write(void *ctx, u32 off, int n, u32 v)
{
    struct rosc *r = (struct rosc *)ctx;
    esk_rf_write(r->p, off, n, v);
    rosc_status(r);
    esk_rp2040_clock_change(r->s);
}

static void rosc_reset(void *ctx) { rosc_status((struct rosc *)ctx); }

const struct esk_dev_ops esk_rp2040_rosc_ops = {
    "rp2040-rosc", rosc_read, rosc_write, rosc_reset, 0, 0,
};

void *esk_rp2040_rosc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct rosc *r = (struct rosc *)esk_mach_alloc(s, sizeof *r);
    if (!r)
        return 0;
    memset(r, 0, sizeof *r);
    r->s = s;
    r->p = p;
    r->ctrl = esk_rf_reg(p, "CTRL");
    r->status = esk_rf_reg(p, "STATUS");
    if (!r->ctrl || !r->status)
        return 0;
    r->m_enable = esk_rf_mask(r->ctrl, "ENABLE");
    r->m_stable = esk_rf_mask(r->status, "STABLE");
    r->m_enabled = esk_rf_mask(r->status, "ENABLED");
    esk_rf_set_reset(r->status, r->status->reset | r->m_stable | r->m_enabled);
    return r;
}

/* ---- the PLLs ---------------------------------------------------------------- */

struct pll {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cs, *pwr, *fbdiv, *prim;
    u64 m_lock, m_bypass, m_refdiv, m_pd, m_vcopd, m_postdivpd, m_fbdiv, m_pd1, m_pd2;
};

static int pll_running(const struct pll *q)
{
    u32 fb = fld(q->fbdiv, q->m_fbdiv), ref = fld(q->cs, q->m_refdiv);
    return !fld(q->pwr, q->m_pd) && !fld(q->pwr, q->m_vcopd) && ref && fb >= 16 && fb <= 320 &&
           xosc_hz(q->s);
}

/* its output, in Hz */
static u32 pll_hz(const struct pll *q)
{
    u64 xosc;
    u32 ref, d1, d2;
    if (!q)
        return 0;
    xosc = xosc_hz(q->s);
    ref = fld(q->cs, q->m_refdiv);
    if (fld(q->cs, q->m_bypass))
        return ref ? (u32)(xosc / ref) : 0;
    if (!pll_running(q) || fld(q->pwr, q->m_postdivpd))
        return 0;
    d1 = fld(q->prim, q->m_pd1);
    d2 = fld(q->prim, q->m_pd2);
    if (!d1 || !d2)
        return 0;
    return (u32)(xosc / ref * fld(q->fbdiv, q->m_fbdiv) / (d1 * d2));
}

static void pll_status(struct pll *q)
{
    u64 v = q->cs->value & ~q->m_lock;
    if (pll_running(q))
        v |= q->m_lock;
    if (v != q->cs->value)
        esk_rf_hw(q->cs, v);
}

static u32 pll_read(void *ctx, u32 off, int n)
{
    struct pll *q = (struct pll *)ctx;
    pll_status(q);
    return esk_rf_read(q->p, off, n);
}

static void pll_write(void *ctx, u32 off, int n, u32 v)
{
    struct pll *q = (struct pll *)ctx;
    esk_rf_write(q->p, off, n, v);
    pll_status(q);
    esk_rp2040_clock_change(q->s);
}

static void pll_reset(void *ctx) { pll_status((struct pll *)ctx); }

const struct esk_dev_ops esk_rp2040_pll_ops = {
    "rp2040-pll", pll_read, pll_write, pll_reset, 0, 0,
};

void *esk_rp2040_pll_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct pll *q = (struct pll *)esk_mach_alloc(s, sizeof *q);
    if (!q)
        return 0;
    memset(q, 0, sizeof *q);
    q->s = s;
    q->p = p;
    q->cs = esk_rf_reg(p, "CS");
    q->pwr = esk_rf_reg(p, "PWR");
    q->fbdiv = esk_rf_reg(p, "FBDIV_INT");
    q->prim = esk_rf_reg(p, "PRIM");
    if (!q->cs || !q->pwr || !q->fbdiv || !q->prim)
        return 0;
    q->m_lock = esk_rf_mask(q->cs, "LOCK");
    q->m_bypass = esk_rf_mask(q->cs, "BYPASS");
    q->m_refdiv = esk_rf_mask(q->cs, "REFDIV");
    q->m_pd = esk_rf_mask(q->pwr, "PD");
    q->m_vcopd = esk_rf_mask(q->pwr, "VCOPD");
    q->m_postdivpd = esk_rf_mask(q->pwr, "POSTDIVPD");
    q->m_fbdiv = esk_rf_mask(q->fbdiv, "FBDIV_INT");
    q->m_pd1 = esk_rf_mask(q->prim, "POSTDIV1");
    q->m_pd2 = esk_rf_mask(q->prim, "POSTDIV2");
    return q;
}

static u32 pll_out(struct esk_mach *s, const char *name)
{
    return pll_hz((const struct pll *)esk_rp_model(s, name, &esk_rp2040_pll_ops));
}

/* ---- the clock generators -------------------------------------------------- */

static const char *const clk_names[RP_NCLK] = {
    "GPOUT0", "GPOUT1", "GPOUT2", "GPOUT3", "REF", "SYS", "PERI", "USB", "ADC", "RTC",
};

struct clocks {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *ctrl[RP_NCLK], *div[RP_NCLK], *sel[RP_NCLK];
    u64 m_src[RP_NCLK], m_aux[RP_NCLK], m_en[RP_NCLK], m_int[RP_NCLK], m_frac[RP_NCLK];
    struct esk_rf_reg *fc0_src, *fc0_status, *fc0_result;
    u64 m_done, m_pass, m_khz, m_frac_khz;
    u32 sys_hz; /* clk_sys as the runtime last had it */
    int in_change;
};

static struct clocks *clocks_of(struct esk_mach *s)
{
    return (struct clocks *)esk_rp_model(s, "CLOCKS", &esk_rp2040_clocks_ops);
}

/* the frequency a clock generator's divider gives from `src` */
static u32 divided(const struct clocks *c, int k, u64 src)
{
    u64 d;
    if (!c->div[k])
        return (u32)src;
    d = (u64)fld(c->div[k], c->m_int[k]) << 8 | fld(c->div[k], c->m_frac[k]);
    if (!(d >> 8))
        d = (u64)1 << 24 | (d & 0xffu); /* INT 0: divide by 2^16 */
    return (u32)(src * 256u / d);
}

static u32 clk_hz(struct esk_mach *s, const struct clocks *c, int k);

/* the auxiliary sources, as each generator's AUXSRC numbers them */
static u32 aux_hz(struct esk_mach *s, const struct clocks *c, int k)
{
    u32 a = fld(c->ctrl[k], c->m_aux[k]);
    switch (k) {
    case RP_CLK_REF:
        return a == 0 ? pll_out(s, "PLL_USB") : 0;
    case RP_CLK_SYS:
        return a == 0 ? pll_out(s, "PLL_SYS") : a == 1 ? pll_out(s, "PLL_USB")
             : a == 2 ? rosc_hz(s) : a == 3 ? xosc_hz(s) : 0;
    case RP_CLK_PERI:
        return a == 0 ? clk_hz(s, c, RP_CLK_SYS) : a == 1 ? pll_out(s, "PLL_SYS")
             : a == 2 ? pll_out(s, "PLL_USB") : a == 3 ? rosc_hz(s) : a == 4 ? xosc_hz(s) : 0;
    case RP_CLK_USB:
    case RP_CLK_ADC:
    case RP_CLK_RTC:
        return a == 0 ? pll_out(s, "PLL_USB") : a == 1 ? pll_out(s, "PLL_SYS")
             : a == 2 ? rosc_hz(s) : a == 3 ? xosc_hz(s) : 0;
    default:
        return 0;
    }
}

static u32 clk_hz(struct esk_mach *s, const struct clocks *c, int k)
{
    if (k == RP_CLK_REF) {
        u32 src = fld(c->ctrl[k], c->m_src[k]);
        u32 f = src == 0 ? rosc_hz(s) : src == 1 ? aux_hz(s, c, k) : src == 2 ? xosc_hz(s) : 0;
        return divided(c, k, f);
    }
    if (k == RP_CLK_SYS) {
        u32 f = fld(c->ctrl[k], c->m_src[k]) ? aux_hz(s, c, k) : clk_hz(s, c, RP_CLK_REF);
        return divided(c, k, f);
    }
    if (k >= RP_CLK_PERI && c->ctrl[k] && fld(c->ctrl[k], c->m_en[k]))
        return divided(c, k, aux_hz(s, c, k));
    return 0;
}

u32 esk_rp2040_hz(struct esk_mach *s, int clk)
{
    struct clocks *c = clocks_of(s);
    if (!c)
        return clk == RP_CLK_SYS || clk == RP_CLK_REF || clk == RP_CLK_PERI ? RP_ROSC_HZ : 0;
    return clk_hz(s, c, clk);
}

static u32 core_hz(struct esk_mach *m)
{
    struct clocks *c = (struct clocks *)m->clock_ctx;
    return c ? clk_hz(m, c, RP_CLK_SYS) : 0;
}

void esk_rp2040_clock_change(struct esk_mach *s)
{
    struct clocks *c = clocks_of(s);
    u32 hz;
    if (!c || c->in_change)
        return;
    c->in_change = 1;
    hz = clk_hz(s, c, RP_CLK_SYS);
    if (hz != c->sys_hz) {
        c->sys_hz = hz;
        s->yield = 1; /* the runtime sees the core's new clock now */
    }
    esk_rp2040_tick_clock(s);
    esk_rp2040_uart_kick(s);
    c->in_change = 0;
}

static void selected(struct clocks *c)
{
    int k;
    for (k = RP_CLK_REF; k <= RP_CLK_SYS; k++) {
        u64 v = (u64)1 << fld(c->ctrl[k], c->m_src[k]);
        if (c->sel[k]->value != v)
            esk_rf_hw(c->sel[k], v);
    }
}

/* FC0: the frequency of the source picked, in kHz and 32nds of one */
static void fc0(struct clocks *c)
{
    static const int src_clk[] = { -1, -1, -1, -1, -1, -1, -1, -1, RP_CLK_REF, RP_CLK_SYS,
                                   RP_CLK_PERI, RP_CLK_USB, RP_CLK_ADC, RP_CLK_RTC };
    u32 sel = (u32)c->fc0_src->value, hz = 0;
    struct esk_mach *s = c->s;
    u64 r;
    if (!sel)
        return;
    if (sel == 1)
        hz = pll_out(s, "PLL_SYS");
    else if (sel == 2)
        hz = pll_out(s, "PLL_USB");
    else if (sel == 3 || sel == 4)
        hz = rosc_hz(s);
    else if (sel == 5)
        hz = xosc_hz(s);
    else if (sel < sizeof src_clk / sizeof src_clk[0] && src_clk[sel] >= 0)
        hz = clk_hz(s, c, src_clk[sel]);
    r = esk_rp_put(0, c->m_khz, hz / 1000u) |
        esk_rp_put(0, c->m_frac_khz, (u32)((u64)(hz % 1000u) * 32u / 1000u));
    esk_rf_hw(c->fc0_result, r);
    esk_rf_hw(c->fc0_status, c->m_done | (hz ? c->m_pass : 0));
}

static u32 clocks_read(void *ctx, u32 off, int n)
{
    struct clocks *c = (struct clocks *)ctx;
    return esk_rf_read(c->p, off, n);
}

static void clocks_write(void *ctx, u32 off, int n, u32 v)
{
    struct clocks *c = (struct clocks *)ctx;
    esk_rf_write(c->p, off, n, v);
    selected(c);
    if (off == esk_rf_off(c->fc0_src))
        fc0(c);
    esk_rp2040_clock_change(c->s);
}

static void clocks_reset(void *ctx)
{
    struct clocks *c = (struct clocks *)ctx;
    selected(c);
    c->sys_hz = RP_ROSC_HZ;
}

const struct esk_dev_ops esk_rp2040_clocks_ops = {
    "rp2040-clocks", clocks_read, clocks_write, clocks_reset, 0, 0,
};

void *esk_rp2040_clocks_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct clocks *c = (struct clocks *)esk_mach_alloc(s, sizeof *c);
    char nm[32];
    int k;
    if (!c)
        return 0;
    memset(c, 0, sizeof *c);
    c->s = s;
    c->p = p;
    for (k = 0; k < RP_NCLK; k++) {
        (void)snprintf(nm, sizeof nm, "CLK_%s_CTRL", clk_names[k]);
        c->ctrl[k] = esk_rf_reg(p, nm);
        (void)snprintf(nm, sizeof nm, "CLK_%s_DIV", clk_names[k]);
        c->div[k] = esk_rf_reg(p, nm);
        (void)snprintf(nm, sizeof nm, "CLK_%s_SELECTED", clk_names[k]);
        c->sel[k] = esk_rf_reg(p, nm);
        if (!c->ctrl[k] || !c->sel[k])
            return 0;
        c->m_src[k] = esk_rf_mask(c->ctrl[k], "SRC");
        c->m_aux[k] = esk_rf_mask(c->ctrl[k], "AUXSRC");
        c->m_en[k] = esk_rf_mask(c->ctrl[k], "ENABLE");
        c->m_int[k] = c->div[k] ? esk_rf_mask(c->div[k], "INT") : 0;
        c->m_frac[k] = c->div[k] ? esk_rf_mask(c->div[k], "FRAC") : 0;
    }
    c->fc0_src = esk_rf_reg(p, "FC0_SRC");
    c->fc0_status = esk_rf_reg(p, "FC0_STATUS");
    c->fc0_result = esk_rf_reg(p, "FC0_RESULT");
    if (!c->fc0_src || !c->fc0_status || !c->fc0_result)
        return 0;
    c->m_done = esk_rf_mask(c->fc0_status, "DONE");
    c->m_pass = esk_rf_mask(c->fc0_status, "PASS");
    c->m_khz = esk_rf_mask(c->fc0_result, "KHZ");
    c->m_frac_khz = esk_rf_mask(c->fc0_result, "FRAC");
    c->sys_hz = RP_ROSC_HZ;
    s->clock_ctx = c;
    s->core_hz = core_hz;
    return c;
}

/* ---- RESETS ------------------------------------------------------------------ */

struct resets {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *reset, *done;
    u64 mask; /* the bits with a peripheral */
    struct esk_rf_periph *held[32][2];
};

static void resets_apply(struct resets *r, u64 was)
{
    u64 now = r->reset->value & r->mask, d;
    int b, k;
    for (b = 0; b < 32; b++)
        if ((now & ~was) >> b & 1)
            for (k = 0; k < 2 && r->held[b][k]; k++)
                esk_rf_periph_reset(r->held[b][k]);
    d = ~now & r->mask;
    if (r->done->value != d)
        esk_rf_hw(r->done, d);
}

static u32 resets_read(void *ctx, u32 off, int n)
{
    return esk_rf_read(((struct resets *)ctx)->p, off, n);
}

static void resets_write(void *ctx, u32 off, int n, u32 v)
{
    struct resets *r = (struct resets *)ctx;
    u64 was = r->reset->value & r->mask;
    esk_rf_write(r->p, off, n, v);
    resets_apply(r, was);
    esk_rp2040_pins_changed(r->s); /* a bank's pads may be out of reset */
}

static void resets_reset(void *ctx)
{
    struct resets *r = (struct resets *)ctx;
    resets_apply(r, ~UINT64_C(0));
    esk_rp2040_boot(r->s);
}

const struct esk_dev_ops esk_rp2040_resets_ops = {
    "rp2040-resets", resets_read, resets_write, resets_reset, 0, 0,
};

/* where RP2040.svd's reset values are not the part's */
static void corrections(struct esk_mach *s)
{
    struct esk_rf_periph *p = esk_rf_periph(s->rf, "SYSINFO");
    struct esk_rf_reg *r = p ? esk_rf_reg(p, "CHIP_ID") : 0;
    if (r)
        esk_rf_set_reset(r, 0x20002927u); /* revision 2 (B2), part 2, 0x927 */
    r = p ? esk_rf_reg(p, "PLATFORM") : 0;
    if (r)
        esk_rf_set_reset(r, esk_rf_mask(r, "ASIC"));
    p = esk_rf_periph(s->rf, "VREG_AND_CHIP_RESET");
    r = p ? esk_rf_reg(p, "CHIP_RESET") : 0;
    if (r)
        esk_rf_set_reset(r, esk_rf_mask(r, "HAD_POR"));
}

void *esk_rp2040_resets_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct resets *r = (struct resets *)esk_mach_alloc(s, sizeof *r);
    int i, k;
    if (!r)
        return 0;
    memset(r, 0, sizeof *r);
    r->s = s;
    r->p = p;
    r->reset = esk_rf_reg(p, "RESET");
    r->done = esk_rf_reg(p, "RESET_DONE");
    if (!r->reset || !r->done)
        return 0;
    for (i = 0; i < r->reset->nf; i++) {
        const struct esk_rf_field *f = &r->reset->f[i];
        const char *names[2];
        if (!f->name || f->width != 1 || f->lsb >= 32)
            continue;
        names[0] = f->name;
        names[1] = 0;
        if (!strcmp(f->name, "USBCTRL")) {
            names[0] = "USB";
            names[1] = "USB_DPRAM";
        }
        for (k = 0; k < 2 && names[k]; k++) {
            struct esk_rf_periph *q = esk_rf_periph(s->rf, names[k]);
            size_t l = strlen(f->name) + 16;
            char *g;
            if (!q)
                continue;
            g = (char *)esk_mach_alloc(s, l);
            if (!g)
                return 0;
            (void)snprintf(g, l, "RESETS.RESET.%s", f->name);
            q->gate = r->reset;
            q->gate_mask = (u64)1 << f->lsb;
            q->gate_held = 1;
            q->gate_name = g;
            r->held[f->lsb][k] = q;
            r->mask |= (u64)1 << f->lsb;
        }
    }
    corrections(s);
    return r;
}

/* the bootrom's flash boot leaves these out of reset */
void esk_rp2040_bootrom_resets(struct esk_mach *s)
{
    struct resets *r = (struct resets *)esk_rp_model(s, "RESETS", &esk_rp2040_resets_ops);
    u64 out, was;
    if (!r)
        return;
    out = esk_rf_mask(r->reset, "IO_QSPI") | esk_rf_mask(r->reset, "PADS_QSPI") |
          esk_rf_mask(r->reset, "PADS_BANK0");
    was = r->reset->value & r->mask;
    esk_rf_hw(r->reset, r->reset->value & ~out);
    resets_apply(r, was);
}

/* ---- PSM: every domain powered but those forced off ------------------------ */

/* FRCE_OFF.PROC1 powers core 1 off; cleared, core 1 starts in its bootrom
 * (sio.c) */
struct psm {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *off, *done;
    u64 m_proc1;
};

static void psm_done(struct psm *m)
{
    u64 v = esk_rf_fields(m->done) & ~m->off->value;
    if (m->done->value != v)
        esk_rf_hw(m->done, v);
}

static u32 psm_read(void *ctx, u32 off, int n) { return esk_rf_read(((struct psm *)ctx)->p, off, n); }

static void psm_write(void *ctx, u32 off, int n, u32 v)
{
    struct psm *m = (struct psm *)ctx;
    esk_rf_write(m->p, off, n, v);
    psm_done(m);
    if (m->m_proc1 && !m->s->bus.debug)
        esk_rp2040_core1_power(m->s, !(m->off->value & m->m_proc1));
}

static void psm_reset(void *ctx) { psm_done((struct psm *)ctx); }

const struct esk_dev_ops esk_rp2040_psm_ops = {
    "rp2040-psm", psm_read, psm_write, psm_reset, 0, 0,
};

void *esk_rp2040_psm_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct psm *m = (struct psm *)esk_mach_alloc(s, sizeof *m);
    if (!m)
        return 0;
    m->s = s;
    m->p = p;
    m->off = esk_rf_reg(p, "FRCE_OFF");
    m->done = esk_rf_reg(p, "DONE");
    if (!m->off || !m->done)
        return 0;
    m->m_proc1 = esk_rf_mask(m->off, "PROC1");
    esk_rf_set_reset(m->done, esk_rf_fields(m->done));
    return m;
}

/* ---- the watchdog and its tick generator --------------------------------- */

struct watchdog {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *ctrl, *load, *reason, *tick;
    u64 m_trigger, m_enable, m_time, m_tick_en, m_running, m_cycles, m_force, m_timer;
    u32 counter; /* CTRL.TIME */
    u64 acc;     /* clk_ref edges, in units of 1/clk_sys */
    u64 since;   /* the cycle its clock started */
    int running; /* its clock: the tick generator runs */
};

static struct watchdog *watchdog_of(struct esk_mach *s)
{
    return (struct watchdog *)esk_rp_model(s, "WATCHDOG", &esk_rp2040_watchdog_ops);
}

u32 esk_rp2040_tick_cycles(struct esk_mach *s)
{
    struct watchdog *w = watchdog_of(s);
    if (!w || !fld(w->tick, w->m_tick_en))
        return 0;
    return fld(w->tick, w->m_cycles);
}

static void wd_fire(struct watchdog *w, int forced)
{
    struct esk_mach *s = w->s;
    esk_rf_hw(w->reason, forced ? w->m_force : w->m_timer);
    esk_rf_diag(s, ES_SEV_INFO, "mcu.watchdog_reset", "%s",
                forced ? "the watchdog's CTRL.TRIGGER resets the chip"
                       : "the watchdog's counter reached 0: it resets the chip");
    s->reset_req = 1;
    s->yield = 1;
}

static void wd_time(struct watchdog *w)
{
    u64 v = esk_rp_put(w->ctrl->value, w->m_time, w->counter);
    if (v != w->ctrl->value)
        esk_rf_hw(w->ctrl, v);
}

static void wd_clock(struct watchdog *w)
{
    u32 n = esk_rp2040_tick_cycles(w->s);
    int was = w->running;
    esk_rf_clock(w->p, &w->running, n && esk_rp2040_hz(w->s, RP_CLK_REF));
    if (w->running && !was)
        w->since = w->s->cycles;
}

static void wd_tick(void *ctx, u32 cycles)
{
    struct watchdog *w = (struct watchdog *)ctx;
    struct esk_mach *s = w->s;
    u64 ref = esk_rp2040_hz(s, RP_CLK_REF), sys = esk_rp2040_hz(s, RP_CLK_SYS), one;
    u32 per = esk_rp2040_tick_cycles(s), n;
    if (!ref || !sys || !per)
        return;
    cycles = esk_mach_after(s, cycles, w->since); /* the instruction that started it */
    w->acc += (u64)cycles * ref;
    one = sys * per;
    if (w->acc < one)
        return;
    n = (u32)(w->acc / one);
    w->acc %= one;
    esk_rp2040_timer_ticks(s, n);
    if (fld(w->ctrl, w->m_enable) && w->counter) {
        u64 dec = 2u * (u64)n; /* twice a tick: RP2040-E1 */
        w->counter = dec >= w->counter ? 0 : w->counter - (u32)dec;
        wd_time(w);
        if (!w->counter)
            wd_fire(w, 0);
    }
}

static int wd_next_event(void *ctx, u32 *cycles)
{
    struct watchdog *w = (struct watchdog *)ctx;
    struct esk_mach *s = w->s;
    u64 ref = esk_rp2040_hz(s, RP_CLK_REF), sys = esk_rp2040_hz(s, RP_CLK_SYS), need, c;
    u32 per = esk_rp2040_tick_cycles(s), ticks = 0;
    int any = esk_rp2040_timer_next(s, &ticks);
    if (fld(w->ctrl, w->m_enable) && w->counter) {
        u32 t = (w->counter + 1u) / 2u;
        if (!any || t < ticks)
            ticks = t;
        any = 1;
    }
    if (!any || !ref || !sys || !per)
        return 0;
    /* the cycles until `ticks` ticks have come */
    need = (u64)ticks * sys * per - w->acc;
    c = (need + ref - 1u) / ref;
    *cycles = c > 0xffffffffu ? 0xffffffffu : (u32)c;
    return 1;
}

static u32 wd_read(void *ctx, u32 off, int n) { return esk_rf_read(((struct watchdog *)ctx)->p, off, n); }

static void wd_write(void *ctx, u32 off, int n, u32 v)
{
    struct watchdog *w = (struct watchdog *)ctx;
    u64 t;
    esk_rf_write(w->p, off, n, v);
    if (off == esk_rf_off(w->load))
        w->counter = v & 0xffffffu;
    if (off == esk_rf_off(w->ctrl) && (v & w->m_trigger))
        wd_fire(w, 1);
    wd_time(w);
    t = esk_rp_put(w->tick->value, w->m_running, fld(w->tick, w->m_tick_en));
    if (t != w->tick->value)
        esk_rf_hw(w->tick, t);
    wd_clock(w);
}

static void wd_reset(void *ctx)
{
    struct watchdog *w = (struct watchdog *)ctx;
    w->counter = 0;
    w->acc = 0;
    w->running = 0;
    esk_rf_hw(w->tick, esk_rp_put(w->tick->value, w->m_running, fld(w->tick, w->m_tick_en)));
    wd_clock(w);
}

const struct esk_dev_ops esk_rp2040_watchdog_ops = {
    "rp2040-watchdog", wd_read, wd_write, wd_reset, wd_tick, wd_next_event,
};

void *esk_rp2040_watchdog_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct watchdog *w = (struct watchdog *)esk_mach_alloc(s, sizeof *w);
    if (!w)
        return 0;
    memset(w, 0, sizeof *w);
    w->s = s;
    w->p = p;
    w->ctrl = esk_rf_reg(p, "CTRL");
    w->load = esk_rf_reg(p, "LOAD");
    w->reason = esk_rf_reg(p, "REASON");
    w->tick = esk_rf_reg(p, "TICK");
    if (!w->ctrl || !w->load || !w->reason || !w->tick)
        return 0;
    w->m_trigger = esk_rf_mask(w->ctrl, "TRIGGER");
    w->m_enable = esk_rf_mask(w->ctrl, "ENABLE");
    w->m_time = esk_rf_mask(w->ctrl, "TIME");
    w->m_tick_en = esk_rf_mask(w->tick, "ENABLE");
    w->m_running = esk_rf_mask(w->tick, "RUNNING");
    w->m_cycles = esk_rf_mask(w->tick, "CYCLES");
    w->m_force = esk_rf_mask(w->reason, "FORCE");
    w->m_timer = esk_rf_mask(w->reason, "TIMER");
    esk_rf_set_reset(w->tick, w->tick->reset | ((w->tick->reset & w->m_tick_en) ? w->m_running : 0));
    return w;
}

/* a clock the watchdog's generator runs on may have started or stopped */
void esk_rp2040_tick_clock(struct esk_mach *s)
{
    struct watchdog *w = watchdog_of(s);
    if (w)
        wd_clock(w);
}
