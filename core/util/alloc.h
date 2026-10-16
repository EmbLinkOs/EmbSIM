/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* alloc.h -- the session's allocator (SPEC-001 §4, CS-16). Every allocation
 * in the core goes through an esk_alloc so an embedder can replace it and a
 * session can account for its memory. */
#ifndef ESK_ALLOC_H
#define ESK_ALLOC_H

#include <stddef.h>
#include "embsim/types.h"

typedef struct esk_alloc {
    es_allocator_t hooks;
    size_t live_bytes; /* approximate: counts requested sizes */
    size_t allocations;
} esk_alloc;

void esk_alloc_init(esk_alloc *a, const es_allocator_t *hooks);
void *esk_malloc(esk_alloc *a, size_t n);
void *esk_calloc(esk_alloc *a, size_t count, size_t size);
void *esk_realloc(esk_alloc *a, void *p, size_t old_n, size_t new_n);
void esk_free(esk_alloc *a, void *p, size_t n);
char *esk_strdup(esk_alloc *a, const char *s);
char *esk_strndup(esk_alloc *a, const char *s, size_t n);
/* free a string made by esk_strdup/esk_strndup */
void esk_strfree(esk_alloc *a, char *s);

#endif
