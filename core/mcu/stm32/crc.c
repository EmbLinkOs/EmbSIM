/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* crc.c -- the STM32F1/F2/F4's CRC calculation unit (RM0390 3, RM0008 4),
 * over the register file.
 *
 *   - a word written to DR is fed to the CRC-32 of Ethernet's polynomial
 *     0x04C11DB7, its most significant bit first, with no reflection of
 *     the input or the output and no final XOR; DR reads the result so
 *     far: the unit's fixed configuration on these families;
 *   - CR's RESET (bit 0, it reads 0) sets DR back to 0xFFFFFFFF;
 *   - IDR is a byte of storage the RESET bit leaves alone;
 *   - its clock is RCC's CRCEN (the register file's gate).
 *
 * Inside: the four AHB clocks a word takes are not seen (a write to DR
 * while the unit computes stalls the bus until it is done, so the program
 * reads the finished value either way); the bus stall itself is not
 * modelled. A byte or halfword write to DR, which RM0390 does not allow
 * (the register is written as a word), feeds its value zero-extended. */
#include "mcu/stm32/stm32.h"

#define CRC_POLY 0x04C11DB7u

struct crc {
    struct esk_rf_periph *p;
    struct esk_rf_reg *dr, *cr;
};

/* the CRC-32 of `crc` followed by the word `w`, MSB first */
static u32 crc_word(u32 crc, u32 w)
{
    int i;
    crc ^= w;
    for (i = 0; i < 32; i++)
        crc = crc & 0x80000000u ? (crc << 1) ^ CRC_POLY : crc << 1;
    return crc;
}

static u32 crc_read(void *ctx, u32 off, int n)
{
    struct crc *c = (struct crc *)ctx;
    return esk_rf_read(c->p, off, n);
}

static void crc_write(void *ctx, u32 off, int n, u32 v)
{
    struct crc *c = (struct crc *)ctx;
    if (c->dr && off - esk_rf_off(c->dr) < 4) {
        if (n == 1)
            v &= 0xffu;
        else if (n == 2)
            v &= 0xffffu;
        esk_rf_hw(c->dr, crc_word((u32)c->dr->value, v));
        return;
    }
    if (c->cr && off - esk_rf_off(c->cr) < 4) {
        if (v & 1u)
            esk_rf_hw(c->dr, 0xFFFFFFFFu);
        return; /* RESET reads 0: nothing else in CR */
    }
    esk_rf_write(c->p, off, n, v);
}

static void crc_reset(void *ctx) { (void)ctx; }

const struct esk_dev_ops esk_stm32_crc_ops = {
    "stm32-crc", crc_read, crc_write, crc_reset, 0, 0,
};

void *esk_stm32_crc_create(struct esk_mach *m, struct esk_rf_periph *p)
{
    struct crc *c;
    if (!esk_rf_reg(p, "DR"))
        return 0;
    c = (struct crc *)esk_mach_alloc(m, sizeof *c);
    if (!c)
        return 0;
    c->p = p;
    c->dr = esk_rf_reg(p, "DR");
    c->cr = esk_rf_reg(p, "CR");
    return c;
}
