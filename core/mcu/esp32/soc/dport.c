/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* dport.c -- the ESP32's DPORT registers (TRM v5 chapters 3, 4 and 5):
 *
 *   - the APP_CPU: held in reset while APPCPU_CTRL_A's RESETTING is set
 *     (1 out of reset), its clock gated while APPCPU_CTRL_B's CLKGATE_EN
 *     is clear, stalled while APPCPU_CTRL_C's RUNSTALL is set; released,
 *     it starts at its reset vector, where the ROM reads APPCPU_CTRL_D's
 *     boot address;
 *   - the interrupt matrix (chapter 4): 69 peripheral sources, each mapped
 *     by its PRO_ and APP_ *_MAP register onto one of each CPU's 32
 *     interrupts; an interrupt sees the OR of the sources mapped onto it,
 *     those numbers the CPU drives itself (its timers 6, 15 and 16, its
 *     software interrupts 7 and 29, profiling 11) none; PRO_ and
 *     APP_INTR_STATUS_0-2 read the sources' levels; CPU_INTR_FROM_CPU_0-3
 *     drive sources 24-27;
 *   - the caches and their flash MMU (chapter 3.3.2): each CPU's table of
 *     64 KB pages at 0x3FF10000 (PRO_CPU) and 0x3FF12000 (APP_CPU), an
 *     entry's bits [7:0] the flash page and bit 8 invalid; entries 0-63
 *     map DROM0 (0x3F400000), 64-127 IRAM0 (0x40000000, of which
 *     0x400D0000 up is the description's flash_irom window); the window
 *     reads the flash's bytes the PRO_CPU's entries name. A cache flush
 *     (PRO_ or APP_CACHE_CTRL's FLUSH_ENA) is done at once (FLUSH_DONE),
 *     and the caches' state (PRO_ and APP_DCACHE_DBUG0's CACHE_STATE)
 *     reads 1, idle, which ESP-IDF waits for before disabling them;
 *     pages whose flash the program has written since they were mapped
 *     show it after a flush;
 *   - CPU_PER_CONF's CPUPERIOD_SEL, which with RTC_CNTL's SOC_CLK_SEL and
 *     APB_CTRL's divider sets the CPU's clock (apbctrl.c);
 *   - PERIP_RST_EN: setting the bit of I2C0 (7), LEDC (11) or I2C1 (18)
 *     returns that peripheral to its reset state.
 * Inside, the caches hold the mapped pages whole rather than 32 KB of
 * lines, and both CPUs read through the PRO_CPU's table (ESP-IDF keeps
 * the two the same); a CPU's cache enable and the buses' mask bits
 * (PRO_CACHE_CTRL1) do not stop accesses.
 * Not yet: the cache's lock regions, PSRAM (DRAM1, which the module does
 * not have), the boot remap (PRO_BOOT_REMAP_CTRL), the APB and AHB
 * access permissions (DPORT_APB_MASK, AHBLITE_MPU_TABLE), the MMU's
 * access-error interrupts, the trace memory and the CPU record, the other
 * peripherals' PERIP_RST_EN bits and every PERIP_CLK_EN gate (a gated
 * peripheral still answers). */
#include <string.h>

#include "mcu/esp32/soc/esp32.h"
#include "mcu/xtensa/lx6.h"


#define APPCPU_CTRL_A 0x2Cu
#define APPCPU_CTRL_B 0x30u
#define APPCPU_CTRL_C 0x34u
#define CPU_PER_CONF 0x3Cu
#define PRO_CACHE_CTRL 0x40u
#define APP_CACHE_CTRL 0x58u
#define FROM_CPU_0 0xDCu
#define PRO_STATUS_0 0xECu
#define APP_STATUS_0 0xF8u
#define PRO_MAP 0x104u
#define APP_MAP 0x218u
#define PRO_DCACHE_DBUG0 0x3F0u
#define APP_DCACHE_DBUG0 0x418u
#define CACHE_FLUSH_ENA 0x10u
#define CACHE_FLUSH_DONE 0x20u
#define PERIP_RST_EN 0xC4u

