/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* inflate.c -- a raw DEFLATE stream decoded (RFC 1951), for the zip archives
 * a library may come as (zip.c, SPEC-014 §4).
 *
 * Stored, fixed-code and dynamic-code blocks. The output's size is known
 * beforehand (a zip entry's), so the output is one buffer and a back
 * reference copies within it. Codes are decoded a bit at a time from their
 * counts per length (canonical Huffman codes, RFC 1951 §3.2.2): slower than
 * a table, but small, and a library is a few files of text. Every read is
 * bounded: a malformed stream is ES_EINVAL, never a read or write outside
 * the buffers. */
#include <string.h>

#include "util/inflate.h"

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct state {
    const uint8_t *in;
    size_t inlen, inpos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t outlen, outpos;
    int bad;
} state;

/* a code: how many of each length, and the symbols by code */
typedef struct huff {
    short count[MAXBITS + 1];
    short symbol[FIXLCODES];
} huff;

static uint32_t bits(state *s, int need)
{
    uint32_t v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos == s->inlen) {
            s->bad = 1;
            return 0;
        }
        v |= (uint32_t)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return v & ((1u << need) - 1u);
}

/* the counts and symbols for `n` code lengths; 0, or -1 for an
 * over-subscribed set (an incomplete one is allowed, as zlib allows it for a
 * single distance code) */
static int construct(huff *h, const short *length, int n)
{
    short offs[MAXBITS + 1];
    int sym, len, left;
    for (len = 0; len <= MAXBITS; len++)
        h->count[len] = 0;
    for (sym = 0; sym < n; sym++)
        h->count[length[sym]]++;
    if (h->count[0] == n)
        return 0; /* no codes: a stream that uses one is malformed */
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return -1;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++)
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; sym++)
        if (length[sym] != 0)
            h->symbol[offs[length[sym]]++] = (short)sym;
    return 0;
}

static int decode(state *s, const huff *h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len <= MAXBITS; len++) {
        int count;
        code |= (int)bits(s, 1);
        if (s->bad)
            return -1;
        count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->bad = 1;
    return -1;
}

static int stored(state *s)
{
    uint32_t len;
    s->bitbuf = 0;
    s->bitcnt = 0; /* to a byte boundary */
    if (s->inlen - s->inpos < 4)
        return -1;
    len = (uint32_t)s->in[s->inpos] | (uint32_t)s->in[s->inpos + 1] << 8;
    if ((s->in[s->inpos + 2] ^ 0xffu) != s->in[s->inpos] || (s->in[s->inpos + 3] ^ 0xffu) != s->in[s->inpos + 1])
        return -1;
    s->inpos += 4;
    if (s->inlen - s->inpos < len || s->outlen - s->outpos < len)
        return -1;
    memcpy(s->out + s->outpos, s->in + s->inpos, len);
    s->inpos += len;
    s->outpos += len;
    return 0;
}

static const short len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                     3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                     193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                     6145, 8193, 12289, 16385, 24577 };
static const short dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                      7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(state *s, const huff *lencode, const huff *distcode)
{
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0)
            return -1;
        if (sym < 256) {
            if (s->outpos == s->outlen)
                return -1;
            s->out[s->outpos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            size_t len, dist;
            sym -= 257;
            if (sym >= 29)
                return -1;
            len = (size_t)len_base[sym] + bits(s, len_extra[sym]);
            sym = decode(s, distcode);
            if (sym < 0 || sym >= 30)
                return -1;
            dist = (size_t)dist_base[sym] + bits(s, dist_extra[sym]);
            if (s->bad || dist > s->outpos || s->outlen - s->outpos < len)
                return -1;
            while (len--) {
                s->out[s->outpos] = s->out[s->outpos - dist];
                s->outpos++;
            }
        }
    }
}

static int fixed(state *s)
{
    huff lencode, distcode;
    short lengths[FIXLCODES];
    int sym;
    for (sym = 0; sym < 144; sym++)
        lengths[sym] = 8;
    for (; sym < 256; sym++)
        lengths[sym] = 9;
    for (; sym < 280; sym++)
        lengths[sym] = 7;
    for (; sym < FIXLCODES; sym++)
        lengths[sym] = 8;
    (void)construct(&lencode, lengths, FIXLCODES);
    for (sym = 0; sym < MAXDCODES; sym++)
        lengths[sym] = 5;
    (void)construct(&distcode, lengths, MAXDCODES);
    return codes(s, &lencode, &distcode);
}

static int dynamic(state *s)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[MAXLCODES + MAXDCODES];
    huff lencode, distcode;
    int nlen, ndist, ncode, index;
    nlen = (int)bits(s, 5) + 257;
    ndist = (int)bits(s, 5) + 1;
    ncode = (int)bits(s, 4) + 4;
    if (s->bad || nlen > MAXLCODES || ndist > MAXDCODES)
        return -1;
    for (index = 0; index < ncode; index++)
        lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++)
        lengths[order[index]] = 0;
    if (s->bad || construct(&lencode, lengths, 19) != 0)
        return -1;
    index = 0;
    while (index < nlen + ndist) {
        int sym = decode(s, &lencode), len = 0, rep;
        if (sym < 0)
            return -1;
        if (sym < 16) {
            lengths[index++] = (short)sym;
            continue;
        }
        if (sym == 16) {
            if (index == 0)
                return -1;
            len = lengths[index - 1];
            rep = 3 + (int)bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + (int)bits(s, 3);
        } else {
            rep = 11 + (int)bits(s, 7);
        }
        if (s->bad || index + rep > nlen + ndist)
            return -1;
        while (rep--)
            lengths[index++] = (short)len;
    }
    if (lengths[256] == 0)
        return -1; /* no end-of-block code */
    if (construct(&lencode, lengths, nlen) != 0 || construct(&distcode, lengths + nlen, ndist) != 0)
        return -1;
    return codes(s, &lencode, &distcode);
}

es_status esk_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen, size_t *produced)
{
    state s;
    int last;
    memset(&s, 0, sizeof s);
    s.in = in;
    s.inlen = inlen;
    s.out = out;
    s.outlen = outlen;
    do {
        uint32_t type;
        int r;
        last = (int)bits(&s, 1);
        type = bits(&s, 2);
        if (s.bad)
            return ES_EINVAL;
        r = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (r != 0 || s.bad)
            return ES_EINVAL;
    } while (!last);
    if (produced)
        *produced = s.outpos;
    return ES_OK;
}
