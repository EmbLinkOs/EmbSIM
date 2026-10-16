/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* exti.c -- the STM32's external interrupt/event controller (RM0008 10.2):
 * edge detection on the lines the pins feed.
 *
 *   - line k (0-15) is the pin k of the port AFIO's EXTICR picks; an edge
 *     of its input (GPIO's IDR, as the pin's level changes) that RTSR
 *     (rising) or FTSR (falling) selects sets PR's bit k;
 *   - writing 1 to a SWIER bit that is 0 sets PR's bit, as an edge would;
 *   - PR is rc_w1, and clearing a PR bit clears its SWIER bit;
 *   - while a line's PR and IMR bits are set its interrupt is pending:
 *     EXTI0 to EXTI4 have their own, lines 5-9 share EXTI9_5 and 10-15
 *     EXTI15_10. The request is a level, so a handler that leaves PR set
 *     is entered again (checked at each access to EXTI).
 * Not modelled: the event outputs (EMR), lines 16-19 (PVD, RTC alarm, USB
 * and Ethernet wake-up). */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

struct exti {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *imr, *rtsr, *ftsr, *swier, *pr;
    int irq[16];
};

static void pend(struct exti *x)
{
    u32 act = (u32)(x->pr->value & x->imr->value);
    int k, last = -1;
    for (k = 0; k < 16; k++)
        if ((act >> k & 1) && x->irq[k] != last) {
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
    if (!x || line < 0 || line > 15)
        return;
    bit = (u64)1u << line;
    if ((level && (x->rtsr->value & bit)) || (!level && (x->ftsr->value & bit))) {
        esk_rf_hw(x->pr, x->pr->value | bit);
        pend(x);
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

void *esk_stm32_exti_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct exti x, *c;
    char name[16];
    int k;
    memset(&x, 0, sizeof x);
    x.s = s;
    x.p = p;
    x.imr = esk_rf_reg(p, "IMR");
    x.rtsr = esk_rf_reg(p, "RTSR");
    x.ftsr = esk_rf_reg(p, "FTSR");
    x.swier = esk_rf_reg(p, "SWIER");
    x.pr = esk_rf_reg(p, "PR");
    if (!x.imr || !x.rtsr || !x.ftsr || !x.swier || !x.pr)
        return 0;
    for (k = 0; k < 16; k++) {
        if (k < 5)
            snprintf(name, sizeof name, "EXTI%d", k);
        else
            snprintf(name, sizeof name, "%s", k < 10 ? "EXTI9_5" : "EXTI15_10");
        x.irq[k] = esk_rf_irq(p, name);
    }
    esk_rf_set_mwv(x.pr, ~(u64)0, ESK_MWV_1CLR);
    c = (struct exti *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = x;
    return c;
}
