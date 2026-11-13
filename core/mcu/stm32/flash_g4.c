/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* flash_g4.c -- the STM32G4's flash interface (RM0440 5), over the
 * register file, for the 512 KiB category 3 parts (the STM32G474xE):
 *   - the organisation: with the option bytes' DBANK set (the factory's)
 *     two banks of 256 KiB, bank 1 at 0x08000000 and bank 2 at
 *     0x08040000, of 128 pages of 2 KiB each; with DBANK clear one bank of
 *     128 pages of 4 KiB;
 *   - KEYR's keys (0x45670123, then 0xCDEF89AB) clear CR's LOCK; a wrong
 *     key is a bus error and keeps CR locked until the next reset; LOCK
 *     set by the program locks it again. A locked CR ignores writes;
 *   - programming (PG): a double word at a time, its first word stored at
 *     an address aligned on 8 bytes and its second at the next word; the
 *     program starts with the second. A store that is not a word is
 *     SIZERR, one out of the pair's place PGAERR, one without PG (or with
 *     PER or MER set) PGSERR, one to a page the option bytes' WRP areas
 *     protect WRPERR; a double word that is not erased is PROGERR unless
 *     all of it is written 0. A refused store changes nothing;
 *   - fast programming (FSTPG): a row of 32 double words (256 bytes) from
 *     its first, each stored in order; a double word out of order is
 *     MISSERR, and the row is programmed at once when its last arrives;
 *   - erasing: PER with PNB (and BKER in dual-bank mode) and STRT erases a
 *     page, MER1 and MER2 their banks (both: the main memory), to 0xFF; a
 *     protected page is WRPERR and stays;
 *   - SR: EOP at an operation's end when EOPIE is set; an error sets
 *     OPERR when ERRIE is set; both raise the FLASH interrupt; the flags
 *     are cleared by writing 1;
 *   - read-while-write: an operation on one bank stalls a program running
 *     from that bank until it ends (the bus waits, nothing runs); one
 *     running from the other bank, or from SRAM, goes on and polls BSY; a
 *     store to the flash while BSY is set waits for it;
 *   - timing: BSY for DS12288's typical times: a double word 81.7 us, a
 *     fast-programmed row 1.91 ms, a page's erase 22.02 ms, a bank's 22.13
 *     ms; EOP at their end;
 *   - the option bytes: OPTKEYR's keys (0x08192A3B, 0x4C5D6E7F) clear CR's
 *     OPTLOCK; OPTSTRT writes OPTR, PCROP1SR/ER, WRP1AR/BR, SEC1R to bank
 *     1's option bytes (0x1FFF7800) and PCROP2SR/ER, WRP2AR/BR, SEC2R to
 *     bank 2's (0x1FFFF808), each word followed by its complement; each
 *     reset loads them, a word whose complement does not match taking its
 *     default (and OPTVERR); OBL_LAUNCH reloads them through a system
 *     reset (RCC_CSR's OBLRSTF). What they say acts from then: the write
 *     protection and the banks here, the watchdogs' hardware start
 *     (wdg.c), the low-power resets (pwr_g4.c), the boot: BOOT0 is PB8's
 *     level at reset while nSWBOOT0 is 1, else nBOOT0's complement; BOOT0
 *     high with nBOOT1 set boots the system memory (ST's bootloader, not
 *     simulated: the runtime says so and the core stays idle);
 *   - read timing (RM0440 5.3.2): the memory is read 64 bits at a time,
 *     each read taking ACR.LATENCY wait states; a fetch from the double
 *     word being executed waits nothing; with PRFTEN the next one is read
 *     meanwhile; with ICEN the instruction cache holds 32 lines, with DCEN
 *     the data cache 8 (LRU); ICRST and DCRST empty them.
 *
 * Not modelled: the times' spread and maximum values (the typical value
 * each time), the option bytes' programming time (OPTSTRT is done at once),
 * the read protection's levels and their regression's mass erase (RDP is
 * stored), PCROP's read refusal (RDERR) and the securable area's
 * protection (SEC_PROT, BOOT_LOCK), ECC (ECCR never reports an error),
 * the bank swap (BFB2 and SYSCFG's FB_MODE are kept; bank 1 stays at
 * 0x08000000), the boot from SRAM (BOOT0 high with nBOOT1 clear: the
 * part boots its flash here), a reset during an operation leaving the memory
 * half-programmed, the power-down modes (PDKEYR, RUN_PD, SLEEP_PD), the
 * caches' line length (lines of one double word here). */
