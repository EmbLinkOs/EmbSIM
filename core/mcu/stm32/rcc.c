/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* stm32-rcc.c -- the STM32's reset and clock control, over the SVD's RCC
 * registers. What it does is found by the fields' names, so it holds
 * across the families whose RCC is laid out this way (F1, F2, F4, F7):
 *
 *   - a ready bit follows its enable: each field XRDY of a register with
 *     a field XON (CR's HSIRDY, HSERDY, PLLRDY and PLLI2SRDY, BDCR's
 *     LSERDY, CSR's LSIRDY). An oscillator or a PLL is ready the moment it
 *     is turned on, and not ready when off;
 *   - CFGR's system clock switch status follows the switch: SWS from SW
 *     (ST's F4 file splits both into SW0, SW1 and SWS0, SWS1), at once;
 *   - the clock gates: a peripheral with an enable bit NAMEEN in one of the
 *     ENR registers (AHB1ENR, APB1ENR, ...; not the low-power LPENRs)
 *     reads as 0 and ignores writes while the bit is clear, as the part's
 *     peripherals do (RM0090 6.3.10: the peripheral's clock is off, so are
 *     its registers). The first such access is a warning (mcu.clock_gated),
 *     so a driver that forgot its clock says so instead of hanging.
 *
 * The F1's (an RCC whose CFGR has PLLMUL: RM0008's) more:
 *   - a clock source is used once it is ready (RM0008, "System clock
 *     (SYSCLK) selection"): CFGR's SWS takes SW's value when that
 *     source's ready bit is set, and else keeps the source in use,
 *     switching when it becomes ready; PLLRDY
 *     sets when PLLON is set and its source (PLLSRC: HSI / 2, or HSE,
 *     halved with PLLXTPRE) is ready;
 *   - the clocks' frequencies: HSI 8 MHz, HSE the board's crystal or
 *     clock (esk_mach.hse_hz; without one HSERDY never sets), the PLL its
 *     source times PLLMUL's factor (2 to 16), SYSCLK the source SWS says,
 *     HCLK SYSCLK over HPRE's divisor, PCLK1 and PCLK2 HCLK over PPRE1's and
 *     PPRE2's. The core runs at HCLK: the MCU runtime turns the core's
 *     cycles into time at it, and a change ends the runtime's quantum. A
 *     GPIO port's clock enable is IOPxEN.
 *
 * Not modelled: the F2-F7's clock frequencies (EmbSim's time is the
 * core's cycles, whatever the PLL says: a timer reads its APB prescaler
 * from CFGR, stm32-tim.c), the clock security system, the interrupts of
 * CIR, the reset registers (a peripheral is not reset by them), RMVF, the
 * oscillators' and the PLL's start-up times.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/stm32-rcc.c. Changes:
 * the HSE frequency is the board's and the core clock is reported to the
 * runtime instead of the model keeping its own time; PCLK1 and PCLK2 for
 * the peripherals' baud rates and timers. */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

#define MAX_FOLLOW 16

struct follow {
    struct esk_rf_reg *r;
    u64 from, to;                   /* `to` takes the bits of `from` */
    int shift;                      /* to's position less from's */
};

struct rcc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct follow f[MAX_FOLLOW];
    int nf;
    /* the F1's */
    int f1;
    struct esk_rf_reg *cr, *cfgr;
    u64 hsion, hsirdy, hseon, hserdy, pllon, pllrdy, sw, sws;
    u64 pllsrc, pllxtpre, pllmul, hpre, ppre1, ppre2;
    u32 hclk;
};

/* the F1's internal oscillator */
#define F1_HSI_HZ 8000000u

static void add_follow(struct rcc *c, struct esk_rf_reg *r, u64 from, u64 to)
{
    int a = 0, b = 0;
    if (c->nf == MAX_FOLLOW || !from || !to)
        return;
    while (!(from >> a & 1))
        a++;
    while (!(to >> b & 1))
        b++;
    c->f[c->nf].r = r;
    c->f[c->nf].from = from;
    c->f[c->nf].to = to;
    c->f[c->nf++].shift = b - a;
}

/* every ready and status bit as its enable or switch has it now */
static void settle(struct rcc *c)
{
    int i;
    for (i = 0; i < c->nf; i++) {
        struct follow *f = &c->f[i];
        u64 v = f->r->value & f->from, nv;
        v = f->shift >= 0 ? v << f->shift : v >> -f->shift;
        nv = (f->r->value & ~f->to) | (v & f->to);
        if (nv != f->r->value)
            esk_rf_hw(f->r, nv);
    }
}

