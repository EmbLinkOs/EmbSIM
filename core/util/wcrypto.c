/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* wcrypto.c -- WPA2-Personal's cryptography (wcrypto.h says which standard
 * each part follows). */
#include <string.h>

#include "util/wcrypto.h"

/* ---- SHA-1 (FIPS 180-4 6.1) ------------------------------------------------ */

static uint32_t rotl(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void sha1_block(uint32_t hv[5], const unsigned char *p)
{
    uint32_t w[80], a = hv[0], b = hv[1], c = hv[2], d = hv[3], e = hv[4], t;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 |
               (uint32_t)p[4 * i + 3];
    for (i = 16; i < 80; i++)
        w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        t = rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = t;
    }
    hv[0] += a;
    hv[1] += b;
    hv[2] += c;
    hv[3] += d;
    hv[4] += e;
}

void esk_sha1_init(esk_sha1 *c)
{
    c->h[0] = 0x67452301u;
    c->h[1] = 0xEFCDAB89u;
    c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u;
    c->h[4] = 0xC3D2E1F0u;
    c->bytes = 0;
    c->used = 0;
}

void esk_sha1_update(esk_sha1 *c, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    c->bytes += len;
    while (len) {
        size_t n = 64 - c->used < len ? 64 - c->used : len;
        memcpy(c->block + c->used, p, n);
        c->used += n;
        p += n;
        len -= n;
        if (c->used == 64) {
            sha1_block(c->h, c->block);
            c->used = 0;
        }
    }
}

void esk_sha1_final(esk_sha1 *c, unsigned char out[ESK_SHA1_LEN])
{
    uint64_t bits = c->bytes * 8u;
    unsigned char pad = 0x80, zero = 0, len[8];
    int i;
    esk_sha1_update(c, &pad, 1);
    while (c->used != 56)
        esk_sha1_update(c, &zero, 1);
    for (i = 0; i < 8; i++)
        len[i] = (unsigned char)(bits >> (56 - 8 * i));
    esk_sha1_update(c, len, 8);
    for (i = 0; i < 20; i++)
        out[i] = (unsigned char)(c->h[i / 4] >> (24 - 8 * (i % 4)));
}

/* ---- HMAC, PBKDF2, the PRF -------------------------------------------------- */

void esk_hmac_sha1(const unsigned char *key, size_t key_len, const unsigned char *data, size_t len,
                   unsigned char out[ESK_SHA1_LEN])
{
    unsigned char k[64], pad[64], inner[ESK_SHA1_LEN];
    esk_sha1 c;
    size_t i;
    memset(k, 0, sizeof k);
    if (key_len > 64) {
        esk_sha1_init(&c);
        esk_sha1_update(&c, key, key_len);
        esk_sha1_final(&c, k);
    } else
        memcpy(k, key, key_len);
    for (i = 0; i < 64; i++)
        pad[i] = (unsigned char)(k[i] ^ 0x36u);
    esk_sha1_init(&c);
    esk_sha1_update(&c, pad, 64);
    esk_sha1_update(&c, data, len);
    esk_sha1_final(&c, inner);
    for (i = 0; i < 64; i++)
        pad[i] = (unsigned char)(k[i] ^ 0x5Cu);
    esk_sha1_init(&c);
    esk_sha1_update(&c, pad, 64);
    esk_sha1_update(&c, inner, sizeof inner);
    esk_sha1_final(&c, out);
}

