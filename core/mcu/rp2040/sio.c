/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* sio.c -- the RP2040's single-cycle IO block (its datasheet, 2.3.1), as
 * core 0 sees it, over the register file.
 *
 *   - CPUID reads 0: the core is core 0;
 *   - GPIO_OUT and GPIO_OE, each with its SET, CLR and XOR aliases, drive
 *     the pins whose function is SIO (io.c); GPIO_IN reads them;
 *     GPIO_HI_* keep what is written (the QSPI bank's pins are not
 *     modelled);
 *   - the 32 spinlocks: a read of SPINLOCKn claims the lock and reads
 *     1 << n, or 0 when it was claimed already; a write releases it;
 *     SPINLOCK_ST is their bitmap;
 *   - the divider: a write of an operand (UDIVIDEND, UDIVISOR, unsigned;
 *     SDIVIDEND, SDIVISOR, signed) starts a calculation, whose QUOTIENT and
 *     REMAINDER are there at once but CSR.READY only after 8 cycles; DIRTY
 *     sets on any write and clears when QUOTIENT is read; a write of
 *     QUOTIENT or REMAINDER halts the calculation (READY). A divisor of 0
 *     gives a quotient of all ones (signed: -1, or 1 for a negative
 *     dividend) and the dividend as the remainder; INT_MIN / -1 gives
 *     INT_MIN, remainder 0;
 *   - the FIFOs: core 0's outgoing FIFO takes eight words (RDY while it has
 *     room; a write when full is lost and sets WOF); its incoming FIFO is
 *     always empty, since core 1 does not run (VLD never sets; a read while
 *     empty sets ROE and reads 0). WOF and ROE are cleared by writing
 *     FIFO_ST. SIO_IRQ_PROC0 is raised when VLD, WOF or ROE sets.
 * Not modelled: the interpolators (their registers keep what is written),
 * core 1's view. */
#include <stdio.h>
#include <string.h>

#include "mcu/rp2040/rp2040.h"

struct sio {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cpuid, *gin, *out, *oe;
    struct esk_rf_reg *out_set, *out_clr, *out_xor, *oe_set, *oe_clr, *oe_xor;
    struct esk_rf_reg *hout, *hout_set, *hout_clr, *hout_xor, *hoe, *hoe_set, *hoe_clr, *hoe_xor;
    struct esk_rf_reg *fifo_st, *fifo_wr, *fifo_rd, *lock_st, *lock[32];
    struct esk_rf_reg *udividend, *udivisor, *sdividend, *sdivisor, *quot, *rem, *csr;
    u64 m_vld, m_rdy, m_wof, m_roe, m_ready, m_dirty;
    int irq;
    u32 locks;
    u32 tx[8];
    int ntx;
    u64 div_at; /* the cycle the calculation started */
    int div_running;
};

/* FIFO_ST, with WOF or ROE set by what just happened (`set`): its IRQ is
 * raised when VLD, WOF or ROE sets */
static void fifo_status(struct sio *x, u64 set)
{
    u64 irq = x->m_vld | x->m_wof | x->m_roe;
    int was = (x->fifo_st->value & irq) != 0;
    u64 v = (x->fifo_st->value | set) & ~(x->m_vld | x->m_rdy);
    if (x->ntx < 8)
        v |= x->m_rdy;
    if (v != x->fifo_st->value)
        esk_rf_hw(x->fifo_st, v);
    if (!was && (v & irq))
        esk_rf_interrupt(x->p, x->irq);
}

static void divide(struct sio *x, int sign)
{
    u32 n = (u32)x->udividend->value, d = (u32)x->udivisor->value, q, r;
    if (sign) {
        s32 sn = (s32)n, sd = (s32)d;
        if (!sd) {
            q = sn < 0 ? 1u : 0xffffffffu;
            r = n;
        } else if (n == 0x80000000u && sd == -1) {
            q = n;
            r = 0;
        } else {
            q = (u32)(sn / sd);
            r = (u32)(sn % sd);
        }
    } else if (!d) {
        q = 0xffffffffu;
        r = n;
    } else {
        q = n / d;
        r = n % d;
    }
    esk_rf_hw(x->quot, q);
    esk_rf_hw(x->rem, r);
    x->div_at = x->s->cycles;
    x->div_running = 1;
}

