/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* hmac.c -- the ESP32-C3's HMAC accelerator (0x6003E000, TRM v1.4 chapter
 * 18): HMAC-SHA256 under a key in an eFuse key block, which software never
 * reads, as ESP-IDF's esp_hmac_calculate, its NVS encryption scheme and the
 * digital signature use it.
 *
 *   - SET_START begins; SET_PARA_PURPOSE (8 HMAC_UP: the result to
 *     software; 7 HMAC_DOWN_DIGITAL_SIGNATURE, 6 HMAC_DOWN_JTAG, 5
 *     HMAC_DOWN_ALL: to hardware) and SET_PARA_KEY (key block 0-5) are
 *     checked at SET_PARA_FINISH against the key block's KEY_PURPOSE:
 *     QUERY_ERROR is 1 when they differ (HMAC_DOWN_ALL serves either
 *     downstream purpose), and nothing is computed.
 *   - Upstream: each 64-byte block written to WR_MESSAGE_MEM and taken by
 *     SET_MESSAGE_ONE goes into the inner hash, started from the key XOR
 *     ipad; SET_MESSAGE_ING announces another block. The message ends with
 *     ONE_BLOCK after its block, or with SET_MESSAGE_PAD then the block
 *     software padded (its length counting the ipad block), or with
 *     SET_MESSAGE_END, the hardware then padding it; the outer hash under
 *     the key XOR opad gives RD_RESULT_MEM, the digest's bytes in order.
 *     SET_RESULT_FINISH clears it.
 *   - Downstream: the message is fixed, 32 bytes of 0xFF for the digital
 *     signature (the key it decrypts its parameters with), 32 bytes of 0
 *     for JTAG; the result stays inside for the DS block, or is compared
 *     with the token written to WR_JTAG (SOFT_JTAG_CTRL then reports the
 *     match). SET_INVALIDATE_DS and SET_INVALIDATE_JTAG discard them.
 *   - QUERY_BUSY reads 0: each step completes at once.
 * Not yet: the time a computation takes, and what a soft JTAG enable does
 * (there is no JTAG side). */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"
#include "util/sha256.h"

#define P_DOWN_ALL 5
#define P_DOWN_JTAG 6
#define P_DOWN_DS 7
#define P_UP 8

