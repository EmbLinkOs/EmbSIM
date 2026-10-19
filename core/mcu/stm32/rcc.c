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
 * The F4's (an RCC with PLLCFGR: RM0390's STM32F446, RM0090's F405/F407)
 * more, its registers at RM0390's bit positions:
 *   - HSI 16 MHz; HSE the board's, ready only as the board drives OSC_IN: a
 *     crystal with HSEBYP clear, a clock (the NUCLEO's ST-Link MCO) with it
 *     set; LSE the board's 32.768 kHz crystal, LSI ready when on;
 *   - the main PLL from HSI or HSE (PLLSRC): VCO = source / PLLM x PLLN,
 *     PLL_P = VCO / 2, 4, 6 or 8, PLL_R = VCO / PLLR; PLLRDY, PLLI2SRDY and
 *     PLLSAIRDY set when the PLL is on and its source is ready;
 *   - SYSCLK from HSI, HSE, PLL_P or (the F446's) PLL_R, switched once the
 *     source is ready (SWS); HCLK over HPRE, PCLK1 and PCLK2 over PPRE1 and
 *     PPRE2; the timers' clock HCLK when the APB prescaler is 1, else twice
 *     the APB's (four times with DCKCFGR's TIMPRE above a prescaler of 4);
 *   - an oscillator or the PLL in use as the system clock cannot be turned
 *     off (RM0390 6.3.1: HSION, HSEON and PLLON cannot be cleared then),
 *     and HSEBYP keeps its value while HSE is on;
 *   - BDCR is written only while PWR's DBP allows it (the backup domain's
 *     write protection), so LSEON without DBP does nothing;
 *   - what the datasheet forbids is said, once, as a warning
 *     (mcu.clock_out_of_spec): a VCO input outside 1 to 2 MHz or output
 *     outside 100 to 432 MHz, SYSCLK above the voltage scale's limit
 *     (scale 3: 120 MHz, 2: 144 or 168 with over-drive, 1: 168 or 180),
 *     PCLK1 above 45 MHz, PCLK2 above 90 MHz; and too few flash wait states
 *     for HCLK (mcu.flash_wait_states, RM0390 table 5 at 2.7 to 3.6 V): the
 *     part would fetch wrong instructions, the model only warns.
 *   - PWR's ready bits follow (pwr_f4.c).
 *
 * Any family's: a peripheral's bit in an *RSTR register (GPIOARST, an F1's
 * IOPARST) set by a write resets it: its registers return to their reset
 * values and its model starts again. CSR's reset flags record each reset:
 * PORRSTF, PINRSTF and BORRSTF at power-on; PINRSTF with every other (the
 * part drives NRST low) and SFTRSTF, IWDGRSTF, WWDGRSTF or LPWRRSTF with
 * its cause, the flags before kept, until RMVF clears them. BDCR keeps its
 * value through every reset but a power-on one, until BDRST resets the
 * backup domain (the RTC's registers with it); LSIRDY is set while the
 * IWDG runs, which forces LSI on.
 *
 * Not modelled: the clock security system, the interrupts of CIR, the
 * peripheral held in reset while its reset bit stays set (accesses then
 * land), the brownout reset (BORRSTF only at power-on), the oscillators'
 * and the PLL's start-up times (ready at once), MCO1 and MCO2 on their
 * pins, the I2S, SAI and SPDIF clocks from PLLI2S and PLLSAI.
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
    /* the F4's */
    int f4;
    struct esk_rf_reg *pllcfgr, *csr, *bdcr, *dckcfgr;
    unsigned warned;                /* the warnings already given */
    /* both families: CSR's reset flags and BDCR, kept through resets */
    u32 csr_flags;
    u64 bdcr_kept;
    /* the peripherals' reset bits in the *RSTR registers */
    struct rst { struct esk_rf_periph *q; struct esk_rf_reg *r; u64 m; } *rst;
    int nrst;
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

static u32 f1_core_hz(struct esk_mach *s);
static u32 f4_core_hz(struct esk_mach *s);

/* the machine's RCC model, kept as its clock model (the core's clock is
 * asked for often: no lookup by name) */
static struct rcc *any_rcc(struct esk_mach *s)
{
    return s->core_hz == f1_core_hz || s->core_hz == f4_core_hz ? (struct rcc *)s->clock_ctx : 0;
}

static struct rcc *f1_rcc(struct esk_mach *s)
{
    struct rcc *c = any_rcc(s);
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

/* ---- the F4's clock tree (RM0390 6.2) -------------------------------- */

#define F4_HSI_HZ 16000000u

/* CR */
#define F4_HSION (1u << 0)
#define F4_HSIRDY (1u << 1)
#define F4_HSEON (1u << 16)
#define F4_HSERDY (1u << 17)
#define F4_HSEBYP (1u << 18)
#define F4_PLLON (1u << 24)
#define F4_PLLRDY (1u << 25)
#define F4_PLLI2SON (1u << 26)
#define F4_PLLI2SRDY (1u << 27)
#define F4_PLLSAION (1u << 28)
#define F4_PLLSAIRDY (1u << 29)
/* PLLCFGR */
#define F4_PLLSRC (1u << 22)
/* CSR's reset flags and RMVF (both families) */
#define CSR_RMVF (1u << 24)
#define CSR_FLAGS 0xfe000000u
/* BDCR, CSR */
#define F4_LSEON (1u << 0)
#define F4_LSERDY (1u << 1)
#define F4_LSEBYP (1u << 2)
#define F4_LSION (1u << 0)
#define F4_LSIRDY (1u << 1)

enum { W_VCO_IN = 1, W_VCO_OUT = 2, W_SYSCLK = 4, W_PCLK1 = 8, W_PCLK2 = 16, W_FLASH = 32 };

static u32 bits(const struct esk_rf_reg *r, int lsb, int width)
{
    return r ? (u32)(r->value >> lsb) & ((1u << width) - 1) : 0;
}

static u32 f4_pll_src_hz(const struct rcc *c)
{
    return (c->pllcfgr->value & F4_PLLSRC) ? c->s->hse_hz : F4_HSI_HZ;
}

/* the VCO's input and output; PLLM below 2 and PLLN below 2 are the
 * manual's "wrong configuration": no output */
static u32 f4_vco_in(const struct rcc *c)
{
    u32 m = bits(c->pllcfgr, 0, 6);
    return m < 2 ? 0 : f4_pll_src_hz(c) / m;
}

static u32 f4_vco(const struct rcc *c)
{
    u32 m = bits(c->pllcfgr, 0, 6), n = bits(c->pllcfgr, 6, 9);
    if (m < 2 || n < 2)
        return 0;
    return (u32)((u64)f4_pll_src_hz(c) * n / m);
}

static u32 f4_sysclk_of(const struct rcc *c, u32 src)
{
    u32 r;
    switch (src) {
    case 0: return F4_HSI_HZ;
    case 1: return c->s->hse_hz;
    case 2: return f4_vco(c) / (2 * (bits(c->pllcfgr, 16, 2) + 1));
    default:
        r = bits(c->pllcfgr, 28, 3);
        return r < 2 ? 0 : f4_vco(c) / r;
    }
}

static u32 f4_hclk(const struct rcc *c)
{
    static const u32 hdiv[8] = { 2, 4, 8, 16, 64, 128, 256, 512 };
    u32 f = f4_sysclk_of(c, bits(c->cfgr, 2, 2)), h = bits(c->cfgr, 4, 4);
    return h & 8 ? f / hdiv[h & 7] : f;
}

static u32 f4_ppre_div(const struct rcc *c, int apb)
{
    u32 p = bits(c->cfgr, apb == 2 ? 13 : 10, 3);
    return p & 4 ? 2u << (p & 3) : 1;
}

/* whether a source of SYSCLK is ready: HSI, HSE, PLL_P, PLL_R */
static int f4_src_ready(const struct rcc *c, u32 src)
{
    u32 cr = (u32)c->cr->value;
    return src == 0 ? (cr & F4_HSIRDY) != 0 : src == 1 ? (cr & F4_HSERDY) != 0 : (cr & F4_PLLRDY) != 0;
}

static struct esk_rf_reg *other_reg(struct esk_mach *s, const char *periph, const char *reg)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, periph) : 0;
    return p ? esk_rf_reg(p, reg) : 0;
}

