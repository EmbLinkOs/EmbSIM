/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* flash_f1.c -- the STM32F1's flash program and erase controller, the FPEC
 * (PM0075 2.3 to 2.5 and 3; RM0008 3.3), over the register file:
 *   - KEYR's keys (0x45670123, then 0xCDEF89AB) clear CR's LOCK; a wrong
 *     key is a bus error and keeps CR locked until the next reset; LOCK
 *     set by the program locks it again. A locked CR ignores writes;
 *   - programming: with PG set, a halfword store of the core's to the main
 *     memory programs it. The FPEC reads the location first: one not
 *     erased (0xFFFF) is skipped with PGERR, unless the halfword written is
 *     0x0000; a page WRPR protects is skipped with WRPRTERR. A store of
 *     another size is a bus error and programs nothing. A store without PG
 *     changes nothing;
 *   - erasing: STRT with PER erases the 1 KiB page AR names, with MER the
 *     whole main memory, to 0xFF; a protected page is WRPRTERR and stays
 *     (MER with any page protected erases nothing);
 *   - SR: EOP at a successful operation's end (cleared by writing 1), the
 *     FLASH interrupt with EOPIE; PGERR and WRPRTERR raise it with ERRIE;
 *   - the option bytes (0x1FFFF800, RDP, USER, Data0 and Data1, WRP0 to
 *     WRP3, each with its complement): OPTKEYR's keys, written while CR is
 *     unlocked, set OPTWRE (a wrong key is a bus error); OPTER with STRT
 *     erases them, OPTPG with a halfword store programs one, the FPEC
 *     writing the byte's complement in its upper half, as a not erased one
 *     gives PGERR. Each reset loads OBR (OPTERR when a byte and its
 *     complement disagree, the byte then taken as 0xFF; an erased pair,
 *     0xFFFF, is taken as 0xFF without OPTERR: PM0075 does not say, but
 *     the HAL's HAL_FLASHEx_OBErase leaves all but RDP so and its every
 *     later operation fails on OPTERR; RDPRT when RDP is
 *     not 0xA5; USER; Data0 and Data1) and WRPR (WRP3..WRP0, a 0 bit
 *     protecting four pages) from them; what they say acts from then: the
 *     hardware watchdog (wdg.c), the write protection here;
 *   - timing: an accepted program or erase sets SR's BSY for its time
 *     (DS5319's flash memory characteristics: a halfword's programming
 *     52.5 us typical; a page's and the mass erase 20 to 40 ms, for which
 *     no typical is given: the model takes the 20 ms minimum), EOP coming
 *     at its end; STRT clears with BSY. A program running from the flash
 *     stalls on its next fetch until the operation ends (PM0075 2.3.1: a
 *     read of the flash while it is written or erased stalls the bus), as
 *     does a store to the flash while BSY is set; one running from SRAM
 *     goes on and polls BSY. The memory holds its new contents from the
 *     start (a read of it would have waited).
 * A program or erase changes the memory's generation, so a core's decoded
 * code there is decoded again.
 *
 * Not modelled: the times' spread (the typical value, or the minimum, each
 * time); HSI having to run while the flash is written; the read protection
 * (RDPRT is loaded and reported; the flash is still read from SRAM and by
 * a debugger, its first four pages not write-protected, its removal does
 * not mass-erase); a reset during an operation leaving the memory half
 * programmed; the access timing (ACR's LATENCY, the half-cycle access and
 * the prefetch buffer keep what is written: the core's timing is
 * estimated, ADR-015). */
#include <string.h>

#include "mcu/stm32/stm32.h"

#define FLASH_BASE 0x08000000u
#define OPT_BASE 0x1FFFF800u
#define PAGE 1024u

#define KEY1 0x45670123u
#define KEY2 0xCDEF89ABu

#define SR_BSY (1u << 0)
#define SR_PGERR (1u << 2)
#define SR_WRPRTERR (1u << 4)
#define SR_EOP (1u << 5)
#define SR_W1C (SR_PGERR | SR_WRPRTERR | SR_EOP)

#define CR_PG (1u << 0)
#define CR_PER (1u << 1)
#define CR_MER (1u << 2)
#define CR_OPTPG (1u << 4)
#define CR_OPTER (1u << 5)
#define CR_STRT (1u << 6)
#define CR_LOCK (1u << 7)
#define CR_OPTWRE (1u << 9)
#define CR_ERRIE (1u << 10)
#define CR_EOPIE (1u << 12)

