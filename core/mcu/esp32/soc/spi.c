/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* spi.c -- the ESP32's SPI0 and SPI1 (TRM v5 chapter 7), the controllers
 * in front of the module's flash (nor.c): SPI0 the caches', SPI1 the
 * program's.
 *
 * A write of CMD starts the transaction its bit names, on the flash:
 *   - FLASH_READ: ADDR[31:24] bytes from ADDR[23:0] into W0-W15, with the
 *     read instruction CTRL selects (FREAD_QIO EBh, FREAD_DIO BBh,
 *     FREAD_QUAD 6Bh, FREAD_DUAL 3Bh, FASTRD_MODE 0Bh, else 03h);
 *   - FLASH_PP: ADDR[31:24] bytes from W0 (02h, or 32h with FWRITE_QUAD);
 *     FLASH_SE (20h), FLASH_BE (D8h), FLASH_CE (C7h) at ADDR;
 *   - FLASH_WREN (06h), FLASH_WRDI (04h), FLASH_RDSR (05h into
 *     RD_STATUS), FLASH_WRSR (01h from RD_STATUS: one byte, two with
 *     WRSR_2B), FLASH_RDID (9Fh into W0), FLASH_DP (B9h), FLASH_RES (ABh),
 *     FLASH_HPM (A3h), FLASH_PES (75h), FLASH_PER (7Ah);
 *   - USR: the user transaction USER enables: the command (USER2's
 *     USR_COMMAND_VALUE, USR_COMMAND_BITLEN + 1 bits), the address
 *     (ADDR's USR_ADDR_BITLEN + 1 most significant bits), the dummy
 *     cycles, MOSI_DLEN + 1 bits out of W0 (W8 with USR_MOSI_HIGHPART) and
 *     MISO_DLEN + 1 bits into W0 (W8 with USR_MISO_HIGHPART).
 * The command's bit reads 1 while the transaction is on the bus -- the
 * bits it carries at the SPI clock (CLOCK: APB_CLK with CLK_EQU_SYSCLK,
 * else APB_CLK / (CLKDIV_PRE + 1) / (CLKCNT_N + 1)), its address and data
 * on two or four lines as the mode says -- and EXT2's ST is not idle;
 * then SLAVE's TRANS_DONE is set, and with its INT_EN bit the matrix
 * source (SPI0 30, SPI1 31 in the register description's table).
 * Bytes in the W buffers are little-endian (RD_BYTE_ORDER and
 * WR_BYTE_ORDER 0).
 * Not modelled: the byte and bit orders' other settings, the controllers'
 * DMA and slave modes (SPI0 and SPI1 are masters on the flash), the CRC,
 * the flash encryption on SPI1's writes, PSRAM (SPI0's cache path to it),
 * the cache's own reads (dport.c reads the flash directly). */
#include <string.h>

#include "mcu/esp32/soc/esp32.h"

#define CMD 0x00u
#define ADDR 0x04u
#define CTRL 0x08u
#define RD_STATUS 0x10u
#define CLOCK 0x18u
#define USER 0x1Cu
#define USER1 0x20u
#define USER2 0x24u
#define MOSI_DLEN 0x28u
#define MISO_DLEN 0x2Cu
#define SLAVE 0x38u
#define W0 0x80u
#define EXT2 0xF8u

#define CMD_USR (1u << 18)
#define CMD_HPM (1u << 19)
#define CMD_RES (1u << 20)
#define CMD_DP (1u << 21)
#define CMD_CE (1u << 22)
#define CMD_BE (1u << 23)
#define CMD_SE (1u << 24)
#define CMD_PP (1u << 25)
#define CMD_WRSR (1u << 26)
#define CMD_RDSR (1u << 27)
#define CMD_RDID (1u << 28)
#define CMD_WRDI (1u << 29)
#define CMD_WREN (1u << 30)
#define CMD_READ (1u << 31)
#define CMD_PES (1u << 17)
#define CMD_PER (1u << 16)
#define CMD_ANY 0xFFFF0000u

struct e32_spi {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_esp32_nor *nor;
    int unit, source, level;
    u32 busy;  /* the command bit in progress */
    u64 left;
    int clocked;
};

static u32 reg(struct e32_spi *k, u32 off)
{
    return esk_rf_read(k->p, off, 4);
}

static void hw(struct e32_spi *k, u32 off, u32 v)
{
    struct esk_rf_reg *r = esk_rf_reg_at(k->p, off);
    if (r)
        esk_rf_hw(r, v);
}

static void buf_get(struct e32_spi *k, u32 at, u8 *d, int n)
{
    int i;
    for (i = 0; i < n && i < 64; i++)
        d[i] = (u8)(reg(k, W0 + at + (u32)(i & ~3)) >> (8 * (i & 3)));
}

static void buf_put(struct e32_spi *k, u32 at, const u8 *d, int n)
{
    int i;
    for (i = 0; i < n && i < 64; i += 4) {
        u32 w = reg(k, W0 + at + (u32)i);
        int b;
        for (b = 0; b < 4 && i + b < n; b++)
            w = (w & ~(0xffu << (8 * b))) | (u32)d[i + b] << (8 * b);
        hw(k, W0 + at + (u32)i, w);
    }
}

/* the SPI clock in Hz */
static double spi_hz(struct e32_spi *k)
{
    u32 c = reg(k, CLOCK);
    double apb = (double)esk_esp32_apb_hz(k->s);
    if (c & (1u << 31))
        return apb;
    return apb / (double)(((c >> 18) & 0x1fffu) + 1u) / (double)(((c >> 12) & 0x3fu) + 1u);
}

/* the flash read instruction CTRL selects, and its data lines */
static u8 read_op(struct e32_spi *k, int *lines)
{
    u32 c = reg(k, CTRL);
    *lines = 1;
    if (c & (1u << 24)) { *lines = 4; return 0xEB; }
    if (c & (1u << 23)) { *lines = 2; return 0xBB; }
    if (c & (1u << 20)) { *lines = 4; return 0x6B; }
    if (c & (1u << 14)) { *lines = 2; return 0x3B; }
    if (c & (1u << 13))
        return 0x0B;
    return 0x03;
}

static void addr3(u8 *tx, u32 a)
{
    tx[1] = (u8)(a >> 16);
    tx[2] = (u8)(a >> 8);
    tx[3] = (u8)a;
}

static void update(struct e32_spi *k)
{
    u32 sl = reg(k, SLAVE);
    int lv = ((sl & 0x1fu) & ((sl >> 5) & 0x1fu)) != 0;
    if (lv != k->level && k->source >= 0) {
        k->level = lv;
        esk_esp32_source(k->s, k->source, lv);
    }
}

/* the transaction: the flash's part now, its bus time in SPI clocks */
static u64 run(struct e32_spi *k, u32 cmd)
{
    u8 tx[80], rx[64];
    u32 a = reg(k, ADDR), len = a >> 24, user = reg(k, USER);
    int lines = 1, n;
    u64 clocks = 8;
    memset(tx, 0, sizeof tx);
    memset(rx, 0xff, sizeof rx);
    if (len > 64)
        len = 64;
    if (cmd == CMD_READ) {
        tx[0] = read_op(k, &lines);
        addr3(tx, a);
        esk_esp32_nor_xfer(k->nor, tx, 4, rx, (int)len);
        buf_put(k, 0, rx, (int)len);
        return clocks + (24u + 8u * len) / (u32)lines + (tx[0] == 0x03 ? 0u : 8u);
    }
    if (cmd == CMD_PP) {
        tx[0] = (user & (1u << 13)) ? 0x32 : 0x02;
        addr3(tx, a);
        buf_get(k, 0, tx + 4, (int)len);
        esk_esp32_nor_xfer(k->nor, tx, 4 + (int)len, rx, 0);
        return clocks + 24u + 8u * len;
    }
    if (cmd == CMD_SE || cmd == CMD_BE) {
        tx[0] = cmd == CMD_SE ? 0x20 : 0xD8;
        addr3(tx, a);
        esk_esp32_nor_xfer(k->nor, tx, 4, rx, 0);
        return clocks + 24u;
    }
    if (cmd == CMD_RDSR) {
        tx[0] = 0x05;
        esk_esp32_nor_xfer(k->nor, tx, 1, rx, 1);
        hw(k, RD_STATUS, (reg(k, RD_STATUS) & ~0xffu) | rx[0]);
        return clocks + 8u;
    }
    if (cmd == CMD_WRSR) {
        u32 st = reg(k, RD_STATUS);
        int two = (reg(k, CTRL) & (1u << 22)) != 0;
        tx[0] = 0x01;
        tx[1] = (u8)st;
        tx[2] = (u8)(st >> 8);
        esk_esp32_nor_xfer(k->nor, tx, two ? 3 : 2, rx, 0);
        return clocks + (two ? 16u : 8u);
    }
    if (cmd == CMD_RDID) {
        tx[0] = 0x9F;
        esk_esp32_nor_xfer(k->nor, tx, 1, rx, 3);
        hw(k, W0, (u32)rx[0] | (u32)rx[1] << 8 | (u32)rx[2] << 16);
        return clocks + 24u;
    }
    if (cmd != CMD_USR) {
        tx[0] = cmd == CMD_WREN ? 0x06 : cmd == CMD_WRDI ? 0x04 : cmd == CMD_CE ? 0xC7 : cmd == CMD_DP ? 0xB9
              : cmd == CMD_RES ? 0xAB : cmd == CMD_HPM ? 0xA3 : cmd == CMD_PES ? 0x75 : 0x7A;
        esk_esp32_nor_xfer(k->nor, tx, 1, rx, 0);
        return clocks;
    }
    /* a user transaction */
    n = 0;
    clocks = 0;
    if (user & (1u << 31)) {
        u32 bits = ((reg(k, USER2) >> 28) & 15u) + 1u, v = reg(k, USER2) & 0xffffu;
        int nb = (int)((bits + 7u) / 8u), i;
        for (i = 0; i < nb; i++)
            tx[n++] = (u8)(v >> (8 * i));
        clocks += bits;
    }
    if (user & (1u << 30)) {
        u32 bits = ((reg(k, USER1) >> 26) & 0x3fu) + 1u;
        int nb = (int)((bits + 7u) / 8u), i;
        for (i = 0; i < nb && i < 4; i++)
            tx[n++] = (u8)(a >> (24 - 8 * i));
        clocks += bits;
    }
    if (user & (1u << 29))
        clocks += (reg(k, USER1) & 0xffu) + 1u;
    if (user & (1u << 27)) {
        u32 bits = (reg(k, MOSI_DLEN) & 0xffffffu) + 1u;
        int nb = (int)((bits + 7u) / 8u);
        if (nb > 64)
            nb = 64;
        buf_get(k, (user & (1u << 25)) ? 32u : 0u, tx + n, nb);
        n += nb;
        clocks += bits;
    }
    {
        int nrx = 0;
        if (user & (1u << 28)) {
            u32 bits = (reg(k, MISO_DLEN) & 0xffffffu) + 1u;
            nrx = (int)((bits + 7u) / 8u);
            if (nrx > 64)
                nrx = 64;
            clocks += bits;
        }
        if (n)
            esk_esp32_nor_xfer(k->nor, tx, n, rx, nrx);
        if (nrx)
            buf_put(k, (user & (1u << 24)) ? 32u : 0u, rx, nrx);
    }
    return clocks;
}

static void done(struct e32_spi *k)
{
    k->busy = 0;
    hw(k, CMD, reg(k, CMD) & ~CMD_ANY);
    hw(k, EXT2, 0);
    hw(k, SLAVE, reg(k, SLAVE) | (1u << 4)); /* TRANS_DONE */
    esk_rf_clock(k->p, &k->clocked, 0);
    update(k);
}

static u32 sp_read(void *ctx, u32 off, int n)
{
    struct e32_spi *k = (struct e32_spi *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void sp_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_spi *k = (struct e32_spi *)ctx;
    esk_rf_write(k->p, off, n, v);
    if (off == CMD && !k->busy && (v & CMD_ANY)) {
        u32 cmd = 0x80000000u, clocks;
        while (cmd && !(v & cmd))
            cmd >>= 1;
        clocks = (u32)run(k, cmd);
        k->busy = cmd;
        hw(k, CMD, cmd);
        hw(k, EXT2, 1);
        k->left = (u64)((double)clocks * (double)esk_mach_core_hz(k->s) / spi_hz(k)) + 1u;
        esk_rf_clock(k->p, &k->clocked, 1);
    } else if (off == CMD && k->busy) {
        hw(k, CMD, (reg(k, CMD) & ~CMD_ANY) | k->busy);
    } else if (off == SLAVE) {
        update(k);
    }
}

static void sp_tick(void *ctx, u32 cycles)
{
    struct e32_spi *k = (struct e32_spi *)ctx;
    if (!k->busy)
        return;
    if (k->left > cycles) {
        k->left -= cycles;
        return;
    }
    done(k);
}

static int sp_next(void *ctx, u32 *cycles)
{
    struct e32_spi *k = (struct e32_spi *)ctx;
    if (!k->busy)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)k->left;
    return 1;
}

static void sp_reset(void *ctx)
{
    struct e32_spi *k = (struct e32_spi *)ctx;
    k->busy = 0;
    k->clocked = 0;
    k->level = 0;
    if (k->unit == 1 && k->nor && k->s->reset_cause == ESK_RESET_POWER)
        esk_esp32_nor_power_on(k->nor);
}

const struct esk_dev_ops esk_esp32_spi_ops = {
    "esp32-spi", sp_read, sp_write, sp_reset, sp_tick, sp_next,
};

void *esk_esp32_spi_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_spi *k;
    if (strcmp(p->name, "SPI0") && strcmp(p->name, "SPI1"))
        return 0;
    k = (struct e32_spi *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    k->s = s;
    k->p = p;
    k->unit = p->name[3] - '0';
    k->source = esk_rf_irq(p, p->name);
    k->nor = esk_esp32_nor_of(s);
    if (!k->nor)
        return 0;
    return k;
}

/* the one flash chip both controllers reach, made by whichever comes first */
struct esk_esp32_nor *esk_esp32_nor_of(struct esk_mach *m)
{
    struct esk_rf_periph *p;
    int i;
    for (i = 0; m->rf && (p = esk_rf_periph_at(m->rf, i)) != 0; i++)
        if (p->ops == &esk_esp32_spi_ops && p->ctx && ((struct e32_spi *)p->ctx)->nor)
            return ((struct e32_spi *)p->ctx)->nor;
    return esk_esp32_nor_create(m);
}
