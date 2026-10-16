/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* sio.c -- the RP2040's single-cycle IO block (its datasheet, 2.3.1), over
 * the register file, as each core sees it.
 *
 *   - CPUID reads the number of the core reading it;
 *   - GPIO_OUT and GPIO_OE, each with its SET, CLR and XOR aliases, drive
 *     the pins whose function is SIO (io.c); GPIO_IN reads them;
 *     GPIO_HI_* keep what is written (the QSPI bank's pins are not
 *     modelled);
 *   - the 32 spinlocks, shared: a read of SPINLOCKn claims the lock and
 *     reads 1 << n, or 0 when it was claimed already; a write releases it;
 *     SPINLOCK_ST is their bitmap;
 *   - each core's divider: a write of an operand (UDIVIDEND, UDIVISOR,
 *     unsigned; SDIVIDEND, SDIVISOR, signed) starts a calculation, whose
 *     QUOTIENT and REMAINDER are there at once but CSR.READY only after 8
 *     cycles; DIRTY sets on any write and clears when QUOTIENT is read; a
 *     write of QUOTIENT or REMAINDER halts the calculation (READY). A
 *     divisor of 0 gives a quotient of all ones (signed: -1, or 1 for a
 *     negative dividend) and the dividend as the remainder; INT_MIN / -1
 *     gives INT_MIN, remainder 0;
 *   - the FIFOs, one each way, eight words deep: a core's FIFO_WR goes to
 *     the other core (RDY while there is room; a write when full is lost
 *     and sets the writer's WOF), its FIFO_RD takes what the other sent
 *     (VLD while a word waits; a read while empty sets ROE and reads 0).
 *     WOF and ROE are cleared by writing FIFO_ST. SIO_IRQ_PROCn is raised
 *     on core n when its VLD, WOF or ROE sets.
 *   - Core 1's bootrom, while core 1 waits in it (mcu/cortexm/smp.c): out
 *     of reset it drains its FIFO and sends core 0 a 0; then it answers
 *     each word core 0 sends with the same word, and after the sequence 0,
 *     0, 1, a vector table, a stack pointer and an entry point it starts
 *     core 1 there (the pico-sdk's multicore_launch_core1).
 * On a part with one core running, core 0's outgoing FIFO has no reader.
 * Not modelled: the interpolators (their registers keep what is written).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/rp2040-sio.c (core 0's
 * view). Changes for EmbSIM: both cores' views, the FIFOs between them, a
 * divider each, core 1's bootrom handshake. */
#include <stdio.h>
#include <string.h>

#include "mcu/rp2040/rp2040.h"

#define NCORE 2
#define FIFO 8

/* what each core has of its own */
struct sio_core {
    u32 rx[FIFO]; /* the FIFO into it */
    int nrx, rxh;
    u64 sticky; /* its WOF and ROE */
    int irq;     /* SIO_IRQ_PROCn */
    int level;   /* its VLD, WOF or ROE, as last seen */
    /* its divider: the operands, the results, CSR */
    u64 div[6], csr;
    u64 div_at;
    int div_running;
};

struct sio {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cpuid, *gin, *out, *oe;
    struct esk_rf_reg *out_set, *out_clr, *out_xor, *oe_set, *oe_clr, *oe_xor;
    struct esk_rf_reg *hout, *hout_set, *hout_clr, *hout_xor, *hoe, *hoe_set, *hoe_clr, *hoe_xor;
    struct esk_rf_reg *fifo_st, *fifo_wr, *fifo_rd, *lock_st, *lock[32];
    struct esk_rf_reg *udividend, *udivisor, *sdividend, *sdivisor, *quot, *rem, *csr;
    u64 m_vld, m_rdy, m_wof, m_roe, m_ready, m_dirty;
    u32 locks;
    struct sio_core k[NCORE];
    int cur;      /* the core whose view the registers hold */
    int boot_seq; /* core 1's bootrom: the words of the launch sequence seen */
    u32 boot_val[3];
};

static struct esk_rf_reg **div_regs(struct sio *x, int i)
{
    struct esk_rf_reg **r[6];
    r[0] = &x->udividend;
    r[1] = &x->udivisor;
    r[2] = &x->sdividend;
    r[3] = &x->sdivisor;
    r[4] = &x->quot;
    r[5] = &x->rem;
    return r[i];
}

static int ncores(struct sio *x)
{
    int n = esk_cortexm_cores(x->s);
    return n > NCORE ? NCORE : n;
}

/* the registers that are a core's own, as core c sees them */
static void view(struct sio *x, int c)
{
    struct sio_core *k = &x->k[c];
    int i;
    if (c != x->cur) {
        struct sio_core *o = &x->k[x->cur];
        for (i = 0; i < 6; i++)
            o->div[i] = (*div_regs(x, i))->value;
        o->csr = x->csr->value;
        for (i = 0; i < 6; i++)
            esk_rf_hw(*div_regs(x, i), k->div[i]);
        esk_rf_hw(x->csr, k->csr);
        x->cur = c;
    }
    esk_rf_hw(x->cpuid, (u64)c);
}

/* core c's FIFO_ST, and its IRQ raised when VLD, WOF or ROE sets */
static void fifo_status(struct sio *x, int c)
{
    struct sio_core *k = &x->k[c];
    int other = ncores(x) > 1 ? 1 - c : -1, level;
    u64 v = k->sticky;
    if (k->nrx)
        v |= x->m_vld;
    if (other < 0 ? x->k[1].nrx < FIFO : x->k[other].nrx < FIFO)
        v |= x->m_rdy;
    if (c == x->cur && v != x->fifo_st->value)
        esk_rf_hw(x->fifo_st, v);
    level = (v & (x->m_vld | x->m_wof | x->m_roe)) != 0;
    if (level && !k->level && k->irq >= 0)
        esk_cortexm_core_interrupt(x->s, c, 16 + k->irq);
    k->level = level;
}

static void push(struct sio *x, int to, u32 w, int from)
{
    struct sio_core *k = &x->k[to];
    if (k->nrx < FIFO)
        k->rx[(k->rxh + k->nrx++) % FIFO] = w;
    else if (from >= 0)
        x->k[from].sticky |= x->m_wof; /* written while full: lost */
}

static void statuses(struct sio *x)
{
    int c;
    for (c = 0; c < ncores(x); c++)
        fifo_status(x, c);
}

/* core 1's bootrom: a word core 0 sent */
static void bootrom_word(struct sio *x, u32 w)
{
    static const u32 want[3] = { 0, 0, 1 };
    push(x, 0, w, 1); /* the echo */
    if (x->boot_seq < 3) {
        x->boot_seq = w == want[x->boot_seq] ? x->boot_seq + 1 : 0;
        return;
    }
    x->boot_val[x->boot_seq - 3] = w;
    if (++x->boot_seq == 6) {
        x->boot_seq = 0;
        esk_cortexm_core_launch(x->s, 1, x->boot_val[0], x->boot_val[1], x->boot_val[2]);
    }
}

/* core 1 enters its bootrom: it drains its FIFO and tells core 0 */
static void bootrom_start(struct sio *x)
{
    x->k[1].nrx = x->k[1].rxh = 0;
    x->boot_seq = 0;
    push(x, 0, 0, -1);
    statuses(x);
}

void esk_rp2040_core1_power(struct esk_mach *s, int on)
{
    struct sio *x = (struct sio *)esk_rp_model(s, "SIO", &esk_rp2040_sio_ops);
    int was = esk_cortexm_core_running(s, 1) || esk_cortexm_core_booting(s, 1);
    if (esk_cortexm_cores(s) < 2 || on == was)
        return;
    esk_cortexm_core_power(s, 1, on);
    if (on && x)
        bootrom_start(x);
}

static void divide(struct sio *x, int sign)
{
    u32 n = (u32)x->udividend->value, d = (u32)x->udivisor->value, q, r;
    struct sio_core *k = &x->k[x->cur];
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
    k->div_at = x->s->cycles;
    k->div_running = 1;
}

static void div_csr(struct sio *x, int dirty)
{
    struct sio_core *k = &x->k[x->cur];
    u64 v = x->csr->value & ~x->m_ready;
    if (!k->div_running || x->s->cycles - k->div_at >= 8) {
        v |= x->m_ready;
        k->div_running = 0;
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
    int side = !s->bus.debug, c = esk_cortexm_core(s) < NCORE ? esk_cortexm_core(s) : 0;
    u32 v;
    view(x, c);
    if (off == esk_rf_off(x->gin)) {
        esk_rf_hw(x->gin, esk_rp2040_gpio_in(s));
    } else if (off == esk_rf_off(x->csr)) {
        div_csr(x, 0);
    } else if (off == esk_rf_off(x->lock_st)) {
        esk_rf_hw(x->lock_st, x->locks);
    } else if (off >= esk_rf_off(x->lock[0]) && off <= esk_rf_off(x->lock[31])) {
        int b = (int)((off - esk_rf_off(x->lock[0])) / 4u);
        v = (x->locks >> b & 1) ? 0 : 1u << b;
        if (side)
            x->locks |= 1u << b;
        return v;
    } else if (off == esk_rf_off(x->fifo_rd)) {
        struct sio_core *k = &x->k[c];
        if (!k->nrx) {
            if (side) {
                k->sticky |= x->m_roe;
                fifo_status(x, c);
            }
            return 0;
        }
        v = k->rx[k->rxh];
        if (side) {
            k->rxh = (k->rxh + 1) % FIFO;
            k->nrx--;
            statuses(x);
        }
        return v;
    } else if (off == esk_rf_off(x->fifo_st)) {
        fifo_status(x, c);
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
    struct esk_mach *s = x->s;
    int c = esk_cortexm_core(s) < NCORE ? esk_cortexm_core(s) : 0;
    view(x, c);
    if (off >= esk_rf_off(x->lock[0]) && off <= esk_rf_off(x->lock[31])) {
        x->locks &= ~(1u << ((off - esk_rf_off(x->lock[0])) / 4u));
        return;
    }
    if (off == esk_rf_off(x->fifo_wr)) {
        if (c == 0 && esk_cortexm_core_booting(s, 1))
            bootrom_word(x, v);
        else
            push(x, ncores(x) > 1 ? 1 - c : 1, v, c);
        statuses(x);
        return;
    }
    if (off == esk_rf_off(x->fifo_st)) {
        /* WOF and ROE: written, cleared */
        x->k[c].sticky &= ~((u64)v & (x->m_wof | x->m_roe));
        fifo_status(x, c);
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
        x->k[c].div_running = 0;
        div_csr(x, 1);
    }
}

static void sio_reset(void *ctx)
{
    struct sio *x = (struct sio *)ctx;
    int c, i;
    x->locks = 0;
    for (c = 0; c < NCORE; c++) {
        struct sio_core *k = &x->k[c];
        k->nrx = k->rxh = 0;
        k->sticky = 0;
        k->level = 0;
        k->div_running = 0;
        for (i = 0; i < 6; i++)
            k->div[i] = (*div_regs(x, i))->reset;
        k->csr = x->csr->reset;
    }
    x->cur = 0;
    x->boot_seq = 0;
    if (ncores(x) > 1)
        bootrom_start(x); /* core 1's bootrom starts with the chip */
    statuses(x);
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
    x->k[0].irq = esk_rf_irq_named(s->rf, "SIO_IRQ_PROC0");
    x->k[1].irq = esk_rf_irq_named(s->rf, "SIO_IRQ_PROC1");
    esk_rf_set_reset(x->cpuid, 0);
    return x;
}
