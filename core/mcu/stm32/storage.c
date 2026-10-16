/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* storage.c -- a peripheral whose registers only keep what is written, by
 * the description's access rules, without the register file's "no model"
 * warning: for registers whose effect the simulation has no use for, such
 * as the F1 flash interface's ACR (wait states and the prefetch buffer;
 * the core's timing is estimated, ADR-015) and PWR's CR. */
#include "mcu/stm32/stm32.h"

static u32 st_read(void *ctx, u32 off, int n)
{
    return esk_rf_read((struct esk_rf_periph *)ctx, off, n);
}

static void st_write(void *ctx, u32 off, int n, u32 v)
{
    esk_rf_write((struct esk_rf_periph *)ctx, off, n, v);
}

const struct esk_dev_ops esk_stm32_storage_ops = { "storage", st_read, st_write, 0, 0, 0 };

void *esk_stm32_storage_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    (void)s;
    return p;
}
