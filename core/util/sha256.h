/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* sha256.h -- SHA-256 (FIPS 180-4), for the identities a recording keeps of
 * its project and images (SPEC-012 §1). */
#ifndef ESK_SHA256_H
#define ESK_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define ESK_SHA256_LEN 32

typedef struct esk_sha256 {
    uint32_t h[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} esk_sha256;

void esk_sha256_init(esk_sha256 *c);
/* the compression function: one 64-byte block into the state `h` (an
 * accelerator's model computes with it) */
void esk_sha256_block(uint32_t h[8], const unsigned char *p);
void esk_sha256_update(esk_sha256 *c, const void *data, size_t len);
void esk_sha256_final(esk_sha256 *c, unsigned char out[ESK_SHA256_LEN]);
/* "sha256:" and the digest of `len` bytes in lower-case hex: 72 characters
 * and a NUL */
void esk_sha256_text(const void *data, size_t len, char out[72]);

#endif