static void div_csr(struct sio *x, int dirty)
{
    u64 v = x->csr->value & ~x->m_ready;
    if (!x->div_running || x->s->cycles - x->div_at >= 8) {
        v |= x->m_ready;
        x->div_running = 0;
    }
    if (dirty > 0)
        v |= x->m_dirty;
    if (dirty < 0)
        v &= ~x->m_dirty;
    esk_rf_hw(x->csr, v);
}

static void pins(struct sio *x)
{
    esk_rp2040_sio_pins(x->s, (u32)x->out->value & 0x3fffffffu, (u32)x->oe->value & 0x3fffffffu);
}

static u32 sio_read(void *ctx, u32 off, int n)
{
    struct sio *x = (struct sio *)ctx;
    struct esk_mach *s = x->s;
    int side = !s->bus.debug;
    u32 v;
    if (off == esk_rf_off(x->gin)) {
        esk_rf_hw(x->gin, esk_rp2040_gpio_in(s));
    } else if (off == esk_rf_off(x->csr)) {
        div_csr(x, 0);
    } else if (off == esk_rf_off(x->lock_st)) {
        esk_rf_hw(x->lock_st, x->locks);
    } else if (off >= esk_rf_off(x->lock[0]) && off <= esk_rf_off(x->lock[31])) {
        int k = (int)((off - esk_rf_off(x->lock[0])) / 4u);
        v = (x->locks >> k & 1) ? 0 : 1u << k;
        if (side)
            x->locks |= 1u << k;
        return v;
    } else if (off == esk_rf_off(x->fifo_rd)) {
        /* the incoming FIFO: core 1 does not run, so it is empty */
        if (side)
            fifo_status(x, x->m_roe);
        return 0;
    }
    v = esk_rf_read(x->p, off, n);
    if (off == esk_rf_off(x->quot) && side)
        div_csr(x, -1);
    return v;
}

/* a SET, CLR or XOR alias of `r` */
static void alias(struct esk_rf_reg *r, const struct esk_rf_reg *set, const struct esk_rf_reg *clr,
                  const struct esk_rf_reg *xor, u32 off, u32 v)
{
    if (off == esk_rf_off(set))
        esk_rf_hw(r, r->value | v);
    else if (off == esk_rf_off(clr))
        esk_rf_hw(r, r->value & ~(u64)v);
    else if (off == esk_rf_off(xor))
        esk_rf_hw(r, r->value ^ v);
}

static void sio_write(void *ctx, u32 off, int n, u32 v)
{
    struct sio *x = (struct sio *)ctx;
    if (off >= esk_rf_off(x->lock[0]) && off <= esk_rf_off(x->lock[31])) {
        x->locks &= ~(1u << ((off - esk_rf_off(x->lock[0])) / 4u));
        return;
    }
    if (off == esk_rf_off(x->fifo_wr)) {
        u64 set = 0;
        if (x->ntx < 8)
            x->tx[x->ntx++] = v;
        else
            set = x->m_wof; /* written while full: lost */
        fifo_status(x, set);
        return;
    }
    esk_rf_write(x->p, off, n, v);
    alias(x->out, x->out_set, x->out_clr, x->out_xor, off, v);
    alias(x->oe, x->oe_set, x->oe_clr, x->oe_xor, off, v);
    alias(x->hout, x->hout_set, x->hout_clr, x->hout_xor, off, v);
    alias(x->hoe, x->hoe_set, x->hoe_clr, x->hoe_xor, off, v);
    if (off >= esk_rf_off(x->out) && off <= esk_rf_off(x->oe_xor))
        pins(x);
    if (off == esk_rf_off(x->udividend) || off == esk_rf_off(x->udivisor)) {
        esk_rf_hw(x->sdividend, x->udividend->value);
        esk_rf_hw(x->sdivisor, x->udivisor->value);
        divide(x, 0);
        div_csr(x, 1);
    } else if (off == esk_rf_off(x->sdividend) || off == esk_rf_off(x->sdivisor)) {
        esk_rf_hw(x->udividend, x->sdividend->value);
        esk_rf_hw(x->udivisor, x->sdivisor->value);
        divide(x, 1);
        div_csr(x, 1);
    } else if (off == esk_rf_off(x->quot) || off == esk_rf_off(x->rem)) {
        x->div_running = 0;
        div_csr(x, 1);
    }
    if (off == esk_rf_off(x->fifo_st))
        fifo_status(x, 0);
}

