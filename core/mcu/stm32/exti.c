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
 *     the ADCs' (EXTI_11, EXTI_15). */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

struct exti {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *imr, *emr, *rtsr, *ftsr, *swier, *pr;
    int nlines;
    int irq[32];
};

static void pend(struct exti *x)
{
    u32 act = (u32)(x->pr->value & x->imr->value);
    int k, last = -1;
    for (k = 0; k < x->nlines; k++)
        if ((act >> k & 1) && x->irq[k] >= 0 && x->irq[k] != last) {
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
    if (!x || line < 0 || line >= x->nlines)
        return;
    bit = (u64)1u << line;
    if ((level && (x->rtsr->value & bit)) || (!level && (x->ftsr->value & bit))) {
        esk_rf_hw(x->pr, x->pr->value | bit);
        if (x->emr && (x->emr->value & bit))
            esk_cortexm_event(s);
        pend(x);
        if (line == 9 || line == 11 || line == 15) {
            /* the line's edge: the DAC's (9) and the ADCs' (11, 15) trigger */
            char sig[8];
            snprintf(sig, sizeof sig, "EXTI%d", line);
            esk_stm32_trigger(s, sig, 1);
            esk_stm32_trigger(s, sig, 0);
        }
    }
}

static u32 exti_read(void *ctx, u32 off, int n)
{
    struct exti *x = (struct exti *)ctx;
    return esk_rf_read(x->p, off, n);
}

static void exti_write(void *ctx, u32 off, int n, u32 v)
{
    struct exti *x = (struct exti *)ctx;
    u64 sw0 = x->swier->value, pr0 = x->pr->value;
    esk_rf_write(x->p, off, n, v);
    if (off == esk_rf_off(x->swier)) {
        u64 up = x->swier->value & ~sw0;
        if (up)
            esk_rf_hw(x->pr, x->pr->value | up);
        if (x->emr && (up & x->emr->value))
            esk_cortexm_event(x->s);
    }
    if (off == esk_rf_off(x->pr)) {
        u64 cleared = pr0 & ~x->pr->value;
        if (cleared)
            esk_rf_hw(x->swier, x->swier->value & ~cleared);
    }
    pend(x);
}

const struct esk_dev_ops esk_stm32_exti_ops = {
    "stm32-exti", exti_read, exti_write, 0, 0, 0,
};

/* the interrupt of an internal line (16 and up), by the names the parts
 * give them, or -1 */
static int line_irq(struct esk_rf *rf, int k)
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
    if (k < 16 || k - 16 >= (int)(sizeof names / sizeof names[0]))
        return -1;
    for (i = 0; i < 3 && names[k - 16][i]; i++)
        if ((n = esk_rf_irq_named(rf, names[k - 16][i])) >= 0)
            return n;
    return -1;
}

void *esk_stm32_exti_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct exti x, *c;
    char name[16];
    int k;
    memset(&x, 0, sizeof x);
    x.s = s;
    x.p = p;
    x.imr = esk_rf_reg(p, "IMR");
    x.emr = esk_rf_reg(p, "EMR");
    x.rtsr = esk_rf_reg(p, "RTSR");
    x.ftsr = esk_rf_reg(p, "FTSR");
    x.swier = esk_rf_reg(p, "SWIER");
    x.pr = esk_rf_reg(p, "PR");
    if (!x.imr || !x.rtsr || !x.ftsr || !x.swier || !x.pr)
        return 0;
    /* the lines IMR has a bit for */
    for (k = 0; k < x.imr->nf; k++)
        if (x.imr->f[k].name && x.imr->f[k].lsb + x.imr->f[k].width > x.nlines)
            x.nlines = x.imr->f[k].lsb + x.imr->f[k].width;
    if (x.nlines < 16 || x.nlines > 32)
        x.nlines = 16;
    for (k = 0; k < x.nlines; k++) {
        if (k < 5)
            snprintf(name, sizeof name, "EXTI%d", k);
        else
            snprintf(name, sizeof name, "%s", k < 10 ? "EXTI9_5" : "EXTI15_10");
        x.irq[k] = k < 16 ? esk_rf_irq(p, name) : line_irq(s->rf, k);
    }
    esk_rf_set_mwv(x.pr, ~(u64)0, ESK_MWV_1CLR);
    c = (struct exti *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = x;
    return c;
}
