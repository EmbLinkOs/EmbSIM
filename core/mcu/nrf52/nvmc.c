/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* nvmc.c -- the nRF52840's non-volatile memory controller and its user
 * information configuration registers (the Product Specification's NVMC
 * and UICR chapters), over the register file:
 *   - CONFIG.WEN: read-only (Ren), write (Wen) or erase (Een) enabled;
 *   - writing: with Wen, a word store of the core's to the flash or to a
 *     UICR register programs it: bits only go from 1 to 0 (the memory ANDs
 *     what is written). A store of another size or alignment, or without
 *     Wen, changes nothing;
 *   - erasing, with Een: ERASEPAGE (and ERASEPCR1 at the same offset,
 *     ERASEPCR0) the 4 KiB page an address names, ERASEALL the whole flash
 *     and UICR, ERASEUICR the UICR, to 0xFF. ERASEPAGEPARTIAL erases the
 *     page for ERASEPAGEPARTIALCFG's DURATION milliseconds: the page is
 *     erased once its partial erases add up to a whole erase's time;
 *   - READY is 0 while a write or an erase is under way; READYNEXT with
 *     it (no write is buffered). A program running from the flash stalls on
 *     its next fetch until the operation ends (the CPU is halted while the
 *     NVMC writes or erases), as does a store to the flash while one is
 *     under way; one running from RAM goes on and polls READY. The memory
 *     holds its new contents from the start (a read of it would have
 *     waited);
 *   - the UICR is non-volatile: a reset keeps what was programmed; an
 *     image's UICR segment (a .hex's, as nrfjprog programs it) is loaded
 *     into it.
 * The times are the Product Specification's, which gives their maxima
 * only: a page's (and the UICR's) erase 85 ms (tERASEPAGE); a word's write
 * 41 us (tWRITE) and ERASEALL's 169 ms (tERASEALL), as recalled from its
 * electrical specification and not yet checked against it.
 * A write or erase changes the memory's generation, so a core's decoded
 * code there is decoded again.
 *
 * Not modelled: the times' spread (the maximum each time), the number of
 * writes a word takes between erases (nWRITE: more are undefined, here
 * each ANDs), a partially erased page's undefined contents (it holds what
 * it held until the partial erases add up), the access control lists (ACL:
 * no region is refused), APPROTECT, the instruction cache (ICACHECNF is
 * kept, IHIT and IMISS stay 0), a reset during an operation leaving the
 * memory half written, the UICR across a power cycle of the session. An
 * address within a page erases that page (the specification asks for its
 * first word). */
#include <string.h>

#include "mcu/nrf52/nrf52.h"

#define FLASH_SIZE 0x100000u
#define PAGE 4096u
#define NPAGES (FLASH_SIZE / PAGE)
#define UICR_WORDS 256u

#define O_READY 0x400u
#define O_READYNEXT 0x408u
#define O_CONFIG 0x504u
#define O_ERASEPAGE 0x508u
#define O_ERASEALL 0x50Cu
#define O_ERASEPCR0 0x510u
#define O_ERASEUICR 0x514u
#define O_ERASEPAGEPARTIAL 0x518u
#define O_ERASEPAGEPARTIALCFG 0x51Cu

#define WEN_WEN 1u
#define WEN_EEN 2u

#define PS_US 1000000ull
#define PS_MS 1000000000ull
#define WRITE_PS (41ull * PS_US)
#define ERASEPAGE_PS (85ull * PS_MS)
#define ERASEALL_PS (169ull * PS_MS)

struct nvmc;

struct uicr {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    u32 word[UICR_WORDS]; /* what is programmed: kept through resets */
};

struct nvmc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *ready, *readynext, *config, *partcfg;
    struct esk_region *flash;
    u16 partial_ms[NPAGES]; /* each page's partial erases so far */
    /* the operation in progress: READY 0 until its time has passed */
    int busy, running;
    u64 left_ps, since;
};

static struct uicr *uicr_of(struct esk_mach *s);

static u32 wen(const struct nvmc *f) { return (u32)f->config->value & 3u; }

static void set_ready(struct nvmc *f, int ready)
{
    esk_rf_hw(f->ready, ready ? 1u : 0u);
    if (f->readynext)
        esk_rf_hw(f->readynext, ready ? 1u : 0u);
}

static void nvmc_clock(struct nvmc *f)
{
    if (f->busy && !f->running)
        f->since = f->s->cycles;
    esk_rf_clock(f->p, &f->running, f->busy);
}

static void finish(struct nvmc *f)
{
    if (!f->busy)
        return;
    f->busy = 0;
    set_ready(f, 1);
    nvmc_clock(f);
}

static u64 ps_to_cycles(const struct nvmc *f, u64 ps)
{
    u64 khz = esk_mach_core_hz(f->s) / 1000u;
    return (ps * (khz ? khz : 64000u) + 999999999ull) / 1000000000ull;
}

