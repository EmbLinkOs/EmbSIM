/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* flash_f4.c -- the STM32F4's flash interface (RM0390 3; PM0081 1.5 and
 * 1.6, the same interface on the F40x/F41x), over the register file:
 *   - KEYR's keys (0x45670123, then 0xCDEF89AB) clear CR's LOCK; a wrong
 *     key is a bus error and keeps CR locked until the next reset; LOCK
 *     set by the program locks it again. A locked CR ignores writes but
 *     LOCK's;
 *   - programming: with PG set, a store of the core's to the main memory
 *     or the OTP area programs it: bits only go from 1 to 0 (the memory
 *     ANDs what is written). The store's size must be PSIZE's (x8 a byte,
 *     x16 a halfword, x32 a word; x64 needs VPP, so no store matches it):
 *     PGPERR otherwise; a store across a 128-bit row is PGAERR; a store
 *     without PG, or with CR locked, is PGSERR; a sector the option
 *     bytes write-protect is WRPERR (nWRP's bit 0, or 1 with SPRMOD's
 *     PCROP). A refused store changes nothing. An OTP block whose lock
 *     byte (0x1FFF7A00 + block) is 0x00 is not programmed;
 *   - erasing: STRT with SER erases sector SNB (the F446's eight: 16, 16,
 *     16, 16, 64, 128, 128 and 128 KiB from 0x08000000), with MER the
 *     main memory, to 0xFF; a protected sector is WRPERR and stays;
 *   - SR: EOP at an operation's end when EOPIE is set; an error sets
 *     OPERR when ERRIE is set; both raise the FLASH interrupt; the flags
 *     are cleared by writing 1;
 *   - the option bytes: OPTKEYR's keys (0x08192A3B, 0x4C5D6E7F) clear
 *     OPTCR's OPTLOCK; OPTSTRT writes OPTCR's RDP, USER (BOR_LEV, WDG_SW,
 *     nRST_STOP, nRST_STDBY), nWRP and SPRMOD to the user configuration
 *     sector (0x1FFFC000, 0x1FFFC008); each reset loads OPTCR from it,
 *     and what it says acts from then: the hardware watchdog (wdg.c), the
 *     reset in Stop and Standby (pwr_f4.c), the write protection here.
 * A program or erase changes the memory's generation, so a core's
 * decoded code there is decoded again.
 *
 * Not modelled: the times a program or erase takes (BSY never reads 1:
 * the operation is done at the store or STRT; the datasheet's times are
 * not at hand here), the bus stall of a read during one, HCLK's 1 MHz
 * minimum, the read protection's levels (RDP is stored; a regression to
 * level 0 does not mass-erase), PCROP's read refusal (RDERR), the
 * ART accelerator's caches (DCRST, ICRST: the core reads the memory). */
#include <string.h>

#include "mcu/stm32/stm32.h"

#define FLASH_BASE 0x08000000u
#define OTP_BASE 0x1FFF7800u
#define OTP_LOCKS 0x1FFF7A00u
#define OPT_BASE 0x1FFFC000u

#define KEY1 0x45670123u
#define KEY2 0xCDEF89ABu
#define OPTKEY1 0x08192A3Bu
#define OPTKEY2 0x4C5D6E7Fu

#define SR_EOP (1u << 0)
#define SR_OPERR (1u << 1)
#define SR_WRPERR (1u << 4)
#define SR_PGAERR (1u << 5)
#define SR_PGPERR (1u << 6)
#define SR_PGSERR (1u << 7)
#define SR_RDERR (1u << 8)
#define SR_W1C (SR_EOP | SR_OPERR | SR_WRPERR | SR_PGAERR | SR_PGPERR | SR_PGSERR | SR_RDERR)

#define CR_PG (1u << 0)
#define CR_SER (1u << 1)
#define CR_MER (1u << 2)
#define CR_STRT (1u << 16)
#define CR_EOPIE (1u << 24)
#define CR_ERRIE (1u << 25)
#define CR_LOCK (1u << 31)

#define OPTCR_OPTLOCK (1u << 0)
#define OPTCR_OPTSTRT (1u << 1)
#define OPTCR_SPRMOD (1u << 31)

