/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* regi2c.c -- the ESP32's internal I2C master to its analog blocks, at
 * 0x3FF4E000 (0x6000E000 on the AHB view, where the ROM reaches it): the
 * bus over which the BBPLL, the APLL, the bias and the regulators are set
 * up. The TRM does not describe it; what is modelled is what the boot
 * ROM's rom_chip_i2c_readReg and rom_chip_i2c_writeReg (0x40004110,
 * 0x40004168) do with it -- the routines ESP-IDF reaches through
 * esp_rom_regi2c_* -- and the registers ESP-IDF's soc/regi2c_*.h name.
 * The ESP32-C3's model (mcu/esp32c3/regi2c.c) has the same protocol.
 *
 *   - Host n's control register (+4n, n 0-7): the block's address in bits
 *     7:0, the register's in 15:8, the data in 23:16, bit 24 for a write,
 *     bit 25 busy. A write of it reads or writes the block's register at
 *     once: busy reads 0, and a read's byte is in bits 23:16.
 *   - ANA_CONFIG (+0x44, ESP-IDF's ANA_CONFIG_REG: the blocks' I2C enables,
 *     I2C_BBPLL bit 17, I2C_APLL bit 14) and the words up to +0xFFC: kept as
 *     written.
 *   - The blocks keep their registers through every reset but a chip's
 *     (power-on, CHIP_PU): they are analog, outside the digital system.
 * Not yet: the registers' reset values (a block's registers read 0 until
 * written), the transfers' time, and what the values do (the PLL's
 * frequency comes from DPORT's CPU_PER_CONF and RTC_CNTL's SOC_CLK_SEL,
 * the regulators' voltages are not modelled). */
#include <string.h>

#include "mcu/esp32/soc/esp32.h"

#define REGI2C_BASE 0x3FF4E000u
#define REGI2C_SIZE 0x1000u
#define NBLOCKS 16 /* the blocks' addresses are 0x60 to 0x6F */

struct e32_regi2c {
    struct esk_mach *s;
    u32 word[REGI2C_SIZE / 4u];
    u8 reg[NBLOCKS][256];
};

static u8 *block_reg(struct e32_regi2c *k, u32 block, u32 r)
{
    if (block < 0x60u || block >= 0x60u + NBLOCKS)
        return 0;
    return &k->reg[block - 0x60u][r & 0xffu];
}

static void transfer(struct e32_regi2c *k, u32 w, u32 v)
{
    u32 block = v & 0xffu, r = (v >> 8) & 0xffu, data = (v >> 16) & 0xffu;
    u8 *p = block_reg(k, block, r);
    if (v & 1u << 24) {
        if (p)
            *p = (u8)data;
        k->word[w / 4u] = v & ~(1u << 25);
    } else {
        data = p ? *p : 0;
        k->word[w / 4u] = (v & ~(0xffu << 16 | 1u << 25)) | data << 16;
    }
}

static u32 ri_read(void *ctx, u32 off, int n)
{
    struct e32_regi2c *k = (struct e32_regi2c *)ctx;
    ES_UNUSED(n);
    return k->word[(off & ~3u) / 4u] >> (8u * (off & 3u));
}

static void ri_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_regi2c *k = (struct e32_regi2c *)ctx;
    u32 w = off & ~3u, sh = 8u * (off & 3u);
    u32 mask = (n >= 4 ? 0xffffffffu : ((1u << (8 * n)) - 1u)) << sh;
    v = (k->word[w / 4u] & ~mask) | (v << sh & mask);
    if (w < 0x20u)
        transfer(k, w, v);
    else
        k->word[w / 4u] = v;
}

static void ri_reset(void *ctx)
{
    struct e32_regi2c *k = (struct e32_regi2c *)ctx;
    if (k->s->reset_cause == ESK_RESET_POWER || k->s->reset_cause == ESK_RESET_PIN) {
        memset(k->word, 0, sizeof k->word);
        memset(k->reg, 0, sizeof k->reg);
    }
    memset(k->word, 0, 0x20u); /* the hosts are idle */
}

static const struct esk_dev_ops regi2c_ops = {
    "esp32-regi2c", ri_read, ri_write, ri_reset, 0, 0,
};

int esk_esp32_regi2c_add(struct esk_mach *s)
{
    struct e32_regi2c *k = (struct e32_regi2c *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return -1;
    k->s = s;
    return esk_mach_add_dev(s, REGI2C_BASE, REGI2C_SIZE, &regi2c_ops, k) == ES_OK ? 0 : -1;
}
