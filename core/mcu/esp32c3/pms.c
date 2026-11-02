/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* pms.c -- the ESP32-C3's permission control on the CPU's own accesses
 * (SENSITIVE at 0x600C1000, TRM v1.4 chapter 14), as ESP-IDF's memory
 * protection (esp_memprot, on by default) configures it; the register
 * layout from Espressif's SVD and ESP-IDF's memprot_ll.h, sensitive_reg.h
 * and memprot_defs.h.
 *
 *   - Internal SRAM: on the instruction bus (IRAM0, 0x40380000-0x403DFFFF)
 *     four areas cut by the split lines I0, I1 and the main I/D line, each
 *     with world 0's read, write and execute permissions (IRAM0
 *     PMS_CONSTRAIN_2); on the data bus (DRAM0, 0x3FC80000-0x3FCDFFFF) four
 *     areas cut by the main line, D0 and D1, with read and write (DRAM0
 *     PMS_CONSTRAIN_1). A split line register gives three 128 KB blocks a
 *     category each (0 below the line, 1 or 2 the block the line is in, at
 *     SPLITADDR x 512 bytes, 3 above); a line with no block holding it lies
 *     below the first block above it, or above all three. The 16 KB below
 *     IRAM0's blocks (0x4037C000) has its own permissions
 *     (CACHEDATAARRAY_PMS_0), as have the ROM on both buses.
 *   - RTC memory (0x50000000-0x50001FFF): a low and a high area cut by
 *     RTCFAST_SPLTADDR_WORLD_0 x 4 (PIF PMS_CONSTRAIN_9), with world 0's
 *     write, read and execute permissions (PMS_CONSTRAIN_10).
 *   - A denied access is blocked: a load reads 0, a store writes nothing,
 *     a fetch reads 0 (an illegal instruction), with no exception of its
 *     own. The monitor of its bus, while its VIOLATE_EN is set and
 *     VIOLATE_CLR clear, latches the first: VIOLATE_INTR, the address, the
 *     world (1, world 0), whether it wrote, whether it was a load or store
 *     rather than a fetch (IRAM0, the PIF's HPORT_0), the byte lanes
 *     (DRAM0) or the size (the PIF); VIOLATE_CLR clears it. VIOLATE_INTR
 *     drives the interrupt sources 56 (IRAM0), 57 (DRAM0) and 58 (the
 *     PIF), which ESP-IDF routes to its panic handler.
 *   - The lock registers stick at 1 until a reset, and their constraints
 *     and monitor settings then ignore writes. A CPU reset resets it all
 *     (rtc.c).
 * Not yet: world 1 and the world controller (the CPU runs in world 0, as
 * ESP-IDF keeps it), the peripheral regions (REGION_PMS_*), the PIF's
 * non-word access monitor, the DMA and cache permission checks (their
 * registers keep what is written), the DRAM0 monitor's STATUS_LOCK. */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define IRAM_CACHE_LO 0x4037C000u
#define IRAM_LO 0x40380000u
#define IRAM_HI 0x403E0000u
#define DRAM_LO 0x3FC80000u
#define DRAM_HI 0x3FCE0000u
#define RTC_LO 0x50000000u
#define RTC_HI 0x50002000u
#define IROM_LO 0x40000000u
#define IROM_HI 0x40060000u
#define DROM_LO 0x3FF00000u
#define DROM_HI 0x3FF20000u
#define BLOCK 0x20000u

#define SRC_IRAM0 56
#define SRC_DRAM0 57
#define SRC_PIF 58

/* the registers, by offset */
#define R_LINE_LOCK 0x90u
#define R_LINE_MAIN 0x94u
#define R_LINE_I0 0x98u
#define R_LINE_I1 0x9Cu
#define R_LINE_D0 0xA0u
#define R_LINE_D1 0xA4u
#define R_IRAM_LOCK 0xA8u
#define R_IRAM_W1 0xACu
#define R_IRAM_W0 0xB0u
#define R_IMON_LOCK 0xB4u
#define R_IMON_CTRL 0xB8u
#define R_IMON_ST 0xBCu
#define R_DRAM_LOCK 0xC0u
#define R_DRAM_PMS 0xC4u
#define R_DMON_LOCK 0xC8u
#define R_DMON_CTRL 0xCCu
#define R_DMON_ST 0xD0u
#define R_DMON_ST2 0xD4u
#define R_PIF_LOCK 0xD8u
#define R_PIF_FIRST 0xDCu
#define R_PIF_RTC_LINE 0xFCu
#define R_PIF_RTC_PMS 0x100u
#define R_REGION_LOCK 0x104u
#define R_REGION_FIRST 0x108u
#define R_REGION_LAST 0x12Cu
#define R_PMON_LOCK 0x130u
#define R_PMON_CTRL 0x134u
#define R_PMON_ST 0x138u
#define R_PMON_ADDR 0x13Cu
#define R_PMON_NW_CTRL 0x140u

#define MON_CLR 1u
#define MON_EN 2u

enum { MON_IRAM, MON_DRAM, MON_PIF };

struct c3_pms {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    /* the configuration in force, from the registers */
    u32 iline[3];    /* IRAM0: I0, I1, main */
    u32 dline[3];    /* DRAM0: main, D0, D1 */
    u32 rtc_line;
    u8 iperm[4], dperm[4], cache_perm, irom_perm, drom_perm, rtc_perm[2];
    int src_level[3];
    struct esk_rf_reg *reg[0x150 / 4]; /* by offset */
};

#define NREGS (0x150u / 4u)

/* a register's value (what the hardware holds, whatever a bus read gives) */
static u32 rd(struct c3_pms *k, u32 off)
{
    struct esk_rf_reg *r = off / 4u < NREGS ? k->reg[off / 4u] : NULL;
    return r ? (u32)r->value : 0u;
}

/* a register's value as the hardware sets it */
static void hw(struct c3_pms *k, u32 off, u32 v)
{
    struct esk_rf_reg *r = off / 4u < NREGS ? k->reg[off / 4u] : NULL;
    if (r)
        esk_rf_hw(r, v);
}

/* a split line register's address on a bus whose blocks start at `base` */
static u32 line_of(u32 v, u32 base)
{
    u32 split = (v >> 14) & 0xffu, j;
    for (j = 0; j < 3; j++) {
        u32 cat = (v >> (2 * j)) & 3u;
        if (cat == 1u || cat == 2u)
            return base + j * BLOCK + (split << 9);
    }
    for (j = 0; j < 3; j++)
        if (((v >> (2 * j)) & 3u) == 3u)
            return base + j * BLOCK;
    return base + 3u * BLOCK;
}

static int check(void *ctx, u32 a, int n, int kind);

/* the configuration from the registers; the check is in force unless it
 * all allows everything (the reset's) */
static void configure(struct c3_pms *k)
{
    u32 iw0 = rd(k, R_IRAM_W0), dp = rd(k, R_DRAM_PMS), rp = rd(k, R_PIF_RTC_PMS);
    int i, all = 1;
    k->iline[0] = line_of(rd(k, R_LINE_I0), IRAM_LO);
    k->iline[1] = line_of(rd(k, R_LINE_I1), IRAM_LO);
    k->iline[2] = line_of(rd(k, R_LINE_MAIN), IRAM_LO);
    k->dline[0] = line_of(rd(k, R_LINE_MAIN), DRAM_LO);
    k->dline[1] = line_of(rd(k, R_LINE_D0), DRAM_LO);
    k->dline[2] = line_of(rd(k, R_LINE_D1), DRAM_LO);
    for (i = 0; i < 4; i++) {
        k->iperm[i] = (u8)((iw0 >> (3 * i)) & 7u);
        k->dperm[i] = (u8)((dp >> (2 * i)) & 3u);
        all = all && k->iperm[i] == 7u && k->dperm[i] == 3u;
    }
    k->cache_perm = (u8)((iw0 >> 12) & 7u);
    k->irom_perm = (u8)((iw0 >> 18) & 7u);
    k->drom_perm = (u8)((dp >> 24) & 3u);
    k->rtc_line = RTC_LO + ((rd(k, R_PIF_RTC_LINE) & 0x7ffu) << 2);
    k->rtc_perm[0] = (u8)(rp & 7u);
    k->rtc_perm[1] = (u8)((rp >> 3) & 7u);
    all = all && k->cache_perm == 7u && k->irom_perm == 7u && k->drom_perm == 3u &&
          k->rtc_perm[0] == 7u && k->rtc_perm[1] == 7u;
    if (all)
        k->s->access_denied = 0;
    else
        k->s->access_denied = check;
    k->s->access_ctx = k;
}

static void update_sources(struct c3_pms *k)
{
    static const u32 ctrl[3] = { R_IMON_CTRL, R_DMON_CTRL, R_PMON_CTRL };
    static const u32 st[3] = { R_IMON_ST, R_DMON_ST, R_PMON_ST };
    static const int src[3] = { SRC_IRAM0, SRC_DRAM0, SRC_PIF };
    int i;
    for (i = 0; i < 3; i++) {
        int level = (rd(k, ctrl[i]) & MON_EN) && (rd(k, st[i]) & 1u);
        if (level != k->src_level[i]) {
            k->src_level[i] = level;
            esk_esp32c3_source(k->s, src[i], level);
        }
    }
}

/* the monitor of `mon` latches its first violation */
static void violate(struct c3_pms *k, int mon, u32 a, int n, int kind)
{
    static const u32 ctrl[3] = { R_IMON_CTRL, R_DMON_CTRL, R_PMON_CTRL };
    static const u32 st[3] = { R_IMON_ST, R_DMON_ST, R_PMON_ST };
    u32 c = rd(k, ctrl[mon]), wr = kind == ESK_ACCESS_STORE ? 1u : 0u;
    if (!(c & MON_EN) || (c & MON_CLR) || (rd(k, st[mon]) & 1u))
        return;
    switch (mon) {
    case MON_IRAM:
        hw(k, R_IMON_ST, 1u | wr << 1 | (kind != ESK_ACCESS_FETCH ? 1u : 0u) << 2 | 1u << 3 |
                             (((a - 0x40000000u) >> 2) & 0xffffffu) << 5);
        break;
    case MON_DRAM: {
        u32 lanes = ((1u << (n >= 4 ? 4 : n)) - 1u) << (a & 3u);
        hw(k, R_DMON_ST, 1u | 1u << 2 | (((a - 0x3C000000u) >> 2) & 0xffffffu) << 4);
        hw(k, R_DMON_ST2, wr | (lanes & 0xfu) << 1);
        break;
    }
    default:
        hw(k, R_PMON_ST, 1u | (kind != ESK_ACCESS_FETCH ? 1u : 0u) << 1 |
                             (u32)(n >= 4 ? 2 : n >= 2 ? 1 : 0) << 2 | wr << 5 | 1u << 6);
        hw(k, R_PMON_ADDR, a);
        break;
    }
    update_sources(k);
}

static int check(void *ctx, u32 a, int n, int kind)
{
    struct c3_pms *k = (struct c3_pms *)ctx;
    u32 need;
    int mon, area;
    u8 perm;
    if (a >= IRAM_CACHE_LO && a < IRAM_HI) {
        /* R 1, W 2, X 4 */
        mon = MON_IRAM;
        if (a < IRAM_LO)
            perm = k->cache_perm;
        else {
            area = a < k->iline[0] ? 0 : a < k->iline[1] ? 1 : a < k->iline[2] ? 2 : 3;
            perm = k->iperm[area];
        }
        need = kind == ESK_ACCESS_FETCH ? 4u : kind == ESK_ACCESS_STORE ? 2u : 1u;
    } else if (a >= DRAM_LO && a < DRAM_HI) {
        if (kind == ESK_ACCESS_FETCH)
            return 0;
        mon = MON_DRAM;
        area = a < k->dline[0] ? 0 : a < k->dline[1] ? 1 : a < k->dline[2] ? 2 : 3;
        perm = k->dperm[area];
        need = kind == ESK_ACCESS_STORE ? 2u : 1u;
    } else if (a >= RTC_LO && a < RTC_HI) {
        /* W 1, R 2, X 4 */
        mon = MON_PIF;
        perm = k->rtc_perm[a < k->rtc_line ? 0 : 1];
        need = kind == ESK_ACCESS_FETCH ? 4u : kind == ESK_ACCESS_STORE ? 1u : 2u;
    } else if (a >= IROM_LO && a < IROM_HI) {
        mon = MON_IRAM;
        perm = k->irom_perm;
        need = kind == ESK_ACCESS_FETCH ? 4u : kind == ESK_ACCESS_STORE ? 2u : 1u;
    } else if (a >= DROM_LO && a < DROM_HI) {
        if (kind == ESK_ACCESS_FETCH)
            return 0;
        mon = MON_DRAM;
        perm = k->drom_perm;
        need = kind == ESK_ACCESS_STORE ? 2u : 1u;
    } else {
        return 0;
    }
    if (perm & need)
        return 0;
    violate(k, mon, a, n, kind);
    return 1;
}

/* the register a lock register guards: its lock's offset, or 0 */
static u32 lock_of(u32 off)
{
    if (off >= R_LINE_MAIN && off <= R_LINE_D1)
        return R_LINE_LOCK;
    if (off == R_IRAM_W1 || off == R_IRAM_W0)
        return R_IRAM_LOCK;
    if (off == R_IMON_CTRL)
        return R_IMON_LOCK;
    if (off == R_DRAM_PMS)
        return R_DRAM_LOCK;
    if (off == R_DMON_CTRL)
        return R_DMON_LOCK;
    if (off >= R_PIF_FIRST && off <= R_PIF_RTC_PMS)
        return R_PIF_LOCK;
    if (off >= R_REGION_FIRST && off <= R_REGION_LAST)
        return R_REGION_LOCK;
    if (off == R_PMON_CTRL || off == R_PMON_NW_CTRL)
        return R_PMON_LOCK;
    return 0;
}

static int is_lock(u32 off)
{
    return off == R_LINE_LOCK || off == R_IRAM_LOCK || off == R_IMON_LOCK || off == R_DRAM_LOCK ||
           off == R_DMON_LOCK || off == R_PIF_LOCK || off == R_REGION_LOCK || off == R_PMON_LOCK;
}

static u32 pms_read(void *ctx, u32 off, int n)
{
    struct c3_pms *k = (struct c3_pms *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void pms_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_pms *k = (struct c3_pms *)ctx;
    u32 w = off & ~3u, lock = lock_of(w);
    if (lock && (rd(k, lock) & 1u))
        return;
    if (is_lock(w)) {
        if (v & 1u)
            hw(k, w, 1u);
        return;
    }
    if (w == R_IMON_ST || w == R_DMON_ST || w == R_DMON_ST2 || w == R_PMON_ST || w == R_PMON_ADDR)
        return; /* the status is the monitor's */
    esk_rf_write(k->p, off, n, v);
    if (w == R_IMON_CTRL || w == R_DMON_CTRL || w == R_PMON_CTRL) {
        if (rd(k, w) & MON_CLR) {
            if (w == R_IMON_CTRL)
                hw(k, R_IMON_ST, 0);
            else if (w == R_DMON_CTRL) {
                hw(k, R_DMON_ST, 0);
                hw(k, R_DMON_ST2, 0);
            } else {
                hw(k, R_PMON_ST, 0);
                hw(k, R_PMON_ADDR, 0);
            }
        }
        update_sources(k);
        return;
    }
    if ((w >= R_LINE_MAIN && w <= R_IRAM_W0) || w == R_DRAM_PMS || w == R_PIF_RTC_LINE ||
        w == R_PIF_RTC_PMS)
        configure(k);
}

static void pms_reset(void *ctx)
{
    struct c3_pms *k = (struct c3_pms *)ctx;
    int i;
    configure(k);
    for (i = 0; i < 3; i++)
        k->src_level[i] = -1;
    update_sources(k);
}

const struct esk_dev_ops esk_esp32c3_pms_ops = {
    "esp32c3-pms", pms_read, pms_write, pms_reset, 0, 0,
};

void *esk_esp32c3_pms_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_pms *k = (struct c3_pms *)esk_mach_alloc(s, sizeof *k);
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    for (i = 0; i < p->nreg; i++) {
        u32 off = p->reg[i]->addr - p->base;
        if (off / 4u < NREGS)
            k->reg[off / 4u] = p->reg[i];
    }
    configure(k);
    return k;
}
