/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rcc_g4.c -- the STM32G4's reset and clock control (RM0440 7), over the
 * register file.
 *
 * The clocks (RM0440 7.2):
 *   - HSI16 16 MHz (CR.HSION, out of reset), HSE the board's crystal or
 *     clock (esk_mach.hse_hz), ready only as the board drives OSC_IN: a
 *     crystal with HSEBYP clear, a clock with it set; LSI 32 kHz
 *     (CSR.LSION, or forced by the IWDG); LSE the board's 32.768 kHz
 *     crystal (BDCR.LSEON, not with LSEBYP); HSI48 48 MHz (CRRCR.HSI48ON);
 *   - each is ready its start-up time after it is turned on and its ready
 *     bit then sets (HSIRDY, HSERDY, LSIRDY, LSERDY, HSI48RDY); off, it is
 *     not ready at once. LSE keeps running through every reset but a
 *     power-on one, as the backup domain does;
 *   - the main PLL from HSI16 or HSE (PLLCFGR.PLLSRC 10, 11; 00 none): its
 *     VCO's input is the source over PLLM + 1, its output that times PLLN;
 *     PLLP the VCO over PLLPDIV (2 to 31), or 7 or 17 (PLLP) while PLLPDIV
 *     is 0; PLLQ and PLLR the VCO over 2, 4, 6 or 8; each output on while
 *     its PLLxEN is set; PLLRDY sets the PLL's lock time after PLLON with
 *     its source ready;
 *   - SYSCLK from HSI16 (SW 01), HSE (10) or PLLR (11, with PLLREN),
 *     switched once the source is ready (CFGR.SWS); HCLK over HPRE (1 to
 *     512), PCLK1 and PCLK2 over PPRE1 and PPRE2 (1 to 16); the timers'
 *     clock is the APB's when its prescaler is 1, else twice it;
 *   - what the system clock uses cannot be turned off (RM0440 7.4.1:
 *     HSION, HSEON and PLLON stay set while used, directly or through the
 *     PLL), and HSEBYP keeps its value while HSE is on;
 *   - CIER, CIFR, CICR: a source that becomes ready with its RDYIE set sets
 *     its RDYF and makes the RCC interrupt pending; CICR's bits clear the
 *     flags (CICR reads 0);
 *   - the peripherals' kernel clocks as CCIPR and CCIPR2 select them
 *     (esk_stm32g4_kernel_hz: the U(S)ARTs and LPUART, the I2Cs, LPTIM1,
 *     the ADCs, the FDCANs, the 48 MHz clock, the QUADSPI, SAI1 and I2S);
 *   - MCO (CFGR.MCOSEL, MCOPRE) as a frequency (esk_stm32g4_mco_hz) for
 *     the pins' model;
 *   - BDCR is written only while PWR_CR1's DBP allows it; BDRST resets the
 *     backup domain (the RTC's and TAMP's registers);
 *   - what the datasheet forbids is said, once, as a warning
 *     (mcu.clock_out_of_spec): a VCO input outside 2.66 to 16 MHz or
 *     output outside 96 to 344 MHz, a PLL output above 170 MHz, SYSCLK
 *     above the voltage range's limit (PWR's: range 1 boost 170 MHz, range
 *     1 normal 150, range 2 26); and too few flash wait states for HCLK
 *     (mcu.flash_wait_states, RM0440 table 9): the part would fetch wrong
 *     instructions, the model only warns.
 *
 * Resets (RM0440 7.1): a peripheral's bit in an *RSTR register set by a
 * write resets it, its registers back to their reset values; CSR's flags
 * record each reset: BORRSTF and PINRSTF at power-on; PINRSTF with every
 * other (the part drives NRST low) and SFTRSTF, IWDGRSTF, WWDGRSTF,
 * LPWRRSTF or OBLRSTF (the option bytes' reload) with its cause, the
 * earlier flags kept until RMVF clears them; leaving Standby keeps them.
 * BDCR keeps its value through every reset but a power-on one; LSIRDY sets
 * while the IWDG runs, which forces LSI on.
 *
 * The start-up times, in DS12288's typical values: HSI16 0.8 us, an HSE
 * crystal 2 ms (an external clock six of its periods), LSI 80 us, LSE 2 s,
 * HSI48 2.5 us, the PLL's lock 15 us.
 *
 * Not modelled: the clock security systems (CSSON and LSECSSON are kept;
 * the board's oscillators never fail), the start-up times' spread and
 * their dependence on temperature, LSEDRV's drive strength, HSITRIM's and
 * HSI48CAL's effect on the frequencies (the nominal frequencies always),
 * the CRS's trimming of HSI48 (the CRS model's), a peripheral held in
 * reset while its *RSTR bit stays set (its reset happens on the write),
 * the brownout reset (BORRSTF only at power-on), the low-power modes'
 * sleep enables (*SMENR: a peripheral's clock in Sleep is its run
 * clock). */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

#define HSI_HZ 16000000u
#define LSI_HZ 32000u
#define HSI48_HZ 48000000u

/* CR */
#define HSION (1u << 8)
#define HSIRDY (1u << 10)
#define HSEON (1u << 16)
#define HSERDY (1u << 17)
#define HSEBYP (1u << 18)
#define PLLON (1u << 24)
#define PLLRDY (1u << 25)
/* CSR */
#define LSION (1u << 0)
#define LSIRDY (1u << 1)
#define RMVF (1u << 23)
#define CSR_FLAGS 0xfe000000u
#define OBLRSTF (1u << 25)
#define PINRSTF (1u << 26)
#define BORRSTF (1u << 27)
#define SFTRSTF (1u << 28)
#define IWDGRSTF (1u << 29)
#define WWDGRSTF (1u << 30)
#define LPWRRSTF (1u << 31)
/* BDCR */
#define LSEON (1u << 0)
#define LSERDY (1u << 1)
#define LSEBYP (1u << 2)
#define BDRST (1u << 16)
/* CRRCR */
#define HSI48ON (1u << 0)
#define HSI48RDY (1u << 1)
/* PLLCFGR */
#define PLLPEN (1u << 16)
#define PLLQEN (1u << 20)
#define PLLREN (1u << 24)

#define PS_US 1000000ull

/* the clock sources, the PLL after its sources */
enum { O_HSI, O_HSE, O_PLL, O_LSI, O_LSE, O_HSI48, O_N };
/* CIER/CIFR/CICR's bit for each */
static const int osc_irq_bit[O_N] = { 3, 4, 5, 0, 1, 10 };

enum { W_VCO_IN = 1, W_VCO_OUT = 2, W_SYSCLK = 4, W_FLASH = 8, W_PLL_OUT = 16 };

struct g4rcc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr, *cfgr, *pllcfgr, *cier, *cifr, *cicr, *csr, *bdcr, *crrcr, *ccipr, *ccipr2;
    struct osc { int ready, starting; u64 left_ps; } osc[O_N];
    int running, irq;
    u64 since;
    u32 hclk;
    unsigned warned;
    u32 csr_flags;
    u64 bdcr_kept;
    int obl;  /* the coming reset is the option bytes' reload */
    struct rst { struct esk_rf_periph *q; struct esk_rf_reg *r; u64 m; } *rst;
    int nrst;
};