static struct esk_region *flash_mem(struct nvmc *f)
{
    if (!f->flash)
        f->flash = esk_bus_region(&f->s->bus, 0, 4);
    return f->flash;
}

/* an operation takes `ps`: READY 0 until then. A program in the flash
 * fetches its next instruction from it, and the CPU is halted until the
 * operation is done */
static void begin(struct nvmc *f, u64 ps)
{
    u32 pc = f->s->cpu ? f->s->cpu->ops->pc(f->s->cpu) : 0;
    finish(f); /* one in progress: the bus waited for it */
    f->busy = 1;
    f->left_ps = ps;
    set_ready(f, 0);
    nvmc_clock(f);
    if (pc < FLASH_SIZE)
        f->s->bus_stall += ps_to_cycles(f, ps);
}

/* a store to the NVMC's registers or the memory while it is busy waits
 * for the operation to end */
static void wait_done(struct nvmc *f)
{
    if (f->busy && f->s->bus_stall == 0)
        f->s->bus_stall = ps_to_cycles(f, f->left_ps);
    finish(f);
}

static void changed(struct nvmc *f)
{
    f->s->flash_written = 1; /* a power cycle keeps the flash as it is now */
    esk_bus_code_changed(&f->s->bus);
}

static void uicr_erase(struct uicr *u);

/* ---- writing ------------------------------------------------------------------------- */

static void rom_write(void *ctx, struct esk_region *r, u32 a, int n, u32 v)
{
    struct nvmc *f = (struct nvmc *)ctx;
    u32 old;
    if (r->kind != ESK_MEM_FLASH || wen(f) != WEN_WEN || n != 4 || (a & 3u))
        return; /* not written: the store changes nothing */
    wait_done(f);
    old = esk_mem_rd_le(r->mem + (a - r->base), 4);
    esk_mem_wr_le(r->mem + (a - r->base), 4, old & v);
    changed(f);
    begin(f, WRITE_PS);
}

/* ---- erasing --------------------------------------------------------------------------- */

static void erase_page(struct nvmc *f, u32 a)
{
    struct esk_region *m = flash_mem(f);
    u32 page = a / PAGE;
    if (!m || a >= m->size)
        return; /* no page there */
    memset(m->mem + page * PAGE, 0xff, PAGE);
    f->partial_ms[page] = 0;
    changed(f);
}

static void erase(struct nvmc *f, u32 off, u32 v)
{
    struct esk_region *m;
    struct uicr *u;
    if (wen(f) != WEN_EEN)
        return;
    switch (off) {
    case O_ERASEPAGE:
    case O_ERASEPCR0:
        if (v >= FLASH_SIZE)
            return;
        erase_page(f, v);
        begin(f, ERASEPAGE_PS);
        return;
    case O_ERASEALL:
        if (!(v & 1u))
            return;
        m = flash_mem(f);
        if (m)
            memset(m->mem, 0xff, m->size);
        memset(f->partial_ms, 0, sizeof f->partial_ms);
        u = uicr_of(f->s);
        if (u)
            uicr_erase(u);
        changed(f);
        begin(f, ERASEALL_PS);
        return;
    case O_ERASEUICR:
        if (!(v & 1u))
            return;
        u = uicr_of(f->s);
        if (u)
            uicr_erase(u);
        begin(f, ERASEPAGE_PS);
        return;
    case O_ERASEPAGEPARTIAL: {
        u32 ms = f->partcfg ? (u32)f->partcfg->value & 0x7fu : 10u, page = v / PAGE;
        if (v >= FLASH_SIZE || !ms)
            return;
        if ((u64)f->partial_ms[page] + ms >= ERASEPAGE_PS / PS_MS)
            erase_page(f, v); /* a whole erase's time: erased */
        else
            f->partial_ms[page] = (u16)(f->partial_ms[page] + ms);
        begin(f, (u64)ms * PS_MS);
        return;
    }
    }
}

/* ---- the NVMC's registers -------------------------------------------------------------- */

static u32 nvmc_read(void *ctx, u32 off, int n)
{
    struct nvmc *f = (struct nvmc *)ctx;
    return esk_rf_read(f->p, off, n);
}

static void nvmc_write(void *ctx, u32 off, int n, u32 v)
{
    struct nvmc *f = (struct nvmc *)ctx;
    if (off == O_READY || off == O_READYNEXT)
        return; /* read-only */
    if (off >= O_ERASEPAGE && off <= O_ERASEPAGEPARTIAL) {
        if (n != 4)
            return;
        wait_done(f);
        erase(f, off, v);
        return;
    }
    if (off == O_CONFIG)
        wait_done(f);
    esk_rf_write(f->p, off, n, v); /* CONFIG, ERASEPAGEPARTIALCFG, ICACHECNF, the ACLs */
}