/* PERIP_RST_EN's bits of the modelled peripherals */
static const struct {
    const char *name;
    int bit;
} resets[] = { { "I2C0", 7 }, { "LEDC", 11 }, { "I2C1", 18 } };
#define NRESETS (sizeof resets / sizeof resets[0])

#define MMU_BASE 0x3FF10000u
#define MMU_WORDS 2048 /* per CPU: 0x2000 bytes */
#define MMU_INVALID 0x100u
#define PAGE 0x10000u
#define DROM 0x3F400000u
#define IROM 0x400D0000u
#define IROM_FIRST 77 /* 0x400D0000's entry */
#define INTERNAL_INTS 0x20018840u /* 6, 7, 11, 15, 16, 29: the CPU's own */

struct e32_dport {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    u8 src[ESP32_NSOURCES];
    u32 lines[2]; /* each CPU's interrupt inputs as the matrix drives them */
    u32 mmu[2][MMU_WORDS];
    u8 stale[128];
    u8 *app_rom; /* the ROM as the APP_CPU sees it */
    struct esk_rf_periph *rst[3]; /* resets[]'s peripherals */
};

static struct e32_dport *dport_of(struct esk_mach *m)
{
    return m->rf ? (struct e32_dport *)esk_rf_ctx_of(m->rf, &esk_esp32_dport_ops) : 0;
}

static u32 reg(struct e32_dport *k, u32 off)
{
    return esk_rf_read(k->p, off, 4);
}

static void hw(struct e32_dport *k, u32 off, u32 v)
{
    struct esk_rf_reg *r = esk_rf_reg_at(k->p, off);
    if (r)
        esk_rf_hw(r, v);
}

/* ---- the interrupt matrix ---------------------------------------------- */

static void route(struct e32_dport *k)
{
    int cpu, i, n = esk_lx6_cores(k->s);
    for (cpu = 0; cpu < 2; cpu++) {
        u32 lines = 0, st[3] = { 0, 0, 0 };
        for (i = 0; i < ESP32_NSOURCES; i++) {
            u32 map = reg(k, (cpu ? APP_MAP : PRO_MAP) + 4u * (u32)i) & 31u;
            if (!(k->src[i] & (1u << cpu)))
                continue;
            st[i / 32] |= 1u << (i % 32);
            if (!(INTERNAL_INTS & (1u << map)))
                lines |= 1u << map;
        }
        for (i = 0; i < 3; i++)
            hw(k, (cpu ? APP_STATUS_0 : PRO_STATUS_0) + 4u * (u32)i, st[i]);
        if (cpu < n && lines != k->lines[cpu]) {
            u32 ch = lines ^ k->lines[cpu];
            k->lines[cpu] = lines;
            for (i = 0; i < 32; i++)
                if (ch & (1u << i))
                    esk_lx6_irq(k->s, cpu, i, (int)((lines >> i) & 1u));
        }
    }
}

void esk_esp32_source(struct esk_mach *m, int src, int level)
{
    struct e32_dport *k = dport_of(m);
    u8 v = level ? 3u : 0u;
    if (!k || src < 0 || src >= ESP32_NSOURCES || k->src[src] == v)
        return;
    k->src[src] = v;
    route(k);
}

void esk_esp32_source_cpu(struct esk_mach *m, int cpu, int src, int level)
{
    struct e32_dport *k = dport_of(m);
    u8 v;
    if (!k || src < 0 || src >= ESP32_NSOURCES || cpu < 0 || cpu > 1)
        return;
    v = (u8)(level ? k->src[src] | (1u << cpu) : k->src[src] & ~(1u << cpu));
    if (v == k->src[src])
        return;
    k->src[src] = v;
    route(k);
}

/* ---- the flash MMU and the windows ------------------------------------- */

static void fill(struct e32_dport *k, int entry)
{
    struct esk_store *st = esk_mach_store(k->s, "flash");
    u32 base = entry < 64 ? DROM + (u32)entry * PAGE : IROM + (u32)(entry - IROM_FIRST) * PAGE;
    struct esk_region *w;
    u32 e = k->mmu[0][entry];
    u8 *dst;
    if (entry >= 64 && entry < IROM_FIRST)
        return;
    w = esk_bus_region(&k->s->bus, base, PAGE);
    if (!w || w->kind != ESK_MEM_WINDOW)
        return;
    dst = w->mem + (base - w->base);
    if ((e & MMU_INVALID) || !st || st->size < PAGE)
        memset(dst, 0, PAGE);
    else
        memcpy(dst, st->mem + ((e & 0xffu) * PAGE) % st->size, PAGE);
    if (entry < 128)
        k->stale[entry] = 0;
    esk_bus_code_changed(&k->s->bus);
}