#define NSECTORS 8

struct flash {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *keyr, *optkeyr, *sr, *cr, *optcr;
    int irq;
    int key_step, optkey_step; /* the keys written so far */
    int wrong_key;             /* locked until the next reset */
    u32 wrp;                   /* the sectors write-protected, from the option bytes */
};

static const u32 sector_kib[NSECTORS] = { 16, 16, 16, 16, 64, 128, 128, 128 };

static u32 sector_base(int i)
{
    u32 a = FLASH_BASE;
    int k;
    for (k = 0; k < i; k++)
        a += sector_kib[k] * 1024u;
    return a;
}

/* the sector at main-memory offset `off`, or -1 */
static int sector_of(u32 off)
{
    u32 a = 0;
    int k;
    for (k = 0; k < NSECTORS; k++) {
        if (off < a + sector_kib[k] * 1024u)
            return k;
        a += sector_kib[k] * 1024u;
    }
    return -1;
}

static void irq_if(struct flash *f)
{
    u32 sr = (u32)f->sr->value, cr = (u32)f->cr->value;
    if (((sr & SR_EOP) && (cr & CR_EOPIE)) || ((sr & SR_OPERR) && (cr & CR_ERRIE)))
        esk_rf_interrupt(f->p, f->irq);
}

/* an operation's end: EOP, or an error with OPERR */
static void done(struct flash *f, u32 err)
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

static struct esk_region *region_at(struct esk_mach *s, u32 a, u32 n)
{
    return esk_bus_region(&s->bus, a, n);
}

/* ---- programming --------------------------------------------------------------------- */

static void rom_write(void *ctx, struct esk_region *r, u32 a, int n, u32 v)
{
    struct flash *f = (struct flash *)ctx;
    u32 cr = (u32)f->cr->value, off, psize = cr >> 8 & 3u, old;
    int sec = -1, otp;
    if (r->size == 512u * 1024u && r->kind != ESK_MEM_ROM) {
        off = a - r->base; /* the main memory, or its alias at 0 */
        sec = sector_of(off);
    } else if (a >= OTP_BASE && a + (u32)n <= OTP_LOCKS + 16u) {
        off = 0;
    } else {
        return; /* system memory, the option bytes: read-only to stores */
    }
    otp = sec < 0;
    if ((cr & CR_LOCK) || !(cr & CR_PG) || (cr & (CR_SER | CR_MER))) {
        done(f, SR_PGSERR);
        return;
    }
    if ((u32)n != 1u << psize) {
        done(f, SR_PGPERR);
        return;
    }
    if ((a & 15u) + (u32)n > 16u) {
        done(f, SR_PGAERR);
        return;
    }
    if (!otp && (f->wrp >> sec & 1u)) {
        done(f, SR_WRPERR);
        return;
    }
    if (otp && a < OTP_LOCKS) {
        u32 lock = 0xff;
        esk_bus_debug_read(&f->s->bus, OTP_LOCKS + (a - OTP_BASE) / 32u, 1, &lock);
        if ((lock & 0xffu) == 0) {
            done(f, 0); /* a locked block: nothing programmed */
            return;
        }
    }
    (void)off;
    old = esk_mem_rd_le(r->mem + (a - r->base), n);
    esk_mem_wr_le(r->mem + (a - r->base), n, old & v);
    esk_bus_code_changed(&f->s->bus);
    done(f, 0);
}

/* ---- erasing --------------------------------------------------------------------------- */

static void erase(struct flash *f, u32 base, u32 size)
{
    struct esk_region *r = region_at(f->s, base, size);
    if (r)
        memset(r->mem + (base - r->base), 0xff, size);
    esk_bus_code_changed(&f->s->bus);
}

static void start(struct flash *f)
{
    u32 cr = (u32)f->cr->value;
    int k;
    if (cr & CR_MER) {
        for (k = 0; k < NSECTORS; k++)
            if (f->wrp >> k & 1u) {
                done(f, SR_WRPERR);
                return;
            }
        erase(f, FLASH_BASE, 512u * 1024u);
        done(f, 0);
        return;
    }
    if (cr & CR_SER) {
        k = (int)(cr >> 3 & 15u);
        if (k >= NSECTORS || (f->wrp >> k & 1u)) {
            done(f, SR_WRPERR);
            return;
        }
        erase(f, sector_base(k), sector_kib[k] * 1024u);
        done(f, 0);
    }
}

