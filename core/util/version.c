/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "util/version.h"
#include <string.h>

#define VERSION_MAX 2147483647UL
#define VERSION_TEXT 255u

typedef struct version {
    unsigned long n[3];
    unsigned parts;
    char pre[256];
} version;

static int digit(char c) { return c >= '0' && c <= '9'; }
static int identifier(char c)
{
    return digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
}
static int space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static int suffix(const char **cursor, int prerelease, char *out)
{
    const char *start = *cursor, *p = start;
    for (;;) {
        const char *segment = p;
        int numeric = 1;
        while (identifier(*p)) { if (!digit(*p)) numeric = 0; p++; }
        if (p == segment || (prerelease && numeric && p - segment > 1 && *segment == '0')) return -1;
        if (*p != '.') break;
        p++;
    }
    if (out) { memcpy(out, start, (size_t)(p - start)); out[p - start] = 0; }
    *cursor = p;
    return 0;
}

static int parse(const char **cursor, version *v)
{
    const char *p = *cursor;
    memset(v, 0, sizeof *v);
    do {
        const char *start = p;
        unsigned long n = 0;
        if (!digit(*p)) return -1;
        while (digit(*p)) {
            unsigned long d = (unsigned long)(*p - '0');
            if (n > (VERSION_MAX - d) / 10) return -1;
            n = n * 10 + d; p++;
        }
        if (p - start > 1 && *start == '0') return -1;
        v->n[v->parts++] = n;
        if (*p != '.') break;
        if (v->parts == 3) return -1;
        p++;
    } while (v->parts < 3);
    if (*p == '-') {
        if (v->parts != 3) return -1;
        p++;
        if (suffix(&p, 1, v->pre)) return -1;
    }
    if (*p == '+') {
        if (v->parts != 3) return -1;
        p++;
        if (suffix(&p, 0, NULL)) return -1;
    }
    *cursor = p;
    return 0;
}

static int core(const version *a, const version *b)
{
    unsigned i;
    for (i = 0; i < 3; i++) if (a->n[i] != b->n[i]) return a->n[i] < b->n[i] ? -1 : 1;
    return 0;
}
static int precedence(const version *a, const version *b)
{
    const char *x = a->pre, *y = b->pre;
    int c = core(a, b);
    if (c) return c;
    if (!*x || !*y) return *x ? -1 : *y ? 1 : 0;
    for (;;) {
        const char *xe = strchr(x, '.'), *ye = strchr(y, '.');
        size_t nx = xe ? (size_t)(xe - x) : strlen(x), ny = ye ? (size_t)(ye - y) : strlen(y), i;
        int xn = 1, yn = 1;
        for (i = 0; i < nx; i++) if (!digit(x[i])) xn = 0;
        for (i = 0; i < ny; i++) if (!digit(y[i])) yn = 0;
        if (xn != yn) return xn ? -1 : 1;
        if (xn && nx != ny) return nx < ny ? -1 : 1;
        c = memcmp(x, y, nx < ny ? nx : ny);
        if (c) return c < 0 ? -1 : 1;
        if (nx != ny) return nx < ny ? -1 : 1;
        if (!xe || !ye) return xe ? 1 : ye ? -1 : 0;
        x = xe + 1; y = ye + 1;
    }
}
static int upper(version *v, unsigned at)
{
    unsigned i;
    if (v->n[at] == VERSION_MAX) return -1;
    v->n[at]++;
    for (i = at + 1; i < 3; i++) v->n[i] = 0;
    v->pre[0] = 0;
    return 0;
}

int esk_version_satisfies(const char *text, const char *range)
{
    version actual;
    const char *p;
    unsigned count = 0;
    int match = 1, pre_allowed;
    if (!text || !range || strlen(text) > VERSION_TEXT || strlen(range) > VERSION_TEXT) return -1;
    p = text;
    if (parse(&p, &actual) || *p || actual.parts != 3) return -1;
    pre_allowed = !*actual.pre;
    p = range;
    while (space(*p)) p++;
    while (*p) {
        char op = '=', modifier = 0;
        version v, limit;
        int c, pass;
        if (++count > 16) return -1;
        if (*p == '^' || *p == '~') op = *p++;
        else if (*p == '>' || *p == '<' || *p == '=') {
            op = *p++;
            if (*p == '=') { if (op == '=') return -1; modifier = *p++; }
        }
        while (space(*p)) p++;
        if (parse(&p, &v)) return -1;
        if (*v.pre && !core(&actual, &v)) pre_allowed = 1;
        c = precedence(&actual, &v);
        limit = v;
        if (op == '^' || op == '~' || (op == '=' && v.parts < 3)) {
            unsigned at = v.parts - 1;
            if (op == '~') at = v.parts == 1 ? 0 : 1;
            if (op == '^') { at = 0; while (at + 1 < v.parts && !v.n[at]) at++; }
            if (upper(&limit, at)) return -1;
            pass = c >= 0 && precedence(&actual, &limit) < 0;
        } else if ((op == '>' && !modifier && v.parts < 3) || (op == '<' && modifier && v.parts < 3)) {
            if (upper(&limit, v.parts - 1)) return -1;
            pass = op == '>' ? precedence(&actual, &limit) >= 0 : precedence(&actual, &limit) < 0;
        } else if (op == '>') pass = modifier ? c >= 0 : c > 0;
        else if (op == '<') pass = modifier ? c <= 0 : c < 0;
        else pass = c == 0;
        if (!pass) match = 0;
        if (!*p) break;
        {
            const char *separator = p;
            while (space(*p)) p++;
            if (*p == ',') { p++; while (space(*p)) p++; if (!*p) return -1; }
            else if (p == separator && *p != '<' && *p != '>' && *p != '=') return -1;
        }
    }
    return count ? match && pre_allowed : -1;
}
