/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* regi2c.c -- the ESP32-C3's internal I2C master to its analog blocks (at
 * 0x6000E000): the bus over which the BBPLL, the bias, the digital and RTC
 * regulators, the SAR ADC and the temperature sensor are set up. The TRM
 * does not describe it; what is modelled is what the boot ROM's
 * rom_i2c_readReg and rom_i2c_writeReg do with it (the same routines
 * ESP-IDF calls through esp_rom_regi2c_*), and the registers ESP-IDF's
 * soc/regi2c_*.h name.
 *
 *   - A host's control register (I2C0_CTRL at +0, I2C1_CTRL at +4): the
 *     block's address in bits 7:0, the register's in 15:8, the data in
 *     23:16, bit 24 for a write and bit 25 busy. A write of the register
 *     reads or writes the block's register at once: busy reads 0, and a
 *     read's byte is in bits 23:16.
 *   - ANA_CONF0 (+0x40), ANA_CONFIG (+0x44), ANA_CONFIG2 (+0x48): the
 *     blocks' select and power bits, which ESP-IDF and the ROM write and
 *     read back; kept as written.
 *   - The blocks keep their registers through every reset but a chip's
 *     (power-on, CHIP_PU): they are analog, outside the digital system.
 *   - The low-power bias block (0x61): its o-code calibration, started by
 *     IR_RESETB (register 0, bit 0) going from 0 to 1, reports done
 *     (register 3, O_DONE_FLAG bit 0 and BG_O_DONE_FLAG bit 3), which
 *     ESP-IDF's calibrate_ocode waits for.
 * Not yet: the registers' reset values (a block's registers read 0 until
 * written), the time a transfer and the calibration take, and what the
 * values do (the PLL's frequency comes from SYSTEM's CPU_PER_CONF and
 * SYSCLK_CONF, the regulators' voltages are not modelled). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define REGI2C_BASE 0x6000E000u
#define REGI2C_SIZE 0x100u
#define NBLOCKS     16 /* the blocks' addresses are 0x60 to 0x6F */
#define BLOCK_ULP   0x61u

struct c3_regi2c {
    struct esk_mach *s;
    u32 ctrl[2];
    u32 conf[3]; /* ANA_CONF0, ANA_CONFIG, ANA_CONFIG2 */
    u8 reg[NBLOCKS][256];
};

static u8 *block_reg(struct c3_regi2c *k, u32 block, u32 r)
{
    if (block < 0x60u || block >= 0x60u + NBLOCKS)
        return 0;
    return &k->reg[block - 0x60u][r & 0xffu];
}

static void transfer(struct c3_regi2c *k, int host, u32 v)
{
    u32 block = v & 0xffu, r = (v >> 8) & 0xffu, data = (v >> 16) & 0xffu;
    u8 *p = block_reg(k, block, r);
    if (v & 1u << 24) {
        if (p) {
            u8 *done;
            int started = block == BLOCK_ULP && r == 0 && !(*p & 1u) && (data & 1u);
            *p = (u8)data;
            done = &k->reg[BLOCK_ULP - 0x60u][3];
            if (started) /* the o-code calibration: done */
                *done = (u8)(*done | 0x09u);
            if (block == BLOCK_ULP && r == 0 && !(data & 1u))
                *done = (u8)(*done & 0xF6u);
        }
        k->ctrl[host] = v & ~(1u << 25);
    } else {
        data = p ? *p : 0;
        k->ctrl[host] = (v & ~(0xffu << 16 | 1u << 25)) | data << 16;
    }
}

static u32 ri_read(void *ctx, u32 off, int n)
{
    struct c3_regi2c *k = (struct c3_regi2c *)ctx;
    u32 w = off & ~3u, v = 0;
    (void)n;
    if (w < 8u)
        v = k->ctrl[w / 4u];
    else if (w >= 0x40u && w < 0x4Cu)
        v = k->conf[(w - 0x40u) / 4u];
    return v >> (8u * (off & 3u));
}

static void ri_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_regi2c *k = (struct c3_regi2c *)ctx;
    u32 w = off & ~3u, sh = 8u * (off & 3u);
    u32 mask = (n >= 4 ? 0xffffffffu : ((1u << (8 * n)) - 1u)) << sh;
    u32 *slot = 0;
    if (w < 8u)
        slot = &k->ctrl[w / 4u];
    else if (w >= 0x40u && w < 0x4Cu)
        slot = &k->conf[(w - 0x40u) / 4u];
    if (!slot)
        return;
    v = (*slot & ~mask) | (v << sh & mask);
    if (w < 8u)
        transfer(k, (int)(w / 4u), v);
    else
        *slot = v;
}

static void ri_reset(void *ctx)
{
    struct c3_regi2c *k = (struct c3_regi2c *)ctx;
    k->ctrl[0] = k->ctrl[1] = 0;
    if (k->s->reset_cause == ESK_RESET_POWER || k->s->reset_cause == ESK_RESET_PIN) {
        memset(k->conf, 0, sizeof k->conf);
        memset(k->reg, 0, sizeof k->reg);
    }
}

static const struct esk_dev_ops regi2c_ops = {
    "esp32c3-regi2c", ri_read, ri_write, ri_reset, 0, 0,
};

int esk_esp32c3_regi2c_create(struct esk_mach *s)
{
    struct c3_regi2c *k = (struct c3_regi2c *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return -1;
    memset(k, 0, sizeof *k);
    k->s = s;
    return esk_mach_add_dev(s, REGI2C_BASE, REGI2C_SIZE, &regi2c_ops, k) == ES_OK ? 0 : -1;
}
