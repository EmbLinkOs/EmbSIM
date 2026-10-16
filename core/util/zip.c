/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* zip.c -- a zip archive's entries, read from memory (zip.h).
 *
 * The end of central directory record is found from the end (it may be
 * followed by a comment of up to 64 KiB); each central directory header
 * gives an entry's name, method, sizes, CRC and local header. An entry's
 * data follows its local header, whose name and extra lengths are its own.
 * Every offset and length is checked against the archive's bounds. */
#include <string.h>

#include "util/inflate.h"
#include "util/str.h"
#include "util/zip.h"

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

uint32_t esk_crc32(const uint8_t *data, size_t len)
{
    uint32_t c = 0xffffffffu;
    size_t i;
    int k;
    for (i = 0; i < len; i++) {
        c ^= data[i];
        for (k = 0; k < 8; k++)
            c = c & 1u ? (c >> 1) ^ 0xedb88320u : c >> 1;
    }
    return c ^ 0xffffffffu;
}

static es_status bad(char *why, size_t cap, const char *what)
{
    if (why && cap)
        esk_snprintf(why, cap, "%s", what);
    return ES_EINVAL;
}

void esk_zip_close(esk_zip *z)
{
    size_t i;
    for (i = 0; i < z->n; i++)
        esk_strfree(z->a, z->e[i].name);
    if (z->e)
        esk_free(z->a, z->e, z->cap * sizeof *z->e);
    memset(z, 0, sizeof *z);
}

es_status esk_zip_open(esk_zip *z, esk_alloc *a, const uint8_t *data, size_t len, char *why, size_t cap)
{
    size_t eocd = 0, i, at;
    uint32_t count, cd_size, cd_off;
    int found = 0;
    memset(z, 0, sizeof *z);
    z->a = a;
    z->data = data;
    z->len = len;
    if (len < 22)
        return bad(why, cap, "too short to be a zip archive");
    for (i = len - 22 + 1; i-- > 0 && len - i <= 22 + 0xffffu;) {
        if (le32(data + i) == 0x06054b50u && i + 22 + le16(data + i + 20) == len) {
            eocd = i;
            found = 1;
            break;
        }
    }
    if (!found)
        return bad(why, cap, "no end of central directory record: not a zip archive");
    if (le16(data + eocd + 4) != 0 || le16(data + eocd + 6) != 0)
        return bad(why, cap, "a multi-disk archive");
    count = le16(data + eocd + 10);
    cd_size = le32(data + eocd + 12);
    cd_off = le32(data + eocd + 16);
    if (count == 0xffffu || cd_off == 0xffffffffu)
        return bad(why, cap, "a ZIP64 archive (not read)");
    if ((uint64_t)cd_off + cd_size > eocd)
        return bad(why, cap, "the central directory is outside the archive");
    if (count) {
        z->e = (esk_zip_entry *)esk_calloc(a, count, sizeof *z->e);
        if (!z->e)
            return ES_ENOMEM;
        z->cap = count;
    }
    at = cd_off;
    for (i = 0; i < count; i++) {
        const uint8_t *h = data + at;
        uint32_t nlen, xlen, clen, flags;
        esk_zip_entry *e = &z->e[i];
        if (at + 46 > eocd || le32(h) != 0x02014b50u) {
            esk_zip_close(z);
            return bad(why, cap, "a malformed central directory");
        }
        flags = le16(h + 8);
        nlen = le16(h + 28);
        xlen = le16(h + 30);
        clen = le16(h + 32);
        if (at + 46 + nlen + xlen + clen > eocd) {
            esk_zip_close(z);
            return bad(why, cap, "a malformed central directory");
        }
        e->method = le16(h + 10);
        e->crc = le32(h + 16);
        e->csize = le32(h + 20);
        e->usize = le32(h + 24);
        e->local = le32(h + 42);
        e->name = esk_strndup(a, (const char *)h + 46, nlen);
        z->n = i + 1;
        if (!e->name) {
            esk_zip_close(z);
            return ES_ENOMEM;
        }
        e->dir = nlen > 0 && e->name[nlen - 1] == '/';
        if (flags & 1u) {
            esk_zip_close(z);
            return bad(why, cap, "an encrypted entry");
        }
        if (e->csize == 0xffffffffu || e->usize == 0xffffffffu || e->local == 0xffffffffu) {
            esk_zip_close(z);
            return bad(why, cap, "a ZIP64 entry (not read)");
        }
        at += 46 + nlen + xlen + clen;
    }
    return ES_OK;
}

es_status esk_zip_read(esk_zip *z, size_t i, uint8_t **out, size_t *len, char *why, size_t cap)
{
    const esk_zip_entry *e;
    const uint8_t *h, *src;
    size_t start, got = 0;
    uint8_t *buf;
    es_status st = ES_OK;
    *out = NULL;
    *len = 0;
    if (i >= z->n)
        return ES_EINVAL;
    e = &z->e[i];
    if ((uint64_t)e->local + 30 > z->len || le32(z->data + e->local) != 0x04034b50u)
        return bad(why, cap, "a malformed local header");
    h = z->data + e->local;
    start = (size_t)e->local + 30 + le16(h + 26) + le16(h + 28);
    if ((uint64_t)start + e->csize > z->len)
        return bad(why, cap, "an entry's data is outside the archive");
    src = z->data + start;
    if (e->method != 0 && e->method != 8)
        return bad(why, cap, "an entry compressed by a method other than deflate");
    if (e->method == 0 && e->csize != e->usize)
        return bad(why, cap, "a stored entry whose sizes differ");
    buf = (uint8_t *)esk_malloc(z->a, (size_t)e->usize + 1);
    if (!buf)
        return ES_ENOMEM;
    if (e->method == 0) {
        memcpy(buf, src, e->usize);
        got = e->usize;
    } else {
        st = esk_inflate(src, e->csize, buf, e->usize, &got);
        if (st != ES_OK || got != e->usize) {
            esk_free(z->a, buf, (size_t)e->usize + 1);
            return bad(why, cap, "a deflated entry that does not decode");
        }
    }
    if (esk_crc32(buf, got) != e->crc) {
        esk_free(z->a, buf, (size_t)e->usize + 1);
        return bad(why, cap, "an entry whose CRC-32 does not match");
    }
    buf[got] = 0;
    *out = buf;
    *len = got;
    return ES_OK;
}
