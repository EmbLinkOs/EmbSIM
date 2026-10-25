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
 *   - The radio's calibrations answer as an ideal radio
 *     (docs/engineering/c3-radios.md, step 2): the BBPLL block (0x66)
 *     reads calibrated and locked, OR_CAL_END and OR_LOCK set and OR_CAL_CAP
 *     8, mid-range (register 8, ESP-IDF's soc/regi2c_bbpll.h); the RF PLL
 *     block (0x62) reads its calibration ended (register 7 bit 1, which
 *     libphy's wait_rfpll_cal_end waits for).
 *   - The rest of the page (+0x4C to +0xFFC) holds the radio's analog
 *     controls, which ESP-IDF's closed PHY drives and no document names;
 *     kept as written, with the measurements its calibrations wait for:
 *     the power detector's start (+0x5C bit 19, rom_pkdet_vol_start) sets
 *     its state (+0x50 bits 26:24) to 7, done; the DC estimator
 *     (txdc_cal_v70) reads done (+0x4C bit 24); the RF PLL's frequency
 *     calibration (set_chan_freq_sw_start) is never busy (+0x168 bit 31)
 *     and finds the code it was given (+0x150 bits 26:20, read back at
 *     +0x170 bits 23:17). Their readings are those of a calibrated front
 *     end: nothing outside the chip sees them.
 * Not yet: the registers' reset values (a block's registers read 0 until
 * written), the time a transfer and the calibrations take, and what the
 * values do (the PLL's frequency comes from SYSTEM's CPU_PER_CONF and
 * SYSCLK_CONF, the regulators' voltages are not modelled). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define REGI2C_BASE 0x6000E000u
#define REGI2C_SIZE 0x1000u
#define NBLOCKS     16 /* the blocks' addresses are 0x60 to 0x6F */
#define BLOCK_ULP   0x61u

struct c3_regi2c {
    struct esk_mach *s;
    u32 ctrl[2];
    u32 conf[3]; /* ANA_CONF0, ANA_CONFIG, ANA_CONFIG2 */
    u8 reg[NBLOCKS][256];
    u32 ana[(REGI2C_SIZE - 0x4Cu) / 4u]; /* the radio's analog controls, +0x4C on */
};

#define BLOCK_RFPLL 0x62u
#define BLOCK_BBPLL 0x66u
#define ANA(k, off) ((k)->ana[((off) - 0x4Cu) / 4u])

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
        if (block == BLOCK_RFPLL && r == 7u)
            *p = (u8)(*p | 0x02u); /* the RF PLL's calibration has ended */
        if (block == BLOCK_BBPLL && r == 8u)
            *p = 0xC8u; /* OR_LOCK, OR_CAL_END, OR_CAL_CAP 8 */
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
    else if (w >= 0x4Cu) {
        if (w == 0x4Cu)
            ANA(k, 0x4Cu) |= 1u << 24; /* the DC estimate is done */
        if (w == 0x170u) /* the RF PLL found the code it was given */
            ANA(k, 0x170u) = (ANA(k, 0x170u) & ~(0x7Fu << 17)) | ((ANA(k, 0x150u) >> 20) & 0x7Fu) << 17;
        v = ANA(k, w);
    }
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
    else
        slot = &ANA(k, w);
    v = (*slot & ~mask) | (v << sh & mask);
    if (w < 8u) {
        transfer(k, (int)(w / 4u), v);
        return;
    }
    if (w == 0x5Cu && (v & ~*slot & 1u << 19)) /* the power detector's reading: done */
        ANA(k, 0x50u) |= 7u << 24;
    *slot = v;
    if (w == 0x168u)
        *slot &= ~(1u << 31); /* the RF PLL is never busy */
}

static void ri_reset(void *ctx)
{
    struct c3_regi2c *k = (struct c3_regi2c *)ctx;
    k->ctrl[0] = k->ctrl[1] = 0;
    memset(k->ana, 0, sizeof k->ana);
    if (k->s->reset_cause == ESK_RESET_POWER || k->s->reset_cause == ESK_RESET_PIN) {
        memset(k->conf, 0, sizeof k->conf);
        memset(k->reg, 0, sizeof k->reg);
    }
}

static const struct esk_dev_ops regi2c_ops = {
    "esp32c3-regi2c", ri_read, ri_write, ri_reset, 0, 0,
};

/* a block's register, as the analog side has it (0 for none) */
u32 esk_esp32c3_regi2c_get(struct esk_mach *s, u32 block, u32 r)
{
    int i;
    for (i = 0; i < s->bus.ndev; i++)
        if (s->bus.dev[i].ops == &regi2c_ops) {
            u8 *p = block_reg((struct c3_regi2c *)s->bus.dev[i].ctx, block, r);
            return p ? *p : 0;
        }
    return 0;
}

int esk_esp32c3_regi2c_create(struct esk_mach *s)
{
    struct c3_regi2c *k = (struct c3_regi2c *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return -1;
    memset(k, 0, sizeof *k);
    k->s = s;
    return esk_mach_add_dev(s, REGI2C_BASE, REGI2C_SIZE, &regi2c_ops, k) == ES_OK ? 0 : -1;
}
