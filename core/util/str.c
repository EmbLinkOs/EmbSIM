/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "util/str.h"
#include <stdio.h>
#include <string.h>

void esk_strbuf_init(esk_strbuf *b, esk_alloc *a)
{
    b->a = a;
    b->data = NULL;
    b->len = b->cap = 0;
    b->oom = 0;
}

void esk_strbuf_free(esk_strbuf *b)
{
    esk_free(b->a, b->data, b->cap);
    b->data = NULL;
    b->len = b->cap = 0;
}

static int reserve(esk_strbuf *b, size_t need)
{
    size_t ncap;
    char *p;
    if (b->oom)
        return -1;
    if (need <= b->cap)
        return 0;
    ncap = b->cap ? b->cap : 64;
    while (ncap < need) {
        if (ncap > ((size_t)-1) / 2) {
            b->oom = 1;
            return -1;
        }
        ncap *= 2;
    }
    p = (char *)esk_realloc(b->a, b->data, b->cap, ncap);
    if (!p) {
        b->oom = 1;
        return -1;
    }
    b->data = p;
    b->cap = ncap;
    return 0;
}

int esk_strbuf_append(esk_strbuf *b, const char *s, size_t n)
{
    if (reserve(b, b->len + n + 1))
        return -1;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
    return 0;
}

int esk_strbuf_puts(esk_strbuf *b, const char *s) { return esk_strbuf_append(b, s, strlen(s)); }
int esk_strbuf_putc(esk_strbuf *b, char c) { return esk_strbuf_append(b, &c, 1); }

int esk_strbuf_printf(esk_strbuf *b, const char *fmt, ...)
{
    va_list ap;
    int n;
    char tmp[256];
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return -1;
    if ((size_t)n < sizeof tmp)
        return esk_strbuf_append(b, tmp, (size_t)n);
    if (reserve(b, b->len + (size_t)n + 1))
        return -1;
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    return 0;
}

char *esk_strbuf_detach(esk_strbuf *b, size_t *len)
{
    char *p;
    if (b->oom) {
        esk_strbuf_free(b);
        if (len)
            *len = 0;
        return NULL;
    }
    if (!b->data) {
        p = (char *)esk_malloc(b->a, 1);
        if (p)
            p[0] = 0;
        if (len)
            *len = 0;
        return p;
    }
    /* shrink so the owner can free with len + 1 */
    p = (char *)esk_realloc(b->a, b->data, b->cap, b->len + 1);
    if (!p)
        p = b->data;
    if (len)
        *len = b->len;
    b->data = NULL;
    b->len = b->cap = 0;
    return p;
}

void esk_strbuf_clear(esk_strbuf *b)
{
    b->len = 0;
    if (b->data)
        b->data[0] = 0;
}

int esk_str_eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

int esk_str_eqn(const char *a, size_t alen, const char *b)
{
    return strlen(b) == alen && memcmp(a, b, alen) == 0;
}

int esk_glob_match(const char *p, const char *s)
{
    while (*p) {
        if (*p == '*') {
            while (*p == '*')
                p++;
            if (!*p)
                return 1;
            while (*s) {
                if (esk_glob_match(p, s))
                    return 1;
                s++;
            }
            return 0;
        }
        if (!*s || (*p != '?' && *p != *s))
            return 0;
        p++;
        s++;
    }
    return *s == 0;
}

size_t esk_snprintf(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (!cap)
        return 0;
    va_start(ap, fmt);
    n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    if (n < 0) {
        buf[0] = 0;
        return 0;
    }
    return (size_t)n < cap ? (size_t)n : cap - 1;
}

int esk_str_has_prefix(const char *s, const char *prefix)
{
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

int esk_is_identifier(const char *s)
{
    size_t i;
    if (!s || !*s)
        return 0;
    if (!((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')))
        return 0;
    for (i = 1; s[i]; i++) {
        char c = s[i];
        if (i > 63)
            return 0;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}
