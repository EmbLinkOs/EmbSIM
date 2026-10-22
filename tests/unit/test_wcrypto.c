/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* WPA2's cryptography (util/wcrypto.h) against the standards' own vectors:
 * FIPS 180-4's SHA-1, RFC 2202's HMAC-SHA1, IEEE 802.11's PSK from
 * "password" and "IEEE" and its PRF, FIPS 197's AES-128, RFC 3394's key
 * wrap, IEEE 802.11's CCMP example MPDU. */
#include "harness.h"
#include "util/wcrypto.h"

static void hex(const char *h, unsigned char *out)
{
    size_t i;
    for (i = 0; h[2 * i] && h[2 * i + 1]; i++) {
        unsigned v = 0;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (unsigned char)v;
    }
}

static int same(const unsigned char *a, const char *h)
{
    unsigned char b[256];
    size_t n = strlen(h) / 2;
    hex(h, b);
    return memcmp(a, b, n) == 0;
}

static void test_sha1_hmac(void)
{
    unsigned char d[ESK_SHA1_LEN], a1000[1000];
    esk_sha1 c;
    esk_sha1_init(&c);
    esk_sha1_update(&c, "abc", 3);
    esk_sha1_final(&c, d);
    {
        char text[2 * ESK_SHA1_LEN + 1];
        int i;
        for (i = 0; i < ESK_SHA1_LEN; i++)
            snprintf(text + 2 * i, 3, "%02x", d[i]);
        CHECK_STR(text, "a9993e364706816aba3e25717850c26c9cd0d89d");
    }
    memset(a1000, 'a', sizeof a1000);
    esk_sha1_init(&c);
    esk_sha1_update(&c, a1000, 999);
    esk_sha1_update(&c, a1000, 1);
    esk_sha1_final(&c, d);
    CHECK(same(d, "291e9a6c66994949b57ba5e650361e98fc36b1ba"));
    esk_hmac_sha1((const unsigned char *)"key", 3,
                  (const unsigned char *)"The quick brown fox jumps over the lazy dog", 43, d);
    CHECK(same(d, "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9"));
    /* RFC 2202 case 6: a key longer than the block */
    {
        unsigned char key[80];
        memset(key, 0xAA, sizeof key);
        esk_hmac_sha1(key, sizeof key, (const unsigned char *)"Test Using Larger Than Block-Size Key - Hash Key First",
                      54, d);
        CHECK(same(d, "aa4ae5e15272d00e95705637ce8a3b55ed402112"));
    }
}

static void test_psk_prf(void)
{
    unsigned char psk[32], out[80];
    /* IEEE 802.11-2020 J.4.2: passphrase "password", SSID "IEEE" */
    esk_pbkdf2_sha1((const unsigned char *)"password", 8, (const unsigned char *)"IEEE", 4, 4096, psk, 32);
    CHECK(same(psk, "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e"));
    /* J.3.2's PRF test 1: key 0x0b x 20, "prefix", "Hi There" */
    {
        unsigned char key[20];
        memset(key, 0x0B, sizeof key);
        esk_prf_sha1(key, sizeof key, "prefix", (const unsigned char *)"Hi There", 8, out, 80);
        CHECK(same(out, "bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606e17d8da35402ffee75df78c3d31e0f889f012120"
                        "c0862beb67753e7439ae242edb8373698356cf5a"));
    }
}

static void test_aes_wrap(void)
{
    unsigned char k[16], p[16], c[16], w[24], u[16];
    esk_aes128 a;
    hex("000102030405060708090a0b0c0d0e0f", k);
    hex("00112233445566778899aabbccddeeff", p);
    esk_aes128_init(&a, k);
    esk_aes128_encrypt(&a, p, c);
    CHECK(same(c, "69c4e0d86a7b0430d8cdb78070b4c55a")); /* FIPS 197 C.1 */
    esk_aes_wrap(k, p, 2, w);
    CHECK(same(w, "1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5")); /* RFC 3394 4.1 */
    CHECK_EQ(esk_aes_unwrap(k, w, 2, u), 0);
    CHECK(!memcmp(u, p, 16));
    w[5] ^= 1;
    CHECK_EQ(esk_aes_unwrap(k, w, 2, u), -1);
}

static void test_ccmp(void)
{
    unsigned char tk[16], hdr[26], pt[20], out[64], back[64];
    uint64_t pn = 0;
    long n;
    /* IEEE 802.11's CCMP example: a data frame, PN 0xB5039776E70C */
    hex("c97c1f67ce371185514a8a19f2bdd52f", tk);
    hex("0848c32c0fd2e128a57c5030f1844408abaea5b8fcba8033", hdr);
    hex("f8ba1a55d02f85ae967bb62fb6cda8eb7e78a050", pt);
    CHECK_EQ(esk_80211_header_len(hdr, 24), 24);
    CHECK_EQ(esk_ccmp_encrypt(tk, hdr, 24, UINT64_C(0xB5039776E70C), 0, pt, 20, out), 36);
    CHECK(same(out, "0ce70020769703b5f3d0a2fe9a3dbf2342a643e43246e80c3c04d0197845ce0b16f97623"));
    n = esk_ccmp_decrypt(tk, hdr, 24, out, 36, back, &pn);
    CHECK_EQ(n, 20);
    CHECK_EQ(pn, UINT64_C(0xB5039776E70C));
    CHECK(!memcmp(back, pt, 20));
    out[20] ^= 0x80; /* a flipped bit: the MIC fails */
    CHECK_EQ(esk_ccmp_decrypt(tk, hdr, 24, out, 36, back, &pn), -1);
    /* QoS data: the TID in the nonce and the AAD */
    hex("8841c32c0fd2e128a57c5030f1844408abaea5b8fcba80330500", hdr);
    CHECK_EQ(esk_80211_header_len(hdr, 26), 26);
    esk_ccmp_encrypt(tk, hdr, 26, UINT64_C(0xB5039776E70C), 0, pt, 20, out);
    CHECK(same(out, "0ce70020769703b52a9fcd87ea9d63ab12f683107947fe820ff75008caf0005f81788c19"));
}

TEST_MAIN_BEGIN
RUN(test_sha1_hmac);
RUN(test_psk_prf);
RUN(test_aes_wrap);
RUN(test_ccmp);
TEST_MAIN_END