static void f4_warn(struct rcc *c, unsigned w, const char *code, const char *fmt, u32 a, u32 b)
{
    if (c->warned & w)
        return;
    c->warned |= w;
    esk_rf_diag(c->s, ES_SEV_WARNING, code, fmt, (unsigned long)a, (unsigned long)b);
}

/* what the datasheet forbids, said once */
static void f4_check(struct rcc *c)
{
    struct esk_rf_reg *pcr = other_reg(c->s, "PWR", "CR"), *pcsr = other_reg(c->s, "PWR", "CSR");
    struct esk_rf_reg *acr = other_reg(c->s, "FLASH", "ACR");
    u32 vos = pcr ? bits(pcr, 14, 2) : 3, od = pcsr ? bits(pcsr, 17, 1) : 0;
    u32 sys = f4_sysclk_of(c, bits(c->cfgr, 2, 2)), hclk = c->hclk, lim, ws;
    if (c->cr->value & F4_PLLON) {
        u32 in = f4_vco_in(c), vco = f4_vco(c);
        if (in < 1000000u || in > 2000000u)
            f4_warn(c, W_VCO_IN, "mcu.clock_out_of_spec",
                    "the PLL's VCO input is %lu Hz: RM0390 asks 1 to 2 MHz (PLLM %lu)", in, bits(c->pllcfgr, 0, 6));
        if (vco < 100000000u || vco > 432000000u)
            f4_warn(c, W_VCO_OUT, "mcu.clock_out_of_spec",
                    "the PLL's VCO runs at %lu Hz: RM0390 asks 100 to 432 MHz (PLLN %lu)", vco, bits(c->pllcfgr, 6, 9));
    }
    /* VOS 11 scale 1, 10 scale 2, 01 (and the reserved 00) scale 3, which
     * has no over-drive */
    lim = vos == 3 ? (od ? 180000000u : 168000000u) : vos == 2 ? (od ? 168000000u : 144000000u) : 120000000u;
    if (sys > lim)
        f4_warn(c, W_SYSCLK, "mcu.clock_out_of_spec",
                "SYSCLK is %lu Hz, above the %lu Hz the voltage scale and over-drive allow", sys, lim);
    if (hclk / f4_ppre_div(c, 1) > 45000000u)
        f4_warn(c, W_PCLK1, "mcu.clock_out_of_spec", "PCLK1 is %lu Hz, above APB1's %lu Hz",
                hclk / f4_ppre_div(c, 1), 45000000u);
    if (hclk / f4_ppre_div(c, 2) > 90000000u)
        f4_warn(c, W_PCLK2, "mcu.clock_out_of_spec", "PCLK2 is %lu Hz, above APB2's %lu Hz",
                hclk / f4_ppre_div(c, 2), 90000000u);
    ws = hclk <= 30000000u ? 0 : (hclk - 1) / 30000000u;
    if (acr && bits(acr, 0, 4) < ws)
        f4_warn(c, W_FLASH, "mcu.flash_wait_states",
                "HCLK is %lu Hz with %lu flash wait states: the part would fetch wrong instructions", hclk,
                bits(acr, 0, 4));
}

