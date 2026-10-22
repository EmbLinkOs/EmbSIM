/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* aes.c -- the ESP32-C3's AES accelerator (TRM v1.4 chapter 19), as
 * ESP-IDF's aes_ll and mbedtls port drive it:
 *
 *   - MODE: 0 AES-128 and 2 AES-256 encryption, 4 and 6 their
 *     decryption; the key in KEY_0-7, its bytes in memory order (each word
 *     four of them as the CPU stores them), as are TEXT_IN, TEXT_OUT and
 *     IV_MEM's;
 *   - Typical AES (DMA_ENABLE 0): a write to TRIGGER transforms the block
 *     in TEXT_IN into TEXT_OUT; STATE reads idle;
 *   - DMA-AES (DMA_ENABLE 1): TRIGGER transforms BLOCK_NUM blocks the GDMA
 *     out channel connected to AES (peripheral 6) gives, into its in
 *     channel, then ends the in channel's list (its EOF), leaves the IV for
 *     the next call in IV_MEM, reads DONE (2) in STATE and raises the
 *     interrupt (source 51, while INT_ENA; INT_CLEAR clears it); a write to
 *     DMA_EXIT makes it idle again. BLOCK_MODE: ECB, CBC, OFB, CTR (INC_SEL
 *     0 counts the last 32 bits, 1 all 128), CFB8 and CFB128; the stream
 *     modes use the key's encryption whatever MODE says, as they do.
 * Not yet: the time a block takes (DONE at once), AES-192 (the TRM's
 * reserved modes 1 and 5: treated as a 24-byte key), a GDMA list shorter
 * than the blocks asked for (the accelerator stops at its end). */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"
#include "util/wcrypto.h"

#define OFF_TRIGGER   0x48u
#define OFF_INT_CLEAR 0xACu
#define OFF_INT_ENA   0xB0u
#define OFF_DMA_EXIT  0xB8u
#define AES_PERI      6  /* its GDMA peripheral number */
#define AES_SOURCE    51 /* ETS_AES_INTR_SOURCE */

enum { ECB, CBC, OFB, CTR, CFB8, CFB128 };

struct c3_aes {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *key[8], *in[4], *out[4], *iv[4], *mode, *state, *dma_enable, *block_mode, *block_num,
        *inc_sel, *int_ena;
    int irq;
};

static void get_bytes(struct esk_rf_reg **r, int words, u8 *b)
{
    int i;
    for (i = 0; i < 4 * words; i++)
        b[i] = (u8)((u32)r[i / 4]->value >> (8 * (i % 4)));
}

static void put_bytes(struct esk_rf_reg **r, int words, const u8 *b)
{
    int i;
    for (i = 0; i < words; i++)
        esk_rf_hw(r[i], (u32)b[4 * i] | (u32)b[4 * i + 1] << 8 | (u32)b[4 * i + 2] << 16 | (u32)b[4 * i + 3] << 24);
}

/* the key's schedule; 1 when MODE decrypts */
static int key_of(struct c3_aes *k, esk_aes *a)
{
    u32 mode = (u32)k->mode->value & 7u;
    u8 key[32];
    size_t bytes = (mode & 3u) == 0 ? 16u : (mode & 3u) == 2 ? 32u : 24u;
    get_bytes(k->key, 8, key);
    esk_aes_init(a, key, bytes);
    return (mode & 4u) != 0;
}

static void irq(struct c3_aes *k)
{
    esk_esp32c3_source(k->s, AES_SOURCE, k->irq && (k->int_ena->value & 1u));
}

static void typical(struct c3_aes *k)
{
    esk_aes a;
    u8 in[16], out[16];
    int dec = key_of(k, &a);
    get_bytes(k->in, 4, in);
    if (dec)
        esk_aes_decrypt(&a, in, out);
    else
        esk_aes_encrypt(&a, in, out);
    put_bytes(k->out, 4, out);
}

static void counter_inc(u8 iv[16], int all)
{
    int i;
    for (i = 15; i >= (all ? 0 : 12); i--)
        if (++iv[i])
            break;
}

