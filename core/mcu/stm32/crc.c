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
 * The programmable unit (an STM32G4's, RM0440 16; registers INIT and POL):
 *   - CR's POLYSIZE gives the CRC's width (32, 16, 8 or 7 bits) and POL its
 *     polynomial; RESET loads DR with INIT; DR is written a byte, halfword
 *     or word at a time, the access's width of data fed most significant
 *     bit first;
 *   - REV_IN reverses the data's bits by bytes (01), halfwords (10) or the
 *     word (11), within the width written (a byte written with 11 is
 *     reversed as a byte); REV_OUT makes DR read the CRC bit-reversed
 *     within its width.
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
    struct esk_rf_reg *dr, *cr, *init, *pol;
    u32 crc; /* the programmable unit's CRC, DR shows it (REV_OUT) */
};

static u32 reverse(u32 v, int width)
{
    u32 r = 0;
    int k;
    for (k = 0; k < width; k++)
        r |= (v >> k & 1u) << (width - 1 - k);
    return r;
}

static int poly_width(const struct crc *c)
{
    static const int w[4] = { 32, 16, 8, 7 };
    return w[(u32)c->cr->value >> 3 & 3u];
}

static u32 width_mask(int w) { return w >= 32 ? 0xffffffffu : (1u << w) - 1; }

/* DR as the programmable unit shows its CRC */
static void show(struct crc *c)
{
    int w = poly_width(c);
    u32 v = c->crc & width_mask(w);
    if (c->cr->value & (1u << 7))
        v = reverse(v, w);
    esk_rf_hw(c->dr, v);
}

/* `nbits` of `data` (its most significant first) into the CRC */
static void feed(struct crc *c, u32 data, int nbits)
{
    int w = poly_width(c), k;
    u32 poly = (u32)c->pol->value & width_mask(w), top = 1u << (w - 1), crc = c->crc;
    for (k = nbits - 1; k >= 0; k--) {
        u32 in = data >> k & 1u, msb = (crc & top) != 0;
        crc = (crc << 1) & width_mask(w);
        if (in ^ msb)
            crc ^= poly;
    }
    c->crc = crc;
}

static void v2_write(struct crc *c, int n, u32 v)
{
    u32 rev = (u32)c->cr->value >> 5 & 3u;
    int bits = 8 * n, unit = rev == 1 ? 8 : rev == 2 ? 16 : 32, k;
    if (n < 4)
        v &= width_mask(bits);
    if (rev) {
        u32 out = 0;
        if (unit > bits)
            unit = bits;
        for (k = 0; k < bits; k += unit)
            out |= reverse(v >> k & width_mask(unit), unit) << k;
        v = out;
    }
    feed(c, v, bits);
    show(c);
}

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
    if (c->pol) { /* the programmable unit */
        if (off - esk_rf_off(c->dr) < 4) {
            v2_write(c, n, v);
            return;
        }
        esk_rf_write(c->p, off, n, v);
        if (off - esk_rf_off(c->cr) < 4) {
            if (c->cr->value & 1u) {
                c->crc = (u32)c->init->value & width_mask(poly_width(c));
                esk_rf_hw(c->cr, c->cr->value & ~(u64)1u); /* RESET reads 0 */
            }
            show(c);
        }
        return;
    }
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

static void crc_reset(void *ctx)
{
    struct crc *c = (struct crc *)ctx;
    c->crc = 0xffffffffu;
}

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
    c->init = esk_rf_reg(p, "INIT");
    c->pol = esk_rf_reg(p, "POL");
    if (!c->init || !c->cr)
        c->pol = 0;
    c->crc = 0xffffffffu;
    return c;
}