static void set_bit(u64 *v, u32 bit, int on)
{
    *v = on ? (*v | bit) : (*v & ~(u64)bit);
}

/* the F4's ready bits, the clock in use; a new HCLK ends the quantum */
static void f4_settle(struct rcc *c)
{
    u64 cr = c->cr->value, v;
    u32 sws = bits(c->cfgr, 2, 2), sw = bits(c->cfgr, 0, 2), h;
    int pll_hse = (c->pllcfgr->value & F4_PLLSRC) != 0, hse_ok, src_ok;
    /* what the system clock uses cannot be turned off */
    if (sws == 0 || (sws >= 2 && !pll_hse))
        cr |= F4_HSION;
    if (sws == 1 || (sws >= 2 && pll_hse))
        cr |= F4_HSEON;
    if (sws >= 2)
        cr |= F4_PLLON;
    hse_ok = c->s->hse_hz && ((cr & F4_HSEBYP) != 0) == (c->s->hse_clock != 0);
    set_bit(&cr, F4_HSIRDY, (cr & F4_HSION) != 0);
    set_bit(&cr, F4_HSERDY, (cr & F4_HSEON) && hse_ok);
    src_ok = pll_hse ? (cr & F4_HSERDY) != 0 : (cr & F4_HSIRDY) != 0;
    set_bit(&cr, F4_PLLRDY, (cr & F4_PLLON) && src_ok);
    set_bit(&cr, F4_PLLI2SRDY, (cr & F4_PLLI2SON) && src_ok);
    set_bit(&cr, F4_PLLSAIRDY, (cr & F4_PLLSAION) && src_ok);
    if (cr != c->cr->value)
        esk_rf_hw(c->cr, cr);
    if (c->csr) {
        v = c->csr->value;
        set_bit(&v, F4_LSIRDY, (v & F4_LSION) != 0);
        if (v != c->csr->value)
            esk_rf_hw(c->csr, v);
    }
    if (c->bdcr) {
        v = c->bdcr->value;
        set_bit(&v, F4_LSERDY, (v & F4_LSEON) && c->s->lse_hz && !(v & F4_LSEBYP));
        if (v != c->bdcr->value)
            esk_rf_hw(c->bdcr, v);
    }
    if (sw != sws && f4_src_ready(c, sw))
        esk_rf_hw(c->cfgr, (c->cfgr->value & ~(u64)0xC) | (u64)sw << 2);
    esk_stm32f4_pwr_settle(c->s);
    h = f4_hclk(c);
    if (h != c->hclk) {
        c->hclk = h;
        c->s->yield = 1;
        f4_check(c);
    } else if (c->cr->value & F4_PLLON) {
        f4_check(c);
    }
}

