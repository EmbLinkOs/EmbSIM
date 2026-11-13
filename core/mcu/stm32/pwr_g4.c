/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* pwr_g4.c -- the STM32G4's power control (RM0440 6), over the register
 * file.
 *
 *   - the voltage ranges: CR1.VOS 01 range 1 (CR5.R1MODE 0 its boost mode,
 *     to 170 MHz; 1 normal, to 150 MHz), 10 range 2 (26 MHz); the RCC model
 *     says when SYSCLK or the flash's wait states do not fit (rcc_g4.c).
 *     SR2.VOSF reads 0: the regulator is taken to settle at once;
 *   - low-power run: CR1.LPR puts the regulator in low-power mode, SR2's
 *     REGLPF follows it; REGLPS reads 1 (the low-power regulator ready);
 *   - the programmable voltage detector: with CR2.PVDE, SR2.PVDO is 1 while
 *     VDD is below the threshold PLS selects (DS12288's: falling 2.05 to
 *     2.90 V, rising about 100 mV higher), or with PLS 111 while PVD_IN
 *     (PB7) is below VREFINT (1.212 V); its changes are
 *     EXTI line 16's input; the peripheral voltage monitors' outputs
 *     (PVMOx) read 0, VDDA being at VDD;
 *   - DBP (CR1) allows writes to the backup domain (RCC's BDCR, rcc_g4.c);
 *   - the wake-up pins WKUP1 to WKUP5 (PA0, PC13, PE6, PA2, PC5) with
 *     CR3.EWUPx: an edge of the polarity CR4.WPx gives (0 rising, 1
 *     falling) sets SR1.WUFx; in Standby and Shutdown each enabled pin is
 *     an input with a pull-down (WPx 0) or a pull-up (WPx 1);
 *   - SCR's CWUFx and CSBF clear WUFx and SBF.
 *
 * The low-power modes (RM0440 6.3): the core's WFI or WFE with SLEEPDEEP
 * set enters the mode CR1.LPMS gives:
 *   - Stop 0 (000) or Stop 1 (001): the core's domain's clocks stop but
 *     the RTC's and the IWDG's; HSE and the PLL turn off (rcc_g4.c); RAM,
 *     the registers and the pins keep their state; any EXTI line's
 *     interrupt or event wakes the core, on HSI16;
 *   - Standby (011): the core's domain is off. RAM is lost but SRAM2 while
 *     CR3.RRS keeps it; the pins let go, those CR3.APC applies keeping the
 *     pull-up or pull-down PUCRx or PDCRx gives them, the enabled wake-up
 *     pins their pull; a wake-up pin's edge, an RTC or TAMP event with its
 *     interrupt enabled (CR3.EIWUL, the internal wake-up line), NRST or the
 *     IWDG ends it with a reset of all but the backup domain, SR1.SBF set;
 *   - Shutdown (1xx): as Standby, SRAM2 lost too and no SBF; leaving it is
 *     a brownout reset (RCC_CSR's BORRSTF, rcc_g4.c).
 *   A wake-up flag already set as Standby or Shutdown is entered ends it
 *   at once. The option bytes' nRST_STOP, nRST_STDBY or nRST_SHDW cleared
 *   (FLASH_OPTR) makes entering that mode a reset instead (LPWRRSTF).
 *
 * Not modelled: the regulator's settling (VOSF never set) and the wake-up
 * times (the core runs at the wake-up event), the low-power run and Stop
 * modes' supply currents, the PVD's hysteresis and the PVMs' thresholds
 * (VDDA is VDD here), the USB Type-C dead-battery bits (UCPD1_DBDIS,
 * UCPD1_STDBY), VBAT charging (CR4.VBE, VBRS), the PVD thresholds' spread
 * between parts (the typical values here). */
#include "mcu/stm32/stm32.h"

#define CR1_LPMS 7u
#define CR1_DBP (1u << 8)
#define CR1_LPR (1u << 14)
#define CR2_PVDE (1u << 0)
#define CR3_RRS (1u << 8)
#define CR3_APC (1u << 10)
#define CR3_EIWUL (1u << 15)
#define SR1_SBF (1u << 8)
#define SR1_WUFI (1u << 15)
#define SR2_REGLPS (1u << 8)
#define SR2_REGLPF (1u << 9)
#define SR2_PVDO (1u << 11)
/* FLASH_OPTR's user options (RM0440 5.7.6) */
#define OPT_NRST_STOP (1u << 12)
#define OPT_NRST_STDBY (1u << 13)
#define OPT_NRST_SHDW (1u << 14)

#define SRAM2_BASE 0x20014000u
#define SRAM2_SIZE 0x4000u

enum { RUN, STOP, STANDBY, SHUTDOWN };

/* the wake-up pins: port, pin */
static const int wkup_pin[5][2] = { { 0, 0 }, { 2, 13 }, { 4, 6 }, { 0, 2 }, { 2, 5 } };

struct g4pwr {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr1, *cr2, *cr3, *cr4, *sr1, *sr2, *scr, *pucr[7], *pdcr[7];
    int mode, was_shutdown;
    u64 sr1_was;
    int pvdo;
    int level[5];   /* each wake-up pin's level as last seen, -1 unknown */
};

static struct g4pwr *g4pwr(struct esk_mach *m)
{
    struct esk_rf_periph *p = m && m->rf ? esk_rf_periph(m->rf, "PWR") : 0;
    return p && p->ops == &esk_stm32g4_pwr_ops ? (struct g4pwr *)p->ctx : 0;
}

int esk_stm32g4_pwr(struct esk_mach *m)
{
    return g4pwr(m) != 0;
}

int esk_stm32g4_pwr_was_shutdown(struct esk_mach *m)
{
    struct g4pwr *w = g4pwr(m);
    return w && w->mode == SHUTDOWN;
}

static void sr1_set(struct g4pwr *w, u64 v)
{
    if (v != w->sr1->value)
        esk_rf_hw(w->sr1, v);
    w->sr1_was = v;
}

/* ---- the PVD ------------------------------------------------------------- */

static int pin_volts(struct esk_mach *m, int port, int bit, double *v)
{
    return m->host.pin_volts && m->host.pin_volts(m->host.ctx, port, bit, v);
}

static void pvd(struct g4pwr *w)
{
    /* DS12288 Rev 6 table 19's typical thresholds: PVDO rises as VDD falls
     * below the falling one and clears once VDD rises above the rising one */
    static const double fall[7] = { 2.05, 2.20, 2.36, 2.52, 2.64, 2.81, 2.90 };
    static const double rise[7] = { 2.15, 2.31, 2.46, 2.61, 2.74, 2.91, 2.98 };
    struct esk_mach *m = w->s;
    u32 cr2 = (u32)w->cr2->value, pls = cr2 >> 1 & 7;
    int o = 0;
    u64 v;
    if (cr2 & CR2_PVDE) {
        double vdd = m->vdd > 0.0 ? m->vdd : 3.3, in;
        if (pls < 7)
            o = vdd < (w->pvdo ? rise[pls] : fall[pls]);
        else
            o = pin_volts(m, 1, 7, &in) && in < 1.212;
    }
    v = (w->sr2->value & ~(u64)(SR2_PVDO | SR2_REGLPF | 0xF000u)) | SR2_REGLPS;
    if (o)
        v |= SR2_PVDO;
    if (w->cr1->value & CR1_LPR)
        v |= SR2_REGLPF;
    if (v != w->sr2->value)
        esk_rf_hw(w->sr2, v);
    if (o != w->pvdo) {
        w->pvdo = o;
        esk_stm32_exti_input(m, 16, o);
    }
}

/* ---- wake-up -------------------------------------------------------------- */

static void standby_ends(struct esk_mach *m)
{
    if (m->reset_req)
        return;
    m->reset_cause = ESK_RESET_STANDBY;
    m->reset_req = 1;
    m->yield = 1;
}

/* an internal wake-up event (the RTC's or TAMP's, its interrupt enabled) */
void esk_stm32g4_pwr_wakeup(struct esk_mach *m)
{
    struct g4pwr *w = g4pwr(m);
    if (!w || !(w->cr3->value & CR3_EIWUL))
        return;
    sr1_set(w, w->sr1->value | SR1_WUFI);
    if (w->mode == STANDBY || w->mode == SHUTDOWN)
        standby_ends(m);
}

static int wkup_of(int port, int pin)
{
    int k;
    for (k = 0; k < 5; k++)
        if (wkup_pin[k][0] == port && wkup_pin[k][1] == pin)
            return k;
    return -1;
}

u32 esk_stm32g4_pwr_wkup_mask(struct esk_mach *m, int port)
{
    struct g4pwr *w = g4pwr(m);
    u32 mask = 0;
    int k;
    if (!w)
        return 0;
    for (k = 0; k < 5; k++)
        if (wkup_pin[k][0] == port && (w->cr3->value >> k & 1))
            mask |= 1u << wkup_pin[k][1];
    return mask;
}

int esk_stm32g4_pwr_standby(struct esk_mach *m)
{
    struct g4pwr *w = g4pwr(m);
    return w && (w->mode == STANDBY || w->mode == SHUTDOWN);
}

/* the pull a pin has in Standby or Shutdown: 1 up, 2 down, 0 none */
int esk_stm32g4_pwr_standby_pull(struct esk_mach *m, int port, int pin)
{
    struct g4pwr *w = g4pwr(m);
    int k = wkup_of(port, pin);
    if (!w)
        return 0;
    if (k >= 0 && (w->cr3->value >> k & 1))
        return (w->cr4->value >> k & 1) ? 1 : 2;
    if ((w->cr3->value & CR3_APC) && port >= 0 && port < 7) {
        if (w->pucr[port] && (w->pucr[port]->value >> pin & 1))
            return 1;
        if (w->pdcr[port] && (w->pdcr[port]->value >> pin & 1))
            return 2;
    }
    return 0;
}

void esk_stm32g4_pwr_pin_edge(struct esk_mach *m, int port, int pin, int level)
{
    struct g4pwr *w = g4pwr(m);
    int k = wkup_of(port, pin);
    if (!w || k < 0)
        return;
    if (w->level[k] == level)
        return;
    w->level[k] = level;
    if (!(w->cr3->value >> k & 1))
        return;
    if (level == !((w->cr4->value >> k) & 1)) { /* WPx 0: rising; 1: falling */
        sr1_set(w, w->sr1->value | (u64)1u << k);
        if (w->mode == STANDBY || w->mode == SHUTDOWN)
            standby_ends(m);
    }
}

/* ---- Stop, Standby and Shutdown -------------------------------------------- */

static int allowed(struct esk_mach *m, u32 opt)
{
    struct esk_rf_periph *f = esk_rf_periph(m->rf, "FLASH");
    struct esk_rf_reg *optr = f ? esk_rf_reg(f, "OPTR") : 0;
    return !optr || (optr->value & opt) != 0;
}

/* RAM lost, but [keep, keep + n) */
static void ram_lost(struct esk_mach *m, u32 keep, u32 n)
{
    int r;
    for (r = 0; r < m->bus.nrg; r++) {
        struct esk_region *g = &m->bus.rg[r];
        u32 a;
        if (g->kind != ESK_MEM_RAM || g->mirror || !g->mem)
            continue;
        if (!n || keep + n <= g->base || keep >= g->base + g->size) {
            esk_bus_erase(g);
            continue;
        }
        for (a = 0; a + 4 <= g->size; a += 4)
            if (g->base + a < keep || g->base + a >= keep + n)
                esk_mem_wr_le(g->mem + a, 4, g->erased);
        g->gen++;
    }
}

static void deep_sleep(struct esk_mach *m)
{
    struct g4pwr *w = g4pwr(m);
    u32 lpms, opt;
    if (!w || w->mode != RUN)
        return;
    esk_mach_sync(m);
    lpms = (u32)w->cr1->value & CR1_LPMS;
    opt = lpms <= 1 ? OPT_NRST_STOP : lpms == 3 ? OPT_NRST_STDBY : lpms & 4 ? OPT_NRST_SHDW : OPT_NRST_STOP;
    if (!allowed(m, opt)) {
        esk_rf_diag(m, ES_SEV_INFO, "mcu.low_power_reset", "%s",
                    opt == OPT_NRST_STOP ? "the option bytes' nRST_STOP is 0: entering Stop resets the part"
                    : opt == OPT_NRST_STDBY ? "the option bytes' nRST_STDBY is 0: entering Standby resets the part"
                                            : "the option bytes' nRST_SHDW is 0: entering Shutdown resets the part");
        if (!m->reset_req) {
            m->reset_cause = ESK_RESET_LOW_POWER;
            m->reset_req = 1;
            m->yield = 1;
        }
        return;
    }
    esk_stm32g4_rcc_stop(m, 1);
    if (lpms <= 1 || lpms == 2) { /* 010 is reserved: taken as Stop 1 */
        w->mode = STOP;
        esk_mach_clocks(m, 1);
        return;
    }
    w->mode = lpms == 3 ? STANDBY : SHUTDOWN;
    if (w->mode == STANDBY && (w->cr3->value & CR3_RRS))
        ram_lost(m, SRAM2_BASE, SRAM2_SIZE);
    else
        ram_lost(m, 0, 0);
    m->powered_down = 1;
    esk_mach_clocks(m, 1);
    esk_stm32f4_pins_changed(m); /* they let go */
    if (w->sr1->value & (0x1Fu | SR1_WUFI))
        standby_ends(m);
}

static void deep_wake(struct esk_mach *m)
{
    struct g4pwr *w = g4pwr(m);
    if (w && (w->mode == STANDBY || w->mode == SHUTDOWN)) {
        m->asleep = 1; /* only a reset ends them */
        return;
    }
    if (w)
        w->mode = RUN;
    esk_mach_clocks(m, 0);
}

/* ---- the registers ----------------------------------------------------------------- */

static u32 pwr_read(void *ctx, u32 off, int n)
{
    struct g4pwr *w = (struct g4pwr *)ctx;
    if (off == esk_rf_off(w->sr2))
        pvd(w);
    return esk_rf_read(w->p, off, n);
}

static void pwr_write(void *ctx, u32 off, int n, u32 v)
{
    struct g4pwr *w = (struct g4pwr *)ctx;
    struct esk_mach *m = w->s;
    u64 cr3 = w->cr3->value, cr4 = w->cr4->value, cr1 = w->cr1->value, cr5 = 0;
    struct esk_rf_reg *r5 = esk_rf_reg(w->p, "CR5");
    if (r5)
        cr5 = r5->value;
    esk_rf_write(w->p, off, n, v);
    if (w->scr && off == esk_rf_off(w->scr)) {
        u64 clear = (w->scr->value & 0x1Fu) | ((w->scr->value & 0x100u) ? SR1_SBF : 0u);
        esk_rf_hw(w->scr, 0);
        sr1_set(w, w->sr1->value & ~clear);
        if (clear & 0x1Fu)
            esk_rf_hw(w->sr1, w->sr1->value & ~(u64)SR1_WUFI);
    }
    pvd(w);
    if (((cr1 ^ w->cr1->value) >> 9 & 3) || (r5 && ((cr5 ^ r5->value) & 0x100u)))
        esk_stm32g4_rcc_check(m);
    if ((cr3 ^ w->cr3->value) & (0x1Fu | CR3_APC) || (cr4 ^ w->cr4->value) & 0x1Fu) {
        int port;
        for (port = 0; port < 7; port++)
            esk_stm32f4_port_changed(m, port); /* the wake-up pins' pulls */
    }
}

static void pwr_reset(void *ctx)
{
    struct g4pwr *w = (struct g4pwr *)ctx;
    struct esk_mach *m = w->s;
    u64 keep = 0;
    int k;
    if (m->reset_cause == ESK_RESET_STANDBY && w->mode == STANDBY)
        keep = (w->sr1_was & 0x801Fu) | SR1_SBF;
    else if (m->reset_cause == ESK_RESET_STANDBY)
        keep = w->sr1_was & 0x801Fu; /* Shutdown: no SBF */
    else if (m->reset_cause != ESK_RESET_POWER)
        keep = w->sr1_was & SR1_SBF;
    w->was_shutdown = w->mode == SHUTDOWN;
    w->mode = RUN;
    w->pvdo = 0;
    for (k = 0; k < 5; k++)
        w->level[k] = -1;
    esk_rf_hw(w->sr1, keep);
    w->sr1_was = keep;
    pvd(w);
}

const struct esk_dev_ops esk_stm32g4_pwr_ops = { "stm32g4-pwr", pwr_read, pwr_write, pwr_reset, 0, 0 };

void *esk_stm32g4_pwr_create(struct esk_mach *m, struct esk_rf_periph *p)
{
    struct g4pwr *w;
    char name[8];
    int k;
    if (!esk_rf_reg(p, "CR1") || !esk_rf_reg(p, "SR1") || !esk_rf_reg(p, "SCR") || !esk_rf_reg(p, "CR3") ||
        !esk_rf_reg(p, "CR4") || !esk_rf_reg(p, "SR2") || !esk_rf_reg(p, "CR2"))
        return 0;
    w = (struct g4pwr *)esk_mach_alloc(m, sizeof *w);
    if (!w)
        return 0;
    w->s = m;
    w->p = p;
    w->cr1 = esk_rf_reg(p, "CR1");
    w->cr2 = esk_rf_reg(p, "CR2");
    w->cr3 = esk_rf_reg(p, "CR3");
    w->cr4 = esk_rf_reg(p, "CR4");
    w->sr1 = esk_rf_reg(p, "SR1");
    w->sr2 = esk_rf_reg(p, "SR2");
    w->scr = esk_rf_reg(p, "SCR");
    for (k = 0; k < 7; k++) {
        name[0] = 'P';
        name[1] = 'U';
        name[2] = 'C';
        name[3] = 'R';
        name[4] = (char)('A' + k);
        name[5] = 0;
        w->pucr[k] = esk_rf_reg(p, name);
        name[1] = 'D';
        w->pdcr[k] = esk_rf_reg(p, name);
    }
    for (k = 0; k < 5; k++)
        w->level[k] = -1;
    /* the flags read-only to the program, cleared through SCR */
    esk_rf_set_acc(w->sr1, 0x811Fu, ESK_ACC_RO);
    m->deep_sleep = deep_sleep;
    m->deep_wake = deep_wake;
    return w;
}
