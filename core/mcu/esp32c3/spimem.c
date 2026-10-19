/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* spimem.c -- the ESP32-C3's SPI0 and SPI1 (SPI_MEM), the controllers in
 * front of the flash (nor.c): SPI1 for the program's own flash commands,
 * SPI0 for the cache's reads. The TRM (v1.4) leaves them out; the register
 * fields are Espressif's SVD and ESP-IDF's soc/esp32c3/register/soc/
 * spi_mem_struct.h, and the commands as the C3's ROM (esp32c3_rev3_rom.elf:
 * SPI_read_data, SPI_page_program, esp_rom_spiflash_read_user_cmd) and
 * ESP-IDF's hal/spimem_flash_ll.h drive them.
 *
 * A write that sets a command bit of CMD starts a transaction; the bit
 * reads 1 until it is done and then 0, as the program polls it. The
 * transactions:
 *   - USR (with or without FLASH_PE): the phases USER enables -- the
 *     command (USER2's value, its bit length), the address (ADDR's low
 *     USR_ADDR_BITLEN + 1 bits), the dummy, the data out (USR_MOSI_DBITLEN
 *     + 1 bits from W0, or from W8 with USR_MOSI_HIGHPART) and in
 *     (USR_MISO_DBITLEN + 1 bits into W0, or W8 with USR_MISO_HIGHPART),
 *     each MSB first, the data's bytes in W0's byte order;
 *   - the flash commands: WREN (06h), WRDI (04h), RDID (9Fh: three bytes
 *     into W0), RDSR (05h: status register 1 into RD_STATUS[7:0]), WRSR
 *     (01h: RD_STATUS[7:0], and [15:8] with CTRL.WRSR_2B), PP (02h: ADDR
 *     [23:0] the address, ADDR[31:24] the bytes from W0), SE (20h), BE
 *     (D8h), CE (C7h), DP (B9h), RES (ABh).
 * The flash answers each as nor.c says; every transaction ends at once
 * (the SPI clock's time is not counted), the flash's own busy time being
 * what the program waits for.
 * Not yet: FLASH_READ and FLASH_HPM, which neither the ROM nor ESP-IDF
 * issues (they finish without a transfer); the auto-suspend and auto-
 * resume, the wait-idle after FLASH_PE and their interrupts (PER_END,
 * PES_END, ...); two chip selects (CS1 has no device); the SPI0's own
 * read configuration for the cache, which the cache (cache.c) does not
 * consult; the pins (the flash is in the package). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define CMD_USR  (1u << 18)
#define CMD_PE   (1u << 17)
#define CMD_HPM  (1u << 19)
#define CMD_RES  (1u << 20)
#define CMD_DP   (1u << 21)
#define CMD_CE   (1u << 22)
#define CMD_BE   (1u << 23)
#define CMD_SE   (1u << 24)
#define CMD_PP   (1u << 25)
#define CMD_WRSR (1u << 26)
#define CMD_RDSR (1u << 27)
#define CMD_RDID (1u << 28)
#define CMD_WRDI (1u << 29)
#define CMD_WREN (1u << 30)
#define CMD_READ (1u << 31)

#define USER_MISO_HIGHPART (1u << 24)
#define USER_MOSI_HIGHPART (1u << 25)
#define USER_MOSI          (1u << 27)
#define USER_MISO          (1u << 28)
#define USER_ADDR          (1u << 30)
#define USER_COMMAND       (1u << 31)
#define CTRL_WRSR_2B       (1u << 22)

#define OFF_CMD 0x00u
#define OFF_W0  0x58u

struct c3_spimem {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cmd, *addr, *ctrl, *user, *user1, *user2, *mosi_dlen, *miso_dlen, *rd_status,
        *w[16];
    struct esk_c3_nor *nor;
};

/* the bits a transaction sends, packed MSB first */
struct bits {
    u8 b[80];
    int n;
};

static void put_bits(struct bits *t, u32 v, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--) {
        if (t->n >= (int)sizeof t->b * 8)
            return;
        if (v >> i & 1u)
            t->b[t->n / 8] = (u8)(t->b[t->n / 8] | (0x80u >> (t->n % 8)));
        t->n++;
    }
}