/* one block of `mode` from `in` into `out`, the IV moving on */
static void block(const esk_aes *a, int dec, int mode, int inc_all, u8 iv[16], const u8 in[16], u8 out[16])
{
    u8 ks[16];
    int i, j;
    switch (mode) {
    case CBC:
        if (dec) {
            esk_aes_decrypt(a, in, out);
            for (i = 0; i < 16; i++)
                out[i] ^= iv[i];
            memcpy(iv, in, 16);
        } else {
            for (i = 0; i < 16; i++)
                ks[i] = (u8)(in[i] ^ iv[i]);
            esk_aes_encrypt(a, ks, out);
            memcpy(iv, out, 16);
        }
        return;
    case OFB:
        esk_aes_encrypt(a, iv, iv);
        for (i = 0; i < 16; i++)
            out[i] = (u8)(in[i] ^ iv[i]);
        return;
    case CTR:
        esk_aes_encrypt(a, iv, ks);
        for (i = 0; i < 16; i++)
            out[i] = (u8)(in[i] ^ ks[i]);
        counter_inc(iv, inc_all);
        return;
    case CFB8:
        for (i = 0; i < 16; i++) {
            esk_aes_encrypt(a, iv, ks);
            out[i] = (u8)(in[i] ^ ks[0]);
            for (j = 0; j < 15; j++)
                iv[j] = iv[j + 1];
            iv[15] = dec ? in[i] : out[i];
        }
        return;
    case CFB128:
        esk_aes_encrypt(a, iv, ks);
        for (i = 0; i < 16; i++)
            out[i] = (u8)(in[i] ^ ks[i]);
        memcpy(iv, dec ? in : out, 16);
        return;
    default: /* ECB */
        if (dec)
            esk_aes_decrypt(a, in, out);
        else
            esk_aes_encrypt(a, in, out);
    }
}

static void dma(struct c3_aes *k)
{
    esk_aes a;
    u8 iv[16];
    int dec = key_of(k, &a), mode = (int)(k->block_mode->value & 7u), all = (int)(k->inc_sel->value & 1u);
    u32 n = (u32)k->block_num->value, b;
    get_bytes(k->iv, 4, iv);
    for (b = 0; b < n; b++) {
        u8 in[16], out[16];
        int i, eof = 0;
        for (i = 0; i < 16; i++)
            if (!esk_esp32c3_dma_out(k->s, AES_PERI, &in[i], &eof))
                break;
        if (i < 16)
            break; /* the list ended early */
        block(&a, dec, mode, all, iv, in, out);
        for (i = 0; i < 16; i++)
            (void)esk_esp32c3_dma_in(k->s, AES_PERI, out[i]);
    }
    esk_esp32c3_dma_in_eof(k->s, AES_PERI);
    if (mode != ECB)
        put_bytes(k->iv, 4, iv);
    esk_rf_hw(k->state, 2); /* DONE */
    k->irq = 1;
    irq(k);
}

static u32 aes_read(void *ctx, u32 off, int n)
{
    struct c3_aes *k = (struct c3_aes *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void aes_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_aes *k = (struct c3_aes *)ctx;
    esk_rf_write(k->p, off, n, v);
    if (off == OFF_TRIGGER && (v & 1u)) {
        if (k->dma_enable->value & 1u)
            dma(k);
        else
            typical(k);
    } else if (off == OFF_DMA_EXIT)
        esk_rf_hw(k->state, 0);
    else if (off == OFF_INT_CLEAR && (v & 1u)) {
        k->irq = 0;
        irq(k);
    } else if (off == OFF_INT_ENA)
        irq(k);
}

static void aes_reset(void *ctx)
{
    struct c3_aes *k = (struct c3_aes *)ctx;
    k->irq = 0;
}

const struct esk_dev_ops esk_esp32c3_aes_ops = {
    "esp32c3-aes", aes_read, aes_write, aes_reset, 0, 0,
};

static int regs(struct esk_rf_periph *p, struct esk_rf_reg **r, const char *fmt, int n)
{
    char name[16];
    int i;
    for (i = 0; i < n; i++) {
        snprintf(name, sizeof name, fmt, i);
        if (!(r[i] = esk_rf_reg(p, name)))
            return -1;
    }
    return 0;
}

void *esk_esp32c3_aes_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_aes *k = (struct c3_aes *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    if (regs(p, k->key, "KEY_%d", 8) || regs(p, k->in, "TEXT_IN_%d", 4) || regs(p, k->out, "TEXT_OUT_%d", 4) ||
        regs(p, k->iv, "IV_MEM%d", 4))
        return 0;
    k->mode = esk_rf_reg(p, "MODE");
    k->state = esk_rf_reg(p, "STATE");
    k->dma_enable = esk_rf_reg(p, "DMA_ENABLE");
    k->block_mode = esk_rf_reg(p, "BLOCK_MODE");
    k->block_num = esk_rf_reg(p, "BLOCK_NUM");
    k->inc_sel = esk_rf_reg(p, "INC_SEL");
    k->int_ena = esk_rf_reg(p, "INT_ENA");
    if (!k->mode || !k->state || !k->dma_enable || !k->block_mode || !k->block_num || !k->inc_sel || !k->int_ena)
        return 0;
    return k;
}
