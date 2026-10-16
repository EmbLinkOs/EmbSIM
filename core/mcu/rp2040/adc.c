/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* adc.c -- the RP2040's ADC (its datasheet, 4.9), over the register file.
 *
 *   - with CS.EN, a conversion starts on START_ONCE, or one after another
 *     with START_MANY: each takes 96 cycles of clk_adc (the SDK's 48 MHz
 *     from PLL_USB; none ends while it is stopped), READY
 *     low while it runs; with START_MANY a non-zero DIV paces the starts
 *     at 1 + INT + FRAC / 256 clk_adc cycles apart instead;
 *   - the input is AINSEL's: 0 to 3 are GPIO26 to GPIO29's voltages from
 *     the circuit, 4 the temperature sensor's (0.706 V, its 27 C value,
 *     with CS.TS_EN, else 0 V); RROBIN moves AINSEL to the next channel
 *     its mask names after each conversion; the voltage is sampled as the
 *     conversion starts;
 *   - the result is floor(Vin * 4096 / 3.3 V), 0 to 4095, in RESULT; with
 *     FCS.EN it is also pushed to the four-entry FIFO (FCS.SHIFT keeps its
 *     top 8 bits; a full FIFO sets OVER and drops it); reading FIFO pops it
 *     (UNDER when empty); LEVEL, EMPTY and FULL follow; INTR.FIFO is set
 *     while LEVEL >= THRESH (THRESH 0 never), and INTS raises
 *     ADC_IRQ_FIFO.
 *
 * Not modelled: the conversion error flags (ERR, the FIFO's ERR bit), DMA
 * requests, the analog front end's input impedance and noise. */
#include <string.h>

#include "mcu/rp2040/rp2040.h"

#define CS_EN (1u << 0)
#define CS_TS_EN (1u << 1)
#define CS_START_ONCE (1u << 2)
#define CS_START_MANY (1u << 3)
#define CS_READY (1u << 8)
#define FCS_EN (1u << 0)
#define FCS_SHIFT (1u << 1)
#define FCS_EMPTY (1u << 8)
#define FCS_FULL (1u << 9)
#define FCS_UNDER (1u << 10)
#define FCS_OVER (1u << 11)

struct adc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cs, *result, *fcs, *fifo, *div, *intr, *inte, *intf, *ints;
    int irq, running;
    int busy;
    u32 sample;      /* the conversion's code */
    u64 left, since; /* core cycles to the conversion's end, or the next start */
    int waiting;     /* START_MANY paced by DIV: waiting for the next start */
    u16 q[4];
    int nq;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

/* core cycles for n clk_adc cycles (n in 1/256ths) */
static u64 adc_cycles(const struct adc *a, u64 n256)
{
    u32 sys = esk_rp2040_hz(a->s, RP_CLK_SYS), adc = esk_rp2040_hz(a->s, RP_CLK_ADC);
    if (!adc)
        return (u64)1 << 62; /* clk_adc stopped: the conversion does not end */
    if (!sys)
        sys = esk_mach_core_hz(a->s);
    return (n256 * sys + (u64)adc * 256u - 1u) / ((u64)adc * 256u);
}

static double input(const struct adc *a, int ch)
{
    struct esk_mach *s = a->s;
    double v = 0.0;
    if (ch == 4)
        return (reg(a->cs) & CS_TS_EN) ? 0.706 : 0.0;
    if (ch < 4 && s->host.pin_volts && s->host.pin_volts(s->host.ctx, (26 + ch) / 16, (26 + ch) % 16, &v))
        return v;
    return 0.0;
}

static void fifo_regs(struct adc *a)
{
    u32 fcs = reg(a->fcs) & ~(FCS_EMPTY | FCS_FULL | (15u << 16));
    u32 thresh = reg(a->fcs) >> 24 & 15u;
    int was;
    fcs |= (u32)a->nq << 16;
    if (!a->nq)
        fcs |= FCS_EMPTY;
    if (a->nq == 4)
        fcs |= FCS_FULL;
    esk_rf_hw(a->fcs, fcs);
    esk_rf_hw(a->fifo, a->nq ? a->q[0] : 0);
    esk_rf_hw(a->intr, thresh && (u32)a->nq >= thresh ? 1u : 0u);
    was = reg(a->ints) != 0;
    esk_rf_hw(a->ints, (reg(a->intr) | reg(a->intf)) & reg(a->inte) & 1u);
    if (reg(a->ints) && !was)
        esk_rf_interrupt(a->p, a->irq);
}

static void start(struct adc *a)
{
    int ch = (int)(reg(a->cs) >> 12 & 7u);
    double x = input(a, ch) * 4096.0 / 3.3;
    a->sample = x <= 0.0 ? 0u : x >= 4095.0 ? 4095u : (u32)x;
    a->busy = 1;
    a->waiting = 0;
    a->left = adc_cycles(a, 96u * 256u);
    a->since = a->s->cycles;
    esk_rf_hw(a->cs, reg(a->cs) & ~(u64)(CS_READY | CS_START_ONCE));
    esk_rf_clock(a->p, &a->running, 1);
}

static void finish(struct adc *a)
{
    u32 cs = reg(a->cs), mask = cs >> 16 & 31u;
    a->busy = 0;
    esk_rf_hw(a->result, a->sample);
    if (reg(a->fcs) & FCS_EN) {
        if (a->nq < 4)
            a->q[a->nq++] = (u16)((reg(a->fcs) & FCS_SHIFT) ? a->sample >> 4 : a->sample);
        else
            esk_rf_hw(a->fcs, reg(a->fcs) | FCS_OVER);
    }
    if (mask) {
        /* round robin: the next channel the mask names */
        u32 ch = cs >> 12 & 7u, k;
        for (k = 1; k <= 5; k++)
            if (mask >> ((ch + k) % 5) & 1u) {
                ch = (ch + k) % 5;
                break;
            }
        esk_rf_hw(a->cs, (reg(a->cs) & ~(u64)(7u << 12)) | (u64)ch << 12);
    }
    fifo_regs(a);
    if ((reg(a->cs) & (CS_EN | CS_START_MANY)) == (CS_EN | CS_START_MANY)) {
        u32 d = reg(a->div) & 0xffffffu;
        if (d) {
            /* the next start, 1 + INT + FRAC / 256 clk_adc cycles after this
             * one's: what is left after the 96 */
            u64 period = 256u + (u64)d, conv = 96u * 256u;
            a->waiting = 1;
            a->left = period > conv ? adc_cycles(a, period - conv) : 1;
            a->since = a->s->cycles;
            return;
        }
        start(a);
        return;
    }
    esk_rf_hw(a->cs, reg(a->cs) | CS_READY);
    esk_rf_clock(a->p, &a->running, 0);
}

static void adc_tick(void *ctx, u32 cycles)
{
    struct adc *a = (struct adc *)ctx;
    if (!a->running)
        return;
    cycles = esk_mach_after(a->s, cycles, a->since);
    a->since = 0;
    while (a->running) {
        if (a->left > cycles) {
            a->left -= cycles;
            return;
        }
        cycles -= (u32)a->left;
        a->left = 0;
        if (a->waiting) {
            if ((reg(a->cs) & (CS_EN | CS_START_MANY)) == (CS_EN | CS_START_MANY)) {
                start(a);
            } else {
                a->waiting = 0;
                esk_rf_hw(a->cs, reg(a->cs) | CS_READY);
                esk_rf_clock(a->p, &a->running, 0);
            }
        } else {
            finish(a);
        }
    }
}

static int adc_next(void *ctx, u32 *cycles)
{
    struct adc *a = (struct adc *)ctx;
    if (!a->running)
        return 0;
    *cycles = a->left > 0xffffffffu ? 0xffffffffu : (u32)(a->left ? a->left : 1);
    return 1;
}

static u32 adc_read(void *ctx, u32 off, int n)
{
    struct adc *a = (struct adc *)ctx;
    u32 v = esk_rf_read(a->p, off, n);
    if (off == esk_rf_off(a->fifo) && !a->s->bus.debug) {
        if (a->nq) {
            a->nq--;
            memmove(a->q, a->q + 1, (size_t)a->nq * sizeof a->q[0]);
        } else {
            esk_rf_hw(a->fcs, reg(a->fcs) | FCS_UNDER);
        }
        fifo_regs(a);
    }
    return v;
}

static void adc_write(void *ctx, u32 off, int n, u32 v)
{
    struct adc *a = (struct adc *)ctx;
    esk_rf_write(a->p, off, n, v);
    if (off == esk_rf_off(a->cs)) {
        if (!(reg(a->cs) & CS_EN)) {
            a->busy = a->waiting = 0;
            esk_rf_hw(a->cs, reg(a->cs) & ~(u64)(CS_READY | CS_START_ONCE));
            esk_rf_clock(a->p, &a->running, 0);
            return;
        }
        if (!a->busy && !a->waiting && (v & (CS_START_ONCE | CS_START_MANY)))
            start(a);
        else if (!a->busy && !a->waiting)
            esk_rf_hw(a->cs, reg(a->cs) | CS_READY);
        esk_rf_hw(a->cs, reg(a->cs) & ~(u64)CS_START_ONCE);
    } else if (off == esk_rf_off(a->fcs)) {
        if (!(reg(a->fcs) & FCS_EN))
            a->nq = 0;
        fifo_regs(a);
    } else if (off == esk_rf_off(a->inte) || off == esk_rf_off(a->intf)) {
        fifo_regs(a);
    }
}

static void adc_reset(void *ctx)
{
    struct adc *a = (struct adc *)ctx;
    a->running = 0;
    a->busy = a->waiting = 0;
    a->nq = 0;
    a->left = 0;
    fifo_regs(a);
}

const struct esk_dev_ops esk_rp2040_adc_ops = {
    "rp2040-adc", adc_read, adc_write, adc_reset, adc_tick, adc_next,
};

void *esk_rp2040_adc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct adc a, *c;
    memset(&a, 0, sizeof a);
    a.s = s;
    a.p = p;
    a.cs = esk_rf_reg(p, "CS");
    a.result = esk_rf_reg(p, "RESULT");
    a.fcs = esk_rf_reg(p, "FCS");
    a.fifo = esk_rf_reg(p, "FIFO");
    a.div = esk_rf_reg(p, "DIV");
    a.intr = esk_rf_reg(p, "INTR");
    a.inte = esk_rf_reg(p, "INTE");
    a.intf = esk_rf_reg(p, "INTF");
    a.ints = esk_rf_reg(p, "INTS");
    if (!a.cs || !a.result || !a.fcs || !a.fifo || !a.div || !a.intr || !a.inte || !a.intf || !a.ints)
        return 0;
    a.irq = esk_rf_irq(p, "ADC");
    c = (struct adc *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = a;
    return c;
}
