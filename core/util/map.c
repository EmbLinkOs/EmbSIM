/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "util/map.h"
#include <string.h>

static unsigned hash_str(const char *s)
{
    unsigned h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h ? h : 1;
}

void esk_map_init(esk_map *m, esk_alloc *a)
{
    memset(m, 0, sizeof *m);
    m->a = a;
}

void esk_map_free(esk_map *m)
{
    size_t i;
    for (i = 0; i < m->cap; i++)
        if (m->slots[i].key)
            esk_strfree(m->a, m->slots[i].key);
    esk_free(m->a, m->slots, m->cap * sizeof *m->slots);
    m->slots = NULL;
    m->cap = m->len = 0;
}

static int grow(esk_map *m)
{
    size_t ncap = m->cap ? m->cap * 2 : 16, i;
    esk_map_entry *ns = (esk_map_entry *)esk_calloc(m->a, ncap, sizeof *ns);
    if (!ns)
        return -1;
    for (i = 0; i < m->cap; i++) {
        esk_map_entry *e = &m->slots[i];
        size_t j;
        if (!e->key)
            continue;
        j = e->hash & (ncap - 1);
        while (ns[j].key)
            j = (j + 1) & (ncap - 1);
        ns[j] = *e;
    }
    esk_free(m->a, m->slots, m->cap * sizeof *m->slots);
    m->slots = ns;
    m->cap = ncap;
    return 0;
}

static esk_map_entry *find(const esk_map *m, const char *key, unsigned h)
{
    size_t j;
    if (!m->cap)
        return NULL;
    j = h & (m->cap - 1);
    for (;;) {
        esk_map_entry *e = &m->slots[j];
        if (!e->key)
            return NULL;
        if (e->hash == h && !strcmp(e->key, key))
            return e;
        j = (j + 1) & (m->cap - 1);
    }
}

int esk_map_set(esk_map *m, const char *key, void *value)
{
    unsigned h = hash_str(key);
    esk_map_entry *e = find(m, key, h);
    size_t j;
    if (e) {
        e->value = value;
        return 0;
    }
    if ((m->len + 1) * 4 >= m->cap * 3 && grow(m))
        return -1;
    j = h & (m->cap - 1);
    while (m->slots[j].key)
        j = (j + 1) & (m->cap - 1);
    m->slots[j].key = esk_strdup(m->a, key);
    if (!m->slots[j].key)
        return -1;
    m->slots[j].value = value;
    m->slots[j].hash = h;
    m->len++;
    return 0;
}

void *esk_map_get(const esk_map *m, const char *key)
{
    esk_map_entry *e = key ? find(m, key, hash_str(key)) : NULL;
    return e ? e->value : NULL;
}

int esk_map_has(const esk_map *m, const char *key) { return key && find(m, key, hash_str(key)) != NULL; }

int esk_map_next(const esk_map *m, size_t *i, const char **key, void **value)
{
    while (*i < m->cap) {
        esk_map_entry *e = &m->slots[(*i)++];
        if (e->key) {
            *key = e->key;
            *value = e->value;
            return 1;
        }
    }
    return 0;
}