void esk_esp32_flash_written(struct esk_mach *m, u32 addr, u32 len)
{
    struct e32_dport *k = dport_of(m);
    int i;
    if (!k || !len)
        return;
    for (i = 0; i < 128; i++) {
        u32 at = (k->mmu[0][i] & 0xffu) * PAGE;
        if (!(k->mmu[0][i] & MMU_INVALID) && addr < at + PAGE && at < addr + len)
            k->stale[i] = 1;
    }
}

static void flush(struct e32_dport *k)
{
    int i;
    for (i = 0; i < 128; i++)
        if (k->stale[i])
            fill(k, i);
}

static u32 mmu_read(void *ctx, u32 off, int n)
{
    struct e32_dport *k = (struct e32_dport *)ctx;
    u32 cpu = off >= 0x2000u, i = (off & 0x1fffu) / 4u;
    ES_UNUSED(n);
    return k->mmu[cpu][i] >> (8 * (off & 3u));
}

static void mmu_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_dport *k = (struct e32_dport *)ctx;
    u32 cpu = off >= 0x2000u, i = (off & 0x1fffu) / 4u;
    if (n != 4)
        return;
    v &= 0x1ffu;
    if (k->mmu[cpu][i] == v)
        return;
    k->mmu[cpu][i] = v;
    if (cpu == 0 && i < 128)
        fill(k, (int)i);
}

static void mmu_reset(void *ctx)
{
    struct e32_dport *k = (struct e32_dport *)ctx;
    int i, c;
    for (c = 0; c < 2; c++)
        for (i = 0; i < MMU_WORDS; i++)
            k->mmu[c][i] = MMU_INVALID;
    for (i = 0; i < 128; i++)
        fill(k, i);
}

static const struct esk_dev_ops mmu_ops = {
    "esp32-mmu", mmu_read, mmu_write, mmu_reset, 0, 0,
};

/* ---- the APP_CPU ------------------------------------------------------- */

static void app_cpu(struct e32_dport *k)
{
    esk_lx6_core_stall(k->s, 1, 0, !(reg(k, APPCPU_CTRL_B) & 1u));
    esk_lx6_core_stall(k->s, 1, 1, (int)(reg(k, APPCPU_CTRL_C) & 1u));
    esk_lx6_core_reset(k->s, 1, (int)(reg(k, APPCPU_CTRL_A) & 1u));
}

/* ---- the APP_CPU's ROM -------------------------------------------------- */

/* The APP_CPU's ROM: the PRO_CPU's with every word that points into the
 * PRO_CPU's ROM data (0x3FFE0010-0x3FFE3F20: its .data, .bss and stack)
 * pointing into the APP_CPU's (0x3FFE3F20 on: +0x3F10), so that the ROM's
 * routines keep each CPU's state apart (ESP-IDF's esp32.rom.ld names
 * g_ticks_per_us_pro and _app, and ets_update_cpu_frequency_rom "updates
 * g_ticks_per_us on the current CPU only"). Espressif's QEMU loads this
 * view as a second image (esp32-v3-rom-app.bin, sha256 6d3240e7...8442):
 * the one made here equals it byte for byte over its 455,722 bytes. */
#define ROM_BASE 0x40000000u
#define ROM_SIZE 0x70000u
#define ROM1_DBUS 0x3FF90000u
#define PRO_DATA_LO 0x3FFE0010u
#define PRO_DATA_HI 0x3FFE3F20u
#define APP_DATA_SHIFT 0x3F10u