#include <string.h>

#include "mcu/stm32/stm32.h"

#define FLASH_BASE 0x08000000u
#define FLASH_SIZE (512u * 1024u)
#define BANK_SIZE (256u * 1024u)
#define OTP_BASE 0x1FFF7000u
#define OTP_SIZE 1024u
#define OB1_BASE 0x1FFF7800u
#define OB2_BASE 0x1FFFF800u

#define KEY1 0x45670123u
#define KEY2 0xCDEF89ABu
#define OPTKEY1 0x08192A3Bu
#define OPTKEY2 0x4C5D6E7Fu

#define SR_EOP (1u << 0)
#define SR_OPERR (1u << 1)
#define SR_PROGERR (1u << 3)
#define SR_WRPERR (1u << 4)
#define SR_PGAERR (1u << 5)
#define SR_SIZERR (1u << 6)
#define SR_PGSERR (1u << 7)
#define SR_MISSERR (1u << 8)
#define SR_FASTERR (1u << 9)
#define SR_RDERR (1u << 14)
#define SR_OPTVERR (1u << 15)
#define SR_BSY (1u << 16)
#define SR_W1C 0xC3FBu

#define CR_PG (1u << 0)
#define CR_PER (1u << 1)
#define CR_MER1 (1u << 2)
#define CR_BKER (1u << 11)
#define CR_MER2 (1u << 15)
#define CR_STRT (1u << 16)
#define CR_OPTSTRT (1u << 17)
#define CR_FSTPG (1u << 18)
#define CR_EOPIE (1u << 24)
#define CR_ERRIE (1u << 25)
#define CR_OBL_LAUNCH (1u << 27)
#define CR_OPTLOCK (1u << 30)
#define CR_LOCK (1u << 31)

#define OPTR_DBANK (1u << 22)

#define ACR_PRFTEN 0x100u
#define ACR_ICEN 0x200u
#define ACR_DCEN 0x400u
#define ACR_ICRST 0x800u
#define ACR_DCRST 0x1000u

#define ILINES 32
#define DLINES 8
#define NO_LINE 0xFFFFFFFFu

/* DS12288 Rev 6's typical programming times (table 47) */
#define PS_US 1000000ull
#define PROGRAM_PS (81700000ull)
#define ROW_PS (1910ull * PS_US)
#define PAGE_ERASE_PS (22020ull * PS_US)
#define BANK_ERASE_PS (22130ull * PS_US)

/* the option registers and their place in the option bytes: bank 1's at
 * OB1_BASE + off, bank 2's at OB2_BASE + off */
static const struct { const char *reg; int bank; u32 off; u32 dflt; } obs[] = {
    { "OPTR", 1, 0x00, 0xFFEFF8AAu },     { "PCROP1SR", 1, 0x08, 0x00007FFFu }, { "PCROP1ER", 1, 0x10, 0 },
    { "WRP1AR", 1, 0x18, 0x0000007Fu },   { "WRP1BR", 1, 0x20, 0x0000007Fu },   { "SEC1R", 1, 0x28, 0 },
    { "PCROP2SR", 2, 0x08, 0x00007FFFu }, { "PCROP2ER", 2, 0x10, 0 },           { "WRP2AR", 2, 0x18, 0x0000007Fu },
    { "WRP2BR", 2, 0x20, 0x0000007Fu },   { "SEC2R", 2, 0x28, 0 },
};
#define NOBS ((int)(sizeof obs / sizeof obs[0]))