static u32 bits(const struct esk_rf_reg *r, int lsb, int width)
{
    return r ? (u32)(r->value >> lsb) & ((1u << width) - 1) : 0;
}

static void set_bit(u64 *v, u32 bit, int on)
{
    *v = on ? (*v | bit) : (*v & ~(u64)bit);
}

static u32 g4_core_hz(struct esk_mach *s);

static struct g4rcc *g4(struct esk_mach *s)
{
    return s && s->core_hz == g4_core_hz ? (struct g4rcc *)s->clock_ctx : 0;
}

int esk_stm32g4_rcc(struct esk_mach *s)
{
    return g4(s) != 0;
}

/* ---- frequencies -------------------------------------------------------- */

static u32 pll_src_hz(const struct g4rcc *c)
{
    u32 src = bits(c->pllcfgr, 0, 2);
    return src == 2 ? HSI_HZ : src == 3 ? c->s->hse_hz : 0;
}

static u32 vco_in(const struct g4rcc *c)
{
    return pll_src_hz(c) / (bits(c->pllcfgr, 4, 4) + 1);
}

static u64 vco(const struct g4rcc *c)
{
    return (u64)vco_in(c) * bits(c->pllcfgr, 8, 7);
}

/* the PLL's P, Q and R outputs ('P', 'Q', 'R'): 0 while it is not locked or
 * the output is off */
static u32 pll_out(const struct g4rcc *c, char which)
{
    u32 d;
    if (!(c->cr->value & PLLRDY))
        return 0;
    switch (which) {
    case 'P':
        if (!(c->pllcfgr->value & PLLPEN))
            return 0;
        d = bits(c->pllcfgr, 27, 5);
        if (d == 1)
            return 0; /* reserved */
        if (!d)
            d = bits(c->pllcfgr, 17, 1) ? 17 : 7;
        break;
    case 'Q':
        if (!(c->pllcfgr->value & PLLQEN))
            return 0;
        d = 2 * (bits(c->pllcfgr, 21, 2) + 1);
        break;
    default:
        if (!(c->pllcfgr->value & PLLREN))
            return 0;
        d = 2 * (bits(c->pllcfgr, 25, 2) + 1);
        break;
    }
    return (u32)(vco(c) / d);
}

static u32 sysclk_of(const struct g4rcc *c, u32 sw)
{
    return sw == 1 ? HSI_HZ : sw == 2 ? c->s->hse_hz : sw == 3 ? pll_out(c, 'R') : HSI_HZ;
}

static u32 sysclk(const struct g4rcc *c)
{
    return sysclk_of(c, bits(c->cfgr, 2, 2));
}

static u32 hpre_div(u32 hpre)
{
    static const u32 d[8] = { 2, 4, 8, 16, 64, 128, 256, 512 };
    return hpre & 8 ? d[hpre & 7] : 1;
}

static u32 ppre_div(const struct g4rcc *c, int apb)
{
    u32 v = bits(c->cfgr, apb == 2 ? 11 : 8, 3);
    return v & 4 ? 2u << (v & 3) : 1;
}

