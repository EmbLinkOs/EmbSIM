/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* cpu.c -- the ATmega328P's clock and power control registers that change
 * what the rest of the part does (datasheet 9, 10, 18), over the register
 * file:
 *
 *   - CLKPR: CLKPCE written alone opens a 4-cycle window in which a write
 *     of CLKPS with CLKPCE 0 sets the system clock prescaler (1 to 256);
 *     the core clock is the board's crystal (the fuses select it, as the
 *     Uno's do), or the 8 MHz internal oscillator without one, divided by
 *     it;
 *   - MCUCR's PUD disables every pull-up: the ports look again;
 *   - MCUCR's IVSEL moves the interrupt vectors to the boot section's
 *     start: IVCE written to one opens a 4-cycle window (interrupts held
 *     off through it) in which a write with IVCE zero sets IVSEL, and
 *     interrupts stay off until after the next instruction; IVSEL written
 *     without the sequence is ignored;
 *   - SPMCSR is the core's (avr.c, boot loader support);
 *   - CKDIV8 programmed in the board's low fuse starts CLKPR at /8;
 *   - SMCR, MCUSR, GPIORn, PRR, OSCCAL keep what is written (SLEEP reads
 *     SMCR.SE; the power reduction bits do not stop the peripherals).
 *
 * Not modelled: the brown-out detector; the clock source the low fuse's
 * CKSEL and SUT select (the board's description gives the clock). */
#include <string.h>

#include "mcu/avr/avr.h"

struct avr_cpu {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *clkpr, *mcucr, *spmcsr;
    u64 window; /* the cycle CLKPR's change window closes, 0 when shut */
    u64 iv_window; /* the cycle IVCE's closes, 0 when shut */
    u32 base_hz;
};

static u32 core_hz(struct esk_mach *m);

/* the model, when this machine's clock is an AVR's */
static struct avr_cpu *cpu_of(struct esk_mach *m)
{
    return m->core_hz == core_hz ? (struct avr_cpu *)m->clock_ctx : NULL;
}

static u32 core_hz(struct esk_mach *m)
{
    struct avr_cpu *c = cpu_of(m);
    u32 ps;
    if (!c)
        return 0;
    ps = (u32)(c->clkpr->value & 15u);
    return (m->hse_hz ? m->hse_hz : c->base_hz) >> (ps > 8 ? 8 : ps);
}

int esk_avr_pud(struct esk_mach *m)
{
    struct avr_cpu *c = cpu_of(m);
    return c && (c->mcucr->value & 0x10u) != 0;
}

static u32 cpu_read(void *ctx, u32 off, int n)
{
    struct avr_cpu *c = (struct avr_cpu *)ctx;
    if (c->spmcsr && off == esk_rf_off(c->spmcsr) && n == 1)
        return esk_avr_spmcsr_read(c->s);
    if (off == esk_rf_off(c->mcucr) && n == 1 && c->iv_window && c->s->cycles > c->iv_window) {
        c->iv_window = 0; /* IVCE clears after its four cycles */
        esk_rf_hw(c->mcucr, c->mcucr->value & ~1u);
    }
    return esk_rf_read(c->p, off, n);
}

static void cpu_write(void *ctx, u32 off, int n, u32 v)
{
    struct avr_cpu *c = (struct avr_cpu *)ctx;
    struct esk_mach *m = c->s;
    if (off == esk_rf_off(c->clkpr)) {
        if ((v & 0xffu) == 0x80u) {
            c->window = m->cycles + 4;
            esk_rf_hw(c->clkpr, (c->clkpr->value & 15u) | 0x80u);
        } else if (c->window && m->cycles <= c->window && !(v & 0x80u)) {
            esk_rf_hw(c->clkpr, v & 15u);
            c->window = 0;
            m->yield = 1; /* the runtime sees the new clock now */
        }
        return;
    }
    if (c->spmcsr && off == esk_rf_off(c->spmcsr) && n == 1) {
        esk_avr_spmcsr_write(m, (u8)v);
        return;
    }
    if (off == esk_rf_off(c->mcucr) && n == 1) {
        u32 keep = c->mcucr->value & 2u; /* IVSEL */
        int open = c->iv_window && m->cycles <= c->iv_window;
        if (v & 1u) {
            /* IVCE: the window opens, interrupts held through it */
            c->iv_window = m->cycles + 4;
            esk_avr_irq_hold(m, c->iv_window + 1);
            keep |= 1u;
        } else if (open && !(v & 1u)) {
            keep = v & 2u;
            c->iv_window = 0;
            esk_avr_ivsel(m, (int)keep);
            esk_avr_irq_hold(m, 0);
        }
        esk_rf_write(c->p, off, n, (v & ~3u) | keep);
        esk_avr_pins_changed(m);
        return;
    }
    esk_rf_write(c->p, off, n, v);
}

static void cpu_reset(void *ctx)
{
    struct avr_cpu *c = (struct avr_cpu *)ctx;
    c->window = 0;
    c->iv_window = 0;
    /* CKDIV8 programmed (low fuse bit 7 zero): the clock starts at /8 */
    if (c->s->has_fuses && !(c->s->fuse[0] & 0x80u))
        esk_rf_hw(c->clkpr, 3u);
}

/* SPM's operation ending or its window shutting */
static int cpu_next(void *ctx, u32 *cycles)
{
    return esk_avr_spm_next(((struct avr_cpu *)ctx)->s, cycles);
}

const struct esk_dev_ops esk_avr_cpu_ops = {
    "avr-cpu", cpu_read, cpu_write, cpu_reset, 0, cpu_next,
};

void *esk_avr_cpu_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct avr_cpu c, *k;
    memset(&c, 0, sizeof c);
    c.s = s;
    c.p = p;
    c.clkpr = esk_rf_reg(p, "CLKPR");
    c.mcucr = esk_rf_reg(p, "MCUCR");
    c.spmcsr = esk_rf_reg(p, "SPMCSR");
    if (!c.clkpr || !c.mcucr)
        return 0;
    c.base_hz = s->reset_hz ? s->reset_hz : 8000000u;
    k = (struct avr_cpu *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    *k = c;
    s->clock_ctx = k;
    s->core_hz = core_hz;
    return k;
}