struct g4flash {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *acr, *keyr, *optkeyr, *sr, *cr, *optr, *ob[NOBS];
    int irq;
    int key_step, optkey_step, wrong_key;
    /* the double word being programmed: its first word's address and value */
    int half;
    u32 at, lo;
    /* fast programming: the row's start, the double words received, the data */
    u32 row;
    int row_n;
    u32 row_data[64];
    /* the operation in progress */
    int busy, running, bank;
    u64 left_ps, since;
    /* read timing */
    u32 cur_line, pf_line;
    u64 pf_ready, use;
    u32 iline[ILINES], dline[DLINES];
    u64 iuse[ILINES], duse[DLINES];
};

static int dual(const struct g4flash *f) { return f->optr && (f->optr->value & OPTR_DBANK) != 0; }

static u32 page_size(const struct g4flash *f) { return dual(f) ? 2048u : 4096u; }

/* the bank (1 or 2) holding main-memory offset `off` */
static int bank_of(const struct g4flash *f, u32 off)
{
    return dual(f) && off >= BANK_SIZE ? 2 : 1;
}

/* whether the WRP areas protect the page at `off` */
static int protected_at(const struct g4flash *f, u32 off)
{
    int b = bank_of(f, off), k;
    u32 page = (dual(f) ? off % BANK_SIZE : off) / page_size(f);
    for (k = 0; k < 2; k++) {
        const char *name = b == 1 ? (k ? "WRP1BR" : "WRP1AR") : (k ? "WRP2BR" : "WRP2AR");
        struct esk_rf_reg *r = esk_rf_reg(f->p, name);
        u32 v, strt, end;
        if (!r)
            continue;
        v = (u32)r->value;
        strt = v & 0x7Fu;
        end = v >> 16 & 0x7Fu;
        if (strt <= end && page >= strt && page <= end)
            return 1;
    }
    return 0;
}

static void irq_if(struct g4flash *f)
{
    u32 sr = (u32)f->sr->value, cr = (u32)f->cr->value;
    if (((sr & SR_EOP) && (cr & CR_EOPIE)) || ((sr & SR_OPERR) && (cr & CR_ERRIE)))
        esk_rf_interrupt(f->p, f->irq);
}

static void done(struct g4flash *f, u32 err)
{
    u32 sr = (u32)f->sr->value;
    if (err) {
        sr |= err;
        if (f->cr->value & CR_ERRIE)
            sr |= SR_OPERR;
    } else if (f->cr->value & CR_EOPIE) {
        sr |= SR_EOP;
    }
    esk_rf_hw(f->sr, sr);
    irq_if(f);
}

static void flash_clock(struct g4flash *f)
{
    if (f->busy && !f->running)
        f->since = f->s->cycles;
    esk_rf_clock(f->p, &f->running, f->busy);
}

static void finish(struct g4flash *f)
{
    if (!f->busy)
        return;
    f->busy = 0;
    esk_rf_hw(f->sr, f->sr->value & ~(u64)SR_BSY);
    esk_rf_hw(f->cr, f->cr->value & ~(u64)CR_STRT);
    flash_clock(f);
    done(f, 0);
}

static u64 ps_to_cycles(const struct g4flash *f, u64 ps)
{
    u64 khz = esk_mach_core_hz(f->s) / 1000u;
    return (ps * (khz ? khz : 16000u) + 999999999ull) / 1000000000ull;
}

/* an accepted operation on bank `bank` (0: both) takes `ps`; a program
 * running from that bank stalls until it ends */
static void begin(struct g4flash *f, u64 ps, int bank)
{
    u32 pc = f->s->cpu ? f->s->cpu->ops->pc(f->s->cpu) : 0, off;
    finish(f);
    f->busy = 1;
    f->bank = bank;
    f->left_ps = ps;
    esk_rf_hw(f->sr, f->sr->value | SR_BSY);
    flash_clock(f);
    off = pc >= FLASH_BASE && pc < FLASH_BASE + FLASH_SIZE ? pc - FLASH_BASE : pc < FLASH_SIZE ? pc : FLASH_SIZE;
    if (off < FLASH_SIZE && (!bank || !dual(f) || bank_of(f, off) == bank))
        f->s->bus_stall += ps_to_cycles(f, ps);
}

static void changed(struct g4flash *f)
{
    f->s->flash_written = 1;
    esk_bus_code_changed(&f->s->bus);
}