static u32 calc_hclk(const struct g4rcc *c)
{
    return sysclk(c) / hpre_div(bits(c->cfgr, 4, 4));
}

static int src_ready(const struct g4rcc *c, u32 sw)
{
    if (sw == 1)
        return c->osc[O_HSI].ready;
    if (sw == 2)
        return c->osc[O_HSE].ready;
    if (sw == 3)
        return c->osc[O_PLL].ready && (c->pllcfgr->value & PLLREN);
    return 0;
}

static struct esk_rf_reg *other_reg(struct esk_mach *s, const char *periph, const char *reg)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, periph) : 0;
    return p ? esk_rf_reg(p, reg) : 0;
}

/* ---- the datasheet's limits ----------------------------------------------- */

static void warn(struct g4rcc *c, unsigned w, const char *code, const char *fmt, u32 a, u32 b)
{
    char msg[200];
    if (c->warned & w)
        return;
    c->warned |= w;
    snprintf(msg, sizeof msg, fmt, a, b);
    esk_rf_diag(c->s, ES_SEV_WARNING, code, "%s", msg);
}

/* PWR's voltage range: 1 boost, 1 normal or 2 (PWR_CR1.VOS, PWR_CR5.R1MODE) */
static u32 range_limit(struct esk_mach *s, u32 *lim)
{
    struct esk_rf_reg *cr1 = other_reg(s, "PWR", "CR1"), *cr5 = other_reg(s, "PWR", "CR5");
    u32 vos = cr1 ? (u32)(cr1->value >> 9 & 3) : 1;
    if (vos == 2) {
        *lim = 26000000u;
        return 2;
    }
    if (cr5 && !(cr5->value & (1u << 8))) {
        *lim = 170000000u;
        return 0; /* range 1 boost (R1MODE 0) */
    }
    *lim = 150000000u;
    return 1;
}

/* RM0440 table 9: the highest HCLK each wait-state count allows */
static u32 flash_limit(u32 range, u32 ws)
{
    static const u32 boost[5] = { 34, 68, 102, 136, 170 }, normal[5] = { 30, 60, 90, 120, 150 },
                     r2[3] = { 12, 24, 26 };
    if (range == 2)
        return ws < 3 ? r2[ws] * 1000000u : 26000000u;
    if (ws > 4)
        return 170000000u;
    return (range == 0 ? boost[ws] : normal[ws]) * 1000000u;
}

static void check(struct g4rcc *c)
{
    u32 lim, range = range_limit(c->s, &lim), sys = sysclk(c);
    struct esk_rf_reg *acr = other_reg(c->s, "FLASH", "ACR");
    if (c->cr->value & PLLON) {
        u32 in = vco_in(c);
        u64 out = vco(c);
        if (in && (in < 2660000u || in > 16000000u))
            warn(c, W_VCO_IN, "mcu.clock_out_of_spec",
                 "the PLL's VCO input is %u Hz, outside 2.66 to 16 MHz (DS12288)", in, 0);
        if (out && (out < 96000000u || out > 344000000u))
            warn(c, W_VCO_OUT, "mcu.clock_out_of_spec",
                 "the PLL's VCO output is %u Hz, outside 96 to 344 MHz (DS12288)", (u32)out, 0);
        if (pll_out(c, 'R') > 170000000u || pll_out(c, 'Q') > 170000000u || pll_out(c, 'P') > 170000000u)
            warn(c, W_PLL_OUT, "mcu.clock_out_of_spec", "a PLL output is above 170 MHz (DS12288)", 0, 0);
    }
    if (sys > lim)
        warn(c, W_SYSCLK, "mcu.clock_out_of_spec", "SYSCLK %u Hz is above the voltage range's %u Hz (RM0440 6.1.5)",
             sys, lim);
    if (acr && c->hclk > flash_limit(range, (u32)(acr->value & 15)))
        warn(c, W_FLASH, "mcu.flash_wait_states",
             "HCLK %u Hz needs more flash wait states than LATENCY %u (RM0440 table 9)", c->hclk,
             (u32)(acr->value & 15));
}

void esk_stm32g4_rcc_check(struct esk_mach *s)
{
    struct g4rcc *c = g4(s);
    if (c) {
        c->warned &= ~(unsigned)(W_SYSCLK | W_FLASH); /* a new setting: said again if still wrong */
        check(c);
    }
}

/* ---- the oscillators' start-ups ---------------------------------------- */

static u64 startup_ps(const struct g4rcc *c, int i)
{
    switch (i) {
    case O_HSI: return 800000ull;
    case O_HSE:
        if (c->s->hse_clock)
            return c->s->hse_hz ? 6000000000000ull / c->s->hse_hz : 1;
        return 2000ull * PS_US;
    case O_PLL: return 15ull * PS_US;
    case O_LSI: return 80ull * PS_US;
    case O_LSE: return 2000000ull * PS_US;
    default: return 2500000ull; /* HSI48 */
    }
}

