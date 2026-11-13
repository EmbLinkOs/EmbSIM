/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* exti.c -- the STM32's external interrupt/event controller (RM0008 10.2,
 * RM0390 10.2): edge detection on the lines the pins and the internal
 * sources feed.
 *
 *   - line k (0-15) is the pin k of the port AFIO's EXTICR (the F1) or
 *     SYSCFG's (the F4) picks; the lines above 15, as many as IMR has
 *     (the F1's PVD, RTC alarm and USB wake-up; the F4's PVD, RTC alarm,
 *     USB OTG FS and HS wake-up, RTC tamper and time stamp, RTC wake-up),
 *     are fed by their peripherals' models when those have them;
 *   - an edge of a line's input that RTSR (rising) or FTSR (falling)
 *     selects sets PR's bit; writing 1 to a SWIER bit that is 0 does the
 *     same, as an edge would;
 *   - PR is rc_w1, and clearing a PR bit clears its SWIER bit;
 *   - while a line's PR and IMR bits are set its interrupt is pending:
 *     EXTI0 to EXTI4 have their own, lines 5-9 share EXTI9_5 and 10-15
 *     EXTI15_10, and the internal lines have their peripherals' (PVD,
 *     RTC_Alarm, OTG_FS_WKUP, ...). The request is a level, so a handler
 *     that leaves PR set is entered again (checked at each access to EXTI);
 *   - an edge or a SWIER trigger on a line whose EMR bit is set is an event
 *     to the core: it ends a WFE (esk_cortexm_event);
 *   - an edge on line 9 is the DAC's external trigger, on lines 11 and 15
 *     the ADCs' (EXTI_11, EXTI_15).
 *
 * The STM32G4's (RM0440 15), its registers in two banks (IMR1 to PR1 for
 * lines 0 to 31, IMR2 to PR2 for 32 to 43): the configurable lines as
 * above, their internal sources' interrupts by the G4's names (16 and 38
 * to 41 PVD_PVM, 17 RTC_ALARM, 19 RTC_TAMP_CSS_LSE, 20 RTC_WKUP, 21, 22
 * and 29 COMP1_2_3, 30 to 32 COMP4_5_6, 33 COMP7); the direct lines (18,
 * 23 to 28, 34 to 37, 42, 43: the USB's, the I2Cs', U(S)ARTs', LPUART's
 * and LPTIM's wake-ups) have no edge detection or pending bit, their
 * peripherals' interrupts being their own; an edge on lines 0 to 15 is also
 * the DMA request multiplexer's synchronisation and trigger input. */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

#define MAX_LINES 64

struct exti {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    /* bank 0: lines 0-31, bank 1: lines 32-63 */
    struct esk_rf_reg *imr[2], *emr[2], *rtsr[2], *ftsr[2], *swier[2], *pr[2];
    int nbanks, nlines, g4;
    int irq[MAX_LINES];
};

static u64 pending(const struct exti *x, int b)
{
    return x->pr[b] && x->imr[b] ? x->pr[b]->value & x->imr[b]->value : 0;
}

static void pend(struct exti *x)
{
    int k, last = -1;
    for (k = 0; k < x->nlines; k++)
        if ((pending(x, k / 32) >> (k % 32) & 1) && x->irq[k] >= 0 && x->irq[k] != last) {
            esk_rf_interrupt(x->p, x->irq[k]);
            last = x->irq[k];
        }
}

static struct exti *exti_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "EXTI") : 0;
    return p && p->ops == &esk_stm32_exti_ops && !esk_rf_gated(p) ? (struct exti *)p->ctx : 0;
}

