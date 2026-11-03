/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ds.c -- the ESP32-C3's digital signature peripheral (0x6003D000, TRM
 * v1.4 chapter 22): an RSA private-key operation of up to 3072 bits whose
 * parameters software holds only encrypted, under a key the HMAC
 * accelerator derives downstream from an eFuse key (hmac.c), as ESP-IDF's
 * esp_ds_sign and its TLS client authentication use it.
 *
 *   - Memories: the encrypted parameters C at +0x000 (Y, 384 bytes), +0x200
 *     (M), +0x400 (Rb) and +0x600 (the box: 48 bytes); the IV at +0x630;
 *     the message X at +0x800; the result Z at +0xA00, all little-endian.
 *     (Espressif's register map leaves the IV's four words out; ESP-IDF's
 *     ds_ll_configure_iv writes them at DS_IV_BASE.)
 *   - SET_START takes the HMAC accelerator's digital-signature key; while
 *     there is none QUERY_BUSY stays 1 and QUERY_KEY_WRONG counts the
 *     attempts (ESP-IDF times out with ESP_ERR_HW_CRYPTO_DS_INVALID_KEY).
 *   - SET_ME decrypts the 1200 bytes of C with AES-256-CBC under that key
 *     and the IV into Y || M || Rb || MD || M' || LENGTH || 8 bytes of 0x08
 *     (ESP-IDF's gen_digital_signature_tests.py and esp_ds_encrypt_params
 *     build them so), checks MD against SHA-256(Y || M || Rb || M' ||
 *     LENGTH || IV) and the padding: QUERY_CHECK bit 0 when MD differs, bit
 *     1 when the padding does; with MD right it computes Z = X^Y mod M over
 *     LENGTH + 1 words by Montgomery products with Rb and M'.
 *   - SET_FINISH clears the memories and returns to idle.
 * Not yet: the time an operation takes (it completes at once), and the
 * memories' read-back (reading C, X or the IV gives what was written; the
 * hardware gives 0). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"
#include "util/sha256.h"
#include "util/wcrypto.h"

#define MAXB 384  /* 3072 bits */
#define CLEN 1200 /* 3 x 384 + 48 */
#define OFF_IV 0x630u
#define OFF_X 0x800u
#define OFF_Z 0xA00u
#define OFF_START 0xE00u
#define OFF_ME 0xE04u
#define OFF_FINISH 0xE08u
#define OFF_BUSY 0xE0Cu
#define OFF_KEY_WRONG 0xE10u
#define OFF_CHECK 0xE14u

struct c3_ds {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    u8 c[0x630];  /* Y, M, Rb at 0x200 apart, the box at 0x600 */
    u8 iv[16];
    u8 x[MAXB];
    u8 z[MAXB];
    u8 key[32];
    int have_key;
    u32 busy, key_wrong, check;
};

static u32 le32(const u8 *b) { return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24; }

static void words(u32 *w, const u8 *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        w[i] = le32(b + 4 * i);
}

/* the operation SET_ME starts */
static void sign(struct c3_ds *k)
{
    u8 enc[CLEN], p[CLEN], prev[16], md[32];
    u32 y[96], m[96], rb[96], x[96], z[96], one[96], mp, len;
    esk_aes a;
    esk_sha256 h;
    int i, n, j, bad_pad = 0;
    memcpy(enc, k->c, MAXB);
    memcpy(enc + MAXB, k->c + 0x200, MAXB);
    memcpy(enc + 2 * MAXB, k->c + 0x400, MAXB);
    memcpy(enc + 3 * MAXB, k->c + 0x600, 48);
    esk_aes_init(&a, k->key, 32);
    memcpy(prev, k->iv, 16);
    for (i = 0; i < CLEN; i += 16) {
        esk_aes_decrypt(&a, enc + i, p + i);
        for (j = 0; j < 16; j++)
            p[i + j] ^= prev[j];
        memcpy(prev, enc + i, 16);
    }
    /* MD over Y || M || Rb || M' || LENGTH || IV */
    esk_sha256_init(&h);
    esk_sha256_update(&h, p, 3 * MAXB);
    esk_sha256_update(&h, p + 3 * MAXB + 32, 8);
    esk_sha256_update(&h, k->iv, 16);
    esk_sha256_final(&h, md);
    for (i = 0; i < 8; i++)
        if (p[CLEN - 8 + i] != 0x08u)
            bad_pad = 1;
    k->check = (memcmp(md, p + 3 * MAXB, 32) ? 1u : 0u) | (bad_pad ? 2u : 0u);
    memset(k->z, 0, sizeof k->z);
    if (k->check & 1u)
        return;
    mp = le32(p + 3 * MAXB + 32);
    len = le32(p + 3 * MAXB + 36);
    n = (int)(len & 0x7fu) + 1;
    if (n > 96)
        return;
    words(y, p, n);
    words(m, p + MAXB, n);
    words(rb, p + 2 * MAXB, n);
    words(x, k->x, n);
    memset(one, 0, sizeof one);
    one[0] = 1;
    esk_esp32c3_mont(x, x, rb, m, mp, n); /* X R mod M */
    esk_esp32c3_mont(z, rb, one, m, mp, n); /* R mod M */
    for (i = 32 * n - 1; i >= 0; i--) {
        esk_esp32c3_mont(z, z, z, m, mp, n);
        if ((y[i / 32] >> (i % 32)) & 1u)
            esk_esp32c3_mont(z, z, x, m, mp, n);
    }
    esk_esp32c3_mont(z, z, one, m, mp, n);
    for (i = 0; i < n; i++) {
        k->z[4 * i] = (u8)z[i];
        k->z[4 * i + 1] = (u8)(z[i] >> 8);
        k->z[4 * i + 2] = (u8)(z[i] >> 16);
        k->z[4 * i + 3] = (u8)(z[i] >> 24);
    }
    memset(p, 0, sizeof p);
}

