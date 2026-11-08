/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* efuse.c -- the ESP32's eFuse controller (TRM v5 chapter 20): four blocks
 * of one-time-programmable bits (BLOCK0's seven words, BLOCK1-3's eight),
 * read into the RDATA registers at a chip reset and again by a read
 * command, programmed (bits only from 0 to 1) from the WDATA registers by
 * a program command.
 *
 *   - CMD's READ_CMD (bit 0) with CONF's OP_CODE 0x5AA5 loads every block
 *     into its RDATA registers; PGM_CMD (bit 1) with 0x5A5A ORs every
 *     block's WDATA into its bits. CMD reads the command until it is done
 *     (the ROM checks that it does: a CMD that reads 0 at once is a
 *     glitch, and it resets the chip), then 0, and INT_RAW's READ_DONE or
 *     PGM_DONE is set; another op code does nothing. The WDATA registers
 *     keep what is written until written again.
 *   - Read protection: BLOCK0's RD_DIS bits (16-19) make BLOCK1, BLOCK2 or
 *     BLOCK3 read 0 (the flash encryption key in BLOCK1 is still used by
 *     the hardware). Write protection: a block whose WR_DIS bit (7, 8, 9
 *     for BLOCK1-3) is set is not programmed; BLOCK0's own fields are
 *     programmed whatever their WR_DIS bits say (not yet: per field).
 *
 * The chip's bits out of the factory, those of an ESP32-D0WD-V3 of chip
 * revision v3.1 as esptool reads the owner's board: CHIP_PACKAGE 1
 * (D0WD), CHIP_VER_REV1 and CHIP_VER_REV2 set (with APB_CTRL's DATE bit
 * 31, the major revision 3), WAFER_VERSION_MINOR 1; Coding Scheme None;
 * an ADC reference voltage calibration (ADC_VREF, "Vref calibration in
 * eFuse"); the MAC 24:0A:C4:45:6D:62, Espressif's OUI and a serial of the
 * model's, with its CRC-8 (the ROM's esp_rom_efuse_mac_address_crc8), or
 * the one a board's settings give its chip (settings.mac); every other
 * bit 0 (no secure boot, no flash encryption, no keys).
 * Inside, a read takes 1 us and a program 1 ms, whatever EFUSE_CLK's
 * timing fields say (estimates: the TRM gives the timing parameters, not
 * the operations' durations).
 * Not yet: the 3/4 coding scheme's check, BLOCK0's per-field write
 * protection. */
#include <string.h>

#include "mcu/esp32/soc/esp32.h"

#define OP_READ 0x5AA5u
#define OP_PGM 0x5A5Au
#define CONF 0xFCu
#define CMD 0x104u
#define INT_RAW 0x108u
#define INT_ST 0x10Cu
#define INT_ENA 0x110u
#define INT_CLR 0x114u

/* each block's RDATA and WDATA offsets and words */
static const struct {
    u32 rd, wr;
    int words;
} blocks[4] = { { 0x00, 0x1C, 7 }, { 0x38, 0x98, 8 }, { 0x58, 0xB8, 8 }, { 0x78, 0xD8, 8 } };

struct e32_efuse {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    u32 fuse[4][8]; /* the bits themselves */
    u32 raw;        /* INT_RAW */
    int programmed; /* the bits differ from the factory's */
    u32 busy;       /* the command in progress (1 read, 2 program), or 0 */
    u64 left;       /* its cycles still to go */
    int clocked;
};

static void hw(struct esk_rf_periph *p, u32 off, u32 v)
{
    struct esk_rf_reg *r = esk_rf_reg_at(p, off);
    if (r)
        esk_rf_hw(r, v);
}

static struct e32_efuse *efuse_of(struct esk_mach *m)
{
    return m->rf ? (struct e32_efuse *)esk_rf_ctx_of(m->rf, &esk_esp32_efuse_ops) : 0;
}

/* CRC-8, polynomial x^8 + x^5 + x^4 + 1 reflected (0x8C), as the ROM's
 * esp_rom_efuse_mac_address_crc8 computes it over the MAC */
static u8 crc8(const u8 *p, int n)
{
    u8 crc = 0;
    int i, b;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (b = 0; b < 8; b++)
            crc = (u8)((crc & 1u) ? (crc >> 1) ^ 0x8Cu : crc >> 1);
    }
    return crc;
}

static void factory(struct e32_efuse *k)
{
    static const u8 dflt[6] = { 0x24, 0x0A, 0xC4, 0x45, 0x6D, 0x62 };
    const u8 *mac = k->s->has_factory_mac ? k->s->factory_mac : dflt;
    memset(k->fuse, 0, sizeof k->fuse);
    /* MAC: byte 0 at bit 72, ..., byte 5 at bit 32; its CRC at bit 80 */
    k->fuse[0][1] = (u32)mac[5] | (u32)mac[4] << 8 | (u32)mac[3] << 16 | (u32)mac[2] << 24;
    k->fuse[0][2] = (u32)mac[1] | (u32)mac[0] << 8 | (u32)crc8(mac, 6) << 16;
    k->fuse[0][3] = 1u << 9    /* CHIP_PACKAGE 1: D0WD */
                  | 1u << 15;  /* CHIP_VER_REV1 */
    k->fuse[0][4] = 0x0Au << 8; /* ADC_VREF */
    k->fuse[0][5] = 1u << 20   /* CHIP_VER_REV2 */
                  | 1u << 24;  /* WAFER_VERSION_MINOR 1 */
}

