/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* inflate.h -- a raw DEFLATE stream (RFC 1951) decoded into a buffer of the
 * size the container says it holds (zip.h). */
#ifndef ESK_INFLATE_H
#define ESK_INFLATE_H

#include <stddef.h>
#include <stdint.h>
#include "embsim/types.h"

/* in[0..inlen) into out[0..outlen): ES_OK and the bytes written, or
 * ES_EINVAL for a malformed stream or one that would not fit */
es_status esk_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen, size_t *produced);

#endif
