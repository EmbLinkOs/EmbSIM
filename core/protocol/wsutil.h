/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* wsutil.h -- the pure parts of the WebSocket endpoint (RFC 6455): SHA-1
 * (RFC 3174) and base64 (RFC 4648) for the handshake, the Sec-WebSocket-
 * Accept computation, and frame header encoding/decoding. No I/O, no
 * allocation, no OS calls: testable on their own. */
#ifndef ESK_WSUTIL_H
#define ESK_WSUTIL_H

#include <stddef.h>
#include <stdint.h>

#define ESK_SHA1_DIGEST_LEN 20
#define ESK_WS_ACCEPT_LEN 28 /* base64 of 20 bytes, without the NUL */

void esk_sha1(const unsigned char *data, size_t len, unsigned char out[ESK_SHA1_DIGEST_LEN]);

/* Encode `n` bytes. Returns the encoded length (without the NUL); writes
 * the text plus a NUL when cap is large enough (4 * ceil(n / 3) + 1). */
size_t esk_base64_encode(const unsigned char *in, size_t n, char *out, size_t cap);
/* Decode `n` characters (padding optional, whitespace rejected). Returns
 * the decoded length, or (size_t)-1 on bad input or when cap is too small. */
size_t esk_base64_decode(const char *in, size_t n, unsigned char *out, size_t cap);

/* Sec-WebSocket-Accept for a client key: base64(sha1(key + GUID)); writes
 * ESK_WS_ACCEPT_LEN characters plus a NUL. Returns 0, or -1 on a bad key. */
int esk_ws_accept_key(const char *key, size_t key_len, char out[ESK_WS_ACCEPT_LEN + 1]);

enum {
    ESK_WS_OP_CONTINUATION = 0x0,
    ESK_WS_OP_TEXT = 0x1,
    ESK_WS_OP_BINARY = 0x2,
    ESK_WS_OP_CLOSE = 0x8,
    ESK_WS_OP_PING = 0x9,
    ESK_WS_OP_PONG = 0xA
};

typedef struct esk_ws_frame {
    int fin;
    int opcode;
    int masked;
    unsigned char mask[4];
    size_t header_len;    /* bytes before the payload */
    uint64_t payload_len; /* bytes of payload */
} esk_ws_frame;

#define ESK_WS_HEADER_MAX 14 /* 2 + 8 + 4 */

/* Encode a server-to-client (unmasked) frame header. Returns its length. */
size_t esk_ws_encode_header(unsigned char out[ESK_WS_HEADER_MAX], int fin, int opcode, uint64_t payload_len);
/* Parse a frame header from `len` buffered bytes. Returns 0 when complete
 * (f filled), 1 when more bytes are needed, -1 on a protocol violation
 * (reserved bits, bad opcode, a control frame that is fragmented or longer
 * than 125 bytes, a 64-bit length with the top bit set). */
int esk_ws_decode_header(const unsigned char *buf, size_t len, esk_ws_frame *f);
/* Unmask `n` payload bytes in place with the frame's mask. */
void esk_ws_unmask(const esk_ws_frame *f, unsigned char *payload, size_t n);

#endif