void esk_stm32_exti_input(struct esk_mach *s, int line, int level)
{
    struct exti *x = exti_of(s);
    u64 bit;
    int b;
    if (!x || line < 0 || line >= x->nlines)
        return;
    b = line / 32;
    bit = (u64)1u << (line % 32);
    if (!x->rtsr[b] || !x->ftsr[b] || !x->pr[b])
        return;
    if (line < 16 && x->g4) {
        /* the DMA request multiplexer's synchronisation and trigger inputs
         * see every edge of lines 0-15 (dmamux_g4.c) */
        char sig[8];
        snprintf(sig, sizeof sig, "EXTI%d", line);
        esk_stm32_trigger(s, sig, level);
    }
    if ((level && (x->rtsr[b]->value & bit)) || (!level && (x->ftsr[b]->value & bit))) {
        esk_rf_hw(x->pr[b], x->pr[b]->value | bit);
        if (x->emr[b] && (x->emr[b]->value & bit))
            esk_cortexm_event(s);
        pend(x);
        if (!x->g4 && (line == 9 || line == 11 || line == 15)) {
            /* the line's edge: the DAC's (9) and the ADCs' (11, 15) trigger */
            char sig[8];
            snprintf(sig, sizeof sig, "EXTI%d", line);
            esk_stm32_trigger(s, sig, 1);
            esk_stm32_trigger(s, sig, 0);
        }
    }
}

/* a direct line's event (a G4's wake-up lines): an event to the core when
 * its EMR bit is set */
void esk_stm32_exti_direct(struct esk_mach *s, int line)
{
    struct exti *x = exti_of(s);
    int b;
    if (!x || line < 0 || line >= x->nlines)
        return;
    b = line / 32;
    if (x->emr[b] && (x->emr[b]->value >> (line % 32) & 1))
        esk_cortexm_event(s);
}

static u32 exti_read(void *ctx, u32 off, int n)
{
    struct exti *x = (struct exti *)ctx;
    return esk_rf_read(x->p, off, n);
}

static void exti_write(void *ctx, u32 off, int n, u32 v)
{
    struct exti *x = (struct exti *)ctx;
    u64 sw0[2], pr0[2];
    int b;
    for (b = 0; b < x->nbanks; b++) {
        sw0[b] = x->swier[b] ? x->swier[b]->value : 0;
        pr0[b] = x->pr[b] ? x->pr[b]->value : 0;
    }
    esk_rf_write(x->p, off, n, v);
    for (b = 0; b < x->nbanks; b++) {
        if (x->swier[b] && off == esk_rf_off(x->swier[b])) {
            u64 up = x->swier[b]->value & ~sw0[b];
            if (up && x->pr[b])
                esk_rf_hw(x->pr[b], x->pr[b]->value | up);
            if (x->emr[b] && (up & x->emr[b]->value))
                esk_cortexm_event(x->s);
        }
        if (x->pr[b] && off == esk_rf_off(x->pr[b])) {
            u64 cleared = pr0[b] & ~x->pr[b]->value;
            if (cleared && x->swier[b])
                esk_rf_hw(x->swier[b], x->swier[b]->value & ~cleared);
        }
    }
    pend(x);
}

const struct esk_dev_ops esk_stm32_exti_ops = {
    "stm32-exti", exti_read, exti_write, 0, 0, 0,
};

/* the interrupt of an internal line (16 and up), by the names the parts
 * give them, or -1 */