void esk_pbkdf2_sha1(const unsigned char *pass, size_t pass_len, const unsigned char *salt, size_t salt_len,
                     unsigned iterations, unsigned char *out, size_t out_len)
{
    unsigned char u[ESK_SHA1_LEN], t[ESK_SHA1_LEN], msg[64 + 4];
    uint32_t block = 1;
    while (out_len) {
        size_t n = out_len < ESK_SHA1_LEN ? out_len : ESK_SHA1_LEN, i;
        unsigned it;
        size_t sl = salt_len > 64 ? 64 : salt_len; /* an SSID is at most 32 bytes */
        memcpy(msg, salt, sl);
        msg[sl] = (unsigned char)(block >> 24);
        msg[sl + 1] = (unsigned char)(block >> 16);
        msg[sl + 2] = (unsigned char)(block >> 8);
        msg[sl + 3] = (unsigned char)block;
        esk_hmac_sha1(pass, pass_len, msg, sl + 4, u);
        memcpy(t, u, sizeof t);
        for (it = 1; it < iterations; it++) {
            esk_hmac_sha1(pass, pass_len, u, sizeof u, u);
            for (i = 0; i < ESK_SHA1_LEN; i++)
                t[i] ^= u[i];
        }
        memcpy(out, t, n);
        out += n;
        out_len -= n;
        block++;
    }
}

void esk_prf_sha1(const unsigned char *key, size_t key_len, const char *label, const unsigned char *data,
                  size_t data_len, unsigned char *out, size_t out_len)
{
    unsigned char msg[256], h[ESK_SHA1_LEN];
    size_t ll = strlen(label), n = 0;
    unsigned char i = 0;
    if (ll + 1 + data_len + 1 > sizeof msg)
        return;
    memcpy(msg, label, ll);
    msg[ll] = 0;
    memcpy(msg + ll + 1, data, data_len);
    while (n < out_len) {
        size_t k = out_len - n < ESK_SHA1_LEN ? out_len - n : ESK_SHA1_LEN;
        msg[ll + 1 + data_len] = i++;
        esk_hmac_sha1(key, key_len, msg, ll + 1 + data_len + 1, h);
        memcpy(out + n, h, k);
        n += k;
    }
}

/* ---- AES-128 (FIPS 197) ---------------------------------------------------- */

static unsigned char sbox[256];
static int sbox_ready;

static unsigned char xtime(unsigned char x)
{
    unsigned v = x;
    return (unsigned char)((v << 1) ^ ((v & 0x80u) ? 0x1Bu : 0u));
}

static unsigned char gmul(unsigned char a, unsigned char b)
{
    unsigned char p = 0;
    while (b) {
        if (b & 1u)
            p ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return p;
}

/* the S-box from its definition: the inverse in GF(2^8), then the affine map */
static void make_sbox(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        unsigned char inv = 0, x, s;
        int j;
        if (i)
            for (j = 1; j < 256; j++)
                if (gmul((unsigned char)i, (unsigned char)j) == 1) {
                    inv = (unsigned char)j;
                    break;
                }
        x = inv;
        s = (unsigned char)(x ^ (unsigned char)(x << 1 | x >> 7) ^ (unsigned char)(x << 2 | x >> 6) ^
                            (unsigned char)(x << 3 | x >> 5) ^ (unsigned char)(x << 4 | x >> 4) ^ 0x63u);
        sbox[i] = s;
    }
    sbox_ready = 1;
}

static uint32_t sub_word(uint32_t w)
{
    return (uint32_t)sbox[w >> 24] << 24 | (uint32_t)sbox[(w >> 16) & 0xFFu] << 16 |
           (uint32_t)sbox[(w >> 8) & 0xFFu] << 8 | (uint32_t)sbox[w & 0xFFu];
}

void esk_aes_init(esk_aes *a, const unsigned char *key, size_t key_bytes)
{
    static const unsigned char rcon[10] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36 };
    int i, nk = (int)(key_bytes / 4u), total;
    if (!sbox_ready)
        make_sbox();
    if (nk != 4 && nk != 6 && nk != 8)
        nk = 4;
    a->nr = nk + 6;
    total = 4 * (a->nr + 1);
    for (i = 0; i < nk; i++)
        a->rk[i] = (uint32_t)key[4 * i] << 24 | (uint32_t)key[4 * i + 1] << 16 | (uint32_t)key[4 * i + 2] << 8 |
                   (uint32_t)key[4 * i + 3];
    for (i = nk; i < total; i++) {
        uint32_t t = a->rk[i - 1];
        if (i % nk == 0)
            t = sub_word(t << 8 | t >> 24) ^ (uint32_t)rcon[i / nk - 1] << 24;
        else if (nk > 6 && i % nk == 4)
            t = sub_word(t);
        a->rk[i] = a->rk[i - nk] ^ t;
    }
}