/* ---- programming ---------------------------------------------------------------- */

static int erased_dw(const u8 *p)
{
    int k;
    for (k = 0; k < 8; k++)
        if (p[k] != 0xFF)
            return 0;
    return 1;
}

static void put_dw(struct g4flash *f, struct esk_region *r, u32 a, u32 lo, u32 hi)
{
    u8 *p = r->mem + (a - r->base);
    esk_mem_wr_le(p, 4, esk_mem_rd_le(p, 4) & lo);
    esk_mem_wr_le(p + 4, 4, esk_mem_rd_le(p + 4, 4) & hi);
    (void)f;
}

static void rom_write(void *ctx, struct esk_region *r, u32 a, int n, u32 v)
{
    struct g4flash *f = (struct g4flash *)ctx;
    u32 cr = (u32)f->cr->value, off;
    int otp = 0;
    if (r->size == FLASH_SIZE && r->kind != ESK_MEM_ROM)
        off = a - r->base;
    else if (a >= OTP_BASE && a + (u32)n <= OTP_BASE + OTP_SIZE)
        off = 0, otp = 1;
    else
        return; /* system memory, the option bytes: read-only to stores */
    if (f->busy && f->s->bus_stall == 0)
        f->s->bus_stall = ps_to_cycles(f, f->left_ps); /* a store while BSY waits */
    if ((cr & CR_LOCK) || !(cr & (CR_PG | CR_FSTPG)) || (cr & (CR_PER | CR_MER1 | CR_MER2))) {
        f->half = 0;
        done(f, SR_PGSERR);
        return;
    }
    if (n != 4) {
        f->half = 0;
        done(f, SR_SIZERR);
        return;
    }
    if (!f->half) {
        if (a & 7u) {
            done(f, SR_PGAERR);
            return;
        }
        if (!otp && protected_at(f, off)) {
            done(f, SR_WRPERR);
            return;
        }
        f->half = 1;
        f->at = a;
        f->lo = v;
        return;
    }
    f->half = 0;
    if (a != f->at + 4u) {
        done(f, SR_PGAERR);
        return;
    }
    {
        const u8 *p = r->mem + (f->at - r->base);
        if (!erased_dw(p) && (f->lo || v)) {
            done(f, SR_PROGERR);
            return;
        }
    }
    if (cr & CR_FSTPG) {
        u32 row = (f->at & ~255u);
        if (f->row_n == 0) {
            if (f->at != row) {
                done(f, SR_MISSERR);
                return;
            }
            f->row = row;
        } else if (f->at != f->row + 8u * (u32)f->row_n) {
            f->row_n = 0;
            done(f, SR_MISSERR);
            return;
        }
        f->row_data[2 * f->row_n] = f->lo;
        f->row_data[2 * f->row_n + 1] = v;
        if (++f->row_n < 32)
            return;
        {
            int k;
            for (k = 0; k < 32; k++)
                put_dw(f, r, f->row + 8u * (u32)k, f->row_data[2 * k], f->row_data[2 * k + 1]);
        }
        f->row_n = 0;
        changed(f);
        begin(f, ROW_PS, otp ? 1 : bank_of(f, off));
        return;
    }
    put_dw(f, r, f->at, f->lo, v);
    changed(f);
    begin(f, PROGRAM_PS, otp ? 1 : bank_of(f, off));
}

/* ---- erasing -------------------------------------------------------------------- */

static void erase(struct g4flash *f, u32 off, u32 size)
{
    struct esk_region *r = esk_bus_region(&f->s->bus, FLASH_BASE + off, size);
    if (r)
        memset(r->mem + (FLASH_BASE + off - r->base), 0xff, size);
    changed(f);
}

static int range_protected(const struct g4flash *f, u32 off, u32 size)
{
    u32 a;
    for (a = off; a < off + size; a += page_size(f))
        if (protected_at(f, a))
            return 1;
    return 0;
}

