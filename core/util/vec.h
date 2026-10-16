/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* vec.h -- a growable array of any element type, allocated through an
 * esk_alloc. Usage: ESK_VEC(int) v = ESK_VEC_INIT; esk_vec_push(a, &v, 3). */
#ifndef ESK_VEC_H
#define ESK_VEC_H

#include <string.h>
#include "util/alloc.h"

#define ESK_VEC(T) struct { T *data; size_t len, cap; }
#define ESK_VEC_INIT { NULL, 0, 0 }

/* returns 0 on success, -1 on allocation failure */
int esk_vec_grow_(esk_alloc *a, void **data, size_t *cap, size_t elem, size_t want);

#define esk_vec_reserve(a, v, n) esk_vec_grow_((a), (void **)&(v)->data, &(v)->cap, sizeof *(v)->data, (n))
#define esk_vec_push(a, v, x) \
    ((esk_vec_reserve((a), (v), (v)->len + 1) == 0) ? ((v)->data[(v)->len++] = (x), 0) : -1)
#define esk_vec_free(a, v) \
    do { esk_free((a), (v)->data, (v)->cap * sizeof *(v)->data); (v)->data = NULL; (v)->len = (v)->cap = 0; } while (0)
#define esk_vec_clear(v) ((v)->len = 0)
#define esk_vec_last(v) ((v)->data[(v)->len - 1])
#define esk_vec_remove(v, i) \
    do { if ((i) + 1 < (v)->len) memmove(&(v)->data[(i)], &(v)->data[(i) + 1], ((v)->len - (i) - 1) * sizeof *(v)->data); (v)->len--; } while (0)

#endif