void esk_aes_encrypt(const esk_aes *a, const unsigned char in[16], unsigned char out[16])
{
    unsigned char s[16], t[16];
    int r, i, c;
    for (i = 0; i < 16; i++)
        s[i] = (unsigned char)(in[i] ^ (a->rk[i / 4] >> (24 - 8 * (i % 4))));
    for (r = 1; r <= a->nr; r++) {
        for (i = 0; i < 16; i++) /* SubBytes, ShiftRows */
            t[i] = sbox[s[(i + 4 * (i % 4)) % 16]];
        if (r < a->nr)
            for (c = 0; c < 4; c++) { /* MixColumns */
                unsigned char *col = t + 4 * c, a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = (unsigned char)(xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3);
                col[1] = (unsigned char)(a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3);
                col[2] = (unsigned char)(a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3);
                col[3] = (unsigned char)(xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3));
            }
        for (i = 0; i < 16; i++)
            s[i] = (unsigned char)(t[i] ^ (a->rk[4 * r + i / 4] >> (24 - 8 * (i % 4))));
    }
    memcpy(out, s, 16);
}

/* the inverse cipher */
static unsigned char inv_sbox[256];
static int inv_ready;

void esk_aes_decrypt(const esk_aes *a, const unsigned char in[16], unsigned char out[16])
{
    unsigned char s[16], t[16];
    int r, i, c;
    if (!inv_ready) {
        for (i = 0; i < 256; i++)
            inv_sbox[sbox[i]] = (unsigned char)i;
        inv_ready = 1;
    }
    for (i = 0; i < 16; i++)
        s[i] = (unsigned char)(in[i] ^ (a->rk[4 * a->nr + i / 4] >> (24 - 8 * (i % 4))));
    for (r = a->nr - 1; r >= 0; r--) {
        for (i = 0; i < 16; i++) /* InvShiftRows, InvSubBytes */
            t[(i + 4 * (i % 4)) % 16] = inv_sbox[s[i]];
        for (i = 0; i < 16; i++)
            t[i] = (unsigned char)(t[i] ^ (a->rk[4 * r + i / 4] >> (24 - 8 * (i % 4))));
        if (r > 0)
            for (c = 0; c < 4; c++) { /* InvMixColumns */
                unsigned char *col = t + 4 * c, a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = (unsigned char)(gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9));
                col[1] = (unsigned char)(gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13));
                col[2] = (unsigned char)(gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11));
                col[3] = (unsigned char)(gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14));
            }
        memcpy(s, t, 16);
    }
    memcpy(out, s, 16);
}

/* ---- the key wrap (RFC 3394) ------------------------------------------------ */

void esk_aes_wrap(const unsigned char kek[16], const unsigned char *in, size_t n, unsigned char *out)
{
    esk_aes a;
    unsigned char b[16], *r = out + 8;
    size_t i;
    int j;
    esk_aes_init(&a, kek, 16);
    memset(out, 0xA6, 8);
    memcpy(r, in, 8 * n);
    for (j = 0; j < 6; j++)
        for (i = 1; i <= n; i++) {
            uint64_t t = (uint64_t)n * (uint64_t)j + i;
            int k;
            memcpy(b, out, 8);
            memcpy(b + 8, r + 8 * (i - 1), 8);
            esk_aes_encrypt(&a, b, b);
            for (k = 0; k < 8; k++)
                out[k] = (unsigned char)(b[k] ^ (unsigned char)(t >> (56 - 8 * k)));
            memcpy(r + 8 * (i - 1), b + 8, 8);
        }
}

