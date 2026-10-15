/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* num.h -- numbers as descriptions write them: SI suffixes (SPEC-005 §4),
 * times with units (SPEC-007 §2), addresses and sizes with 0x/K/M
 * (SPEC-006 §1), and picosecond formatting. */
#ifndef ESK_NUM_H
#define ESK_NUM_H

#include <stddef.h>
#include <stdint.h>
#include "embsim/types.h"
#include "util/json.h"

/* "4.7k", "100n", "2.2u", "1meg", "10", "3.3"; returns 0 or -1 */
int esk_parse_si(const char *s, double *out);
/* a JSON number or a string with an SI suffix */
int esk_json_to_double(const esk_json *j, double *out);
/* "10ms", "1.5us", "12345ps", "2s"; exact; -1 on sub-picosecond fractions or bad text */
int esk_parse_time(const char *s, es_time_t *out);
/* a JSON string (unit or bare picoseconds) or integer (picoseconds) */
int esk_json_to_time(const esk_json *j, es_time_t *out);
/* "0x08000000", "64K", "2M", "1024"; returns 0 or -1 */
int esk_parse_u64(const char *s, uint64_t *out);
int esk_json_to_u64(const esk_json *j, uint64_t *out);
/* picoseconds as "12.345 ms" style text (for humans) */
size_t esk_format_time(char *buf, size_t cap, es_time_t ps);
/* checked time arithmetic (CS-6) */
int esk_time_add(es_time_t a, es_time_t b, es_time_t *out);

#endif
