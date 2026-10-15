/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* sanity.c -- the implementation-defined behaviour the core relies on (CS-11). */
#include <limits.h>
#include <stdint.h>
#include "embsim/compiler.h"

ES_STATIC_ASSERT(CHAR_BIT == 8, char_bit);
ES_STATIC_ASSERT((-1 & 3) == 3, twos_complement);
ES_STATIC_ASSERT(sizeof(double) == 8, ieee_double);
ES_STATIC_ASSERT(sizeof(void *) >= 4, pointer_width);
ES_STATIC_ASSERT(sizeof(uint64_t) == 8, u64);

int esk_sanity_marker(void);
int esk_sanity_marker(void) { return 1; }