static void start(struct g4flash *f)
{
    u32 cr = (u32)f->cr->value;
    if (cr & (CR_MER1 | CR_MER2)) {
        u32 off = 0, size = FLASH_SIZE;
        int bank = 0;
        if (dual(f) && !((cr & CR_MER1) && (cr & CR_MER2))) {
            bank = cr & CR_MER1 ? 1 : 2;
            off = bank == 1 ? 0 : BANK_SIZE;
            size = BANK_SIZE;
        }
        if (range_protected(f, off, size)) {
            done(f, SR_WRPERR);
            return;
        }
        erase(f, off, size);
        begin(f, BANK_ERASE_PS, bank);
        return;
    }
    if (cr & CR_PER) {
        u32 pnb = cr >> 3 & 0x7Fu, off;
        if (dual(f))
            off = ((cr & CR_BKER) ? BANK_SIZE : 0) + pnb * 2048u;
        else
            off = pnb * 4096u;
        if (protected_at(f, off)) {
            done(f, SR_WRPERR);
            return;
        }
        erase(f, off, page_size(f));
        begin(f, PAGE_ERASE_PS, bank_of(f, off));
        return;
    }
    done(f, SR_PGSERR); /* STRT with neither PER nor MER */
}

/* ---- the option bytes ---------------------------------------------------------- */

static u32 ob_addr(int k) { return (obs[k].bank == 1 ? OB1_BASE : OB2_BASE) + obs[k].off; }

static void load_options(struct g4flash *f)
{
    int k, bad = 0;
    for (k = 0; k < NOBS; k++) {
        u32 v = 0xFFFFFFFFu, nv = 0xFFFFFFFFu;
        if (!f->ob[k])
            continue;
        if (esk_bus_debug_read(&f->s->bus, ob_addr(k), 4, &v) || esk_bus_debug_read(&f->s->bus, ob_addr(k) + 4u, 4, &nv) ||
            (v ^ nv) != 0xFFFFFFFFu) {
            v = obs[k].dflt;
            bad = 1;
        }
        esk_rf_hw(f->ob[k], v); /* the reserved bits too: they read the option bytes */
    }
    if (bad)
        esk_rf_hw(f->sr, f->sr->value | SR_OPTVERR);
}

static void program_options(struct g4flash *f)
{
    int k;
    for (k = 0; k < NOBS; k++) {
        u32 v;
        if (!f->ob[k])
            continue;
        v = (u32)f->ob[k]->value | (u32)~esk_rf_fields(f->ob[k]);
        if (esk_bus_debug_write(&f->s->bus, ob_addr(k), 4, v) == 0)
            (void)esk_bus_debug_write(&f->s->bus, ob_addr(k) + 4u, 4, ~v);
    }
    f->s->flash_written = 1;
    done(f, 0);
}

/* ---- read timing --------------------------------------------------------------- */

static void cache_clear(u32 *line, u64 *use, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        line[i] = NO_LINE;
        use[i] = 0;
    }
}

static int cache_hit(struct g4flash *f, u32 *line, u64 *use, int n, u32 l)
{
    int i;
    for (i = 0; i < n; i++)
        if (line[i] == l) {
            use[i] = ++f->use;
            return 1;
        }
    return 0;
}

static void cache_fill(struct g4flash *f, u32 *line, u64 *use, int n, u32 l)
{
    int i, lru = 0;
    for (i = 1; i < n; i++)
        if (use[i] < use[lru])
            lru = i;
    line[lru] = l;
    use[lru] = ++f->use;
}

static u32 ifetch(void *ctx, u32 off, u64 now)
{
    struct g4flash *f = (struct g4flash *)ctx;
    u32 acr = (u32)f->acr->value, ws = acr & 0xfu, l = off >> 3, st;
    if (l == f->cur_line)
        return 0;
    if ((acr & ACR_PRFTEN) && l == f->pf_line)
        st = f->pf_ready > now ? (u32)(f->pf_ready - now) : 0;
    else if ((acr & ACR_ICEN) && cache_hit(f, f->iline, f->iuse, ILINES, l))
        st = 0;
    else {
        st = ws;
        if (acr & ACR_ICEN)
            cache_fill(f, f->iline, f->iuse, ILINES, l);
    }
    f->cur_line = l;
    if (acr & ACR_PRFTEN) {
        f->pf_line = l + 1;
        f->pf_ready = now + st + ws + 1;
    } else {
        f->pf_line = NO_LINE;
    }
    return st;
}

