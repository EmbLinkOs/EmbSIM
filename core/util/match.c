/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* match.c -- see match.h. A backtracking matcher over a pattern compiled
 * into a short list of atoms. */
#include "util/match.h"

#include <string.h>

#define ATOMS_MAX 96

enum { A_CHAR, A_ANY, A_CLASS, A_GROUP_OPEN, A_GROUP_CLOSE, A_END };

typedef struct atom {
    int kind;
    unsigned char c;        /* A_CHAR */
    unsigned char set[32];  /* A_CLASS: a bit per byte */
    char min, max;          /* repeats: min 0/1, max 1 or -1 (unbounded) */
} atom;

typedef struct prog {
    atom a[ATOMS_MAX];
    int n;
    int anchored, at_end, groups;
} prog;

typedef struct run {
    const prog *p;
    const char *text;
    size_t len;
    size_t gs, ge;
} run;

static void set_bit(unsigned char *set, unsigned char c) { set[c >> 3] |= (unsigned char)(1u << (c & 7u)); }
static int has_bit(const unsigned char *set, unsigned char c) { return (int)(((unsigned)set[c >> 3] >> (c & 7u)) & 1u); }

static void class_escape(unsigned char *set, char e)
{
    int c;
    int neg = e == 'D' || e == 'S' || e == 'W';
    unsigned char tmp[32];
    memset(tmp, 0, sizeof tmp);
    for (c = 0; c < 256; c++) {
        int in = 0;
        if (e == 'd' || e == 'D') in = c >= '0' && c <= '9';
        else if (e == 's' || e == 'S') in = c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
        else in = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        if (in != neg)
            set_bit(tmp, (unsigned char)c);
    }
    for (c = 0; c < 32; c++)
        set[c] |= tmp[c];
}

static int is_class_escape(char e) { return e == 'd' || e == 'D' || e == 's' || e == 'S' || e == 'w' || e == 'W'; }

/* the pattern as atoms; -1 for what this matcher does not do */
static int compile(const char *pat, prog *p)
{
    const char *s = pat;
    memset(p, 0, sizeof *p);
    if (*s == '^') {
        p->anchored = 1;
        s++;
    }
    while (*s) {
        atom *a;
        if (p->n >= ATOMS_MAX - 1)
            return -1;
        if (*s == '$' && !s[1]) {
            p->at_end = 1;
            break;
        }
        a = &p->a[p->n];
        memset(a, 0, sizeof *a);
        a->min = a->max = 1;
        switch (*s) {
        case '(':
            if (p->groups++)
                return -1; /* one group */
            a->kind = A_GROUP_OPEN;
            s++;
            p->n++;
            continue;
        case ')':
            a->kind = A_GROUP_CLOSE;
            s++;
            p->n++;
            continue;
        case '|': case '{': case '}':
            return -1;
        case '*': case '+': case '?':
            return -1; /* a quantifier with nothing before it */
        case '.':
            a->kind = A_ANY;
            s++;
            break;
        case '\\':
            if (!s[1])
                return -1;
            if (is_class_escape(s[1])) {
                a->kind = A_CLASS;
                class_escape(a->set, s[1]);
            } else {
                a->kind = A_CHAR;
                a->c = (unsigned char)s[1];
            }
            s += 2;
            break;
        case '[': {
            int neg = 0, k;
            unsigned char set[32];
            memset(set, 0, sizeof set);
            s++;
            if (*s == '^') {
                neg = 1;
                s++;
            }
            if (*s == ']') { /* a leading ']' is literal */
                set_bit(set, ']');
                s++;
            }
            while (*s && *s != ']') {
                unsigned char lo = (unsigned char)*s, hi;
                if (*s == '\\' && s[1]) {
                    if (is_class_escape(s[1])) {
                        class_escape(set, s[1]);
                        s += 2;
                        continue;
                    }
                    lo = (unsigned char)s[1];
                    s++;
                }
                s++;
                hi = lo;
                if (*s == '-' && s[1] && s[1] != ']') {
                    hi = (unsigned char)s[1];
                    s += 2;
                }
                for (k = lo; k <= hi; k++)
                    set_bit(set, (unsigned char)k);
            }
            if (*s != ']')
                return -1;
            s++;
            a->kind = A_CLASS;
            for (k = 0; k < 32; k++)
                a->set[k] = neg ? (unsigned char)~set[k] : set[k];
            break;
        }
        default:
            a->kind = A_CHAR;
            a->c = (unsigned char)*s++;
            break;
        }
        if (*s == '*') { a->min = 0; a->max = -1; s++; }
        else if (*s == '+') { a->min = 1; a->max = -1; s++; }
        else if (*s == '?') { a->min = 0; a->max = 1; s++; }
        if (*s == '*' || *s == '+' || *s == '?' || *s == '{')
            return -1;
        p->n++;
    }
    if (p->groups) { /* the group closes once, after it opens */
        int i, open = -1, close = -1;
        for (i = 0; i < p->n; i++) {
            if (p->a[i].kind == A_GROUP_OPEN) open = i;
            if (p->a[i].kind == A_GROUP_CLOSE) { if (close >= 0) return -1; close = i; }
        }
        if (open < 0 || close < open)
            return -1;
    } else {
        int i;
        for (i = 0; i < p->n; i++)
            if (p->a[i].kind == A_GROUP_CLOSE)
                return -1;
    }
    p->a[p->n].kind = A_END;
    return 0;
}

static int one(const atom *a, unsigned char c)
{
    return a->kind == A_ANY ? c != '\n' : a->kind == A_CHAR ? c == a->c : has_bit(a->set, c);
}

/* the atoms from `i` matching at text[pos..]; the match's end in *end */
static int here(run *r, int i, size_t pos, size_t *end)
{
    const atom *a = &r->p->a[i];
    size_t n, most;
    if (a->kind == A_END) {
        if (r->p->at_end && pos != r->len)
            return 0;
        *end = pos;
        return 1;
    }
    if (a->kind == A_GROUP_OPEN || a->kind == A_GROUP_CLOSE) {
        size_t keep = a->kind == A_GROUP_OPEN ? r->gs : r->ge;
        if (a->kind == A_GROUP_OPEN) r->gs = pos; else r->ge = pos;
        if (here(r, i + 1, pos, end))
            return 1;
        if (a->kind == A_GROUP_OPEN) r->gs = keep; else r->ge = keep;
        return 0;
    }
    for (n = 0, most = a->max < 0 ? r->len - pos : (size_t)a->max; n < most && pos + n < r->len && one(a, (unsigned char)r->text[pos + n]); n++)
        ;
    for (;;) { /* greedy: the longest run first */
        if (n >= (size_t)a->min && here(r, i + 1, pos + n, end))
            return 1;
        if (n == 0 || n <= (size_t)a->min)
            return 0;
        n--;
    }
}

int esk_match_last(const char *pattern, const char *text, size_t len, esk_match *m)
{
    prog p;
    run r;
    size_t start, end = 0;
    int found = 0;
    if (!pattern || !text || compile(pattern, &p) != 0)
        return -1;
    memset(&r, 0, sizeof r);
    r.p = &p;
    r.text = text;
    r.len = len;
    for (start = 0; start <= len; start++) {
        r.gs = r.ge = 0;
        if (here(&r, 0, start, &end)) {
            found = 1;
            if (m) {
                m->start = start;
                m->end = end;
                m->has_group = p.groups != 0;
                m->group_start = r.gs;
                m->group_end = r.ge;
            }
            if (end > start) /* the next match starts after this one */
                start = end - 1;
        }
        if (p.anchored)
            break;
    }
    return found;
}
