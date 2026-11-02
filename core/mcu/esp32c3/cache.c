/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* cache.c -- the ESP32-C3's cache and MMU (EXTMEM at 0x600C4000, the MMU
 * table at 0x600C5000): the flash read in place through 64 KB pages
 * (TRM v1.4 section 3.3.3; the registers from Espressif's SVD and
 * ESP-IDF's soc/esp32c3/include/soc/ext_mem_defs.h).
 *
 *   - The MMU table: 128 entries, one per 64 KB page of the 8 MB the
 *     instruction bus (0x42000000) and the data bus (0x3C000000) share.
 *     An entry's bits [7:0] are the flash page, bit 8 marks it invalid;
 *     the other bits read 0.
 *   - The window: the description's `flash_ibus` memory, read-only, with
 *     `flash_dbus` its alias on the data bus. A page reads the flash's
 *     bytes its entry names (the flash's address wraps at its size), an
 *     invalid one zeros.
 *   - While flash encryption is on (eFuse SPI_BOOT_CRYPT_CNT odd), a page
 *     reads the flash decrypted by XTS-AES at its flash address (xts.c).
 *   - The operations the program starts and polls complete at once:
 *     invalidate (SYNC_CTRL.INVALIDATE_ENA, then SYNC_DONE), preload
 *     (PRELOAD_ENA, then PRELOAD_DONE), lock and unlock (LOCK_DONE), freeze
 *     (DONE follows ENA); autoload stays as written, AUTOLOAD_DONE 1;
 *     CACHE_STATE says idle.
 * Inside, the cache holds the mapped pages whole rather than 16 KB of
 * 32-byte lines: a page shows the flash as it was when it was mapped, and
 * a page whose flash the program has since written (through SPI1) shows
 * it again after an invalidate that covers it. A program that reads such
 * a page without invalidating sees the old bytes; on the part it would
 * see them only for the lines the cache still holds.
 * Not yet: the cache's enable and the buses' shut bits (ICACHE_CTRL,
 * ICACHE_CTRL1) do not stop accesses; an access through an invalid entry
 * reads zeros without the MMU entry fault interrupt; the access counters
 * and the PMS split. */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define PAGE        65536u
#define NPAGES      128
#define WINDOW      0x42000000u
#define MMU_BASE    0x600C5000u
#define MMU_INVALID 0x100u

struct c3_cache {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *lock, *sync, *sync_addr, *sync_size, *preload, *autoload, *freeze;
    u16 mmu[NPAGES];
    u8 stale[NPAGES]; /* the flash under the page changed since it was read */
};

static struct c3_cache *cache_of(struct esk_mach *m)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_esp32c3_cache_ops)
            return (struct c3_cache *)p->ctx;
    }
    return NULL;
}

static void fill(struct c3_cache *k, int i)
{
    struct esk_region *w = esk_bus_region(&k->s->bus, WINDOW, 1);
    struct esk_store *st = esk_mach_store(k->s, "flash");
    u8 *dst;
    if (!w || w->size < (u32)(i + 1) * PAGE)
        return;
    dst = w->mem + (u32)i * PAGE;
    if ((k->mmu[i] & MMU_INVALID) || !st || st->size < PAGE)
        memset(dst, 0, PAGE);
    else {
        u32 at = ((u32)(k->mmu[i] & 0xffu) * PAGE) % st->size;
        memcpy(dst, st->mem + at, PAGE);
        if (esk_esp32c3_efuse_flash_crypt(k->s))
            esk_esp32c3_xts_crypt(k->s, at, dst, PAGE, 1);
    }
    k->stale[i] = 0;
    esk_bus_code_changed(&k->s->bus);
}

void esk_esp32c3_flash_written(struct esk_mach *m, u32 addr, u32 len)
{
    struct c3_cache *k = cache_of(m);
    struct esk_store *st = esk_mach_store(m, "flash");
    int i;
    if (!k || !st || !len)
        return;
    for (i = 0; i < NPAGES; i++) {
        u32 at = ((u32)(k->mmu[i] & 0xffu) * PAGE) % st->size;
        if (!(k->mmu[i] & MMU_INVALID) && addr < at + PAGE && at < addr + len)
            k->stale[i] = 1;
    }
}

