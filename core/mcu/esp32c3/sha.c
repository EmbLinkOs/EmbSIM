/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* sha.c -- the ESP32-C3's SHA accelerator (TRM v1.4 chapter 21) in its
 * Typical SHA mode: MODE selects SHA-1 (0), SHA-224 (1) or SHA-256 (2); a
 * write to START hashes the block in M_MEM0-15 from the algorithm's
 * initial hash value, one to CONTINUE from the hash in H_MEM0-7 (which the
 * program may have written back, the interleaved calculation of 21.4.2.1);
 * the result is in H_MEM0-7, BUSY reading 0 again. The registers hold the
 * message and digest bytes in memory order, each word four of them as the
 * CPU stores them (ESP-IDF's sha_ll_fill_text_block and the memcpy of the
 * H words to the digest), so the algorithm's big-endian words are their
 * byte swaps. A MODE above 2 hashes nothing.
 * The DMA-SHA mode (21.4.3): a write to DMA_START (from the initial hash
 * value) or DMA_CONTINUE (from H_MEM) hashes DMA_BLOCK_NUM blocks the GDMA
 * out channel connected to SHA (peripheral 7, as ESP-IDF's mbedtls port
 * sets it) gives, then raises the interrupt (source 49, while IRQ_ENA;
 * CLEAR_IRQ clears it). ESP-IDF's SHA-1, SHA-224 and SHA-256 use it for
 * whole blocks (wpa_supplicant's PBKDF2 among them).
 * Not yet: the time a block takes (BUSY is never seen set), a GDMA list
 * shorter than the blocks asked for (the accelerator stops at its end),
 * and SHA-512/t's T_STRING and T_LENGTH, which this part's accelerator
 * does not use. */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"
#include "util/sha256.h"

#define OFF_BLOCK_NUM    0x0Cu
#define OFF_START        0x10u
#define OFF_CONTINUE     0x14u
#define OFF_DMA_START    0x1Cu
#define OFF_DMA_CONTINUE 0x20u
#define OFF_CLEAR_IRQ    0x24u
#define OFF_IRQ_ENA      0x28u
#define SHA_PERI         7  /* its GDMA peripheral number */
#define SHA_SOURCE       49 /* ETS_SHA_INTR_SOURCE */

static const u32 iv1[5] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u };
static const u32 iv224[8] = { 0xc1059ed8u, 0x367cd507u, 0x3070dd17u, 0xf70e5939u,
                             0xffc00b31u, 0x68581511u, 0x64f98fa7u, 0xbefa4fa4u };
static const u32 iv256[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                             0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };

struct c3_sha {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *mode, *h[8], *m[16], *block_num, *irq_ena;
    int irq; /* a DMA-SHA operation ended, not yet cleared */
};

static u32 swap(u32 v)
{
    return v >> 24 | (v >> 8 & 0xff00u) | (v << 8 & 0xff0000u) | v << 24;
}
static u32 rol(u32 x, int n)
{
    return x << n | x >> (32 - n);
}

/* FIPS 180-4 6.1.2 */
static void sha1_block(u32 hv[5], const u8 *p)
{
    u32 w[80], a = hv[0], b = hv[1], c = hv[2], d = hv[3], e = hv[4], t;
    int i;
    for (i = 0; i < 16; i++)
        w[i] =
            (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (i = 16; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (i = 0; i < 80; i++) {
        u32 f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5a827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }
        t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    hv[0] += a;
    hv[1] += b;
    hv[2] += c;
    hv[3] += d;
    hv[4] += e;
}

/* one block, from the initial hash value (first) or H_MEM, into H_MEM */
static void hash_block(struct c3_sha *k, const u8 *blk, int first)
{
    u32 mode = (u32)k->mode->value & 7u, hv[8];
    int i, nh = mode == 0 ? 5 : 8;
    if (mode > 2)
        return;
    for (i = 0; i < nh; i++)
        hv[i] = first ? (mode == 0   ? iv1[i]
                         : mode == 1 ? iv224[i]
                                     : iv256[i])
                      : swap((u32)k->h[i]->value);
    if (mode == 0)
        sha1_block(hv, blk);
    else
        esk_sha256_block(hv, blk);
    for (i = 0; i < nh; i++)
        esk_rf_hw(k->h[i], swap(hv[i]));
}

/* Typical SHA: the block in M_MEM */
static void hash(struct c3_sha *k, int first)
{
    u8 blk[64];
    int i;
    for (i = 0; i < 16; i++) {
        u32 w = swap((u32)k->m[i]->value);
        blk[4 * i] = (u8)(w >> 24);
        blk[4 * i + 1] = (u8)(w >> 16);
        blk[4 * i + 2] = (u8)(w >> 8);
        blk[4 * i + 3] = (u8)w;
    }
    hash_block(k, blk, first);
}

static void irq(struct c3_sha *k)
{
    esk_esp32c3_source(k->s, SHA_SOURCE, k->irq && (k->irq_ena->value & 1u));
}

/* DMA-SHA: DMA_BLOCK_NUM blocks from the GDMA */
static void hash_dma(struct c3_sha *k, int first)
{
    u32 n = (u32)k->block_num->value & 0x3Fu, b;
    for (b = 0; b < n; b++) {
        u8 blk[64];
        int i, eof = 0;
        for (i = 0; i < 64; i++)
            if (!esk_esp32c3_dma_out(k->s, SHA_PERI, &blk[i], &eof))
                break;
        if (i < 64)
            break; /* the list ended early */
        hash_block(k, blk, first && b == 0);
    }
    k->irq = 1;
    irq(k);
}

static u32 sh_read(void *ctx, u32 off, int n)
{
    struct c3_sha *k = (struct c3_sha *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void sh_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_sha *k = (struct c3_sha *)ctx;
    esk_rf_write(k->p, off, n, v);
    if ((off == OFF_START || off == OFF_CONTINUE) && v)
        hash(k, off == OFF_START);
    else if ((off == OFF_DMA_START || off == OFF_DMA_CONTINUE) && v)
        hash_dma(k, off == OFF_DMA_START);
    else if (off == OFF_CLEAR_IRQ && (v & 1u)) {
        k->irq = 0;
        irq(k);
    } else if (off == OFF_IRQ_ENA)
        irq(k);
}

static void sh_reset(void *ctx)
{
    struct c3_sha *k = (struct c3_sha *)ctx;
    k->irq = 0;
}

const struct esk_dev_ops esk_esp32c3_sha_ops = {
    "esp32c3-sha", sh_read, sh_write, sh_reset, 0, 0,
};

void *esk_esp32c3_sha_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_sha *k = (struct c3_sha *)esk_mach_alloc(s, sizeof *k);
    char name[16];
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->mode = esk_rf_reg(p, "MODE");
    k->block_num = esk_rf_reg(p, "DMA_BLOCK_NUM");
    k->irq_ena = esk_rf_reg(p, "IRQ_ENA");
    for (i = 0; i < 8; i++) {
        snprintf(name, sizeof name, "H_MEM%d", i);
        if (!(k->h[i] = esk_rf_reg(p, name)))
            return 0;
    }
    for (i = 0; i < 16; i++) {
        snprintf(name, sizeof name, "M_MEM%d", i);
        if (!(k->m[i] = esk_rf_reg(p, name)))
            return 0;
    }
    return k->mode && k->block_num && k->irq_ena ? k : 0;
}
