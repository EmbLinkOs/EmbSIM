/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* afio_f1.c -- the STM32F1's alternate-function I/O (RM0008 9.4): MAPR's
 * remaps and the debug port's release (SWJ_CFG), and EXTICR1-4, which pick
 * the port each EXTI line 0-15 listens to. The registers keep what is
 * written; a write sends every GPIO port through its update again, since a
 * remap moves a USART's pins and a release gives the debug pins back.
 * SWJ_CFG is write-only on the part and reads as 0. EVCR (the event output)
 * is kept, not modelled. */
#include <string.h>

#include "mcu/stm32/stm32.h"

struct afio {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *mapr, *exticr[4];
    u32 swj; /* SWJ_CFG as last written */
};

static struct afio *afio_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "AFIO") : 0;
    return p && p->ops == &esk_stm32f1_afio_ops ? (struct afio *)p->ctx : 0;
}

u32 esk_stm32f1_mapr(struct esk_mach *s)
{
    struct afio *a = afio_of(s);
    return a ? ((u32)a->mapr->value & ~(7u << 24)) | a->swj << 24 : 0;
}

int esk_stm32f1_exti_port(struct esk_mach *s, int line)
{
    struct afio *a = afio_of(s);
    if (line < 0 || line > 15)
        return -1;
    if (!a || !a->exticr[line / 4])
        return 0;
    return (int)(a->exticr[line / 4]->value >> (4 * (line % 4)) & 15u);
}

static u32 afio_read(void *ctx, u32 off, int n)
{
    struct afio *a = (struct afio *)ctx;
    return esk_rf_read(a->p, off, n);
}

static void afio_write(void *ctx, u32 off, int n, u32 v)
{
    struct afio *a = (struct afio *)ctx;
    esk_rf_write(a->p, off, n, v);
    if (off == esk_rf_off(a->mapr) && n == 4) {
        a->swj = v >> 24 & 7u;
        esk_rf_hw(a->mapr, a->mapr->value & ~(u64)(7u << 24));
    }
    esk_stm32f1_pins_changed(a->s);
}

static void afio_reset(void *ctx)
{
    ((struct afio *)ctx)->swj = 0;
}

const struct esk_dev_ops esk_stm32f1_afio_ops = {
    "stm32f1-afio", afio_read, afio_write, afio_reset, 0, 0,
};

void *esk_stm32f1_afio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct afio a, *c;
    memset(&a, 0, sizeof a);
    a.s = s;
    a.p = p;
    a.mapr = esk_rf_reg(p, "MAPR");
    a.exticr[0] = esk_rf_reg(p, "EXTICR1");
    a.exticr[1] = esk_rf_reg(p, "EXTICR2");
    a.exticr[2] = esk_rf_reg(p, "EXTICR3");
    a.exticr[3] = esk_rf_reg(p, "EXTICR4");
    if (!a.mapr)
        return 0;
    c = (struct afio *)esk_mach_alloc(s, sizeof *c);
    if (c)
        *c = a;
    return c;
}
