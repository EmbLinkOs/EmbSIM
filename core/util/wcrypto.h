/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* wcrypto.h -- the cryptography of Wi-Fi's WPA2-Personal, as the standards
 * give it, for the bench's access points and the chips' CCMP hardware
 * (docs/engineering/c3-radios.md): SHA-1 (FIPS 180-4), HMAC (RFC 2104),
 * PBKDF2 (RFC 8018), IEEE 802.11's PRF (12.7.1.2), AES-128 (FIPS 197), the
 * AES key wrap (RFC 3394) and CCMP (IEEE 802.11-2020 12.5.3). */
#ifndef ESK_WCRYPTO_H
#define ESK_WCRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define ESK_SHA1_LEN 20

typedef struct esk_sha1 {
    uint32_t h[5];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} esk_sha1;

void esk_sha1_init(esk_sha1 *c);
void esk_sha1_update(esk_sha1 *c, const void *data, size_t len);
void esk_sha1_final(esk_sha1 *c, unsigned char out[ESK_SHA1_LEN]);
void esk_hmac_sha1(const unsigned char *key, size_t key_len, const unsigned char *data, size_t len,
                   unsigned char out[ESK_SHA1_LEN]);
/* PBKDF2 with HMAC-SHA1: WPA2's PSK from its passphrase and SSID is
 * esk_pbkdf2_sha1(pass, ssid, 4096 iterations, 32 bytes) */
void esk_pbkdf2_sha1(const unsigned char *pass, size_t pass_len, const unsigned char *salt, size_t salt_len,
                     unsigned iterations, unsigned char *out, size_t out_len);
/* IEEE 802.11's PRF-n with HMAC-SHA1: `label`, a 0 byte, `data`, a counter */
void esk_prf_sha1(const unsigned char *key, size_t key_len, const char *label, const unsigned char *data,
                  size_t data_len, unsigned char *out, size_t out_len);

typedef struct esk_aes {
    uint32_t rk[60];
    int nr; /* rounds: 10, 12 or 14 */
} esk_aes;

/* a 16-, 24- or 32-byte key's schedule */
void esk_aes_init(esk_aes *a, const unsigned char *key, size_t key_bytes);
void esk_aes_encrypt(const esk_aes *a, const unsigned char in[16], unsigned char out[16]);
void esk_aes_decrypt(const esk_aes *a, const unsigned char in[16], unsigned char out[16]);
/* RFC 3394: `n` 64-bit blocks of `in` wrapped under `kek` into n + 1 blocks */
void esk_aes_wrap(const unsigned char kek[16], const unsigned char *in, size_t n, unsigned char *out);
/* its inverse: 0, or -1 when the integrity check fails */
int esk_aes_unwrap(const unsigned char kek[16], const unsigned char *in, size_t n, unsigned char *out);

/* CCMP: an 802.11 data or management frame's MPDU (`hdr`, `hdr_len` bytes,
 * its Protected bit set or not) encrypted under the temporal key `tk` with
 * packet number `pn` (48 bits) and key ID: `out` gets the CCMP header (8
 * bytes), the encrypted `plain` and the 8-byte MIC; returns its length */
size_t esk_ccmp_encrypt(const unsigned char tk[16], const unsigned char *hdr, size_t hdr_len, uint64_t pn, int key_id,
                        const unsigned char *plain, size_t len, unsigned char *out);
/* a protected MPDU's body (CCMP header, ciphertext, MIC: `len` bytes after
 * the header) decrypted: the plaintext's length and its PN, or -1 when the
 * MIC does not match */
long esk_ccmp_decrypt(const unsigned char tk[16], const unsigned char *hdr, size_t hdr_len, const unsigned char *body,
                      size_t len, unsigned char *plain, uint64_t *pn);
/* the length of an 802.11 MAC header (address 4 with both DS bits, QoS
 * control for QoS data, HT control with Order on QoS data) */
size_t esk_80211_header_len(const unsigned char *f, size_t len);

#endif