#define PS_US 1000000ull
#define PROGRAM_PS 52500000ull     /* 52.5 us */
#define ERASE_PS (20000ull * PS_US) /* 20 ms */

struct flash {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *keyr, *optkeyr, *sr, *cr, *ar, *obr, *wrpr;
    struct esk_region *main; /* the main memory, once the bus has it */
    int irq;
    int key_step, optkey_step; /* the keys written so far */
    int wrong_key;             /* locked until the next reset */
    u32 wrp;                   /* WRPR: a 0 bit protects four pages */
    /* the operation in progress: BSY until its time has passed */
    int busy, running;
    u64 left_ps, since;
};

static void irq_if(struct flash *f)
{
    u32 sr = (u32)f->sr->value, cr = (u32)f->cr->value;
    if (((sr & SR_EOP) && (cr & CR_EOPIE)) || ((sr & (SR_PGERR | SR_WRPRTERR)) && (cr & CR_ERRIE)))
        esk_rf_interrupt(f->p, f->irq);
}

static void flash_clock(struct flash *f)
{
    if (f->busy && !f->running)
        f->since = f->s->cycles;
    esk_rf_clock(f->p, &f->running, f->busy);
}

/* an operation's end: EOP, or its error */
static void done(struct flash *f, u32 err)
{
    esk_rf_hw(f->sr, f->sr->value | (err ? err : SR_EOP));
    irq_if(f);
}

/* the operation in progress ends: BSY and STRT clear, EOP */
static void finish(struct flash *f)
{
    if (!f->busy)
        return;
    f->busy = 0;
    esk_rf_hw(f->sr, f->sr->value & ~(u64)SR_BSY);
    esk_rf_hw(f->cr, f->cr->value & ~(u64)CR_STRT);
    flash_clock(f);
    done(f, 0);
}

static u64 ps_to_cycles(const struct flash *f, u64 ps)
{
    u64 khz = esk_mach_core_hz(f->s) / 1000u;
    return (ps * (khz ? khz : 8000u) + 999999999ull) / 1000000000ull;
}

static struct esk_region *main_mem(struct flash *f)
{
    if (!f->main)
        f->main = esk_bus_region(&f->s->bus, FLASH_BASE, 4);
    return f->main;
}

/* an accepted operation takes `ps`: BSY until then, EOP at its end. A
 * program in the flash (or its alias at 0) fetches its next instruction
 * from it, and the bus stalls until the operation is done */
static void begin(struct flash *f, u64 ps)
{
    struct esk_region *m = main_mem(f);
    u32 pc = f->s->cpu ? f->s->cpu->ops->pc(f->s->cpu) : 0, size = m ? m->size : 0;
    finish(f); /* one in progress: the bus waited for it */
    f->busy = 1;
    f->left_ps = ps;
    esk_rf_hw(f->sr, f->sr->value | SR_BSY);
    flash_clock(f);
    if (pc - FLASH_BASE < size || pc < size)
        f->s->bus_stall += ps_to_cycles(f, ps);
}

static int protected_page(const struct flash *f, u32 page)
{
    u32 bit = page / 4u;
    return bit < 32u && !(f->wrp >> bit & 1u);
}

static void changed(struct flash *f)
{
    f->s->flash_written = 1; /* a power cycle keeps the flash as it is now */
    esk_bus_code_changed(&f->s->bus);
}

/* ---- programming --------------------------------------------------------------------- */

static void rom_write(void *ctx, struct esk_region *r, u32 a, int n, u32 v)
{
    struct flash *f = (struct flash *)ctx;
    u32 cr = (u32)f->cr->value, old;
    int opt = a - OPT_BASE < 16u;
    if (r->kind == ESK_MEM_ROM)
        return; /* the system memory: read-only */
    if (!(cr & (opt ? CR_OPTPG : CR_PG)) || (cr & CR_LOCK) || (opt && !(cr & CR_OPTWRE)))
        return; /* not being programmed: the store changes nothing */
    if (n != 2 || (a & 1u)) {
        esk_bus_fault(&f->s->bus); /* only halfwords are programmed */
        return;
    }
    if (f->busy && f->s->bus_stall == 0)
        f->s->bus_stall = ps_to_cycles(f, f->left_ps); /* a store while BSY waits for it */
    v &= 0xffffu;
    if (opt)
        v = (v & 0xffu) | (~v & 0xffu) << 8; /* the FPEC writes the complement */
    else if (protected_page(f, (a - r->base) / PAGE)) {
        done(f, SR_WRPRTERR);
        return;
    }
    old = esk_mem_rd_le(r->mem + (a - r->base), 2);
    if (old != 0xffffu && v != 0) {
        done(f, SR_PGERR); /* not erased: skipped */
        return;
    }
    esk_mem_wr_le(r->mem + (a - r->base), 2, v);
    changed(f);
    begin(f, PROGRAM_PS);
}