int esk_aes_unwrap(const unsigned char kek[16], const unsigned char *in, size_t n, unsigned char *out)
{
    esk_aes a;
    unsigned char b[16], top[8];
    size_t i;
    int j, k;
    esk_aes_init(&a, kek, 16);
    memcpy(top, in, 8);
    memcpy(out, in + 8, 8 * n);
    for (j = 5; j >= 0; j--)
        for (i = n; i >= 1; i--) {
            uint64_t t = (uint64_t)n * (uint64_t)j + i;
            for (k = 0; k < 8; k++)
                b[k] = (unsigned char)(top[k] ^ (unsigned char)(t >> (56 - 8 * k)));
            memcpy(b + 8, out + 8 * (i - 1), 8);
            esk_aes_decrypt(&a, b, b);
            memcpy(top, b, 8);
            memcpy(out + 8 * (i - 1), b + 8, 8);
        }
    for (k = 0; k < 8; k++)
        if (top[k] != 0xA6u)
            return -1;
    return 0;
}

/* ---- CCMP (IEEE 802.11-2020 12.5.3) ---------------------------------------- */

size_t esk_80211_header_len(const unsigned char *f, size_t len)
{
    size_t n = 24;
    if (len < 24)
        return 0;
    if (((f[0] >> 2) & 3u) == 2u) {
        if ((f[1] & 3u) == 3u)
            n += 6; /* address 4 */
        if (f[0] & 0x80u) {
            n += 2; /* QoS control */
            if (f[1] & 0x80u)
                n += 4; /* HT control */
        }
    }
    return n <= len ? n : 0;
}

/* the AAD (12.5.3.3.3) and the nonce (12.5.3.3.4) of an MPDU's header */
static size_t ccmp_aad(const unsigned char *h, size_t hl, uint64_t pn, unsigned char aad[32], unsigned char nonce[13])
{
    size_t n = 0;
    int data = ((h[0] >> 2) & 3u) == 2u, qos = data && (h[0] & 0x80u), a4 = data && (h[1] & 3u) == 3u, i;
    aad[n++] = (unsigned char)(data ? h[0] & 0x8Fu : h[0]); /* a data frame's subtype bits 4-6 masked */
    aad[n++] = (unsigned char)(((h[1] & ~0x38u) | 0x40u) & (qos ? 0x7Fu : 0xFFu)); /* Retry, PwrMgt, MoreData; Protected */
    memcpy(aad + n, h + 4, 18);
    n += 18;
    aad[n++] = (unsigned char)(h[22] & 0x0Fu); /* the fragment number alone */
    aad[n++] = 0;
    if (a4) {
        memcpy(aad + n, h + 24, 6);
        n += 6;
    }
    if (qos) {
        size_t q = a4 ? 30u : 24u;
        aad[n++] = (unsigned char)(h[q] & 0x0Fu); /* the TID */
        aad[n++] = 0;
    }
    (void)hl;
    nonce[0] = (unsigned char)(qos ? h[a4 ? 30 : 24] & 0x0Fu : 0u);
    if (!data)
        nonce[0] |= 0x10u; /* a management frame */
    memcpy(nonce + 1, h + 10, 6); /* A2 */
    for (i = 0; i < 6; i++)
        nonce[7 + i] = (unsigned char)(pn >> (40 - 8 * i));
    return n;
}

/* CCM with an 8-byte MIC and a 2-byte length (RFC 3610): the CBC-MAC's tag
 * and the counter mode; `enc` is 1 to encrypt, 0 to decrypt */
