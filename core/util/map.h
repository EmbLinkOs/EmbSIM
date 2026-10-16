/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* map.h -- a string-keyed hash map of pointers (open addressing). Keys are
 * copied. */
#ifndef ESK_MAP_H
#define ESK_MAP_H

#include <stddef.h>
#include "util/alloc.h"

typedef struct esk_map_entry {
    char *key;
    void *value;
    unsigned hash;
} esk_map_entry;

typedef struct esk_map {
    esk_alloc *a;
    esk_map_entry *slots;
    size_t cap, len;
} esk_map;

void esk_map_init(esk_map *m, esk_alloc *a);
void esk_map_free(esk_map *m);
/* insert or replace; returns 0 or -1 (allocation) */
int esk_map_set(esk_map *m, const char *key, void *value);
void *esk_map_get(const esk_map *m, const char *key);
int esk_map_has(const esk_map *m, const char *key);
/* iterate: i from 0; returns 1 and fills key/value while entries remain */
int esk_map_next(const esk_map *m, size_t *i, const char **key, void **value);

#endif
