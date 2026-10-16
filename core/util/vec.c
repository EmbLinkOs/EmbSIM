/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "util/vec.h"

int esk_vec_grow_(esk_alloc *a, void **data, size_t *cap, size_t elem, size_t want)
{
    size_t ncap;
    void *p;
    if (want <= *cap)
        return 0;
    ncap = *cap ? *cap : 8;
    while (ncap < want) {
        if (ncap > ((size_t)-1) / 2)
            return -1;
        ncap *= 2;
    }
    if (ncap > ((size_t)-1) / elem)
        return -1;
    p = esk_realloc(a, *data, *cap * elem, ncap * elem);
    if (!p)
        return -1;
    *data = p;
    *cap = ncap;
    return 0;
}
