/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* boot.c -- the RP2040's boot from flash (its datasheet, 2.8.1).
 *
 * EmbSIM does not run the bootrom's code: at each reset its ROM (at 0) is
 * a small stand-in that does what the bootrom does to boot from flash, and
 * hands over as it does. The first 256 bytes of flash are boot2: their
 * CRC-32 (polynomial 0x04c11db7, seed 0xffffffff, not reflected, no final
 * XOR) over the first 252 must be the last word, or the part would go on
 * to its USB boot -- it waits there, and says so (mcu.no_boot2). A good
 * boot2 is copied to the top 256 bytes of SRAM (0x20041f00) and entered at
 * 0x20041f01 with lr 0, with the peripherals as the bootrom leaves them:
 * PADS_BANK0, IO_QSPI and PADS_QSPI out of reset, the QSPI pins given to
 * the XIP, GPIO26-29's input enables off, the rest in reset.
 *
 * The ROM's vector table gives the bootrom's stack top (0x20041f00), its
 * header the bootrom's magic and version (B2's, 3), and its table lookup
 * finds no function: the bootrom's functions (flash programming, the
 * float library, memcpy, reset_usb_boot) are not there. Core 1 stays in
 * the bootrom's wait for its vector: it never runs. */
#include <stdio.h>
#include <string.h>

#include "mcu/rp2040/rp2040.h"

/* EmbSIM's ROM, assembled from this (llvm-mc --triple=thumbv6m):
 *
 *     .word 0x20041f00          @ 0x00: the bootrom's stack top
 *     .word reset + 1           @ 0x04
 *     .word dead + 1            @ 0x08: NMI
 *     .word dead + 1            @ 0x0c: HardFault
 *     .byte 'M', 'u', 1, 3      @ 0x10: magic, version 1, ROM version 3 (B2)
 *     .hword table, table, lookup + 1, 0
 * lookup:                       @ 0x1c: rom_table_lookup finds nothing
 *     movs r0, #0
 *     bx lr
 * reset:                        @ 0x20: enter boot2, as the bootrom does
 *     ldr r0, =0x20041f01
 *     movs r1, #0
 *     mov lr, r1
 *     bx r0
 * dead:                         @ 0x28: the USB boot's wait
 *     wfi
 *     b dead
 * table:                        @ 0x2c: the function and data tables, empty
 *     .hword 0, 0
 *     .word 0x20041f01          @ 0x30: the literal pool */
static const u8 rom_image[] = {
    0x00, 0x1f, 0x04, 0x20, 0x21, 0x00, 0x00, 0x00, 0x29, 0x00, 0x00, 0x00, 0x29, 0x00,
    0x00, 0x00, 0x4d, 0x75, 0x01, 0x03, 0x2c, 0x00, 0x2c, 0x00, 0x1d, 0x00, 0x00, 0x00,
    0x00, 0x20, 0x70, 0x47, 0x03, 0x48, 0x00, 0x21, 0x8e, 0x46, 0x00, 0x47, 0x30, 0xbf,
    0xfd, 0xe7, 0x00, 0x00, 0x00, 0x00, 0x01, 0x1f, 0x04, 0x20,
};
#define ROM_DEAD 0x29u

#define FLASH_BASE 0x10000000u
#define BOOT2_AT 0x20041f00u

u32 esk_rp2040_crc32(const u8 *p, u32 n)
{
    u32 c = 0xffffffffu, i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= (u32)p[i] << 24;
        for (k = 0; k < 8; k++)
            c = (c & 0x80000000u) ? c << 1 ^ 0x04c11db7u : c << 1;
    }
    return c;
}

void esk_rp2040_boot(struct esk_mach *s)
{
    struct esk_region *rom = esk_bus_region(&s->bus, 0, (u32)sizeof rom_image);
    struct esk_region *f = esk_bus_region(&s->bus, FLASH_BASE, 256);
    struct esk_region *m = esk_bus_region(&s->bus, BOOT2_AT, 256);
    struct esk_rf_periph *p;
    const u8 *b2;
    u32 sum, want;
    int i;
    if (!rom || !f || !m)
        return;
    memcpy(rom->mem + (0 - rom->base), rom_image, sizeof rom_image);
    b2 = f->mem + (FLASH_BASE - f->base);
    sum = esk_rp2040_crc32(b2, 252);
    want = (u32)b2[252] | (u32)b2[253] << 8 | (u32)b2[254] << 16 | (u32)b2[255] << 24;
    if (sum != want) {
        /* the reset vector: the USB boot's wait */
        rom->mem[4] = (u8)ROM_DEAD;
        rom->mem[5] = rom->mem[6] = rom->mem[7] = 0;
        esk_rf_diag(s, ES_SEV_WARNING, "mcu.no_boot2",
                    "the RP2040's bootrom finds no boot2 in flash (the CRC-32 of its first 252 bytes is "
                    "0x%08lx, the word after them 0x%08lx): the part waits in its USB boot",
                    (unsigned long)sum, (unsigned long)want);
        return;
    }
    memcpy(m->mem + (BOOT2_AT - m->base), b2, 256);
    esk_rp2040_bootrom_resets(s);
    /* connect_internal_flash: the QSPI pins' function is the XIP's */
    p = esk_rf_periph(s->rf, "IO_QSPI");
    for (i = 0; p && i < p->nreg; i++)
        if (strstr(p->reg[i]->name, "_CTRL"))
            esk_rf_hw(p->reg[i], 0);
    p = esk_rf_periph(s->rf, "PADS_BANK0");
    for (i = 26; p && i <= 29; i++) {
        char nm[16];
        struct esk_rf_reg *q;
        (void)snprintf(nm, sizeof nm, "GPIO%d", i);
        q = esk_rf_reg(p, nm);
        if (q)
            esk_rf_hw(q, q->value & ~esk_rf_mask(q, "IE"));
    }
}
