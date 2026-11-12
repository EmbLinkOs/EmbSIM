/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* syscfg_f4.c -- the STM32F4's system configuration controller (RM0390 8):
 *   - EXTICR1 to EXTICR4 pick the port each EXTI line 0-15 listens to
 *     (0 port A ... 7 port H), for the GPIO model's edges (gpio_f4.c);
 *   - MEMRMP's MEM_MODE picks what answers at 0x00000000: the main flash
 *     (000, out of reset with BOOT0 low), the system memory (001), SRAM1's
 *     112 KiB (011); FMC's bank 1 (010) and SDRAM bank 1 (100) and the
 *     reserved values leave nothing there, as no FMC memory is on the bus;
 *     each reset maps the flash again;
 *   - CMPCR's READY follows CMP_PD: the I/O compensation cell is ready as
 *     soon as it is powered (its start-up time is not modelled);
 *   - PMC (the ADCs' DC2 bits), MEMRMP's SWP_FMC and CFGR (FMPI2C's Fast
 *     mode Plus drive) keep what is written.
 * The alias at 0 is the description's memory mirror at that address, as
 * large as the memory it shows; SRAM1 is the first 112 KiB of the
 * description's SRAM (RM0390 2.3: SRAM1 112 KiB, then SRAM2 16 KiB).
 *
 * The STM32G4's (RM0440 9, a SYSCFG with SCSR): MEM_MODE as above, SRAM1
 * its first 80 KiB, 100 the QUADSPI's memory (none on the bus: nothing
 * there); SCSR's CCMER erases the CCM SRAM (32 KiB at 0x20018000, its
 * alias at 0x10000000) to 0, CCMBSY reading 0 at once; CFGR1 (the FPU's
 * interrupt enables, the I2C pins' Fast-mode Plus drive), CFGR2, SWPR,
 * SKR and MEMRMP's FB_MODE keep what is written. Not modelled: the erase's
 * time (CCMBSY), SWPR's write protection of the CCM SRAM's pages, the
 * parity errors CFGR2 reports, FB_MODE's bank swap. */
#include <string.h>

#include "mcu/stm32/stm32.h"

#define BOOT_ALIAS 0x00000000u

struct syscfg {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *memrmp, *exticr[4], *cmpcr, *scsr;
    u32 mode; /* the MEM_MODE mapped now */
    int g4;
};

static struct syscfg *syscfg_of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "SYSCFG") : 0;
    return p && p->ops == &esk_stm32f4_syscfg_ops ? (struct syscfg *)p->ctx : 0;
}

int esk_stm32f4_exti_port(struct esk_mach *s, int line)
{
    struct syscfg *c = syscfg_of(s);
    if (line < 0 || line > 15)
        return -1;
    if (!c)
        return 0; /* SYSCFG's reset value: port A */
    return (int)(c->exticr[line / 4]->value >> (4 * (line % 4)) & 15);
}

static void remap(struct syscfg *c)
{
    u32 mode = c->memrmp ? (u32)c->memrmp->value & 7 : 0;
    if (mode == c->mode)
        return;
    c->mode = mode;
    switch (mode) {
    case 0:
        (void)esk_bus_remap_mirror(&c->s->bus, BOOT_ALIAS, 0x08000000u, ~0u);
        break;
    case 1:
        (void)esk_bus_remap_mirror(&c->s->bus, BOOT_ALIAS, 0x1FFF0000u, ~0u);
        break;
    case 3:
        (void)esk_bus_remap_mirror(&c->s->bus, BOOT_ALIAS, 0x20000000u, (c->g4 ? 80u : 112u) * 1024u);
        break;
    default:
        (void)esk_bus_remap_mirror(&c->s->bus, BOOT_ALIAS, 0, 0);
        break;
    }
}

static void settle(struct syscfg *c)
{
    if (c->cmpcr) {
        u64 v = c->cmpcr->value & ~(u64)0x100;
        if (v & 1)
            v |= 0x100; /* READY */
        if (v != c->cmpcr->value)
            esk_rf_hw(c->cmpcr, v);
    }
    remap(c);
}

static u32 syscfg_read(void *ctx, u32 off, int n)
{
    return esk_rf_read(((struct syscfg *)ctx)->p, off, n);
}

static void syscfg_write(void *ctx, u32 off, int n, u32 v)
{
    struct syscfg *c = (struct syscfg *)ctx;
    esk_rf_write(c->p, off, n, v);
    if (c->scsr && off == esk_rf_off(c->scsr) && (c->scsr->value & 1u)) {
        /* CCMER: the CCM SRAM erased */
        struct esk_region *r = esk_bus_region(&c->s->bus, 0x20018000u, 32u * 1024u);
        if (r && r->mem) {
            memset(r->mem + (0x20018000u - r->base), 0, 32u * 1024u);
            r->gen++;
        }
        esk_rf_hw(c->scsr, c->scsr->value & ~(u64)3u);
    }
    settle(c);
}

static void syscfg_reset(void *ctx)
{
    struct syscfg *c = (struct syscfg *)ctx;
    c->mode = 0xFF; /* map the flash again */
    settle(c);
}

const struct esk_dev_ops esk_stm32f4_syscfg_ops = {
    "stm32f4-syscfg", syscfg_read, syscfg_write, syscfg_reset, 0, 0,
};

void *esk_stm32f4_syscfg_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct syscfg c, *x;
    static const char *const names[4] = { "EXTICR1", "EXTICR2", "EXTICR3", "EXTICR4" };
    int i;
    memset(&c, 0, sizeof c);
    c.s = s;
    c.p = p;
    for (i = 0; i < 4; i++) {
        c.exticr[i] = esk_rf_reg(p, names[i]);
        if (!c.exticr[i])
            return 0;
    }
    c.memrmp = esk_rf_reg(p, "MEMRM");
    if (!c.memrmp)
        c.memrmp = esk_rf_reg(p, "MEMRMP");
    c.cmpcr = esk_rf_reg(p, "CMPCR");
    c.scsr = esk_rf_reg(p, "SCSR");
    c.g4 = c.scsr != 0;
    c.mode = 0xFF;
    x = (struct syscfg *)esk_mach_alloc(s, sizeof *x);
    if (x)
        *x = c;
    return x;
}