/* SYNC_ADDR and SYNC_SIZE name the bytes; a size of 0 all of them */
static void invalidate(struct c3_cache *k)
{
    u32 a = (u32)k->sync_addr->value & 0x7fffffu, n = (u32)k->sync_size->value & 0x7fffffu;
    int i;
    for (i = 0; i < NPAGES; i++) {
        u32 at = (u32)i * PAGE;
        if (k->stale[i] && (n == 0 || (a < at + PAGE && at < a + n)))
            fill(k, i);
    }
}

static u32 reg(const struct esk_rf_reg *r)
{
    return (u32)r->value;
}

static u32 ca_read(void *ctx, u32 off, int n)
{
    struct c3_cache *k = (struct c3_cache *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void ca_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_cache *k = (struct c3_cache *)ctx;
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->sync) && (reg(k->sync) & 1u)) {
        invalidate(k);
        esk_rf_hw(k->sync, 2u); /* INVALIDATE_ENA done: SYNC_DONE */
    } else if (off == esk_rf_off(k->preload) && (reg(k->preload) & 1u)) {
        esk_rf_hw(k->preload, (reg(k->preload) & ~1u) | 2u);
    } else if (off == esk_rf_off(k->lock) && (reg(k->lock) & 3u)) {
        esk_rf_hw(k->lock, (reg(k->lock) & ~3u) | 4u);
    } else if (off == esk_rf_off(k->freeze)) {
        esk_rf_hw(k->freeze, (reg(k->freeze) & ~4u) | (reg(k->freeze) & 1u) << 2);
    } else if (off == esk_rf_off(k->autoload)) {
        esk_rf_hw(k->autoload, reg(k->autoload) | 8u);
    }
}

static void ca_reset(void *ctx)
{
    struct c3_cache *k = (struct c3_cache *)ctx;
    int i;
    /* the reset's own invalidate (SYNC_CTRL's reset value 1) is done */
    esk_rf_hw(k->sync, 2u);
    for (i = 0; i < NPAGES; i++) {
        int was = !(k->mmu[i] & MMU_INVALID);
        k->mmu[i] = MMU_INVALID;
        if (was)
            fill(k, i);
    }
}

const struct esk_dev_ops esk_esp32c3_cache_ops = {
    "esp32c3-cache", ca_read, ca_write, ca_reset, 0, 0,
};

static u32 mmu_read(void *ctx, u32 off, int n)
{
    struct c3_cache *k = (struct c3_cache *)ctx;
    u32 v = k->mmu[(off / 4) % NPAGES];
    (void)n;
    return v >> (8 * (off & 3u));
}

static void mmu_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_cache *k = (struct c3_cache *)ctx;
    int i = (int)((off / 4) % NPAGES);
    u32 sh = 8u * (off & 3u), mask = (n == 4 ? 0xffffffffu : ((1u << (8 * n)) - 1u)) << sh;
    u32 e = (((u32)k->mmu[i] & ~mask) | (v << sh & mask)) & 0x1ffu;
    if (e == k->mmu[i])
        return;
    k->mmu[i] = (u16)e;
    fill(k, i);
}

static const struct esk_dev_ops mmu_ops = {
    "esp32c3-mmu", mmu_read, mmu_write, 0, 0, 0,
};

void *esk_esp32c3_cache_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_cache *k = (struct c3_cache *)esk_mach_alloc(s, sizeof *k);
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->lock = esk_rf_reg(p, "ICACHE_LOCK_CTRL");
    k->sync = esk_rf_reg(p, "ICACHE_SYNC_CTRL");
    k->sync_addr = esk_rf_reg(p, "ICACHE_SYNC_ADDR");
    k->sync_size = esk_rf_reg(p, "ICACHE_SYNC_SIZE");
    k->preload = esk_rf_reg(p, "ICACHE_PRELOAD_CTRL");
    k->autoload = esk_rf_reg(p, "ICACHE_AUTOLOAD_CTRL");
    k->freeze = esk_rf_reg(p, "ICACHE_FREEZE");
    if (!k->lock || !k->sync || !k->sync_addr || !k->sync_size || !k->preload || !k->autoload ||
        !k->freeze)
        return 0;
    for (i = 0; i < NPAGES; i++)
        k->mmu[i] = MMU_INVALID;
    if (esk_mach_add_dev(s, MMU_BASE, NPAGES * 4u, &mmu_ops, k) != ES_OK)
        return 0;
    return k;
}