static u32 f4_core_hz(struct esk_mach *s)
{
    struct rcc *c = any_rcc(s);
    return c && c->f4 ? c->hclk : 0;
}

u32 esk_stm32_hclk(struct esk_mach *s)
{
    struct rcc *c = any_rcc(s);
    return c && (c->f1 || c->f4) ? c->hclk : 0;
}

u32 esk_stm32_pclk(struct esk_mach *s, int apb)
{
    struct rcc *c = any_rcc(s);
    if (c && c->f4)
        return c->hclk / f4_ppre_div(c, apb);
    return esk_stm32f1_pclk(s, apb);
}

u32 esk_stm32_timclk(struct esk_mach *s, int apb)
{
    struct rcc *c = any_rcc(s);
    u32 d;
    if (!c || !c->f4) {
        u32 p = esk_stm32f1_pclk(s, apb), h = esk_stm32f1_hclk(s);
        return p == h ? p : 2 * p;
    }
    d = f4_ppre_div(c, apb);
    if (c->dckcfgr && (c->dckcfgr->value >> 24 & 1)) /* TIMPRE */
        return d <= 4 ? c->hclk : 4 * (c->hclk / d);
    return d == 1 ? c->hclk : 2 * (c->hclk / d);
}

static u32 rcc_read(void *ctx, u32 off, int n)
{
    struct rcc *c = (struct rcc *)ctx;
    return esk_rf_read(c->p, off, n);
}

static void lsi_forced(struct rcc *c);

static void rcc_write(void *ctx, u32 off, int n, u32 v)
{
    struct rcc *c = (struct rcc *)ctx;
    int i;
    esk_stm32_rtc_sync(c->s); /* the RTC's time at the clocks as they were */
    if (c->f4 && c->bdcr && off - esk_rf_off(c->bdcr) < 4) {
        /* the backup domain is written only while PWR's DBP allows it */
        struct esk_rf_reg *pcr = other_reg(c->s, "PWR", "CR");
        if (pcr && !(pcr->value & (1u << 8)))
            return;
    }
    if (c->csr && off - esk_rf_off(c->csr) < 4 && (v & CSR_RMVF)) {
        c->csr_flags = 0; /* RMVF: the reset flags clear */
        v &= ~CSR_FLAGS;
        esk_rf_hw(c->csr, c->csr->value & ~(u64)CSR_FLAGS);
    }
    if (c->bdcr && off - esk_rf_off(c->bdcr) < 4 && (v & (1u << 16))) {
        /* BDRST: the backup domain's reset, the RTC's registers too */
        struct esk_rf_periph *rtc = esk_rf_periph(c->s->rf, "RTC");
        if (rtc) {
            int keep = rtc->backup;
            rtc->backup = 0;
            esk_rf_periph_reset(rtc);
            rtc->backup = keep;
        }
    }
    if (c->f4 && off - esk_rf_off(c->cr) < 4) {
        /* HSEBYP is written only while HSE is off (RM0390 6.3.1) */
        u64 was = c->cr->value;
        esk_rf_write(c->p, off, n, v);
        if (was & (F4_HSEON | F4_HSERDY))
            esk_rf_hw(c->cr, (c->cr->value & ~(u64)F4_HSEBYP) | (was & F4_HSEBYP));
    } else {
        esk_rf_write(c->p, off, n, v);
    }
    if (c->csr && (c->csr->value & CSR_RMVF))
        esk_rf_hw(c->csr, c->csr->value & ~(u64)CSR_RMVF);
    for (i = 0; i < c->nrst; i++)
        if (off - esk_rf_off(c->rst[i].r) < 4 && (c->rst[i].r->value & c->rst[i].m))
            esk_rf_periph_reset(c->rst[i].q); /* its registers to their reset values */
    settle(c);
    if (c->f1)
        f1_settle(c);
    if (c->f4)
        f4_settle(c);
    lsi_forced(c);
    if (c->bdcr)
        c->bdcr_kept = c->bdcr->value;
    esk_stm32_rtc_sync(c->s); /* and the new core clock from now on */
}

