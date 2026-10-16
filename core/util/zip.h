/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* zip.h -- the entries of a zip archive held in memory (APPNOTE 6.3), for a
 * library distributed as one (SPEC-014 §4): stored and deflated entries,
 * each checked against its CRC-32. ZIP64, encryption and multi-disk
 * archives are refused. */
#ifndef ESK_ZIP_H
#define ESK_ZIP_H

#include <stddef.h>
#include <stdint.h>
#include "util/alloc.h"

typedef struct esk_zip_entry {
    char *name;      /* as the archive names it, '/'-separated (esk_strdup) */
    uint32_t method; /* 0 stored, 8 deflated */
    uint32_t crc, csize, usize, local;
    int dir;         /* a directory's entry (its name ends in '/') */
} esk_zip_entry;

typedef struct esk_zip {
    esk_alloc *a;
    const uint8_t *data;
    size_t len;
    esk_zip_entry *e;
    size_t n, cap; /* entries read, and allocated */
} esk_zip;

/* the archive `data` (which must outlive z): ES_EINVAL when it is not a zip
 * archive this reads, `why` saying what */
es_status esk_zip_open(esk_zip *z, esk_alloc *a, const uint8_t *data, size_t len, char *why, size_t cap);
void esk_zip_close(esk_zip *z);
/* entry i's contents: *out gets usize + 1 bytes (a NUL after them; release
 * with esk_free(a, *out, *len + 1)) */
es_status esk_zip_read(esk_zip *z, size_t i, uint8_t **out, size_t *len, char *why, size_t cap);
/* CRC-32 (ISO 3309, the zip and PNG one) of data */
uint32_t esk_crc32(const uint8_t *data, size_t len);

#endif