static void clock_run(struct g4rcc *c)
{
    int i, any = 0;
    for (i = 0; i < O_N; i++)
        any |= c->osc[i].starting;
    if (any && !c->running)
        c->since = c->s->cycles;
    esk_rf_clock(c->p, &c->running, any);
}

static void follow(struct g4rcc *c, int i, int want)
{
    struct osc *o = &c->osc[i];
    if (!want) {
        o->ready = o->starting = 0;
    } else if (!o->ready && !o->starting) {
        o->starting = 1;
        o->left_ps = startup_ps(c, i);
    }
}

static int ready_bit(const struct g4rcc *c, int i)
{
    switch (i) {
    case O_HSI: return (c->cr->value & HSIRDY) != 0;
    case O_HSE: return (c->cr->value & HSERDY) != 0;
    case O_PLL: return (c->cr->value & PLLRDY) != 0;
    case O_LSI: return c->csr && (c->csr->value & LSIRDY);
    case O_LSE: return c->bdcr && (c->bdcr->value & LSERDY);
    default: return c->crrcr && (c->crrcr->value & HSI48RDY);
    }
}

/* the ready bits, the clock in use; a new HCLK ends the quantum */
static void settle(struct g4rcc *c)
{
    u64 cr = c->cr->value, v;
    u32 sws = bits(c->cfgr, 2, 2), sw = bits(c->cfgr, 0, 2), src = bits(c->pllcfgr, 0, 2), h;
    int want[O_N], i, hse_ok;
    unsigned was = 0;
    for (i = 0; i < O_N; i++)
        if (ready_bit(c, i))
            was |= 1u << i;
    /* what the system clock uses cannot be turned off */
    if (sws == 1 || (sws == 3 && src == 2))
        cr |= HSION;
    if (sws == 2 || (sws == 3 && src == 3))
        cr |= HSEON;
    if (sws == 3)
        cr |= PLLON;
    hse_ok = c->s->hse_hz && ((cr & HSEBYP) != 0) == (c->s->hse_clock != 0);
    want[O_HSI] = (cr & HSION) != 0;
    want[O_HSE] = (cr & HSEON) && hse_ok;
    want[O_LSI] = c->csr && ((c->csr->value & LSION) || esk_stm32_iwdg_running(c->s));
    want[O_LSE] = c->bdcr && (c->bdcr->value & LSEON) && c->s->lse_hz && !(c->bdcr->value & LSEBYP);
    want[O_HSI48] = c->crrcr && (c->crrcr->value & HSI48ON);
    for (i = 0; i < O_N; i++) {
        if (i == O_PLL) {
            int src_ok = src == 2 ? c->osc[O_HSI].ready : src == 3 ? c->osc[O_HSE].ready : 0;
            want[O_PLL] = (cr & PLLON) && src_ok;
        }
        follow(c, i, want[i]);
    }
    set_bit(&cr, HSIRDY, c->osc[O_HSI].ready);
    set_bit(&cr, HSERDY, c->osc[O_HSE].ready);
    set_bit(&cr, PLLRDY, c->osc[O_PLL].ready);
    if (cr != c->cr->value)
        esk_rf_hw(c->cr, cr);
    if (c->csr) {
        v = c->csr->value;
        set_bit(&v, LSIRDY, c->osc[O_LSI].ready);
        if (v != c->csr->value)
            esk_rf_hw(c->csr, v);
    }
    if (c->bdcr) {
        v = c->bdcr->value;
        set_bit(&v, LSERDY, c->osc[O_LSE].ready);
        if (v != c->bdcr->value)
            esk_rf_hw(c->bdcr, v);
    }
    if (c->crrcr) {
        v = c->crrcr->value;
        set_bit(&v, HSI48RDY, c->osc[O_HSI48].ready);
        if (v != c->crrcr->value)
            esk_rf_hw(c->crrcr, v);
    }
    /* CIFR: a source that has become ready with its interrupt enabled */
    for (i = 0; i < O_N; i++)
        if (c->osc[i].ready && !(was >> i & 1) && c->cier && c->cifr) {
            u64 f = (u64)1 << osc_irq_bit[i];
            if ((c->cier->value & f) && !(c->cifr->value & f)) {
                esk_rf_hw(c->cifr, c->cifr->value | f);
                if (c->irq >= 0)
                    esk_rf_interrupt(c->p, c->irq);
            }
        }
    clock_run(c);
    if (sw != sws && src_ready(c, sw))
        esk_rf_hw(c->cfgr, (c->cfgr->value & ~(u64)0xC) | (u64)sw << 2);
    else if (sws == 3 && !src_ready(c, 3))
        esk_rf_hw(c->cfgr, (c->cfgr->value & ~(u64)0xF) | 5u); /* the PLL lost: back to HSI16 */
    h = calc_hclk(c);
    if (h != c->hclk) {
        c->hclk = h;
        c->s->yield = 1;
    }
    check(c);
}

/* ---- the clocks other models ask for --------------------------------------- */

static u32 g4_core_hz(struct esk_mach *s)
{
    struct g4rcc *c = g4(s);
    return c ? c->hclk : 0;
}

u32 esk_stm32g4_hclk(struct esk_mach *s)
{
    struct g4rcc *c = g4(s);
    return c ? c->hclk : 0;
}

