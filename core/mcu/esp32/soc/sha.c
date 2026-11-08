/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* sha.c -- the ESP32's SHA accelerator (TRM v5 chapter 23): SHA-1,
 * SHA-256, SHA-384 and SHA-512 (FIPS 180-4) over message blocks the
 * program writes in SHA_TEXT.
 *
 *   - SHA_TEXT (0x00-0x7C) holds a block as 32-bit words in message order,
 *     each the big-endian value of four message bytes (ESP-IDF's sha_ll
 *     swaps them in): 16 words for SHA-1 and SHA-256, 32 for SHA-384 and
 *     SHA-512 (a 64-bit word's high half first);
 *   - writing 1 to a type's START (0x80, 0x90, 0xA0, 0xB0) hashes the block
 *     from the type's initial state, CONTINUE (+4) from the state so far;
 *     LOAD (+8) puts the state in SHA_TEXT, in the same word order, for
 *     the program to read; BUSY (+0xC) reads 0: each operation is done by
 *     the time the next access comes (the TRM gives no cycle counts).
 * The state is the accelerator's, so a program that hashes with two types
 * in turn without LOAD loses nothing it could see. SHA-384's LOAD puts
 * its full 512-bit state, of which the digest is the first 384 bits.
 * Not yet: DPORT's SHA clock gate and reset (PERI_CLK_EN, PERI_RST_EN). */
#include <string.h>

#include "mcu/esp32/soc/esp32.h"
#include "util/sha256.h"

#define TEXT_WORDS 32

struct e32_sha {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    u32 text[TEXT_WORDS];
    u32 h1[5], h256[8];
    u64 h512[8];
};

static const u32 iv1[5] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u };
static const u32 iv256[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                              0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
static const u64 iv384[8] = { 0xcbbb9d5dc1059ed8ull, 0x629a292a367cd507ull, 0x9159015a3070dd17ull,
                              0x152fecd8f70e5939ull, 0x67332667ffc00b31ull, 0x8eb44a8768581511ull,
                              0xdb0c2e0d64f98fa7ull, 0x47b5481dbefa4fa4ull };
static const u64 iv512[8] = { 0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull,
                              0xa54ff53a5f1d36f1ull, 0x510e527fade682d1ull, 0x9b05688c2b3e6c1full,
                              0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull };
static const u64 k512[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
    0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull };

static u32 rol(u32 x, int n) { return (x << n) | (x >> (32 - n)); }
static u64 ror64(u64 x, int n) { return (x >> n) | (x << (64 - n)); }

static void sha1_block(u32 h[5], const u32 m[16])
{
    u32 w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f, k, t;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = m[i];
    for (; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (i = 0; i < 80; i++) {
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999u; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
        else { f = b ^ c ^ d; k = 0xca62c1d6u; }
        t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

static void sha512_block(u64 h[8], const u32 m[32])
{
    u64 w[80], v[8], t1, t2;
    int i, j;
    for (i = 0; i < 16; i++)
        w[i] = (u64)m[2 * i] << 32 | m[2 * i + 1];
    for (; i < 80; i++) {
        u64 s0 = ror64(w[i - 15], 1) ^ ror64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        u64 s1 = ror64(w[i - 2], 19) ^ ror64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (j = 0; j < 8; j++)
        v[j] = h[j];
    for (i = 0; i < 80; i++) {
        t1 = v[7] + (ror64(v[4], 14) ^ ror64(v[4], 18) ^ ror64(v[4], 41)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) +
             k512[i] + w[i];
        t2 = (ror64(v[0], 28) ^ ror64(v[0], 34) ^ ror64(v[0], 39)) +
             ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
        v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (j = 0; j < 8; j++)
        h[j] += v[j];
}

static void sha256_words(u32 h[8], const u32 m[16])
{
    u8 blk[64];
    int i;
    for (i = 0; i < 16; i++) {
        blk[4 * i] = (u8)(m[i] >> 24);
        blk[4 * i + 1] = (u8)(m[i] >> 16);
        blk[4 * i + 2] = (u8)(m[i] >> 8);
        blk[4 * i + 3] = (u8)m[i];
    }
    esk_sha256_block(h, blk);
}

/* type 0 SHA-1, 1 SHA-256, 2 SHA-384, 3 SHA-512; op 0 start, 1 continue, 2 load */
static void operate(struct e32_sha *k, int type, int op)
{
    int i;
    if (op == 2) {
        if (type == 0)
            for (i = 0; i < 5; i++)
                k->text[i] = k->h1[i];
        else if (type == 1)
            for (i = 0; i < 8; i++)
                k->text[i] = k->h256[i];
        else
            for (i = 0; i < 8; i++) {
                k->text[2 * i] = (u32)(k->h512[i] >> 32);
                k->text[2 * i + 1] = (u32)k->h512[i];
            }
        return;
    }
    if (type == 0) {
        if (op == 0)
            memcpy(k->h1, iv1, sizeof k->h1);
        sha1_block(k->h1, k->text);
    } else if (type == 1) {
        if (op == 0)
            memcpy(k->h256, iv256, sizeof k->h256);
        sha256_words(k->h256, k->text);
    } else {
        if (op == 0)
            memcpy(k->h512, type == 2 ? iv384 : iv512, sizeof k->h512);
        sha512_block(k->h512, k->text);
    }
}

static u32 sh_read(void *ctx, u32 off, int n)
{
    struct e32_sha *k = (struct e32_sha *)ctx;
    if (off < 4u * TEXT_WORDS)
        return k->text[off / 4u] >> (8u * (off & 3u));
    if (off >= 0x80u && off < 0xC0u)
        return 0; /* START, CONTINUE, LOAD are write-only; BUSY: done */
    return esk_rf_read(k->p, off, n);
}

static void sh_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_sha *k = (struct e32_sha *)ctx;
    if (off < 4u * TEXT_WORDS) {
        if (n == 4)
            k->text[off / 4u] = v;
        return;
    }
    if (off >= 0x80u && off < 0xC0u && (off & 0xCu) != 0xCu && (v & 1u)) {
        operate(k, (int)((off - 0x80u) / 0x10u), (int)((off & 0xCu) / 4u));
        return;
    }
    esk_rf_write(k->p, off, n, v);
}

static void sh_reset(void *ctx)
{
    struct e32_sha *k = (struct e32_sha *)ctx;
    memset(k->text, 0, sizeof k->text);
}

const struct esk_dev_ops esk_esp32_sha_ops = {
    "esp32-sha", sh_read, sh_write, sh_reset, 0, 0,
};

void *esk_esp32_sha_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_sha *k = (struct e32_sha *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    k->s = s;
    k->p = p;
    return k;
}