struct c3_hmac {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *err, *busy, *msg[16], *res[8], *jtag_ctrl;
    u8 key[32];
    u32 purpose, keyid;
    int started, configured, up, pad_next, done;
    u32 inner[8];
    u64 blocks;   /* message blocks hashed, the ipad block included */
    u8 ds_key[32];
    int ds_valid;
    u8 jtag_key[32];
    int jtag_valid;
    u8 jtag_token[32];
    u32 jtag_n;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void hmac_inner_start(struct c3_hmac *k)
{
    static const u32 iv[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                               0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    u8 b[64];
    int i;
    memcpy(k->inner, iv, sizeof k->inner);
    for (i = 0; i < 64; i++)
        b[i] = (u8)((i < 32 ? k->key[i] : 0u) ^ 0x36u);
    esk_sha256_block(k->inner, b);
    k->blocks = 1;
}

/* the outer hash of the inner state: the HMAC */
static void hmac_outer(const struct c3_hmac *k, const u32 inner[8], u8 out[32])
{
    esk_sha256 c;
    u8 ipad_digest[32], b[64];
    int i;
    for (i = 0; i < 8; i++) {
        ipad_digest[4 * i] = (u8)(inner[i] >> 24);
        ipad_digest[4 * i + 1] = (u8)(inner[i] >> 16);
        ipad_digest[4 * i + 2] = (u8)(inner[i] >> 8);
        ipad_digest[4 * i + 3] = (u8)inner[i];
    }
    for (i = 0; i < 64; i++)
        b[i] = (u8)((i < 32 ? k->key[i] : 0u) ^ 0x5cu);
    esk_sha256_init(&c);
    esk_sha256_update(&c, b, 64);
    esk_sha256_update(&c, ipad_digest, 32);
    esk_sha256_final(&c, out);
}

/* HMAC(key, message) of a short fixed message, for the downstream modes */
static void hmac_fixed(struct c3_hmac *k, u8 fill, u8 out[32])
{
    u8 b[64];
    u64 bits = (64u + 32u) * 8u;
    int i;
    hmac_inner_start(k);
    memset(b, 0, sizeof b);
    memset(b, fill, 32);
    b[32] = 0x80u;
    for (i = 0; i < 8; i++)
        b[63 - i] = (u8)(bits >> (8 * i));
    esk_sha256_block(k->inner, b);
    hmac_outer(k, k->inner, out);
}

static void message_block(struct c3_hmac *k)
{
    u8 b[64];
    int i;
    for (i = 0; i < 16; i++) {
        u32 v = reg(k->msg[i]);
        b[4 * i] = (u8)v;
        b[4 * i + 1] = (u8)(v >> 8);
        b[4 * i + 2] = (u8)(v >> 16);
        b[4 * i + 3] = (u8)(v >> 24);
    }
    esk_sha256_block(k->inner, b);
    k->blocks++;
}

static void finish(struct c3_hmac *k, const u32 inner[8])
{
    u8 out[32];
    int i;
    hmac_outer(k, inner, out);
    for (i = 0; i < 8; i++)
        esk_rf_hw(k->res[i], (u32)out[4 * i] | (u32)out[4 * i + 1] << 8 | (u32)out[4 * i + 2] << 16 |
                                 (u32)out[4 * i + 3] << 24);
    k->done = 1;
}

static void clear_result(struct c3_hmac *k)
{
    int i;
    for (i = 0; i < 8; i++)
        esk_rf_hw(k->res[i], 0);
    k->done = 0;
}

static void configure(struct c3_hmac *k)
{
    int purpose = -1;
    u32 want = k->purpose;
    int ok;
    if (!k->started || k->keyid > 5u || esk_esp32c3_efuse_key(k->s, (int)k->keyid, k->key, &purpose) != 0) {
        esk_rf_hw(k->err, 1);
        return;
    }
    ok = (int)want == purpose ||
         ((want == P_DOWN_DS || want == P_DOWN_JTAG) && purpose == P_DOWN_ALL);
    if (!ok || (want != P_UP && want != P_DOWN_DS && want != P_DOWN_JTAG && want != P_DOWN_ALL)) {
        esk_rf_hw(k->err, 1);
        memset(k->key, 0, sizeof k->key);
        return;
    }
    esk_rf_hw(k->err, 0);
    k->configured = 1;
    if (want == P_UP) {
        k->up = 1;
        hmac_inner_start(k);
        return;
    }
    if (want == P_DOWN_DS || want == P_DOWN_ALL) {
        hmac_fixed(k, 0xffu, k->ds_key);
        k->ds_valid = 1;
    }
    if (want == P_DOWN_JTAG || want == P_DOWN_ALL) {
        hmac_fixed(k, 0x00u, k->jtag_key);
        k->jtag_valid = 1;
    }
    memset(k->key, 0, sizeof k->key);
}

static u32 hmac_read(void *ctx, u32 off, int n)
{
    struct c3_hmac *k = (struct c3_hmac *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void hmac_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_hmac *k = (struct c3_hmac *)ctx;
    u32 w = off & ~3u;
    if (w >= 0xC0u && w < 0xE0u)
        return; /* RD_RESULT_MEM */
    esk_rf_write(k->p, off, n, v);
    switch (w) {
    case 0x40u: /* SET_START */
        k->started = 1;
        k->configured = k->up = k->pad_next = 0;
        clear_result(k);
        break;
    case 0x44u:
        k->purpose = v & 15u;
        break;
    case 0x48u:
        k->keyid = v & 7u;
        break;
    case 0x4Cu: /* SET_PARA_FINISH */
        configure(k);
        break;
    case 0x50u: /* SET_MESSAGE_ONE */
        if (k->up && !k->done) {
            message_block(k);
            if (k->pad_next) /* the software-padded last block */
                finish(k, k->inner);
        }
        break;
    case 0x54u: /* SET_MESSAGE_ING: another block follows */
        break;
    case 0x58u: /* SET_MESSAGE_END: the hardware pads */
        if (k->up && !k->done) {
            u8 b[64];
            u64 bits = k->blocks * 512u;
            int i;
            u32 h[8];
            memcpy(h, k->inner, sizeof h);
            memset(b, 0, sizeof b);
            b[0] = 0x80u;
            for (i = 0; i < 8; i++)
                b[63 - i] = (u8)(bits >> (8 * i));
            esk_sha256_block(h, b);
            finish(k, h);
        }
        break;
    case 0xF0u: /* SET_MESSAGE_PAD: the next block is the padded last one */
        k->pad_next = 1;
        break;
    case 0xF4u: /* ONE_BLOCK: the block taken was the whole padded message */
        if (k->up && !k->done)
            finish(k, k->inner);
        break;
    case 0x5Cu: /* SET_RESULT_FINISH */
        clear_result(k);
        k->started = k->configured = k->up = k->pad_next = 0;
        memset(k->key, 0, sizeof k->key);
        break;
    case 0x60u: /* SET_INVALIDATE_JTAG */
        k->jtag_valid = 0;
        memset(k->jtag_key, 0, sizeof k->jtag_key);
        esk_rf_hw(k->jtag_ctrl, 0);
        break;
    case 0x64u: /* SET_INVALIDATE_DS */
        k->ds_valid = 0;
        memset(k->ds_key, 0, sizeof k->ds_key);
        break;
    case 0xFCu: /* WR_JTAG: the token, a word at a time */
        if (k->jtag_n < 8u) {
            k->jtag_token[4 * k->jtag_n] = (u8)v;
            k->jtag_token[4 * k->jtag_n + 1] = (u8)(v >> 8);
            k->jtag_token[4 * k->jtag_n + 2] = (u8)(v >> 16);
            k->jtag_token[4 * k->jtag_n + 3] = (u8)(v >> 24);
            k->jtag_n++;
        }
        if (k->jtag_n == 8u) {
            esk_rf_hw(k->jtag_ctrl, k->jtag_valid && !memcmp(k->jtag_token, k->jtag_key, 32) ? 1u : 0u);
            k->jtag_n = 0;
        }
        break;
    default:
        break;
    }
    esk_rf_hw(k->busy, 0);
}

static void hmac_reset(void *ctx)
{
    struct c3_hmac *k = (struct c3_hmac *)ctx;
    k->started = k->configured = k->up = k->pad_next = 0;
    k->ds_valid = k->jtag_valid = 0;
    k->jtag_n = 0;
    memset(k->key, 0, sizeof k->key);
    memset(k->ds_key, 0, sizeof k->ds_key);
    memset(k->jtag_key, 0, sizeof k->jtag_key);
    clear_result(k);
}

const struct esk_dev_ops esk_esp32c3_hmac_ops = {
    "esp32c3-hmac", hmac_read, hmac_write, hmac_reset, 0, 0,
};

/* the digital signature's key, from a downstream computation; -1 if none */
int esk_esp32c3_hmac_ds_key(struct esk_mach *m, u8 key[32])
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        struct c3_hmac *k;
        if (!p)
            break;
        if (p->ops != &esk_esp32c3_hmac_ops)
            continue;
        k = (struct c3_hmac *)p->ctx;
        if (!k->ds_valid)
            return -1;
        memcpy(key, k->ds_key, 32);
        return 0;
    }
    return -1;
}

void *esk_esp32c3_hmac_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_hmac *k = (struct c3_hmac *)esk_mach_alloc(s, sizeof *k);
    char name[24];
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->err = esk_rf_reg(p, "QUERY_ERROR");
    k->busy = esk_rf_reg(p, "QUERY_BUSY");
    k->jtag_ctrl = esk_rf_reg(p, "SOFT_JTAG_CTRL");
    if (!k->err || !k->busy || !k->jtag_ctrl)
        return 0;
    for (i = 0; i < 16; i++) {
        snprintf(name, sizeof name, "WR_MESSAGE_MEM%d", i);
        if (!(k->msg[i] = esk_rf_reg(p, name)))
            return 0;
    }
    for (i = 0; i < 8; i++) {
        snprintf(name, sizeof name, "RD_RESULT_MEM%d", i);
        if (!(k->res[i] = esk_rf_reg(p, name)))
            return 0;
    }
    return k;
}