static u32 reg(const struct esk_rf_reg *r)
{
    return (u32)r->value;
}

static u8 buf_byte(struct c3_spimem *k, int i)
{
    return (u8)(reg(k->w[(i / 4) & 15]) >> (8 * (i % 4)));
}

static void buf_set(struct c3_spimem *k, int i, u8 v)
{
    struct esk_rf_reg *w = k->w[(i / 4) & 15];
    u32 sh = 8u * (u32)(i % 4);
    esk_rf_hw(w, (reg(w) & ~(0xffu << sh)) | (u32)v << sh);
}

/* the flash's answer to `t`, `nin` bytes into the buffer from byte `at` */
static void xfer(struct c3_spimem *k, const struct bits *t, int nin, int at)
{
    u8 in[64];
    int i;
    if (nin > 64)
        nin = 64;
    if (!k->nor) {
        memset(in, 0xff, sizeof in);
    } else {
        esk_esp32c3_nor_xfer(k->nor, t->b, t->n / 8, in, nin);
    }
    for (i = 0; i < nin; i++)
        buf_set(k, (at + i) & 63, in[i]);
}

static void user_cmd(struct c3_spimem *k)
{
    struct bits t;
    u32 user = reg(k->user);
    int nin = 0, at = 0, i;
    memset(&t, 0, sizeof t);
    if (user & USER_COMMAND)
        put_bits(&t, reg(k->user2) & 0xffffu, (int)(reg(k->user2) >> 28 & 15u) + 1);
    if (user & USER_ADDR) {
        int n = (int)(reg(k->user1) >> 26 & 63u) + 1;
        put_bits(&t, reg(k->addr) & (n >= 32 ? 0xffffffffu : ((1u << n) - 1u)), n);
    }
    if (user & USER_MOSI) {
        int nbits = (int)(reg(k->mosi_dlen) & 0x3ffu) + 1,
            from = user & USER_MOSI_HIGHPART ? 32 : 0;
        for (i = 0; i < nbits / 8; i++)
            put_bits(&t, buf_byte(k, (from + i) & 63), 8);
    }
    if (user & USER_MISO) {
        nin = (int)((reg(k->miso_dlen) & 0x3ffu) + 8u) / 8;
        at = user & USER_MISO_HIGHPART ? 32 : 0;
    }
    xfer(k, &t, nin, at);
}

static void simple(struct c3_spimem *k, u8 op, const u8 *data, int n, int nin)
{
    struct bits t;
    int i;
    memset(&t, 0, sizeof t);
    put_bits(&t, op, 8);
    for (i = 0; i < n; i++)
        put_bits(&t, data[i], 8);
    xfer(k, &t, nin, 0);
}

static void addr_cmd(struct c3_spimem *k, u8 op, int with_data)
{
    u8 d[3 + 64];
    u32 a = reg(k->addr);
    int n = 3, i;
    d[0] = (u8)(a >> 16);
    d[1] = (u8)(a >> 8);
    d[2] = (u8)a;
    if (with_data) {
        int len = (int)(a >> 24);
        if (len > 64)
            len = 64;
        for (i = 0; i < len; i++)
            d[n++] = buf_byte(k, i);
    }
    simple(k, op, d, n, 0);
}

