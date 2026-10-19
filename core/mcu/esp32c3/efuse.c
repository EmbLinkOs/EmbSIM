/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* efuse.c -- the ESP32-C3's eFuse controller (TRM v1.4 chapter 4): eleven
 * blocks of one-time-programmable bits, read into the RD_ registers at a
 * chip reset and again by a read command, and programmed (bits only from 0
 * to 1) by a program command.
 *
 *   - CMD's READ_CMD with CONF's OP_CODE 0x5AA5 loads every block into its
 *     RD_ registers (BLOCK0 RD_WR_DIS and RD_REPEAT_DATA0-4, BLOCK1
 *     RD_MAC_SPI_SYS_0-5, BLOCK2 RD_SYS_PART1_DATA0-7, BLOCK3
 *     RD_USR_DATA0-7, BLOCK4-9 RD_KEY0-5_DATA0-7, BLOCK10
 *     RD_SYS_PART2_DATA0-7); PGM_CMD with 0x5A5A ORs PGM_DATA0-7 into block
 *     BLK_NUM. Either completes at once: CMD reads 0 and INT_RAW's
 *     READ_DONE or PGM_DONE is set; another op code does nothing.
 *   - The error registers read 0: the blocks read back without error.
 *
 * The chip's bits out of the factory: a chip revision v0.3 (BLOCK1's
 * WAFER_VERSION_MAJOR 0, MINOR_HI 0, MINOR_LO 3), the revision of the
 * ROM in the description; the QFN32 package (PKG_VERSION 0); the 4 MB
 * flash in the package from XMC (FLASH_CAP 1, FLASH_VENDOR 1), the
 * fields esptool reads as "ESP32-C3 (QFN32) (revision v0.3)" and
 * "Embedded Flash 4MB (XMC)"; the MAC 7C:DF:A1:45:6D:62, Espressif's OUI
 * and a serial of the model's ("Emb"); every other bit 0 (no secure boot,
 * no flash encryption, no keys, and no calibration data in BLOCK2,
 * BLK_VERSION 0 as on the first lots).
 * Not yet: the write and read protection bits (WR_DIS, RD_DIS), the
 * Reed-Solomon check values, the timing registers and the program and
 * read times. */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define NBLOCKS 11
#define OP_READ 0x5AA5u
#define OP_PGM 0x5A5Au

/* each block's first RD_ register and its words */
static const struct {
    const char *rd;
    int words;
} blocks[NBLOCKS] = {
    { "RD_WR_DIS", 6 },     { "RD_MAC_SPI_SYS_0", 6 }, { "RD_SYS_PART1_DATA0", 8 },
    { "RD_USR_DATA0", 8 },  { "RD_KEY0_DATA0", 8 },    { "RD_KEY1_DATA0", 8 },
    { "RD_KEY2_DATA0", 8 }, { "RD_KEY3_DATA0", 8 },    { "RD_KEY4_DATA0", 8 },
    { "RD_KEY5_DATA0", 8 }, { "RD_SYS_PART2_DATA0", 8 },
};

struct c3_efuse {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *conf, *cmd, *raw, *pgm[8];
    u32 bits[NBLOCKS][8]; /* the array */
    struct esk_rf_reg *rd[NBLOCKS][8];
};

static void factory(struct c3_efuse *k)
{
    memset(k->bits, 0, sizeof k->bits);
    k->bits[1][0] = 0xA1456D62u;                         /* MAC_0: the MAC's last four bytes */
    k->bits[1][1] = 0x00007CDFu;                         /* MAC_1: its first two */
    k->bits[1][3] = 1u << 27 | 0u << 21 | 3u << 18;     /* FLASH_CAP, PKG_VERSION, MINOR_LO */
    k->bits[1][4] = 1u;                                 /* FLASH_VENDOR: XMC */
}

/* the array into the RD_ registers */
static void load(struct c3_efuse *k)
{
    int b, w;
    for (b = 0; b < NBLOCKS; b++)
        for (w = 0; w < blocks[b].words; w++)
            esk_rf_hw(k->rd[b][w], k->bits[b][w]);
}

static u32 ef_read(void *ctx, u32 off, int n)
{
    struct c3_efuse *k = (struct c3_efuse *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void ef_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_efuse *k = (struct c3_efuse *)ctx;
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->cmd)) {
        u32 cmd = (u32)k->cmd->value, op = (u32)k->conf->value & 0xffffu;
        if ((cmd & 1u) && op == OP_READ) {
            load(k);
            esk_rf_hw(k->raw, k->raw->value | 1u);
        } else if ((cmd & 2u) && op == OP_PGM) {
            int b = (int)((cmd >> 2) & 15u), w;
            for (w = 0; b < NBLOCKS && w < blocks[b].words; w++)
                k->bits[b][w] |= (u32)k->pgm[w]->value;
            esk_rf_hw(k->raw, k->raw->value | 2u);
        }
        esk_rf_hw(k->cmd, 0);
    }
}

static void ef_reset(void *ctx)
{
    struct c3_efuse *k = (struct c3_efuse *)ctx;
    load(k); /* the chip loads its eFuses at every reset */
}

const struct esk_dev_ops esk_esp32c3_efuse_ops = {
    "esp32c3-efuse", ef_read, ef_write, ef_reset, 0, 0,
};

void *esk_esp32c3_efuse_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_efuse *k = (struct c3_efuse *)esk_mach_alloc(s, sizeof *k);
    char name[16];
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->conf = esk_rf_reg(p, "CONF");
    k->cmd = esk_rf_reg(p, "CMD");
    k->raw = esk_rf_reg(p, "INT_RAW");
    for (i = 0; i < 8; i++) {
        snprintf(name, sizeof name, "PGM_DATA%d", i);
        if (!(k->pgm[i] = esk_rf_reg(p, name)))
            return 0;
    }
    for (i = 0; i < NBLOCKS; i++) {
        struct esk_rf_reg *r = esk_rf_reg(p, blocks[i].rd);
        int w, at = 0;
        if (!r)
            return 0;
        while (at < p->nreg && p->reg[at] != r)
            at++;
        /* a block's words are consecutive registers */
        for (w = 0; w < blocks[i].words; w++) {
            if (at + w >= p->nreg || esk_rf_off(p->reg[at + w]) != esk_rf_off(r) + 4u * (u32)w)
                return 0;
            k->rd[i][w] = p->reg[at + w];
        }
    }
    if (!k->conf || !k->cmd || !k->raw)
        return 0;
    factory(k);
    return k;
}

/* EFUSE_RD_REPEAT_DATA1_REG's WDT_DELAY_SEL: stage 0 of the RWDT is its
 * hold shifted left by this plus one */
int esk_esp32c3_efuse_wdt_delay_sel(struct esk_mach *m)
{
    u32 v = 0;
    if (esk_bus_debug_read(&m->bus, 0x60008834u, 4, &v) != 0)
        return 0;
    return (int)((v >> 16) & 3u);
}