static void nvmc_tick(void *ctx, u32 cycles)
{
    struct nvmc *f = (struct nvmc *)ctx;
    u64 ps, khz;
    if (!f->running)
        return;
    if (f->since > f->s->cycles)
        f->since = 0;
    cycles = esk_mach_after(f->s, cycles, f->since);
    f->since = 0;
    khz = esk_mach_core_hz(f->s) / 1000u;
    ps = (u64)cycles * 1000000000ull / (khz ? khz : 64000u);
    if (f->left_ps <= ps)
        finish(f);
    else
        f->left_ps -= ps;
}

static int nvmc_next(void *ctx, u32 *cycles)
{
    struct nvmc *f = (struct nvmc *)ctx;
    u64 n;
    if (!f->running)
        return 0;
    n = ps_to_cycles(f, f->left_ps);
    *cycles = n == 0 ? 1 : n > 0xffffffffu ? 0xffffffffu : (u32)n;
    return 1;
}

static void nvmc_reset(void *ctx)
{
    struct nvmc *f = (struct nvmc *)ctx;
    f->flash = NULL;
    f->busy = f->running = 0; /* a reset during an operation ends it */
    set_ready(f, 1);
}

const struct esk_dev_ops esk_nrf_nvmc_ops = {
    "nrf-nvmc", nvmc_read, nvmc_write, nvmc_reset, nvmc_tick, nvmc_next,
};

void *esk_nrf_nvmc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct nvmc *f = (struct nvmc *)esk_mach_alloc(s, sizeof *f);
    if (!f)
        return 0;
    memset(f, 0, sizeof *f);
    f->s = s;
    f->p = p;
    f->ready = esk_rf_reg(p, "READY");
    f->readynext = esk_rf_reg(p, "READYNEXT");
    f->config = esk_rf_reg(p, "CONFIG");
    f->partcfg = esk_rf_reg(p, "ERASEPAGEPARTIALCFG");
    if (!f->ready || !f->config)
        return 0;
    s->bus.rom_write = rom_write;
    s->bus.rom_write_ctx = f;
    return f;
}

/* ---- the UICR --------------------------------------------------------------------------- */

static struct nvmc *nvmc_of(struct esk_mach *s)
{
    return s->rf ? (struct nvmc *)esk_rf_ctx_of(s->rf, &esk_nrf_nvmc_ops) : NULL;
}

static struct uicr *uicr_of(struct esk_mach *s)
{
    return s->rf ? (struct uicr *)esk_rf_ctx_of(s->rf, &esk_nrf_uicr_ops) : NULL;
}

/* the programmed words into the registers that have them */
static void uicr_load(struct uicr *u)
{
    int i;
    for (i = 0; i < u->p->nreg; i++) {
        struct esk_rf_reg *r = u->p->reg[i];
        u32 off = r->addr - u->p->base;
        if (off / 4u < UICR_WORDS && !(off & 3u))
            esk_rf_hw(r, u->word[off / 4u]);
    }
}

static void uicr_erase(struct uicr *u)
{
    u32 k;
    for (k = 0; k < UICR_WORDS; k++)
        u->word[k] = 0xFFFFFFFFu;
    uicr_load(u);
}

static u32 uicr_read(void *ctx, u32 off, int n) { return esk_rf_read(((struct uicr *)ctx)->p, off, n); }

static void uicr_write(void *ctx, u32 off, int n, u32 v)
{
    struct uicr *u = (struct uicr *)ctx;
    struct nvmc *f = nvmc_of(u->s);
    if (off / 4u >= UICR_WORDS)
        return;
    if (u->s->bus.debug && !u->s->bus.dap) {
        /* an image's UICR segment, or a debugger's write: as programmed */
        u32 sh = 8u * (off & 3u), m = (n == 4 ? 0xFFFFFFFFu : ((1u << (8 * n)) - 1u)) << sh;
        u->word[off / 4u] = (u->word[off / 4u] & ~m) | ((v << sh) & m);
        uicr_load(u);
        return;
    }
    if (!f || wen(f) != WEN_WEN || n != 4 || (off & 3u))
        return; /* not written */
    wait_done(f);
    u->word[off / 4u] &= v;
    uicr_load(u);
    begin(f, WRITE_PS);
}

static void uicr_reset(void *ctx) { uicr_load((struct uicr *)ctx); }

const struct esk_dev_ops esk_nrf_uicr_ops = { "nrf-uicr", uicr_read, uicr_write, uicr_reset, 0, 0 };

void *esk_nrf_uicr_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct uicr *u = (struct uicr *)esk_mach_alloc(s, sizeof *u);
    u32 k;
    if (!u)
        return 0;
    u->s = s;
    u->p = p;
    for (k = 0; k < UICR_WORDS; k++)
        u->word[k] = 0xFFFFFFFFu;
    return u;
}
