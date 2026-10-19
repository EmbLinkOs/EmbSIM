/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* pwr_f4.c -- the STM32F4's power controller (RM0390 5.4): its registers
 * keep what is written, and CSR's ready bits follow what sets them:
 *   - VOSRDY: the regulator's voltage scale is applied once the main PLL
 *     runs (RM0390 5.1.3: with the PLL off the regulator stays in scale 3,
 *     whatever VOS says), so it is set while RCC's PLLRDY is;
 *   - ODRDY: over-drive is ready once ODEN is set with the PLL running and
 *     VOS at scale 1 or 2 (RM0390 5.1.4's sequence: PLL on, ODEN, wait
 *     ODRDY, ODSWEN, wait ODSWRDY, then switch SYSCLK to the PLL);
 *   - ODSWRDY: the switch to over-drive is done once ODSWEN is set with
 *     ODRDY.
 * The voltage scale and over-drive bound SYSCLK; the RCC model says when
 * it is above them (rcc.c). DBP gates the backup domain's registers (RCC's
 * BDCR, rcc.c).
 *
 * Not modelled: the regulator's settling times (ready at once), the
 * programmable voltage detector (PVDE, PLS, PVDO), the low-power modes'
 * regulator settings (LPDS, PDDS, FPDS, LPLVDS, MRLVDS, under-drive),
 * standby and wake-up flags (WUF, SBF, EWUP), and the backup regulator
 * (BRE, BRR). */
#include "mcu/stm32/stm32.h"

#define CR_VOS_LSB 14
#define CR_ODEN (1u << 16)
#define CR_ODSWEN (1u << 17)
#define CSR_VOSRDY (1u << 14)
#define CSR_ODRDY (1u << 16)
#define CSR_ODSWRDY (1u << 17)
#define RCC_PLLRDY (1u << 25)

struct pwr {
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr, *csr;
};

static struct pwr *f4_pwr(struct esk_mach *m)
{
    struct esk_rf_periph *p = m->rf ? esk_rf_periph(m->rf, "PWR") : 0;
    return p && p->ops == &esk_stm32f4_pwr_ops ? (struct pwr *)p->ctx : 0;
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
    v = w->csr->value & ~(u64)(CSR_VOSRDY | CSR_ODRDY | CSR_ODSWRDY);
    if (pll)
        v |= CSR_VOSRDY;
    if (pll && (cr & CR_ODEN) && vos >= 2) {
        v |= CSR_ODRDY;
        if (cr & CR_ODSWEN)
            v |= CSR_ODSWRDY;
    }
    if (v != w->csr->value)
        esk_rf_hw(w->csr, v);
}

static u32 pwr_read(void *ctx, u32 off, int n)
{
    return esk_rf_read(((struct pwr *)ctx)->p, off, n);
}

static void pwr_write(void *ctx, u32 off, int n, u32 v)
{
    struct pwr *w = (struct pwr *)ctx;
    esk_rf_write(w->p, off, n, v);
    esk_stm32f4_pwr_settle(w->p->sim);
}

static void pwr_reset(void *ctx)
{
    struct pwr *w = (struct pwr *)ctx;
    esk_stm32f4_pwr_settle(w->p->sim);
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
    return w;
}