/* ---- erasing --------------------------------------------------------------------------- */

static void start(struct flash *f)
{
    u32 cr = (u32)f->cr->value, page, k;
    struct esk_region *m = main_mem(f), *o;
    if ((cr & CR_OPTER) && (cr & CR_OPTWRE)) {
        o = esk_bus_region(&f->s->bus, OPT_BASE, 16);
        if (o)
            memset(o->mem + (OPT_BASE - o->base), 0xff, 16);
        changed(f);
        begin(f, ERASE_PS);
        return;
    }
    if (!m)
        return;
    if (cr & CR_MER) {
        for (k = 0; k < m->size / PAGE; k++)
            if (protected_page(f, k)) {
                done(f, SR_WRPRTERR);
                return;
            }
        memset(m->mem, 0xff, m->size);
        changed(f);
        begin(f, ERASE_PS);
        return;
    }
    if (cr & CR_PER) {
        u32 a = (u32)f->ar->value;
        if (a < m->size)
            a += FLASH_BASE; /* the alias at 0 */
        if (a - FLASH_BASE >= m->size)
            return; /* no page there */
        page = (a - FLASH_BASE) / PAGE;
        if (protected_page(f, page)) {
            done(f, SR_WRPRTERR);
            return;
        }
        memset(m->mem + page * PAGE, 0xff, PAGE);
        changed(f);
        begin(f, ERASE_PS);
    }
}

/* ---- the option bytes ---------------------------------------------------------------- */

/* the option byte at `off` (0 RDP, 2 USER, 4 Data0, 6 Data1, 8.. WRP0-3): its
 * value, or 0xFF with OPTERR when its complement disagrees */
static u32 opt_byte(struct flash *f, u32 off, u32 *err)
{
    u32 v = 0xffffu;
    esk_bus_debug_read(&f->s->bus, OPT_BASE + off, 2, &v);
    if ((v & 0xffffu) != 0xffffu && ((v ^ (v >> 8)) & 0xffu) != 0xffu) {
        *err = 1;
        return 0xffu;
    }
    return v & 0xffu;
}

/* OBR and WRPR as each reset loads them */
static void load_options(struct flash *f)
{
    u32 err = 0, user, d0, d1, w = 0, k, raw = 0xffffu;
    /* the flash is unprotected only with RDP 0xA5 and its complement */
    esk_bus_debug_read(&f->s->bus, OPT_BASE, 2, &raw);
    (void)opt_byte(f, 0, &err);
    user = opt_byte(f, 2, &err);
    d0 = opt_byte(f, 4, &err);
    d1 = opt_byte(f, 6, &err);
    for (k = 0; k < 4; k++)
        w |= opt_byte(f, 8 + 2 * k, &err) << (8 * k);
    f->wrp = w;
    if (f->obr)
        esk_rf_hw(f->obr, (u64)(err | ((raw & 0xffffu) == 0x5aa5u ? 0u : 2u) | (user & 0xffu) << 2 | d0 << 10 | d1 << 18));
    if (f->wrpr)
        esk_rf_hw(f->wrpr, w);
}

/* ---- the registers --------------------------------------------------------------------- */

static u32 flash_read(void *ctx, u32 off, int n)
{
    struct flash *f = (struct flash *)ctx;
    if (off - esk_rf_off(f->keyr) < 4 || off - esk_rf_off(f->optkeyr) < 4)
        return 0; /* write-only */
    return esk_rf_read(f->p, off, n);
}