static u32 fld(const struct esk_rf_reg *r, u64 mask)
{
    int sh = 0;
    if (!mask)
        return 0;
    while (!(mask >> sh & 1))
        sh++;
    return (u32)((r->value & mask) >> sh);
}

/* the F1's PLL output */
static u32 f1_pll_hz(const struct rcc *c)
{
    u32 src = fld(c->cfgr, c->pllsrc) ? c->s->hse_hz >> fld(c->cfgr, c->pllxtpre) : F1_HSI_HZ / 2;
    u32 m = fld(c->cfgr, c->pllmul) + 2;
    return src * (m > 16 ? 16 : m);
}

static int f1_ready(const struct rcc *c, u32 src)
{
    u64 cr = c->cr->value;
    return src == 0 ? (cr & c->hsirdy) != 0 : src == 1 ? (cr & c->hserdy) != 0
         : src == 2 ? (cr & c->pllrdy) != 0 : 0;
}

static u32 f1_sysclk(const struct rcc *c)
{
    u32 sws = fld(c->cfgr, c->sws);
    return sws == 0 ? F1_HSI_HZ : sws == 1 ? c->s->hse_hz : sws == 2 ? f1_pll_hz(c) : 0;
}

static u32 f1_hclk(const struct rcc *c)
{
    static const u32 hdiv[8] = { 2, 4, 8, 16, 64, 128, 256, 512 };
    u32 f = f1_sysclk(c), h = fld(c->cfgr, c->hpre);
    return h & 8 ? f / hdiv[h & 7] : f;
}

/* the F1's ready bits, the clock in use; a new HCLK ends the quantum */
static void f1_settle(struct rcc *c)
{
    u64 cr = c->cr->value;
    u32 sw, h;
    int src_ok;
    cr = (cr & ~c->hsirdy) | (cr & c->hsion ? c->hsirdy : 0);
    cr = (cr & ~c->hserdy) | (cr & c->hseon && c->s->hse_hz ? c->hserdy : 0);
    src_ok = fld(c->cfgr, c->pllsrc) ? (cr & c->hserdy) != 0 : (cr & c->hsirdy) != 0;
    cr = (cr & ~c->pllrdy) | (cr & c->pllon && src_ok ? c->pllrdy : 0);
    if (cr != c->cr->value)
        esk_rf_hw(c->cr, cr);
    sw = fld(c->cfgr, c->sw);
    if (sw != fld(c->cfgr, c->sws) && f1_ready(c, sw)) {
        int sh = 0;
        while (!(c->sws >> sh & 1))
            sh++;
        esk_rf_hw(c->cfgr, (c->cfgr->value & ~c->sws) | ((u64)sw << sh & c->sws));
    }
    h = f1_hclk(c);
    if (h != c->hclk) {
        c->hclk = h;
        c->s->yield = 1;
    }
}

static struct rcc *f1_rcc(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "RCC") : 0;
    struct rcc *c = p && p->ops == &esk_stm32_rcc_ops ? (struct rcc *)p->ctx : 0;
    return c && c->f1 ? c : 0;
}

static u32 f1_core_hz(struct esk_mach *s)
{
    struct rcc *c = f1_rcc(s);
    return c ? c->hclk : 0;
}

u32 esk_stm32f1_hclk(struct esk_mach *s)
{
    return f1_core_hz(s);
}

u32 esk_stm32f1_pclk(struct esk_mach *s, int apb)
{
    static const u32 pdiv[4] = { 2, 4, 8, 16 };
    struct rcc *c = f1_rcc(s);
    u32 p;
    if (!c)
        return esk_mach_core_hz(s);
    p = fld(c->cfgr, apb == 2 ? c->ppre2 : c->ppre1);
    return p & 4 ? c->hclk / pdiv[p & 3] : c->hclk;
}

static u32 rcc_read(void *ctx, u32 off, int n)
{
    struct rcc *c = (struct rcc *)ctx;
    return esk_rf_read(c->p, off, n);
}

static void rcc_write(void *ctx, u32 off, int n, u32 v)
{
    struct rcc *c = (struct rcc *)ctx;
    esk_rf_write(c->p, off, n, v);
    settle(c);
    if (c->f1)
        f1_settle(c);
}

static void rcc_reset(void *ctx)
{
    struct rcc *c = (struct rcc *)ctx;
    settle(c);
    if (c->f1) {
        c->hclk = 0;
        f1_settle(c);
    }
}

const struct esk_dev_ops esk_stm32_rcc_ops = {
    "stm32-rcc", rcc_read, rcc_write, rcc_reset, 0, 0,
};

