/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* wsutil.c -- SHA-1, base64, the handshake accept key and the frame header
 * codec of RFC 6455 §5.2. */
#include "protocol/wsutil.h"
#include <string.h>

/* ---- SHA-1 (RFC 3174) ---------------------------------------------------- */

static uint32_t rol(uint32_t v, unsigned n) { return (v << n) | (v >> (32u - n)); }

static void sha1_block(uint32_t h[5], const unsigned char *p)
{
    uint32_t w[80], a, b, c, d, e;
    unsigned i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) |
               (uint32_t)p[4 * i + 3];
    for (i = 16; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = h[0];
    b = h[1];
    c = h[2];
    d = h[3];
    e = h[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k, t;
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
        t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void esk_sha1(const unsigned char *data, size_t len, unsigned char out[ESK_SHA1_DIGEST_LEN])
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    unsigned char tail[128];
    size_t i, rem, tail_len;
    uint64_t bits = (uint64_t)len * 8u;
    for (i = 0; i + 64 <= len; i += 64)
        sha1_block(h, data + i);
    rem = len - i;
    memset(tail, 0, sizeof tail);
    if (rem)
        memcpy(tail, data + i, rem);
    tail[rem] = 0x80;
    tail_len = rem + 1 <= 56 ? 64 : 128;
    for (i = 0; i < 8; i++)
        tail[tail_len - 1 - i] = (unsigned char)(bits >> (8 * i));
    sha1_block(h, tail);
    if (tail_len == 128)
        sha1_block(h, tail + 64);
    for (i = 0; i < 5; i++) {
        out[4 * i] = (unsigned char)(h[i] >> 24);
        out[4 * i + 1] = (unsigned char)(h[i] >> 16);
        out[4 * i + 2] = (unsigned char)(h[i] >> 8);
        out[4 * i + 3] = (unsigned char)h[i];
    }
}

/* ---- base64 (RFC 4648) --------------------------------------------------- */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t esk_base64_encode(const unsigned char *in, size_t n, char *out, size_t cap)
{
    size_t need = 4 * ((n + 2) / 3), i, o = 0;
    if (!out || cap < need + 1)
        return need;
    for (i = 0; i + 3 <= n; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = B64[(v >> 6) & 63];
        out[o++] = B64[v & 63];
    }
    if (n - i == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (n - i == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = B64[(v >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = 0;
    return o;
}

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

size_t esk_base64_decode(const char *in, size_t n, unsigned char *out, size_t cap)
{
    size_t i, o = 0;
    uint32_t acc = 0;
    unsigned bits = 0;
    while (n > 0 && in[n - 1] == '=')
        n--;
    for (i = 0; i < n; i++) {
        int v = b64_value(in[i]);
        if (v < 0)
            return (size_t)-1;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap)
                return (size_t)-1;
            out[o++] = (unsigned char)((acc >> bits) & 0xFFu);
        }
    }
    if (bits >= 6)
        return (size_t)-1; /* a dangling character */
    return o;
}

/* ---- handshake ----------------------------------------------------------- */

int esk_ws_accept_key(const char *key, size_t key_len, char out[ESK_WS_ACCEPT_LEN + 1])
{
    static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char buf[128], digest[ESK_SHA1_DIGEST_LEN];
    if (!key || key_len == 0 || key_len + sizeof GUID - 1 > sizeof buf)
        return -1;
    memcpy(buf, key, key_len);
    memcpy(buf + key_len, GUID, sizeof GUID - 1);
    esk_sha1(buf, key_len + sizeof GUID - 1, digest);
    esk_base64_encode(digest, sizeof digest, out, ESK_WS_ACCEPT_LEN + 1);
    return 0;
}

/* ---- frames -------------------------------------------------------------- */

size_t esk_ws_encode_header(unsigned char out[ESK_WS_HEADER_MAX], int fin, int opcode, uint64_t payload_len)
{
    size_t n = 2;
    out[0] = (unsigned char)((fin ? 0x80u : 0u) | ((unsigned)opcode & 0x0Fu));
    if (payload_len < 126) {
        out[1] = (unsigned char)payload_len;
    } else if (payload_len <= 0xFFFFu) {
        out[1] = 126;
        out[2] = (unsigned char)(payload_len >> 8);
        out[3] = (unsigned char)payload_len;
        n = 4;
    } else {
        unsigned i;
        out[1] = 127;
        for (i = 0; i < 8; i++)
            out[2 + i] = (unsigned char)(payload_len >> (8 * (7 - i)));
        n = 10;
    }
    return n;
}

int esk_ws_decode_header(const unsigned char *buf, size_t len, esk_ws_frame *f)
{
    size_t need = 2;
    unsigned len7;
    if (len < 2)
        return 1;
    memset(f, 0, sizeof *f);
    if (buf[0] & 0x70u)
        return -1; /* RSV bits without an extension */
    f->fin = (buf[0] & 0x80u) != 0;
    f->opcode = buf[0] & 0x0Fu;
    f->masked = (buf[1] & 0x80u) != 0;
    len7 = buf[1] & 0x7Fu;
    if ((f->opcode > 0x2 && f->opcode < 0x8) || f->opcode > 0xA)
        return -1;
    if (f->opcode >= 0x8 && (!f->fin || len7 > 125))
        return -1;
    if (len7 == 126)
        need += 2;
    else if (len7 == 127)
        need += 8;
    if (f->masked)
        need += 4;
    if (len < need)
        return 1;
    if (len7 < 126) {
        f->payload_len = len7;
    } else if (len7 == 126) {
        f->payload_len = ((uint64_t)buf[2] << 8) | buf[3];
    } else {
        unsigned i;
        f->payload_len = 0;
        for (i = 0; i < 8; i++)
            f->payload_len = (f->payload_len << 8) | buf[2 + i];
        if (f->payload_len >> 63)
            return -1;
    }
    if (f->masked)
        memcpy(f->mask, buf + need - 4, 4);
    f->header_len = need;
    return 0;
}

void esk_ws_unmask(const esk_ws_frame *f, unsigned char *payload, size_t n)
{
    size_t i;
    if (!f->masked)
        return;
    for (i = 0; i < n; i++)
        payload[i] ^= f->mask[i & 3u];
}