static u32 dread(void *ctx, u32 off)
{
    struct g4flash *f = (struct g4flash *)ctx;
    u32 acr = (u32)f->acr->value, ws = acr & 0xfu, l = off >> 3;
    if (!ws)
        return 0;
    if (acr & ACR_DCEN) {
        if (cache_hit(f, f->dline, f->duse, DLINES, l))
            return 0;
        cache_fill(f, f->dline, f->duse, DLINES, l);
    }
    return ws;
}

static void timing_reset(struct g4flash *f)
{
    const struct esk_region *r = esk_bus_region(&f->s->bus, FLASH_BASE, 4);
    f->cur_line = f->pf_line = NO_LINE;
    f->pf_ready = 0;
    cache_clear(f->iline, f->iuse, ILINES);
    cache_clear(f->dline, f->duse, DLINES);
    if (r && f->acr) {
        f->s->ftime.mem = r->mem;
        f->s->ftime.ifetch = ifetch;
        f->s->ftime.dread = dread;
        f->s->ftime.ctx = f;
    }
}

/* ---- the registers -------------------------------------------------------------- */

static u32 flash_read(void *ctx, u32 off, int n)
{
    struct g4flash *f = (struct g4flash *)ctx;
    if (off - esk_rf_off(f->keyr) < 4 || off - esk_rf_off(f->optkeyr) < 4)
        return 0; /* write-only */
    return esk_rf_read(f->p, off, n);
}

static int is_ob_reg(const struct g4flash *f, u32 off)
{
    int k;
    for (k = 0; k < NOBS; k++)
        if (f->ob[k] && off - esk_rf_off(f->ob[k]) < 4)
            return 1;
    return 0;
}