static void clear(struct c3_ds *k)
{
    memset(k->c, 0, sizeof k->c);
    memset(k->iv, 0, sizeof k->iv);
    memset(k->x, 0, sizeof k->x);
    memset(k->z, 0, sizeof k->z);
    memset(k->key, 0, sizeof k->key);
    k->have_key = 0;
    k->busy = 0;
    k->check = 0;
}

static u8 *mem_at(struct c3_ds *k, u32 off, u32 *room)
{
    if (off < 0x630u) {
        *room = 0x630u - off;
        return k->c + off;
    }
    if (off < 0x640u) {
        *room = 0x640u - off;
        return k->iv + (off - OFF_IV);
    }
    if (off >= OFF_X && off < OFF_X + MAXB) {
        *room = OFF_X + MAXB - off;
        return k->x + (off - OFF_X);
    }
    if (off >= OFF_Z && off < OFF_Z + MAXB) {
        *room = OFF_Z + MAXB - off;
        return k->z + (off - OFF_Z);
    }
    return NULL;
}

static u32 ds_read(void *ctx, u32 off, int n)
{
    struct c3_ds *k = (struct c3_ds *)ctx;
    u32 room = 0, v = 0;
    u8 *at = mem_at(k, off, &room);
    int i;
    if (at) {
        for (i = 0; i < n && (u32)i < room; i++)
            v |= (u32)at[i] << (8 * i);
        return v;
    }
    switch (off & ~3u) {
    case OFF_BUSY:
        return k->busy;
    case OFF_KEY_WRONG:
        return k->key_wrong;
    case OFF_CHECK:
        return k->check;
    default:
        return esk_rf_read(k->p, off, n);
    }
}

static void ds_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_ds *k = (struct c3_ds *)ctx;
    u32 room = 0;
    u8 *at = mem_at(k, off, &room);
    int i;
    if (at) {
        if (off >= OFF_Z)
            return; /* Z is read-only */
        for (i = 0; i < n && (u32)i < room; i++)
            at[i] = (u8)(v >> (8 * i));
        return;
    }
    switch (off & ~3u) {
    case OFF_START:
        if (esk_esp32c3_hmac_ds_key(k->s, k->key) == 0) {
            k->have_key = 1;
            k->busy = 0;
            k->key_wrong = 0;
        } else {
            k->busy = 1;
            if (k->key_wrong < 15u)
                k->key_wrong++;
        }
        break;
    case OFF_ME:
        if (k->have_key)
            sign(k);
        break;
    case OFF_FINISH:
        clear(k);
        break;
    default:
        esk_rf_write(k->p, off, n, v);
        break;
    }
}

static void ds_reset(void *ctx)
{
    struct c3_ds *k = (struct c3_ds *)ctx;
    clear(k);
    k->key_wrong = 0;
}

const struct esk_dev_ops esk_esp32c3_ds_ops = {
    "esp32c3-ds", ds_read, ds_write, ds_reset, 0, 0,
};

void *esk_esp32c3_ds_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_ds *k = (struct c3_ds *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    return k;
}