static int ends_with(const char *s, const char *t)
{
    size_t a = strlen(s), b = strlen(t);
    return a >= b && !strcmp(s + a - b, t);
}

/* the peripherals whose clock enables are in this RCC */
static int gates(struct esk_mach *s, struct rcc *c)
{
    char want[64];
    struct esk_rf_periph *q;
    struct esk_rf_reg *apb2 = esk_rf_reg(c->p, "APB2ENR");
    int i, k;
    for (i = 0; (q = esk_rf_periph_at(s->rf, i)) != 0; i++) {
        if (q == c->p || strlen(q->name) + 3 > sizeof want)
            continue;
        snprintf(want, sizeof want, "%sEN", q->name);
        /* an F1's GPIO port is IOPx */
        if (!strncmp(q->name, "GPIO", 4) && strlen(q->name) == 5 && !esk_rf_mask(apb2, want) &&
            esk_rf_mask(apb2, "IOPAEN"))
            snprintf(want, sizeof want, "IOP%cEN", q->name[4]);
        for (k = 0; k < c->p->nreg && !q->gate; k++) {
            struct esk_rf_reg *r = c->p->reg[k];
            u64 m;
            size_t l;
            char *g;
            if (!ends_with(r->name, "ENR") || ends_with(r->name, "LPENR"))
                continue;
            m = esk_rf_mask(r, want);
            if (!m)
                continue;
            l = strlen(c->p->name) + strlen(r->name) + strlen(want) + 3;
            g = (char *)esk_mach_alloc(s, l);
            if (!g)
                return -1;
            snprintf(g, l, "%s.%s.%s", c->p->name, r->name, want);
            q->gate = r;
            q->gate_mask = m;
            q->gate_name = g;
        }
    }
    return 0;
}

void *esk_stm32_rcc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct rcc *c = (struct rcc *)esk_mach_alloc(s, sizeof *c);
    int i, k;
    if (!c)
        return 0;
    c->s = s;
    c->p = p;
    c->cr = esk_rf_reg(p, "CR");
    c->cfgr = esk_rf_reg(p, "CFGR");
    c->f1 = c->cr && c->cfgr && esk_rf_mask(c->cfgr, "PLLMUL") && esk_rf_mask(c->cfgr, "SWS");
    if (c->f1) {
        c->hsion = esk_rf_mask(c->cr, "HSION");
        c->hsirdy = esk_rf_mask(c->cr, "HSIRDY");
        c->hseon = esk_rf_mask(c->cr, "HSEON");
        c->hserdy = esk_rf_mask(c->cr, "HSERDY");
        c->pllon = esk_rf_mask(c->cr, "PLLON");
        c->pllrdy = esk_rf_mask(c->cr, "PLLRDY");
        c->sw = esk_rf_mask(c->cfgr, "SW");
        c->sws = esk_rf_mask(c->cfgr, "SWS");
        c->pllsrc = esk_rf_mask(c->cfgr, "PLLSRC");
        c->pllxtpre = esk_rf_mask(c->cfgr, "PLLXTPRE");
        c->pllmul = esk_rf_mask(c->cfgr, "PLLMUL");
        c->hpre = esk_rf_mask(c->cfgr, "HPRE");
        c->ppre1 = esk_rf_mask(c->cfgr, "PPRE1");
        c->ppre2 = esk_rf_mask(c->cfgr, "PPRE2");
        s->core_hz = f1_core_hz;
    }
    for (k = 0; k < p->nreg; k++) {
        struct esk_rf_reg *r = p->reg[k];
        for (i = 0; i < r->nf; i++) {
            const char *fn = r->f[i].name;
            char base[64];
            size_t l = fn ? strlen(fn) : 0;
            if (l < 3 || l >= sizeof base)
                continue;
            if (c->f1 && (r == c->cr || r == c->cfgr))
                continue; /* f1_settle's */
            if (l > 3 && ends_with(fn, "RDY")) { /* HSERDY: HSEON */
                snprintf(base, sizeof base, "%.*sON", (int)(l - 3), fn);
                add_follow(c, r, esk_rf_mask(r, base), esk_rf_mask(r, fn));
            } else if (!strncmp(fn, "SWS", 3)) { /* SWS0: SW0 */
                snprintf(base, sizeof base, "SW%s", fn + 3);
                add_follow(c, r, esk_rf_mask(r, base), esk_rf_mask(r, fn));
            }
        }
    }
    if (gates(s, c))
        return 0;
    settle(c);
    if (c->f1)
        f1_settle(c);
    return c;
}
