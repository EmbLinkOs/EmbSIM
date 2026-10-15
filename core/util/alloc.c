/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "util/alloc.h"
#include <stdlib.h>
#include <string.h>

static void *default_alloc(void *ctx, size_t n) { ES_UNUSED(ctx); return malloc(n ? n : 1); }
static void *default_realloc(void *ctx, void *p, size_t n) { ES_UNUSED(ctx); return realloc(p, n ? n : 1); }
static void default_free(void *ctx, void *p) { ES_UNUSED(ctx); free(p); }

void esk_alloc_init(esk_alloc *a, const es_allocator_t *hooks)
{
    memset(a, 0, sizeof *a);
    if (hooks && hooks->alloc && hooks->realloc && hooks->free) {
        a->hooks = *hooks;
    } else {
        a->hooks.alloc = default_alloc;
        a->hooks.realloc = default_realloc;
        a->hooks.free = default_free;
        a->hooks.ctx = NULL;
    }
}

void *esk_malloc(esk_alloc *a, size_t n)
{
    void *p = a->hooks.alloc(a->hooks.ctx, n);
    if (p) {
        a->live_bytes += n;
        a->allocations++;
    }
    return p;
}

void *esk_calloc(esk_alloc *a, size_t count, size_t size)
{
    size_t n;
    void *p;
    if (size && count > ((size_t)-1) / size)
        return NULL;
    n = count * size;
    p = esk_malloc(a, n);
    if (p)
        memset(p, 0, n);
    return p;
}

void *esk_realloc(esk_alloc *a, void *p, size_t old_n, size_t new_n)
{
    void *q = a->hooks.realloc(a->hooks.ctx, p, new_n);
    if (q) {
        a->live_bytes = a->live_bytes - old_n + new_n;
        if (!p)
            a->allocations++;
    }
    return q;
}

void esk_free(esk_alloc *a, void *p, size_t n)
{
    if (!p)
        return;
    a->hooks.free(a->hooks.ctx, p);
    a->live_bytes -= n;
    a->allocations--;
}

char *esk_strndup(esk_alloc *a, const char *s, size_t n)
{
    char *d = (char *)esk_malloc(a, n + 1);
    if (!d)
        return NULL;
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

char *esk_strdup(esk_alloc *a, const char *s) { return esk_strndup(a, s, strlen(s)); }

void esk_strfree(esk_alloc *a, char *s)
{
    if (s)
        esk_free(a, s, strlen(s) + 1);
}