static void app_rom(struct e32_dport *k)
{
    struct esk_region *r = esk_bus_region(&k->s->bus, ROM_BASE, ROM_SIZE);
    u32 o;
    if (k->app_rom || !r || esk_lx6_cores(k->s) < 2)
        return;
    k->app_rom = (u8 *)esk_mach_alloc(k->s, ROM_SIZE);
    if (!k->app_rom)
        return;
    memcpy(k->app_rom, r->mem + (ROM_BASE - r->base), ROM_SIZE);
    for (o = 0; o < ROM_SIZE; o += 4) {
        u8 *p = k->app_rom + o;
        u32 v = (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
        if (v >= PRO_DATA_LO && v <= PRO_DATA_HI) {
            v += APP_DATA_SHIFT;
            p[0] = (u8)v;
            p[1] = (u8)(v >> 8);
            p[2] = (u8)(v >> 16);
            p[3] = (u8)(v >> 24);
        }
    }
    (void)esk_lx6_view(k->s, 1, ROM_BASE, ROM_SIZE, k->app_rom);
    (void)esk_lx6_view(k->s, 1, ROM1_DBUS, 0x10000u, k->app_rom + 0x60000u);
}

/* ---- the registers ----------------------------------------------------- */

static u32 dp_read(void *ctx, u32 off, int n)
{
    struct e32_dport *k = (struct e32_dport *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void dp_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_dport *k = (struct e32_dport *)ctx;
    u32 was = reg(k, PERIP_RST_EN);
    size_t i;
    esk_rf_write(k->p, off, n, v);
    if (off == PERIP_RST_EN) {
        u32 now = reg(k, PERIP_RST_EN);
        for (i = 0; i < NRESETS; i++)
            if (k->rst[i] && (now & ~was & (1u << resets[i].bit)))
                esk_rf_periph_reset(k->rst[i]);
    } else if (off >= APPCPU_CTRL_A && off <= APPCPU_CTRL_C) {
        app_cpu(k);
    } else if (off == PRO_CACHE_CTRL || off == APP_CACHE_CTRL) {
        u32 c = reg(k, off);
        if (c & CACHE_FLUSH_ENA) {
            flush(k);
            c |= CACHE_FLUSH_DONE;
        } else {
            c &= ~CACHE_FLUSH_DONE;
        }
        hw(k, off, c);
    } else if (off >= FROM_CPU_0 && off < FROM_CPU_0 + 16u) {
        esk_esp32_source(k->s, 24 + (int)((off - FROM_CPU_0) / 4u), (int)(reg(k, off) & 1u));
    } else if (off >= PRO_MAP && off < APP_MAP + 4u * ESP32_NSOURCES) {
        route(k);
    } else if (off == CPU_PER_CONF) {
        k->s->yield = 1; /* the CPU's clock may change (apbctrl.c) */
    }
}

static void dp_reset(void *ctx)
{
    struct e32_dport *k = (struct e32_dport *)ctx;
    app_rom(k);
    memset(k->src, 0, sizeof k->src);
    k->lines[0] = k->lines[1] = 0;
    hw(k, PRO_CACHE_CTRL, reg(k, PRO_CACHE_CTRL) | CACHE_FLUSH_DONE);
    hw(k, APP_CACHE_CTRL, reg(k, APP_CACHE_CTRL) | CACHE_FLUSH_DONE);
    hw(k, PRO_DCACHE_DBUG0, 1u << 7); /* CACHE_STATE: idle */
    hw(k, APP_DCACHE_DBUG0, 1u << 7);
    route(k);
}

const struct esk_dev_ops esk_esp32_dport_ops = {
    "esp32-dport", dp_read, dp_write, dp_reset, 0, 0,
};

void *esk_esp32_dport_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_dport *k = (struct e32_dport *)esk_mach_alloc(s, sizeof *k);
    size_t i;
    if (!k)
        return 0;
    k->s = s;
    k->p = p;
    for (i = 0; i < NRESETS; i++)
        k->rst[i] = esk_rf_periph(s->rf, resets[i].name);
    if (esk_mach_add_dev(s, MMU_BASE, 2u * MMU_WORDS * 4u, &mmu_ops, k) != ES_OK ||
        esk_esp32_misc_add(s) || esk_esp32_regi2c_add(s))
        return 0;
    mmu_reset(k);
    return k;
}

u32 esk_esp32_dport_cpuperiod(struct esk_mach *m)
{
    struct e32_dport *k = dport_of(m);
    return k ? reg(k, CPU_PER_CONF) & 3u : 0;
}