u32 esk_stm32g4_pclk(struct esk_mach *s, int apb)
{
    struct g4rcc *c = g4(s);
    return c ? c->hclk / ppre_div(c, apb) : 0;
}

u32 esk_stm32g4_timclk(struct esk_mach *s, int apb)
{
    struct g4rcc *c = g4(s);
    u32 d;
    if (!c)
        return 0;
    d = ppre_div(c, apb);
    return d == 1 ? c->hclk : 2 * (c->hclk / d);
}

u32 esk_stm32g4_sysclk(struct esk_mach *s)
{
    struct g4rcc *c = g4(s);
    return c ? sysclk(c) : 0;
}

int esk_stm32g4_osc_ready(struct esk_mach *s, char which)
{
    struct g4rcc *c = g4(s);
    if (!c)
        return 0;
    switch (which) {
    case 'H': return c->osc[O_HSI].ready;
    case 'E': return c->osc[O_HSE].ready;
    case 'I': return c->osc[O_LSI].ready;
    case 'L': return c->osc[O_LSE].ready;
    case '4': return c->osc[O_HSI48].ready;
    default: return 0;
    }
}

u32 esk_stm32g4_pll_hz(struct esk_mach *s, char which)
{
    struct g4rcc *c = g4(s);
    return c ? pll_out(c, which) : 0;
}

/* a 2-bit CCIPR selection of PCLK (00), SYSCLK (01), HSI16 (10), LSE (11) */
static u32 sel_pclk_sys_hsi_lse(const struct g4rcc *c, u32 sel, int apb)
{
    switch (sel) {
    case 0: return c->hclk / ppre_div(c, apb);
    case 1: return sysclk(c);
    case 2: return c->osc[O_HSI].ready ? HSI_HZ : 0;
    default: return c->osc[O_LSE].ready ? c->s->lse_hz : 0;
    }
}

u32 esk_stm32g4_kernel_hz(struct esk_mach *s, const char *periph)
{
    struct g4rcc *c = g4(s);
    u32 sel;
    if (!c || !periph)
        return 0;
    if (!strcmp(periph, "USART1"))
        return sel_pclk_sys_hsi_lse(c, bits(c->ccipr, 0, 2), 2);
    if (!strcmp(periph, "USART2"))
        return sel_pclk_sys_hsi_lse(c, bits(c->ccipr, 2, 2), 1);
    if (!strcmp(periph, "USART3"))
        return sel_pclk_sys_hsi_lse(c, bits(c->ccipr, 4, 2), 1);
    if (!strcmp(periph, "UART4"))
        return sel_pclk_sys_hsi_lse(c, bits(c->ccipr, 6, 2), 1);
    if (!strcmp(periph, "UART5"))
        return sel_pclk_sys_hsi_lse(c, bits(c->ccipr, 8, 2), 1);
    if (!strcmp(periph, "LPUART1"))
        return sel_pclk_sys_hsi_lse(c, bits(c->ccipr, 10, 2), 1);
    if (!strncmp(periph, "I2C", 3)) {
        int k = periph[3] - '0';
        sel = k == 4 ? bits(c->ccipr2, 0, 2) : k >= 1 && k <= 3 ? bits(c->ccipr, 12 + 2 * (k - 1), 2) : 0;
        return sel == 3 ? 0 : sel_pclk_sys_hsi_lse(c, sel, 1);
    }
    if (!strcmp(periph, "LPTIM1")) {
        sel = bits(c->ccipr, 18, 2);
        return sel == 0 ? c->hclk / ppre_div(c, 1)
             : sel == 1 ? (c->osc[O_LSI].ready ? LSI_HZ : 0)
             : sel == 2 ? (c->osc[O_HSI].ready ? HSI_HZ : 0)
                        : (c->osc[O_LSE].ready ? c->s->lse_hz : 0);
    }
    if (!strcmp(periph, "SAI1") || !strcmp(periph, "I2S23")) {
        sel = bits(c->ccipr, !strcmp(periph, "SAI1") ? 20 : 22, 2);
        return sel == 0 ? sysclk(c) : sel == 1 ? pll_out(c, 'Q') : sel == 3 ? (c->osc[O_HSI].ready ? HSI_HZ : 0) : 0;
    }
    if (!strncmp(periph, "FDCAN", 5)) {
        sel = bits(c->ccipr, 24, 2);
        return sel == 0 ? (c->osc[O_HSE].ready ? c->s->hse_hz : 0) : sel == 1 ? pll_out(c, 'Q')
             : sel == 2 ? c->hclk / ppre_div(c, 1) : 0;
    }
    if (!strcmp(periph, "CLK48")) {
        sel = bits(c->ccipr, 26, 2);
        return sel == 0 ? (c->osc[O_HSI48].ready ? HSI48_HZ : 0) : sel == 2 ? pll_out(c, 'Q') : 0;
    }
    if (!strcmp(periph, "ADC12") || !strcmp(periph, "ADC345")) {
        sel = bits(c->ccipr, !strcmp(periph, "ADC12") ? 28 : 30, 2);
        return sel == 1 ? pll_out(c, 'P') : sel == 2 ? sysclk(c) : 0;
    }
    if (!strcmp(periph, "QUADSPI")) {
        sel = bits(c->ccipr2, 20, 2);
        return sel == 0 ? sysclk(c) : sel == 1 ? (c->osc[O_HSI].ready ? HSI_HZ : 0) : sel == 2 ? pll_out(c, 'Q') : 0;
    }
    if (!strcmp(periph, "RTC")) {
        sel = c->bdcr ? (u32)(c->bdcr->value >> 8 & 3) : 0;
        return sel == 1 ? (c->osc[O_LSE].ready ? c->s->lse_hz : 0) : sel == 2 ? (c->osc[O_LSI].ready ? LSI_HZ : 0)
             : sel == 3 ? (c->osc[O_HSE].ready ? c->s->hse_hz / 32 : 0) : 0;
    }
    return 0;
}

