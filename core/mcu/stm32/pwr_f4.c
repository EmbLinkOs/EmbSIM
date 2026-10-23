/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* pwr_f4.c -- the STM32F4's power controller (RM0390 5): its registers
 * keep what is written, and CSR's ready bits follow what sets them:
 *   - VOSRDY: the regulator's voltage scale is applied once the main PLL
 *     runs (RM0390 5.1.3: with the PLL off the regulator stays in scale 3,
 *     whatever VOS says), so it is set while RCC's PLLRDY is;
 *   - ODRDY: over-drive is ready once ODEN is set with the PLL running and
 *     VOS at scale 1 or 2 (RM0390 5.1.4's sequence: PLL on, ODEN, wait
 *     ODRDY, ODSWEN, wait ODSWRDY, then switch SYSCLK to the PLL);
 *   - ODSWRDY: the switch to over-drive is done once ODSWEN is set with
 *     ODRDY;
 *   - BRR: the backup regulator is ready while BRE is set.
 * The voltage scale and over-drive bound SYSCLK; the RCC model says when
 * it is above them (rcc.c). DBP gates the backup domain's registers (RCC's
 * BDCR, rcc.c).
 *
 * The low-power modes (RM0390 5.3): the core's WFI or WFE with SCR's
 * SLEEPDEEP set enters
 *   - Stop (PDDS clear): the clocks of the 1.2 V domain stop (the core,
 *     SysTick and every peripheral but the RTC and the IWDG, which run on
 *     LSE or LSI), HSE and the PLLs turn off (rcc.c), RAM, the registers
 *     and the pins keep their state; any EXTI line's interrupt or event
 *     wakes the core, which runs on HSI, the prescalers as they were.
 *     UDEN 11 with LPUDS (and LPDS) or MRUDS (without) is the under-drive
 *     Stop: UDRDY sets as it is entered, and is cleared by writing 1;
 *   - Standby (PDDS set): the 1.2 V domain is off. RAM is lost, and the
 *     backup SRAM too unless BRE keeps its regulator on; the pins let go
 *     (inputs without pulls; the enabled WKUP pins, PA0 for EWUP and PC13
 *     for EWUP2, inputs with a pull-down, as EWUP makes them in every
 *     mode; RTC_OUT on PC13 as the RTC drives it); no interrupt wakes the
 *     core. A wake-up event -- a WKUP pin's rising edge, an RTC alarm,
 *     wake-up, tamper or timestamp event with its interrupt enabled --
 *     sets WUF and ends Standby, as do NRST and the IWDG: the part resets
 *     as at power-on (RCC's CSR flags as at power-on, rcc.c) but for the
 *     backup domain, and CSR keeps its bits with SBF set. WUF already set
 *     as Standby is entered ends it at once; enabling a WKUP pin that is
 *     high is a wake-up event.
 * The option bytes' nRST_STOP or nRST_STDBY cleared makes entering that
 * mode a reset instead (LPWRRSTF). CWUF clears WUF and CSBF clears SBF.
 * Through a system reset CSR keeps SBF (cleared only at power-on or by
 * CSBF) and BRE (the backup domain's); leaving Standby, all of it.
 *
 * Not modelled: the regulator's settling times and the low-power modes'
 * wake-up times (DS10693's low-power mode wake-up timings: the core runs
 * at the wake-up event), the
 * regulator's modes (LPDS, FPDS, LPLVDS, MRLVDS: the supply current is
 * not modelled), the programmable voltage detector (PVDE, PLS, PVDO), the
 * debug unit's DBG_STOP and DBG_STANDBY (DBGMCU_CR: a debugger's halt
 * stops all of the machine's time), and the tamper and timestamp pin's
 * state in Standby beyond RTC_OUT. */
#include "mcu/stm32/stm32.h"

#define CR_LPDS (1u << 0)
#define CR_PDDS (1u << 1)
#define CR_CWUF (1u << 2)
#define CR_CSBF (1u << 3)
#define CR_LPUDS (1u << 10)
#define CR_MRUDS (1u << 11)
#define CR_VOS_LSB 14
#define CR_ODEN (1u << 16)
#define CR_ODSWEN (1u << 17)
#define CR_UDEN (3u << 18)
#define CSR_WUF (1u << 0)
#define CSR_SBF (1u << 1)
#define CSR_BRR (1u << 3)
#define CSR_EWUP2 (1u << 7)
#define CSR_EWUP1 (1u << 8)
#define CSR_BRE (1u << 9)
#define CSR_VOSRDY (1u << 14)
#define CSR_ODRDY (1u << 16)
#define CSR_ODSWRDY (1u << 17)
#define CSR_UDRDY (3u << 18)
#define RCC_PLLRDY (1u << 25)
/* the backup SRAM (RM0390 2.2.2) */
#define BKPSRAM_BASE 0x40024000u
/* FLASH_OPTCR's user option bits (RM0390 3.7.1) */
#define OPT_NRST_STOP (1u << 6)
#define OPT_NRST_STDBY (1u << 7)

enum { RUN, STOP, STANDBY };

struct pwr {
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr, *csr;
    int mode;
    u64 csr_was; /* CSR as it last was, for a reset to keep its bits */
    u32 ewup_new; /* WKUP pins just enabled, their level yet to be seen */
};

static struct pwr *f4_pwr(struct esk_mach *m)
{
    struct esk_rf_periph *p = m->rf ? esk_rf_periph(m->rf, "PWR") : 0;
    return p && p->ops == &esk_stm32f4_pwr_ops ? (struct pwr *)p->ctx : 0;
}

static void csr_set(struct pwr *w, u64 v)
{
    if (v != w->csr->value)
        esk_rf_hw(w->csr, v);
    w->csr_was = v;
}

void esk_stm32f4_pwr_settle(struct esk_mach *m)
{
    struct pwr *w = f4_pwr(m);
    struct esk_rf_periph *rcc;
    struct esk_rf_reg *rcr;
    u32 cr, vos;
    u64 v;
    int pll;
    if (!w)
        return;
    rcc = esk_rf_periph(m->rf, "RCC");
    rcr = rcc ? esk_rf_reg(rcc, "CR") : 0;
    pll = rcr && (rcr->value & RCC_PLLRDY) != 0;
    cr = (u32)w->cr->value;
    vos = cr >> CR_VOS_LSB & 3;
    v = w->csr->value & ~(u64)(CSR_VOSRDY | CSR_ODRDY | CSR_ODSWRDY | CSR_BRR);
    if (pll)
        v |= CSR_VOSRDY;
    if (pll && (cr & CR_ODEN) && vos >= 2) {
        v |= CSR_ODRDY;
        if (cr & CR_ODSWEN)
            v |= CSR_ODSWRDY;
    }
    if (v & CSR_BRE)
        v |= CSR_BRR;
    csr_set(w, v);
}

/* ---- Standby's end ------------------------------------------------------------------ */

static void standby_ends(struct esk_mach *m)
{
    if (m->reset_req)
        return;
    m->reset_cause = ESK_RESET_STANDBY;
    m->reset_req = 1;
    m->yield = 1;
}

void esk_stm32f4_pwr_wakeup(struct esk_mach *m)
{
    struct pwr *w = f4_pwr(m);
    if (!w)
        return;
    csr_set(w, w->csr->value | CSR_WUF);
    if (w->mode == STANDBY)
        standby_ends(m);
}

static u32 wkup_bit(int port, int pin)
{
    return port == 0 && pin == 0 ? CSR_EWUP1 : port == 2 && pin == 13 ? CSR_EWUP2 : 0;
}

int esk_stm32f4_pwr_wkup_pin(struct esk_mach *m, int port, int pin)
{
    struct pwr *w = f4_pwr(m);
    u32 b = wkup_bit(port, pin);
    return w && b && (w->csr->value & b) != 0;
}

void esk_stm32f4_pwr_pin_edge(struct esk_mach *m, int port, int pin, int level)
{
    if (level && esk_stm32f4_pwr_wkup_pin(m, port, pin))
        esk_stm32f4_pwr_wakeup(m);
}

int esk_stm32f4_pwr_standby(struct esk_mach *m)
{
    struct pwr *w = f4_pwr(m);
    return w && w->mode == STANDBY;
}

/* a WKUP pin's level as its port reads it */
static int pin_high(struct esk_mach *m, int port, int pin)
{
    char name[8] = "GPIOx";
    struct esk_rf_periph *p;
    struct esk_rf_reg *idr;
    name[4] = (char)('A' + port);
    p = esk_rf_periph(m->rf, name);
    idr = p ? esk_rf_reg(p, "IDR") : 0;
    return idr && (idr->value >> pin & 1);
}

/* a WKUP pin enabled while high is a wake-up event: seen once its
 * pull-down has reached the circuit (the pins settled, or the program
 * looks again at PWR, or sleeps) */
static void ewup_check(struct pwr *w)
{
    struct esk_mach *m = w->p->sim;
    u32 b = w->ewup_new;
    if (!b)
        return;
    w->ewup_new = 0;
    if (((b & CSR_EWUP1) && pin_high(m, 0, 0)) || ((b & CSR_EWUP2) && pin_high(m, 2, 13)))
        esk_stm32f4_pwr_wakeup(m);
}

void esk_stm32f4_pwr_pins_settled(struct esk_mach *m)
{
    struct pwr *w = f4_pwr(m);
    if (w)
        ewup_check(w);
}

/* ---- Stop and Standby ---------------------------------------------------------------- */

/* whether the option bytes allow the mode (FLASH_OPTCR, loaded at reset) */
static int allowed(struct esk_mach *m, u32 opt)
{
    struct esk_rf_periph *f = esk_rf_periph(m->rf, "FLASH");
    struct esk_rf_reg *optcr = f ? esk_rf_reg(f, "OPTCR") : 0;
    return !optcr || (optcr->value & opt) != 0;
}

static void deep_sleep(struct esk_mach *m)
{
    struct pwr *w = f4_pwr(m);
    u32 cr;
    int r;
    if (!w || w->mode != RUN)
        return;
    /* inside the WFI: the devices first catch up with the core (lazy
     * clocks), so that the RTC counts the time to come from now */
    esk_mach_sync(m);
    ewup_check(w);
    cr = (u32)w->cr->value;
    if (!allowed(m, cr & CR_PDDS ? OPT_NRST_STDBY : OPT_NRST_STOP)) {
        esk_rf_diag(m, ES_SEV_INFO, "mcu.low_power_reset", "%s",
                    cr & CR_PDDS ? "the option bytes' nRST_STDBY is 0: entering Standby resets the part"
                                 : "the option bytes' nRST_STOP is 0: entering Stop resets the part");
        if (!m->reset_req) {
            m->reset_cause = ESK_RESET_LOW_POWER;
            m->reset_req = 1;
            m->yield = 1;
        }
        return;
    }
    esk_stm32f4_rcc_stop(m);
    if (!(cr & CR_PDDS)) {
        if ((cr & CR_UDEN) == CR_UDEN && (cr & (cr & CR_LPDS ? CR_LPUDS : CR_MRUDS)))
            csr_set(w, w->csr->value | CSR_UDRDY);
        w->mode = STOP;
        esk_mach_clocks(m, 1);
        return;
    }
    w->mode = STANDBY;
    for (r = 0; r < m->bus.nrg; r++) {
        struct esk_region *g = &m->bus.rg[r];
        if (g->kind != ESK_MEM_RAM || g->mirror)
            continue;
        if (g->base == BKPSRAM_BASE && (w->csr->value & CSR_BRE))
            continue; /* the backup regulator keeps it */
        esk_bus_erase(g);
    }
    m->powered_down = 1;
    esk_mach_clocks(m, 1);
    esk_stm32f4_pins_changed(m); /* they let go */
    if (w->csr->value & CSR_WUF)
        standby_ends(m);
}

static void deep_wake(struct esk_mach *m)
{
    struct pwr *w = f4_pwr(m);
    if (w && w->mode == STANDBY) {
        m->asleep = 1; /* only a reset ends Standby */
        return;
    }
    if (w)
        w->mode = RUN;
    esk_mach_clocks(m, 0);
}

/* ---- the registers --------------------------------------------------------------------- */

static u32 pwr_read(void *ctx, u32 off, int n)
{
    ewup_check((struct pwr *)ctx);
    return esk_rf_read(((struct pwr *)ctx)->p, off, n);
}

static void pwr_write(void *ctx, u32 off, int n, u32 v)
{
    struct pwr *w = (struct pwr *)ctx;
    struct esk_mach *m = w->p->sim;
    u64 csr, cr, now;
    ewup_check(w);
    csr = w->csr->value;
    esk_rf_write(w->p, off, n, v);
    cr = w->cr->value;
    now = w->csr->value;
    if (cr & (CR_CWUF | CR_CSBF)) {
        /* CWUF and CSBF clear their flags, and read 0 */
        esk_rf_hw(w->cr, cr & ~(u64)(CR_CWUF | CR_CSBF));
        now &= ~(u64)((cr & CR_CWUF ? CSR_WUF : 0u) | (cr & CR_CSBF ? CSR_SBF : 0u));
    }
    if (off - esk_rf_off(w->csr) < 4) /* UDRDY is cleared by writing 1, kept by writing 0 */
        now = (now & ~(u64)CSR_UDRDY) | (csr & CSR_UDRDY & ~((u64)v << (8 * (off - esk_rf_off(w->csr)))));
    csr_set(w, now);
    esk_stm32f4_pwr_settle(m);
    if ((w->csr->value ^ csr) & (CSR_EWUP1 | CSR_EWUP2)) {
        w->ewup_new |= (u32)(w->csr->value & ~csr & (CSR_EWUP1 | CSR_EWUP2));
        w->ewup_new &= (u32)w->csr->value;
        esk_stm32f4_port_changed(m, 0); /* the WKUP pins' pull-down */
        esk_stm32f4_port_changed(m, 2);
    }
}

static void pwr_reset(void *ctx)
{
    struct pwr *w = (struct pwr *)ctx;
    struct esk_mach *m = w->p->sim;
    u64 keep = 0;
    if (m->reset_cause != ESK_RESET_POWER)
        keep = w->mode == STANDBY ? (w->csr_was & ~(u64)CSR_UDRDY) | CSR_SBF : w->csr_was & (CSR_SBF | CSR_BRE);
    w->mode = RUN;
    w->ewup_new = 0;
    w->csr->value = (w->csr->value & ~(u64)(CSR_WUF | CSR_SBF | CSR_EWUP1 | CSR_EWUP2 | CSR_BRE)) | keep;
    w->csr_was = w->csr->value;
    esk_stm32f4_pwr_settle(m);
}

const struct esk_dev_ops esk_stm32f4_pwr_ops = { "stm32f4-pwr", pwr_read, pwr_write, pwr_reset, 0, 0 };

void *esk_stm32f4_pwr_create(struct esk_mach *m, struct esk_rf_periph *p)
{
    struct pwr *w;
    struct esk_rf_reg *cr = esk_rf_reg(p, "CR"), *csr = esk_rf_reg(p, "CSR");
    if (!cr || !csr)
        return 0;
    w = (struct pwr *)esk_mach_alloc(m, sizeof *w);
    if (!w)
        return 0;
    w->p = p;
    w->cr = cr;
    w->csr = csr;
    m->deep_sleep = deep_sleep;
    m->deep_wake = deep_wake;
    return w;
}