/* the RDATA registers from the bits, with read protection */
static void reload(struct e32_efuse *k)
{
    u32 rd_dis = (k->fuse[0][0] >> 16) & 15u;
    int b, w;
    for (b = 0; b < 4; b++)
        for (w = 0; w < blocks[b].words; w++) {
            u32 v = k->fuse[b][w];
            if (b > 0 && (rd_dis & (1u << (b - 1))))
                v = 0;
            hw(k->p, blocks[b].rd + 4u * (u32)w, v);
        }
}

static void program(struct e32_efuse *k)
{
    u32 wr_dis = k->fuse[0][0] & 0xffffu;
    int b, w;
    for (b = 0; b < 4; b++) {
        if (b > 0 && (wr_dis & (1u << (6 + b))))
            continue;
        for (w = 0; w < blocks[b].words; w++)
            k->fuse[b][w] |= esk_rf_read(k->p, blocks[b].wr + 4u * (u32)w, 4);
    }
    k->programmed = 1;
}

static void irq(struct e32_efuse *k)
{
    u32 ena = esk_rf_read(k->p, INT_ENA, 4);
    hw(k->p, INT_RAW, k->raw);
    hw(k->p, INT_ST, k->raw & ena);
    esk_esp32_source(k->s, esk_rf_irq(k->p, "EFUSE"), (k->raw & ena) != 0);
}

static void start(struct e32_efuse *k, u32 cmd, u32 us)
{
    k->busy = cmd;
    k->left = (u64)esk_mach_core_hz(k->s) * us / 1000000u;
    if (!k->left)
        k->left = 1;
    hw(k->p, CMD, cmd);
    esk_rf_clock(k->p, &k->clocked, 1);
}

static void done(struct e32_efuse *k)
{
    if (k->busy == 1) {
        reload(k);
        k->raw |= 1u;
    } else {
        program(k);
        k->raw |= 2u;
    }
    k->busy = 0;
    hw(k->p, CMD, 0);
    esk_rf_clock(k->p, &k->clocked, 0);
    irq(k);
}

static void ef_tick(void *ctx, u32 cycles)
{
    struct e32_efuse *k = (struct e32_efuse *)ctx;
    if (!k->busy)
        return;
    if (k->left > cycles) {
        k->left -= cycles;
        return;
    }
    done(k);
}

static int ef_next(void *ctx, u32 *cycles)
{
    struct e32_efuse *k = (struct e32_efuse *)ctx;
    if (!k->busy)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)k->left;
    return 1;
}

static u32 ef_read(void *ctx, u32 off, int n)
{
    struct e32_efuse *k = (struct e32_efuse *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void ef_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_efuse *k = (struct e32_efuse *)ctx;
    esk_rf_write(k->p, off, n, v);
    if (off == CMD) {
        u32 op = esk_rf_read(k->p, CONF, 4) & 0xffffu;
        if (!k->busy && (v & 1u) && op == OP_READ)
            start(k, 1, 1);
        else if (!k->busy && (v & 2u) && op == OP_PGM)
            start(k, 2, 1000);
        else
            hw(k->p, CMD, k->busy);
    } else if (off == INT_CLR) {
        k->raw &= ~v;
        hw(k->p, INT_CLR, 0);
        irq(k);
    } else if (off == INT_ENA) {
        irq(k);
    }
}

static void ef_reset(void *ctx)
{
    struct e32_efuse *k = (struct e32_efuse *)ctx;
    if (!k->programmed)
        factory(k); /* the board's MAC is known by now */
    k->raw = 0;
    k->busy = 0;
    k->clocked = 0;
    reload(k);
}

const struct esk_dev_ops esk_esp32_efuse_ops = {
    "esp32-efuse", ef_read, ef_write, ef_reset, ef_tick, ef_next,
};

void *esk_esp32_efuse_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_efuse *k = (struct e32_efuse *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    k->s = s;
    k->p = p;
    factory(k);
    return k;
}

int esk_esp32_efuse_flash_crypt(struct esk_mach *m)
{
    struct e32_efuse *k = efuse_of(m);
    u32 cnt, ones = 0;
    if (!k)
        return 0;
    cnt = (k->fuse[0][0] >> 20) & 0x7fu;
    while (cnt) {
        ones += cnt & 1u;
        cnt >>= 1;
    }
    return (int)(ones & 1u);
}

u32 esk_esp32_efuse_word(struct esk_mach *m, int block, int word)
{
    struct e32_efuse *k = efuse_of(m);
    if (!k || block < 0 || block > 3 || word < 0 || word >= blocks[block].words)
        return 0;
    return k->fuse[block][word];
}