static void run(struct c3_spimem *k, u32 cmd)
{
    if (cmd & CMD_USR) {
        user_cmd(k);
    } else if (cmd & CMD_WREN) {
        simple(k, 0x06, 0, 0, 0);
    } else if (cmd & CMD_WRDI) {
        simple(k, 0x04, 0, 0, 0);
    } else if (cmd & CMD_RDID) {
        simple(k, 0x9f, 0, 0, 3);
    } else if (cmd & CMD_RDSR) {
        struct bits t;
        u8 in[1];
        memset(&t, 0, sizeof t);
        put_bits(&t, 0x05, 8);
        if (k->nor)
            esk_esp32c3_nor_xfer(k->nor, t.b, 1, in, 1);
        else
            in[0] = 0xff;
        esk_rf_hw(k->rd_status, (reg(k->rd_status) & ~0xffffu) | in[0]);
    } else if (cmd & CMD_WRSR) {
        u8 d[2];
        u32 st = reg(k->rd_status);
        d[0] = (u8)st;
        d[1] = (u8)(st >> 8);
        simple(k, 0x01, d, reg(k->ctrl) & CTRL_WRSR_2B ? 2 : 1, 0);
    } else if (cmd & CMD_PP) {
        addr_cmd(k, 0x02, 1);
    } else if (cmd & CMD_SE) {
        addr_cmd(k, 0x20, 0);
    } else if (cmd & CMD_BE) {
        addr_cmd(k, 0xd8, 0);
    } else if (cmd & CMD_CE) {
        simple(k, 0xc7, 0, 0, 0);
    } else if (cmd & CMD_DP) {
        simple(k, 0xb9, 0, 0, 0);
    } else if (cmd & CMD_RES) {
        simple(k, 0xab, 0, 0, 0);
    }
}

static u32 sm_read(void *ctx, u32 off, int n)
{
    struct c3_spimem *k = (struct c3_spimem *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void sm_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_spimem *k = (struct c3_spimem *)ctx;
    esk_rf_write(k->p, off, n, v);
    if (off == OFF_CMD) {
        u32 cmd = reg(k->cmd) & 0xfffe0000u;
        if (cmd)
            run(k, cmd);
        /* done: the command bits clear, the state machines are idle */
        esk_rf_hw(k->cmd, 0);
    }
}

/* the flash powers on with the part, not at its other resets */
static void sm_reset(void *ctx)
{
    struct c3_spimem *k = (struct c3_spimem *)ctx;
    if (k->nor && k->s->reset_cause == ESK_RESET_POWER)
        esk_esp32c3_nor_power_on(k->nor);
}

const struct esk_dev_ops esk_esp32c3_spimem_ops = {
    "esp32c3-spimem", sm_read, sm_write, sm_reset, 0, 0,
};

static struct esk_c3_nor *shared_nor(struct esk_mach *s)
{
    int i;
    for (i = 0; s->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(s->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_esp32c3_spimem_ops && p->ctx)
            return ((struct c3_spimem *)p->ctx)->nor;
    }
    return esk_esp32c3_nor_create(s);
}

void *esk_esp32c3_spimem_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    static const char *const names[16] = { "W0", "W1", "W2",  "W3",  "W4",  "W5",  "W6",  "W7",
                                          "W8", "W9", "W10", "W11", "W12", "W13", "W14", "W15" };
    struct c3_spimem *k = (struct c3_spimem *)esk_mach_alloc(s, sizeof *k);
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->cmd = esk_rf_reg(p, "CMD");
    k->addr = esk_rf_reg(p, "ADDR");
    k->ctrl = esk_rf_reg(p, "CTRL");
    k->user = esk_rf_reg(p, "USER");
    k->user1 = esk_rf_reg(p, "USER1");
    k->user2 = esk_rf_reg(p, "USER2");
    k->mosi_dlen = esk_rf_reg(p, "MOSI_DLEN");
    k->miso_dlen = esk_rf_reg(p, "MISO_DLEN");
    k->rd_status = esk_rf_reg(p, "RD_STATUS");
    for (i = 0; i < 16; i++) {
        k->w[i] = esk_rf_reg(p, names[i]);
        if (!k->w[i])
            return 0;
    }
    if (!k->cmd || !k->addr || !k->ctrl || !k->user || !k->user1 || !k->user2 || !k->mosi_dlen ||
        !k->miso_dlen || !k->rd_status)
        return 0;
    k->nor = shared_nor(s);
    return k->nor ? k : 0;
}