void esk_stm32_rcc_lsi(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "RCC") : 0;
    if (p && p->ops == &esk_stm32_rcc_ops)
        lsi_forced((struct rcc *)p->ctx);
}

/* CSR's reset flags: PINRSTF with every reset but a power-on one (the
 * part drives NRST low), and its cause's; kept until RMVF or power-on */
static void reset_flags(struct rcc *c)
{
    static const u32 cause_flag[6] = { 0, 0, 1u << 28, 1u << 29, 1u << 30, 1u << 31 };
    int cause = c->s->reset_cause;
    if (c->csr) {
        u32 f = cause == ESK_RESET_POWER || cause < 0 || cause > 5
                    ? (u32)c->csr->reset & CSR_FLAGS
                    : c->csr_flags | 1u << 26 | cause_flag[cause];
        esk_rf_hw(c->csr, (c->csr->value & ~(u64)CSR_FLAGS) | f);
        c->csr_flags = f;
    }
    if (c->bdcr) {
        if (cause != ESK_RESET_POWER)
            esk_rf_hw(c->bdcr, c->bdcr_kept); /* the backup domain's */
        c->bdcr_kept = c->bdcr->value;
    }
}

/* the IWDG forces LSI on while it runs */
static void lsi_forced(struct rcc *c)
{
    if (c->csr && esk_stm32_iwdg_running(c->s))
        esk_rf_hw(c->csr, c->csr->value | 2u);
}

static void rcc_reset(void *ctx)
{
    struct rcc *c = (struct rcc *)ctx;
    reset_flags(c);
    settle(c);
    if (c->f1) {
        c->hclk = 0;
        f1_settle(c);
    }
    if (c->f4) {
        c->hclk = 0;
        c->warned = 0;
        f4_settle(c);
    }
    lsi_forced(c);
}

const struct esk_dev_ops esk_stm32_rcc_ops = {
    "stm32-rcc", rcc_read, rcc_write, rcc_reset, 0, 0,
};

static int ends_with(const char *s, const char *t)
{
    size_t a = strlen(s), b = strlen(t);
    return a >= b && !strcmp(s + a - b, t);
}

/* the peripheral's reset bit in this RCC's *RSTR registers (an F1's GPIO
 * port's is IOPxRST) */
static void reset_bit(struct rcc *c, struct esk_rf_periph *q)
{
    char want[64];
    struct esk_rf_reg *apb2 = esk_rf_reg(c->p, "APB2RSTR");
    int k;
    snprintf(want, sizeof want, "%sRST", q->name);
    if (!strncmp(q->name, "GPIO", 4) && strlen(q->name) == 5 && !esk_rf_mask(apb2, want) &&
        esk_rf_mask(apb2, "IOPARST"))
        snprintf(want, sizeof want, "IOP%cRST", q->name[4]);
    for (k = 0; k < c->p->nreg; k++) {
        struct esk_rf_reg *r = c->p->reg[k];
        u64 m = ends_with(r->name, "RSTR") ? esk_rf_mask(r, want) : 0;
        if (m) {
            c->rst[c->nrst].q = q;
            c->rst[c->nrst].r = r;
            c->rst[c->nrst].m = m;
            c->nrst++;
            return;
        }
    }
}

/* the peripherals whose clock enables and resets are in this RCC */
static int gates(struct esk_mach *s, struct rcc *c)
{
    char want[64];
    struct esk_rf_periph *q;
    struct esk_rf_reg *apb2 = esk_rf_reg(c->p, "APB2ENR");
    int i, k;
    c->rst = (struct rst *)esk_mach_alloc(s, (size_t)(s->rf->np > 0 ? s->rf->np : 1) * sizeof *c->rst);
    if (!c->rst)
        return -1;
    for (i = 0; (q = esk_rf_periph_at(s->rf, i)) != 0; i++) {
        if (q != c->p && strlen(q->name) + 4 <= sizeof want)
            reset_bit(c, q);
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
        s->clock_ctx = c;
    }
    c->pllcfgr = esk_rf_reg(p, "PLLCFGR");
    c->f4 = !c->f1 && c->cr && c->cfgr && c->pllcfgr;
    c->csr = esk_rf_reg(p, "CSR");
    c->bdcr = esk_rf_reg(p, "BDCR");
    if (c->f4) {
        c->dckcfgr = esk_rf_reg(p, "DCKCFGR");
        s->core_hz = f4_core_hz;
        s->clock_ctx = c;
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
            if (c->f4 && (r == c->cr || r == c->cfgr || r == c->csr || r == c->bdcr))
                continue; /* f4_settle's */
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
    if (c->f4)
        f4_settle(c);
    return c;
}
