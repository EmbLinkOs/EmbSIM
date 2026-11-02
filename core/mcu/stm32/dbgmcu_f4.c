/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* dbgmcu_f4.c -- the STM32F4's MCU debug component (RM0390 33.16):
 *
 *   - DBGMCU_IDCODE: the device and revision (0x10006421 on the F446, its
 *     description's reset value), read-only;
 *   - DBGMCU_CR's DBG_SLEEP, DBG_STOP and DBG_STANDBY: in Stop (or
 *     Standby) the debug port answers only while its bit is set, else the
 *     probe loses the target until the part wakes (mcu.c);
 *   - DBGMCU_APB1_FZ and DBGMCU_APB2_FZ: while the core is halted by a
 *     debugger, the timers (tim.c: TIM1-TIM14) and the independent and
 *     window watchdogs (wdg.c) whose bit is set stop counting;
 *   - the registers are reset at power-on only: a system reset keeps what
 *     a debugger wrote (OpenOCD's stm32f4x.cfg sets them, then resets).
 * Omissions: the RTC's, the I2C SMBus time-outs' and CAN's freeze bits,
 * and TRACE_IOEN and TRACE_MODE (no trace port); a frozen timer's input
 * filters hold too. */
#include <string.h>

#include "mcu/stm32/stm32.h"

struct dbgmcu {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr, *apb1, *apb2;
};

static u32 val(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }

static void publish(struct dbgmcu *d)
{
    d->s->dbg.cr = val(d->cr);
    d->s->dbg.fz[0] = val(d->apb1);
    d->s->dbg.fz[1] = val(d->apb2);
}

static u32 dbg_read(void *ctx, u32 off, int n) { return esk_rf_read(((struct dbgmcu *)ctx)->p, off, n); }

static void dbg_write(void *ctx, u32 off, int n, u32 v)
{
    struct dbgmcu *d = (struct dbgmcu *)ctx;
    esk_rf_write(d->p, off, n, v);
    publish(d);
}

static void dbg_reset(void *ctx)
{
    struct dbgmcu *d = (struct dbgmcu *)ctx;
    if (d->s->reset_cause != ESK_RESET_POWER) { /* the debug domain keeps them */
        if (d->cr)
            esk_rf_hw(d->cr, d->s->dbg.cr);
        if (d->apb1)
            esk_rf_hw(d->apb1, d->s->dbg.fz[0]);
        if (d->apb2)
            esk_rf_hw(d->apb2, d->s->dbg.fz[1]);
    }
    publish(d);
}

const struct esk_dev_ops esk_stm32f4_dbgmcu_ops = { "stm32f4-dbgmcu", dbg_read, dbg_write, dbg_reset, 0, 0 };

void *esk_stm32f4_dbgmcu_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct dbgmcu *d = (struct dbgmcu *)esk_mach_alloc(s, sizeof *d);
    if (!d)
        return 0;
    d->s = s;
    d->p = p;
    d->cr = esk_rf_reg(p, "DBGMCU_CR");
    d->apb1 = esk_rf_reg(p, "DBGMCU_APB1_FZ");
    d->apb2 = esk_rf_reg(p, "DBGMCU_APB2_FZ");
    if (!d->cr)
        return 0;
    return d;
}