static void sio_reset(void *ctx)
{
    struct sio *x = (struct sio *)ctx;
    x->locks = 0;
    x->ntx = 0;
    x->div_running = 0;
    fifo_status(x, 0);
    pins(x);
}

const struct esk_dev_ops esk_rp2040_sio_ops = {
    "rp2040-sio", sio_read, sio_write, sio_reset, 0, 0,
};

void *esk_rp2040_sio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct sio *x = (struct sio *)esk_mach_alloc(s, sizeof *x);
    size_t i;
    int k;
    if (!x)
        return 0;
    memset(x, 0, sizeof *x);
    {
        struct {
            struct esk_rf_reg **r;
            const char *name;
        } regs[] = {
            { &x->cpuid, "CPUID" }, { &x->gin, "GPIO_IN" },
            { &x->out, "GPIO_OUT" }, { &x->out_set, "GPIO_OUT_SET" },
            { &x->out_clr, "GPIO_OUT_CLR" }, { &x->out_xor, "GPIO_OUT_XOR" },
            { &x->oe, "GPIO_OE" }, { &x->oe_set, "GPIO_OE_SET" },
            { &x->oe_clr, "GPIO_OE_CLR" }, { &x->oe_xor, "GPIO_OE_XOR" },
            { &x->hout, "GPIO_HI_OUT" }, { &x->hout_set, "GPIO_HI_OUT_SET" },
            { &x->hout_clr, "GPIO_HI_OUT_CLR" }, { &x->hout_xor, "GPIO_HI_OUT_XOR" },
            { &x->hoe, "GPIO_HI_OE" }, { &x->hoe_set, "GPIO_HI_OE_SET" },
            { &x->hoe_clr, "GPIO_HI_OE_CLR" }, { &x->hoe_xor, "GPIO_HI_OE_XOR" },
            { &x->fifo_st, "FIFO_ST" }, { &x->fifo_wr, "FIFO_WR" }, { &x->fifo_rd, "FIFO_RD" },
            { &x->lock_st, "SPINLOCK_ST" }, { &x->udividend, "DIV_UDIVIDEND" },
            { &x->udivisor, "DIV_UDIVISOR" }, { &x->sdividend, "DIV_SDIVIDEND" },
            { &x->sdivisor, "DIV_SDIVISOR" }, { &x->quot, "DIV_QUOTIENT" },
            { &x->rem, "DIV_REMAINDER" }, { &x->csr, "DIV_CSR" },
        };
        for (i = 0; i < sizeof regs / sizeof regs[0]; i++)
            if (!(*regs[i].r = esk_rf_reg(p, regs[i].name)))
                return 0;
    }
    x->s = s;
    x->p = p;
    for (k = 0; k < 32; k++) {
        char nm[16];
        (void)snprintf(nm, sizeof nm, "SPINLOCK%d", k);
        if (!(x->lock[k] = esk_rf_reg(p, nm)))
            return 0;
        if (k && esk_rf_off(x->lock[k]) != esk_rf_off(x->lock[0]) + 4u * (u32)k)
            return 0; /* the spinlocks are one after another */
    }
    x->m_vld = esk_rf_mask(x->fifo_st, "VLD");
    x->m_rdy = esk_rf_mask(x->fifo_st, "RDY");
    x->m_wof = esk_rf_mask(x->fifo_st, "WOF");
    x->m_roe = esk_rf_mask(x->fifo_st, "ROE");
    x->m_ready = esk_rf_mask(x->csr, "READY");
    x->m_dirty = esk_rf_mask(x->csr, "DIRTY");
    x->irq = esk_rf_irq(p, "SIO_IRQ_PROC0");
    esk_rf_set_reset(x->cpuid, 0);
    return x;
}