/* ---- the option bytes ---------------------------------------------------------------- */

static u32 opt_half(struct flash *f, u32 a)
{
    u32 v = 0xffffu;
    esk_bus_debug_read(&f->s->bus, a, 2, &v);
    return v & 0xffffu;
}

/* OPTCR as the reset loads it, and the protection it gives */
static void load_options(struct flash *f)
{
    u32 w0 = opt_half(f, OPT_BASE), w8 = opt_half(f, OPT_BASE + 8u);
    u32 v = ((u32)f->optcr->reset & 0x0F000003u) | (w0 & 0xffecu) | (w8 & 0xffu) << 16 |
            ((w8 & 0x8000u) ? OPTCR_SPRMOD : 0u);
    esk_rf_hw(f->optcr, v);
    f->wrp = ((w8 & 0x8000u) ? w8 : ~w8) & ((1u << NSECTORS) - 1u);
}

static void program_options(struct flash *f)
{
    u32 v = (u32)f->optcr->value, w0 = opt_half(f, OPT_BASE), w8 = opt_half(f, OPT_BASE + 8u);
    w0 = (w0 & 0x0013u) | (v & 0xffecu);
    w8 = (w8 & 0x7f00u) | (v >> 16 & 0xffu) | ((v & OPTCR_SPRMOD) ? 0x8000u : 0u);
    esk_bus_debug_write(&f->s->bus, OPT_BASE, 2, w0);
    esk_bus_debug_write(&f->s->bus, OPT_BASE + 8u, 2, w8);
    done(f, 0);
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
            esk_bus_fault(&f->s->bus); /* a bus error, CR locked until reset */
        }
        return;
    }
    if (off - esk_rf_off(f->optkeyr) < 4) {
        if (!(f->optcr->value & OPTCR_OPTLOCK))
            return;
        if (f->optkey_step == 0 && v == OPTKEY1) {
            f->optkey_step = 1;
        } else if (f->optkey_step == 1 && v == OPTKEY2) {
            f->optkey_step = 0;
            esk_rf_hw(f->optcr, f->optcr->value & ~(u64)OPTCR_OPTLOCK);
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
            return; /* locked: only an unlock opens it */
        esk_rf_write(f->p, off, n, v);
        now = (u32)f->cr->value;
        if (now & CR_STRT) {
            esk_rf_hw(f->cr, now & ~(u64)CR_STRT); /* STRT reads 0 once BSY clears */
            start(f);
        }
        return;
    }
    if (off - esk_rf_off(f->optcr) < 4) {
        if (f->optcr->value & OPTCR_OPTLOCK) {
            /* OPTLOCK can be set, nothing else */
            return;
        }
        esk_rf_write(f->p, off, n, v);
        if (f->optcr->value & OPTCR_OPTSTRT) {
            esk_rf_hw(f->optcr, f->optcr->value & ~(u64)OPTCR_OPTSTRT);
            if (!(f->cr->value & CR_LOCK))
                program_options(f);
        }
        return;
    }
    esk_rf_write(f->p, off, n, v); /* ACR */
}

static void flash_reset(void *ctx)
{
    struct flash *f = (struct flash *)ctx;
    f->key_step = f->optkey_step = 0;
    f->wrong_key = 0;
    load_options(f);
}

const struct esk_dev_ops esk_stm32f4_flash_ops = { "stm32f4-flash", flash_read, flash_write, flash_reset, 0, 0 };

void *esk_stm32f4_flash_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct flash f, *c;
    memset(&f, 0, sizeof f);
    f.s = s;
    f.p = p;
    f.keyr = esk_rf_reg(p, "KEYR");
    f.optkeyr = esk_rf_reg(p, "OPTKEYR");
    f.sr = esk_rf_reg(p, "SR");
    f.cr = esk_rf_reg(p, "CR");
    f.optcr = esk_rf_reg(p, "OPTCR");
    if (!f.keyr || !f.optkeyr || !f.sr || !f.cr || !f.optcr)
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