static void flash_write(void *ctx, u32 off, int n, u32 v)
{
    struct flash *f = (struct flash *)ctx;
    if (off - esk_rf_off(f->keyr) < 4) {
        if (f->wrong_key || !(f->cr->value & CR_LOCK))
            return;
        if (f->key_step == 0 && v == KEY1) {
            f->key_step = 1;
        } else if (f->key_step == 1 && v == KEY2) {
            f->key_step = 0;
            esk_rf_hw(f->cr, f->cr->value & ~(u64)CR_LOCK);
        } else {
            f->key_step = 0;
            f->wrong_key = 1;
            esk_bus_fault(&f->s->bus); /* a bus error, the FPEC locked until reset */
        }
        return;
    }
    if (off - esk_rf_off(f->optkeyr) < 4) {
        if ((f->cr->value & CR_LOCK) || (f->cr->value & CR_OPTWRE))
            return;
        if (f->optkey_step == 0 && v == KEY1) {
            f->optkey_step = 1;
        } else if (f->optkey_step == 1 && v == KEY2) {
            f->optkey_step = 0;
            esk_rf_hw(f->cr, f->cr->value | CR_OPTWRE);
        } else {
            f->optkey_step = 0;
            esk_bus_fault(&f->s->bus);
        }
        return;
    }
    if (off - esk_rf_off(f->sr) < 4) {
        u32 clr = (v << 8 * (off - esk_rf_off(f->sr))) & SR_W1C;
        esk_rf_hw(f->sr, f->sr->value & ~(u64)clr);
        return;
    }
    if (off - esk_rf_off(f->ar) < 4) {
        if (!f->busy) /* AR is not written while BSY is set */
            esk_rf_write(f->p, off, n, v);
        return;
    }
    if (off - esk_rf_off(f->cr) < 4) {
        u32 was = (u32)f->cr->value, now;
        if (was & CR_LOCK)
            return; /* locked: only an unlock opens it */
        esk_rf_write(f->p, off, n, v);
        now = (u32)f->cr->value;
        /* OPTWRE is set by the keys only; software clears it */
        if (!(was & CR_OPTWRE))
            now &= ~CR_OPTWRE;
        if (now & CR_LOCK)
            now &= ~CR_OPTWRE;
        esk_rf_hw(f->cr, now);
        if ((now & CR_STRT) && !(was & CR_STRT)) {
            start(f);
            if (!f->busy) /* refused: no operation */
                esk_rf_hw(f->cr, f->cr->value & ~(u64)CR_STRT);
        }
        return;
    }
    if (off - esk_rf_off(f->obr) < 4 || off - esk_rf_off(f->wrpr) < 4)
        return; /* read-only */
    esk_rf_write(f->p, off, n, v); /* ACR */
}

static void flash_tick(void *ctx, u32 cycles)
{
    struct flash *f = (struct flash *)ctx;
    u64 ps, khz;
    if (!f->running)
        return;
    if (f->since > f->s->cycles)
        f->since = 0;
    cycles = esk_mach_after(f->s, cycles, f->since);
    f->since = 0;
    khz = esk_mach_core_hz(f->s) / 1000u;
    ps = (u64)cycles * 1000000000ull / (khz ? khz : 8000u);
    if (f->left_ps <= ps)
        finish(f);
    else
        f->left_ps -= ps;
}

static int flash_next(void *ctx, u32 *cycles)
{
    struct flash *f = (struct flash *)ctx;
    u64 n;
    if (!f->running)
        return 0;
    n = ps_to_cycles(f, f->left_ps);
    *cycles = n == 0 ? 1 : n > 0xffffffffu ? 0xffffffffu : (u32)n;
    return 1;
}

static void flash_reset(void *ctx)
{
    struct flash *f = (struct flash *)ctx;
    f->main = NULL;
    f->busy = f->running = 0; /* a reset during an operation ends it */
    f->key_step = f->optkey_step = 0;
    f->wrong_key = 0;
    load_options(f);
}

const struct esk_dev_ops esk_stm32f1_flash_ops = {
    "stm32f1-flash", flash_read, flash_write, flash_reset, flash_tick, flash_next,
};

void *esk_stm32f1_flash_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct flash f, *c;
    memset(&f, 0, sizeof f);
    f.s = s;
    f.p = p;
    f.keyr = esk_rf_reg(p, "KEYR");
    f.optkeyr = esk_rf_reg(p, "OPTKEYR");
    f.sr = esk_rf_reg(p, "SR");
    f.cr = esk_rf_reg(p, "CR");
    f.ar = esk_rf_reg(p, "AR");
    f.obr = esk_rf_reg(p, "OBR");
    f.wrpr = esk_rf_reg(p, "WRPR");
    if (!f.keyr || !f.optkeyr || !f.sr || !f.cr || !f.ar || !f.obr || !f.wrpr)
        return 0;
    f.irq = esk_rf_irq(p, "FLASH");
    c = (struct flash *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = f;
    s->bus.rom_write = rom_write;
    s->bus.rom_write_ctx = c;
    return c;
}