static int line_irq(struct esk_rf *rf, int k, int g4)
{
    static const char *const names[][3] = {
        { "PVD", 0, 0 },                      /* 16 */
        { "RTC_Alarm", "RTCAlarm", 0 },       /* 17 */
        { "OTG_FS_WKUP", "USBWakeup", 0 },    /* 18 */
        { "ETH_WKUP", 0, 0 },                 /* 19 */
        { "OTG_HS_WKUP", 0, 0 },              /* 20 */
        { "TAMP_STAMP", 0, 0 },               /* 21 */
        { "RTC_WKUP", 0, 0 },                 /* 22 */
    };
    int i, n;
    if (g4) {
        static const char *const g4names[44] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            "PVD_PVM", "RTC_ALARM", 0, "RTC_TAMP_CSS_LSE", "RTC_WKUP", "COMP1_2_3", "COMP1_2_3", 0,
            0, 0, 0, 0, 0, "COMP1_2_3", "COMP4_5_6", "COMP4_5_6",
            "COMP4_5_6", "COMP7", 0, 0, 0, 0, "PVD_PVM", "PVD_PVM", "PVD_PVM", "PVD_PVM", 0, 0,
        };
        return k < 44 && g4names[k] ? esk_rf_irq_named(rf, g4names[k]) : -1;
    }
    if (k < 16 || k - 16 >= (int)(sizeof names / sizeof names[0]))
        return -1;
    for (i = 0; i < 3 && names[k - 16][i]; i++)
        if ((n = esk_rf_irq_named(rf, names[k - 16][i])) >= 0)
            return n;
    return -1;
}

static int bank_lines(const struct esk_rf_reg *imr)
{
    int k, n = 0;
    for (k = 0; imr && k < imr->nf; k++)
        if (imr->f[k].name && imr->f[k].lsb + imr->f[k].width > n)
            n = imr->f[k].lsb + imr->f[k].width;
    return n;
}

void *esk_stm32_exti_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct exti x, *c;
    static const char *const sfx[2] = { "1", "2" };
    char name[16];
    int k, b;
    memset(&x, 0, sizeof x);
    x.s = s;
    x.p = p;
    x.imr[0] = esk_rf_reg(p, "IMR");
    if (x.imr[0]) {
        x.nbanks = 1;
        x.emr[0] = esk_rf_reg(p, "EMR");
        x.rtsr[0] = esk_rf_reg(p, "RTSR");
        x.ftsr[0] = esk_rf_reg(p, "FTSR");
        x.swier[0] = esk_rf_reg(p, "SWIER");
        x.pr[0] = esk_rf_reg(p, "PR");
    } else {
        /* the G4's two banks */
        for (b = 0; b < 2; b++) {
            snprintf(name, sizeof name, "IMR%s", sfx[b]);
            x.imr[b] = esk_rf_reg(p, name);
            snprintf(name, sizeof name, "EMR%s", sfx[b]);
            x.emr[b] = esk_rf_reg(p, name);
            snprintf(name, sizeof name, "RTSR%s", sfx[b]);
            x.rtsr[b] = esk_rf_reg(p, name);
            snprintf(name, sizeof name, "FTSR%s", sfx[b]);
            x.ftsr[b] = esk_rf_reg(p, name);
            snprintf(name, sizeof name, "SWIER%s", sfx[b]);
            x.swier[b] = esk_rf_reg(p, name);
            snprintf(name, sizeof name, "PR%s", sfx[b]);
            x.pr[b] = esk_rf_reg(p, name);
            if (x.imr[b])
                x.nbanks = b + 1;
        }
        x.g4 = 1;
    }
    if (!x.imr[0] || !x.rtsr[0] || !x.ftsr[0] || !x.swier[0] || !x.pr[0])
        return 0;
    /* the lines IMR has a bit for */
    x.nlines = bank_lines(x.imr[0]);
    if (x.nbanks == 2)
        x.nlines = 32 + bank_lines(x.imr[1]);
    if (x.nlines < 16 || x.nlines > MAX_LINES)
        x.nlines = 16;
    for (k = 0; k < x.nlines; k++) {
        if (k < 5)
            snprintf(name, sizeof name, "EXTI%d", k);
        else
            snprintf(name, sizeof name, "%s", k < 10 ? "EXTI9_5" : "EXTI15_10");
        x.irq[k] = k < 16 ? esk_rf_irq(p, name) : line_irq(s->rf, k, x.g4);
    }
    for (b = 0; b < x.nbanks; b++)
        if (x.pr[b])
            esk_rf_set_mwv(x.pr[b], ~(u64)0, ESK_MWV_1CLR);
    c = (struct exti *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = x;
    return c;
}