u32 esk_stm32g4_mco_hz(struct esk_mach *s)
{
    struct g4rcc *c = g4(s);
    u32 sel, pre, hz;
    if (!c)
        return 0;
    sel = bits(c->cfgr, 24, 4);
    pre = bits(c->cfgr, 28, 3);
    switch (sel) {
    case 1: hz = sysclk(c); break;
    case 3: hz = c->osc[O_HSI].ready ? HSI_HZ : 0; break;
    case 4: hz = c->osc[O_HSE].ready ? c->s->hse_hz : 0; break;
    case 5: hz = pll_out(c, 'R') ? (u32)(vco(c) / (2 * (bits(c->pllcfgr, 25, 2) + 1))) : 0; break;
    case 6: hz = c->osc[O_LSI].ready ? LSI_HZ : 0; break;
    case 7: hz = c->osc[O_LSE].ready ? c->s->lse_hz : 0; break;
    case 8: hz = c->osc[O_HSI48].ready ? HSI48_HZ : 0; break;
    default: hz = 0; break;
    }
    return pre <= 4 ? hz >> pre : 0;
}

/* ---- Stop and Standby -------------------------------------------------------- */

void esk_stm32g4_rcc_stop(struct esk_mach *s, int wake_hsi)
{
    struct g4rcc *c = g4(s);
    u64 cr;
    if (!c)
        return;
    esk_stm32_rtc_sync(s);
    /* RM0440 7.2.7: HSE and the PLL stop; the part wakes on HSI16 (STOPWUCK
     * is the G4's HSI16 only) */
    cr = c->cr->value & ~(u64)(HSEON | PLLON);
    (void)wake_hsi;
    esk_rf_hw(c->cr, cr | HSION);
    esk_rf_hw(c->cfgr, (c->cfgr->value & ~(u64)0xF) | 5u);
    settle(c);
    esk_stm32_rtc_sync(s);
}

void esk_stm32g4_rcc_obl(struct esk_mach *s)
{
    struct g4rcc *c = g4(s);
    if (c)
        c->obl = 1;
}

/* ---- the device -------------------------------------------------------------- */

static u32 tick_hz(const struct g4rcc *c) { return c->hclk >= 1000u ? c->hclk : HSI_HZ; }

static void rcc_tick(void *ctx, u32 cycles)
{
    struct g4rcc *c = (struct g4rcc *)ctx;
    u64 ps;
    int i, done = 0;
    if (!c->running)
        return;
    if (c->since > c->s->cycles)
        c->since = 0;
    cycles = esk_mach_after(c->s, cycles, c->since);
    c->since = 0;
    ps = (u64)cycles * 1000000000ull / (tick_hz(c) / 1000u);
    for (i = 0; i < O_N; i++) {
        struct osc *o = &c->osc[i];
        if (!o->starting)
            continue;
        if (o->left_ps <= ps) {
            o->starting = 0;
            o->ready = 1;
            done = 1;
        } else {
            o->left_ps -= ps;
        }
    }
    if (done) {
        esk_stm32_rtc_sync(c->s);
        settle(c);
        esk_stm32_rtc_sync(c->s);
        c->s->yield = 1;
    } else {
        clock_run(c);
    }
}

