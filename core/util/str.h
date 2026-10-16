/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* str.h -- a growable string buffer and small string helpers (CS-19). */
#ifndef ESK_STR_H
#define ESK_STR_H

#include <stdarg.h>
#include <stddef.h>
#include "util/alloc.h"

typedef struct esk_strbuf {
    esk_alloc *a;
    char *data;
    size_t len, cap;
    int oom; /* set once an append failed; the buffer is then unusable */
} esk_strbuf;

void esk_strbuf_init(esk_strbuf *b, esk_alloc *a);
void esk_strbuf_free(esk_strbuf *b);
int esk_strbuf_append(esk_strbuf *b, const char *s, size_t n);
int esk_strbuf_puts(esk_strbuf *b, const char *s);
int esk_strbuf_putc(esk_strbuf *b, char c);
int esk_strbuf_printf(esk_strbuf *b, const char *fmt, ...);
/* detach the buffer as a NUL-terminated string owned by the allocator (free with esk_free(a, p, len+1)) */
char *esk_strbuf_detach(esk_strbuf *b, size_t *len);
void esk_strbuf_clear(esk_strbuf *b);

int esk_str_eq(const char *a, const char *b);
int esk_str_eqn(const char *a, size_t alen, const char *b);
/* glob match with `*` (any run) and `?` (one char) */
int esk_glob_match(const char *pattern, const char *s);
/* a safe snprintf that always terminates and returns the length written (truncated) */
size_t esk_snprintf(char *buf, size_t cap, const char *fmt, ...);
int esk_str_has_prefix(const char *s, const char *prefix);
/* identifier rule of SPEC-005 §1: [A-Za-z][A-Za-z0-9_-]{0,63} */
int esk_is_identifier(const char *s);

#endif