static void ccm(const unsigned char tk[16], const unsigned char nonce[13], const unsigned char *aad, size_t aad_len,
                const unsigned char *in, size_t len, unsigned char *out, unsigned char mic[8], int enc)
{
    esk_aes a;
    unsigned char x[16], blk[16], ctr[16], s[16];
    size_t i, j;
    uint16_t cnt = 1;
    esk_aes_init(&a, tk, 16);
    /* the counter mode first when decrypting, to MAC the plaintext */
    ctr[0] = 0x01;
    memcpy(ctr + 1, nonce, 13);
    for (i = 0; i < len; i += 16) {
        size_t n = len - i < 16 ? len - i : 16;
        ctr[14] = (unsigned char)(cnt >> 8);
        ctr[15] = (unsigned char)cnt;
        cnt++;
        esk_aes_encrypt(&a, ctr, s);
        for (j = 0; j < n; j++)
            out[i + j] = (unsigned char)(in[i + j] ^ s[j]);
    }
    /* the CBC-MAC over B0, the AAD and the plaintext */
    x[0] = 0x59; /* Adata, M = 8, L = 2 */
    memcpy(x + 1, nonce, 13);
    x[14] = (unsigned char)(len >> 8);
    x[15] = (unsigned char)len;
    esk_aes_encrypt(&a, x, x);
    memset(blk, 0, 16);
    blk[0] = (unsigned char)(aad_len >> 8);
    blk[1] = (unsigned char)aad_len;
    for (i = 0, j = 2; i < aad_len; i++) {
        blk[j++] = aad[i];
        if (j == 16) {
            for (j = 0; j < 16; j++)
                x[j] ^= blk[j];
            esk_aes_encrypt(&a, x, x);
            memset(blk, 0, 16);
            j = 0;
        }
    }
    if (j) {
        for (j = 0; j < 16; j++)
            x[j] ^= blk[j];
        esk_aes_encrypt(&a, x, x);
    }
    for (i = 0; i < len; i += 16) {
        size_t n = len - i < 16 ? len - i : 16;
        const unsigned char *p = enc ? in + i : out + i;
        for (j = 0; j < n; j++)
            x[j] ^= p[j];
        esk_aes_encrypt(&a, x, x);
    }
    ctr[14] = ctr[15] = 0;
    esk_aes_encrypt(&a, ctr, s);
    for (j = 0; j < 8; j++)
        mic[j] = (unsigned char)(x[j] ^ s[j]);
}

size_t esk_ccmp_encrypt(const unsigned char tk[16], const unsigned char *hdr, size_t hdr_len, uint64_t pn, int key_id,
                        const unsigned char *plain, size_t len, unsigned char *out)
{
    unsigned char aad[32], nonce[13];
    size_t al = ccmp_aad(hdr, hdr_len, pn, aad, nonce);
    out[0] = (unsigned char)pn;
    out[1] = (unsigned char)(pn >> 8);
    out[2] = 0;
    out[3] = (unsigned char)(0x20u | (unsigned)(key_id & 3) << 6); /* ExtIV */
    out[4] = (unsigned char)(pn >> 16);
    out[5] = (unsigned char)(pn >> 24);
    out[6] = (unsigned char)(pn >> 32);
    out[7] = (unsigned char)(pn >> 40);
    ccm(tk, nonce, aad, al, plain, len, out + 8, out + 8 + len, 1);
    return 16 + len;
}

long esk_ccmp_decrypt(const unsigned char tk[16], const unsigned char *hdr, size_t hdr_len, const unsigned char *body,
                      size_t len, unsigned char *plain, uint64_t *pn)
{
    unsigned char aad[32], nonce[13], mic[8];
    uint64_t p;
    size_t al, n, i;
    unsigned diff = 0;
    if (len < 16 || !(body[3] & 0x20u))
        return -1;
    p = (uint64_t)body[0] | (uint64_t)body[1] << 8 | (uint64_t)body[4] << 16 | (uint64_t)body[5] << 24 |
        (uint64_t)body[6] << 32 | (uint64_t)body[7] << 40;
    n = len - 16;
    al = ccmp_aad(hdr, hdr_len, p, aad, nonce);
    ccm(tk, nonce, aad, al, body + 8, n, plain, mic, 0);
    for (i = 0; i < 8; i++)
        diff |= (unsigned)(mic[i] ^ body[8 + n + i]);
    if (diff)
        return -1;
    if (pn)
        *pn = p;
    return (long)n;
}