static int rcc_next(void *ctx, u32 *cycles)
{
    struct g4rcc *c = (struct g4rcc *)ctx;
    u64 best = ~(u64)0, k = tick_hz(c) / 1000u;
    int i;
    if (!c->running)
        return 0;
    for (i = 0; i < O_N; i++)
        if (c->osc[i].starting) {
            u64 n = (c->osc[i].left_ps * k + 999999999ull) / 1000000000ull;
            if (n < best)
                best = n;
        }
    if (best == ~(u64)0)
        return 0;
    *cycles = best == 0 ? 1 : best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static u32 rcc_read(void *ctx, u32 off, int n)
{
    struct g4rcc *c = (struct g4rcc *)ctx;
    return esk_rf_read(c->p, off, n);
}

static void lsi_forced(struct g4rcc *c)
{
    if (esk_stm32_iwdg_running(c->s) && !c->osc[O_LSI].ready && !c->osc[O_LSI].starting)
        settle(c);
}

static void rcc_write(void *ctx, u32 off, int n, u32 v)
{
    struct g4rcc *c = (struct g4rcc *)ctx;
    int i;
    esk_stm32_rtc_sync(c->s);
    if (c->bdcr && off - esk_rf_off(c->bdcr) < 4) {
        struct esk_rf_reg *pcr = other_reg(c->s, "PWR", "CR1");
        if (pcr && !(pcr->value & (1u << 8)))
            return; /* DBP clear: the backup domain is write-protected */
    }
    if (c->csr && off - esk_rf_off(c->csr) < 4 && (v & RMVF)) {
        c->csr_flags = 0;
        v &= ~CSR_FLAGS;
        esk_rf_hw(c->csr, c->csr->value & ~(u64)CSR_FLAGS);
    }
    if (c->bdcr && off - esk_rf_off(c->bdcr) < 4 && (v & BDRST)) {
        static const char *const dom[2] = { "RTC", "TAMP" };
        int k;
        for (k = 0; k < 2; k++) {
            struct esk_rf_periph *q = esk_rf_periph(c->s->rf, dom[k]);
            if (q) {
                int keep = q->backup;
                q->backup = 0;
                esk_rf_periph_reset(q);
                q->backup = keep;
            }
        }
    }
    if (c->cicr && off - esk_rf_off(c->cicr) < 4) {
        if (c->cifr)
            esk_rf_hw(c->cifr, c->cifr->value & ~(u64)(v & 0x7bbu));
        return; /* CICR reads 0 */
    }
    if (off - esk_rf_off(c->cr) < 4) {
        u64 was = c->cr->value;
        esk_rf_write(c->p, off, n, v);
        if (was & (HSEON | HSERDY)) /* HSEBYP kept while HSE is on */
            esk_rf_hw(c->cr, (c->cr->value & ~(u64)HSEBYP) | (was & HSEBYP));
    } else {
        esk_rf_write(c->p, off, n, v);
    }
    if (c->csr && (c->csr->value & RMVF))
        esk_rf_hw(c->csr, c->csr->value & ~(u64)RMVF);
    if (c->bdcr && (c->bdcr->value & BDRST) && off - esk_rf_off(c->bdcr) < 4) {
        esk_rf_hw(c->bdcr, c->bdcr->value & BDRST); /* the domain's reset: all else cleared */
        c->osc[O_LSE].ready = c->osc[O_LSE].starting = 0;
    }
    for (i = 0; i < c->nrst; i++)
        if (off - esk_rf_off(c->rst[i].r) < 4 && (c->rst[i].r->value & c->rst[i].m))
            esk_rf_periph_reset(c->rst[i].q);
    settle(c);
    lsi_forced(c);
    if (c->bdcr)
        c->bdcr_kept = c->bdcr->value;
    esk_stm32_rtc_sync(c->s);
}

void esk_stm32g4_rcc_lsi(struct esk_mach *s)
{
    struct g4rcc *c = g4(s);
    if (c)
        lsi_forced(c);
}

static void reset_flags(struct g4rcc *c)
{
    int cause = c->s->reset_cause;
    if (c->csr) {
        u32 f;
        if (cause == ESK_RESET_POWER || cause < 0)
            f = BORRSTF | PINRSTF;
        else if (cause == ESK_RESET_STANDBY && esk_stm32g4_pwr_was_shutdown(c->s))
            f = c->csr_flags | BORRSTF | PINRSTF; /* leaving Shutdown: a brownout reset */
        else if (cause == ESK_RESET_STANDBY)
            f = c->csr_flags; /* leaving Standby: the flags kept */
        else {
            f = c->csr_flags | PINRSTF;
            if (c->obl)
                f |= OBLRSTF;
            else if (cause == ESK_RESET_SOFTWARE)
                f |= SFTRSTF;
            else if (cause == ESK_RESET_WATCHDOG)
                f |= IWDGRSTF;
            else if (cause == ESK_RESET_WINDOW)
                f |= WWDGRSTF;
            else if (cause == ESK_RESET_LOW_POWER)
                f |= LPWRRSTF;
        }
        esk_rf_hw(c->csr, (c->csr->value & ~(u64)CSR_FLAGS) | f);
        c->csr_flags = f;
    }
    c->obl = 0;
    if (c->bdcr) {
        if (c->s->reset_cause != ESK_RESET_POWER)
            esk_rf_hw(c->bdcr, c->bdcr_kept);
        c->bdcr_kept = c->bdcr->value;
    }
}

static void rcc_reset(void *ctx)
{
    struct g4rcc *c = (struct g4rcc *)ctx;
    int i, k, listed = 0;
    reset_flags(c);
    c->hclk = 0;
    c->warned = 0;
    for (k = 0; k < c->p->rf->ntk; k++)
        listed |= c->p->rf->tk[k] == c->p;
    if (listed)
        esk_rf_clock(c->p, &c->running, 0);
    c->running = 0;
    for (i = 0; i < O_N; i++)
        if (i != O_LSE || c->s->reset_cause == ESK_RESET_POWER)
            c->osc[i].ready = c->osc[i].starting = 0;
    c->osc[O_HSI].ready = 1; /* the part leaves reset on HSI16 */
    settle(c);
    c->since = 0;
    lsi_forced(c);
}

const struct esk_dev_ops esk_stm32g4_rcc_ops = {
    "stm32g4-rcc", rcc_read, rcc_write, rcc_reset, rcc_tick, rcc_next,
};

/* ---- gates and resets ---------------------------------------------------------- */

/* the clock-enable field's stem of a peripheral the RCC does not name after
 * it (DMAMUX1EN for DMAMUX, ADC12EN for ADC1), or its own name */
static const char *gate_stem(const char *name)
{
    static const char *const map[][2] = {
        { "DMAMUX", "DMAMUX1" }, { "ADC1", "ADC12" }, { "ADC2", "ADC12" }, { "ADC12_Common", "ADC12" },
        { "ADC3", "ADC345" }, { "ADC4", "ADC345" }, { "ADC5", "ADC345" }, { "ADC345_Common", "ADC345" },
        { "QUADSPI", "QSPI" }, { "RTC", "RTCAPB" }, { "TAMP", "RTCAPB" }, { "USB_FS_device", "USB" },
        { "FDCAN1", "FDCAN" }, { "FDCAN2", "FDCAN" }, { "FDCAN3", "FDCAN" }, { "COMP", "SYSCFG" },
        { "OPAMP", "SYSCFG" }, { "VREFBUF", "SYSCFG" }, { "SAI", "SAI1" },
    };
    size_t i;
    if (!strncmp(name, "HRTIM_", 6))
        return "HRTIM1";
    for (i = 0; i < sizeof map / sizeof map[0]; i++)
        if (!strcmp(name, map[i][0]))
            return map[i][1];
    return name;
}

static int ends_with(const char *s, const char *t)
{
    size_t a = strlen(s), b = strlen(t);
    return a >= b && !strcmp(s + a - b, t);
}

static int gates(struct esk_mach *s, struct g4rcc *c)
{
    struct esk_rf_periph *q;
    int i, k;
    c->rst = (struct rst *)esk_mach_alloc(s, (size_t)(s->rf->np > 0 ? s->rf->np : 1) * sizeof *c->rst);
    if (!c->rst)
        return -1;
    for (i = 0; (q = esk_rf_periph_at(s->rf, i)) != 0; i++) {
        char en[64], rst[64];
        const char *stem;
        if (q == c->p || !strcmp(q->name, "FLASH") || strlen(q->name) > 40)
            continue; /* the flash interface's clock goes off only in power-down */
        stem = gate_stem(q->name);
        snprintf(en, sizeof en, "%sEN", stem);
        snprintf(rst, sizeof rst, "%sRST", stem);
        for (k = 0; k < c->p->nreg; k++) {
            struct esk_rf_reg *r = c->p->reg[k];
            u64 m;
            if (!q->gate && (ends_with(r->name, "ENR") || ends_with(r->name, "ENR1") || ends_with(r->name, "ENR2")) &&
                !strstr(r->name, "SMENR") && (m = esk_rf_mask(r, en)) != 0) {
                size_t l = strlen(c->p->name) + strlen(r->name) + strlen(en) + 3;
                char *g = (char *)esk_mach_alloc(s, l);
                if (!g)
                    return -1;
                snprintf(g, l, "%s.%s.%s", c->p->name, r->name, en);
                q->gate = r;
                q->gate_mask = m;
                q->gate_name = g;
            }
            if (strstr(r->name, "RSTR") && (m = esk_rf_mask(r, rst)) != 0) {
                c->rst[c->nrst].q = q;
                c->rst[c->nrst].r = r;
                c->rst[c->nrst].m = m;
                c->nrst++;
            }
        }
    }
    return 0;
}

void *esk_stm32g4_rcc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct g4rcc *c;
    if (!esk_rf_reg(p, "PLLCFGR") || !esk_rf_reg(p, "CIER") || !esk_rf_reg(p, "CCIPR"))
        return 0; /* not the G4's layout */
    c = (struct g4rcc *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    c->s = s;
    c->p = p;
    c->cr = esk_rf_reg(p, "CR");
    c->cfgr = esk_rf_reg(p, "CFGR");
    c->pllcfgr = esk_rf_reg(p, "PLLCFGR");
    c->cier = esk_rf_reg(p, "CIER");
    c->cifr = esk_rf_reg(p, "CIFR");
    c->cicr = esk_rf_reg(p, "CICR");
    c->csr = esk_rf_reg(p, "CSR");
    c->bdcr = esk_rf_reg(p, "BDCR");
    c->crrcr = esk_rf_reg(p, "CRRCR");
    c->ccipr = esk_rf_reg(p, "CCIPR");
    c->ccipr2 = esk_rf_reg(p, "CCIPR2");
    if (!c->cr || !c->cfgr)
        return 0;
    c->irq = esk_rf_irq(p, "RCC");
    c->osc[O_HSI].ready = 1;
    p->always_on = 1; /* LSE and LSI start through Stop too */
    s->core_hz = g4_core_hz;
    s->clock_ctx = c;
    if (gates(s, c))
        return 0;
    settle(c);
    return c;
}