static void flash_write(void *ctx, u32 off, int n, u32 v)
{
    struct g4flash *f = (struct g4flash *)ctx;
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
            esk_bus_fault(&f->s->bus);
        }
        return;
    }
    if (off - esk_rf_off(f->optkeyr) < 4) {
        if (!(f->cr->value & CR_OPTLOCK) || (f->cr->value & CR_LOCK))
            return;
        if (f->optkey_step == 0 && v == OPTKEY1) {
            f->optkey_step = 1;
        } else if (f->optkey_step == 1 && v == OPTKEY2) {
            f->optkey_step = 0;
            esk_rf_hw(f->cr, f->cr->value & ~(u64)CR_OPTLOCK);
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
    if (off - esk_rf_off(f->cr) < 4) {
        u32 was = (u32)f->cr->value, now;
        if (was & CR_LOCK)
            return;
        if (f->busy)
            return;
        esk_rf_write(f->p, off, n, v);
        now = (u32)f->cr->value;
        if (was & CR_OPTLOCK) /* OPTSTRT and OBL_LAUNCH need OPTLOCK clear */
            now &= ~(CR_OPTSTRT | CR_OBL_LAUNCH);
        if ((was & CR_OPTLOCK) && !(now & CR_OPTLOCK))
            now |= CR_OPTLOCK; /* cleared only by the keys */
        esk_rf_hw(f->cr, now);
        if (!(now & (CR_PG | CR_FSTPG)))
            f->half = 0, f->row_n = 0;
        if ((now & CR_STRT) && !(was & CR_STRT)) {
            start(f);
            if (!f->busy)
                esk_rf_hw(f->cr, f->cr->value & ~(u64)CR_STRT);
        }
        if ((now & CR_OPTSTRT) && !(was & CR_OPTSTRT)) {
            program_options(f);
            esk_rf_hw(f->cr, f->cr->value & ~(u64)CR_OPTSTRT);
        }
        if (now & CR_OBL_LAUNCH) {
            /* the option bytes reload through a system reset */
            esk_stm32g4_rcc_obl(f->s);
            if (!f->s->reset_req) {
                f->s->reset_cause = ESK_RESET_SOFTWARE;
                f->s->reset_req = 1;
                f->s->yield = 1;
            }
        }
        return;
    }
    if (is_ob_reg(f, off)) {
        if (f->cr->value & CR_OPTLOCK)
            return; /* the option registers are written only with OPTLOCK clear */
        esk_rf_write(f->p, off, n, v);
        return;
    }
    esk_rf_write(f->p, off, n, v); /* ACR, ECCR, PDKEYR */
    if (f->acr && off - esk_rf_off(f->acr) < 4) {
        u32 acr = (u32)f->acr->value;
        if ((acr & ACR_ICRST) && !(acr & ACR_ICEN))
            cache_clear(f->iline, f->iuse, ILINES);
        if ((acr & ACR_DCRST) && !(acr & ACR_DCEN))
            cache_clear(f->dline, f->duse, DLINES);
        esk_stm32g4_rcc_check(f->s);
    }
}

static void flash_tick(void *ctx, u32 cycles)
{
    struct g4flash *f = (struct g4flash *)ctx;
    u64 ps, khz;
    if (!f->running)
        return;
    if (f->since > f->s->cycles)
        f->since = 0;
    cycles = esk_mach_after(f->s, cycles, f->since);
    f->since = 0;
    khz = esk_mach_core_hz(f->s) / 1000u;
    ps = (u64)cycles * 1000000000ull / (khz ? khz : 16000u);
    if (f->left_ps <= ps)
        finish(f);
    else
        f->left_ps -= ps;
}

static int flash_next(void *ctx, u32 *cycles)
{
    struct g4flash *f = (struct g4flash *)ctx;
    u64 n;
    if (!f->running)
        return 0;
    n = ps_to_cycles(f, f->left_ps);
    *cycles = n == 0 ? 1 : n > 0xffffffffu ? 0xffffffffu : (u32)n;
    return 1;
}

static void flash_reset(void *ctx)
{
    struct g4flash *f = (struct g4flash *)ctx;
    timing_reset(f);
    f->busy = f->running = 0;
    f->key_step = f->optkey_step = 0;
    f->wrong_key = 0;
    f->half = 0;
    f->row_n = 0;
    load_options(f);
}

/* RM0440 2.6, table 6: BOOT0 from PB8 (nSWBOOT0 1) or the complement of
 * nBOOT0; with BOOT0 high and nBOOT1 set, the system memory */
static int boot_system(struct esk_mach *s, int pin_high)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "FLASH") : 0;
    struct esk_rf_reg *optr = p ? esk_rf_reg(p, "OPTR") : 0;
    u32 o;
    int boot0;
    if (!optr)
        return 0;
    o = (u32)optr->value;
    boot0 = (o >> 26 & 1u) ? pin_high : !(o >> 27 & 1u);
    return boot0 && (o >> 23 & 1u);
}

const struct esk_dev_ops esk_stm32g4_flash_ops = {
    "stm32g4-flash", flash_read, flash_write, flash_reset, flash_tick, flash_next,
};

void *esk_stm32g4_flash_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct g4flash *f;
    int k;
    if (!esk_rf_reg(p, "OPTR") || !esk_rf_reg(p, "KEYR") || !esk_rf_reg(p, "CR") || !esk_rf_reg(p, "SR"))
        return 0;
    f = (struct g4flash *)esk_mach_alloc(s, sizeof *f);
    if (!f)
        return 0;
    f->s = s;
    f->p = p;
    f->acr = esk_rf_reg(p, "ACR");
    f->keyr = esk_rf_reg(p, "KEYR");
    f->optkeyr = esk_rf_reg(p, "OPTKEYR");
    f->sr = esk_rf_reg(p, "SR");
    f->cr = esk_rf_reg(p, "CR");
    f->optr = esk_rf_reg(p, "OPTR");
    if (!f->optkeyr)
        return 0;
    for (k = 0; k < NOBS; k++)
        f->ob[k] = esk_rf_reg(p, obs[k].reg);
    f->irq = esk_rf_irq(p, "FLASH");
    s->bus.rom_write = rom_write;
    s->bus.rom_write_ctx = f;
    s->boot_pin = "PB8";
    s->boot_system = boot_system;
    timing_reset(f);
    return f;
}
