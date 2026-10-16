/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "util/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct parser {
    esk_alloc *a;
    const char *s, *end;
    int line;
    int depth;
    const char *err;
} parser;

static esk_json *node(esk_alloc *a, esk_json_type t)
{
    esk_json *j = (esk_json *)esk_calloc(a, 1, sizeof *j);
    if (j)
        j->type = t;
    return j;
}

void esk_json_free(esk_alloc *a, esk_json *j)
{
    size_t i;
    if (!j)
        return;
    switch (j->type) {
    case ESK_JSON_STRING: esk_free(a, j->u.string.data, j->u.string.len + 1); break;
    case ESK_JSON_NUMBER: esk_strfree(a, j->u.number.text); break;
    case ESK_JSON_ARRAY:
    case ESK_JSON_OBJECT:
        for (i = 0; i < j->u.array.len; i++)
            esk_json_free(a, j->u.array.items[i]);
        esk_free(a, j->u.array.items, j->u.array.cap * sizeof *j->u.array.items);
        break;
    default: break;
    }
    esk_strfree(a, j->key);
    esk_free(a, j, sizeof *j);
}

static void skip_ws(parser *p)
{
    while (p->s < p->end) {
        char c = *p->s;
        if (c == '\n') {
            p->line++;
            p->s++;
        } else if (c == ' ' || c == '\t' || c == '\r')
            p->s++;
        else
            break;
    }
}

static int push_item(esk_alloc *a, esk_json *parent, esk_json *child)
{
    if (parent->u.array.len == parent->u.array.cap) {
        size_t ncap = parent->u.array.cap ? parent->u.array.cap * 2 : 4;
        esk_json **items = (esk_json **)esk_realloc(a, parent->u.array.items, parent->u.array.cap * sizeof *items, ncap * sizeof *items);
        if (!items)
            return -1;
        parent->u.array.items = items;
        parent->u.array.cap = ncap;
    }
    parent->u.array.items[parent->u.array.len++] = child;
    return 0;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int put_utf8(esk_strbuf *b, unsigned cp)
{
    char out[4];
    size_t n = 0;
    if (cp < 0x80) {
        out[n++] = (char)cp;
    } else if (cp < 0x800) {
        out[n++] = (char)(0xC0 | (cp >> 6));
        out[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out[n++] = (char)(0xE0 | (cp >> 12));
        out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        out[n++] = (char)(0xF0 | (cp >> 18));
        out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[n++] = (char)(0x80 | (cp & 0x3F));
    }
    return esk_strbuf_append(b, out, n);
}

/* parses a string starting at the opening quote; returns an owned buffer */
static char *parse_string(parser *p, size_t *len)
{
    esk_strbuf b;
    esk_strbuf_init(&b, p->a);
    p->s++; /* opening quote */
    while (p->s < p->end) {
        char c = *p->s++;
        if (c == '"') {
            return esk_strbuf_detach(&b, len);
        }
        if (c == '\\') {
            if (p->s >= p->end)
                break;
            c = *p->s++;
            switch (c) {
            case '"': esk_strbuf_putc(&b, '"'); break;
            case '\\': esk_strbuf_putc(&b, '\\'); break;
            case '/': esk_strbuf_putc(&b, '/'); break;
            case 'b': esk_strbuf_putc(&b, '\b'); break;
            case 'f': esk_strbuf_putc(&b, '\f'); break;
            case 'n': esk_strbuf_putc(&b, '\n'); break;
            case 'r': esk_strbuf_putc(&b, '\r'); break;
            case 't': esk_strbuf_putc(&b, '\t'); break;
            case 'u': {
                unsigned cp = 0;
                int i;
                if (p->end - p->s < 4) {
                    p->err = "bad \\u escape";
                    esk_strbuf_free(&b);
                    return NULL;
                }
                for (i = 0; i < 4; i++) {
                    int h = hexval(p->s[i]);
                    if (h < 0) {
                        p->err = "bad \\u escape";
                        esk_strbuf_free(&b);
                        return NULL;
                    }
                    cp = cp * 16 + (unsigned)h;
                }
                p->s += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && p->end - p->s >= 6 && p->s[0] == '\\' && p->s[1] == 'u') {
                    unsigned lo = 0;
                    for (i = 0; i < 4; i++) {
                        int h = hexval(p->s[2 + i]);
                        if (h < 0)
                            break;
                        lo = lo * 16 + (unsigned)h;
                    }
                    if (i == 4 && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p->s += 6;
                    }
                }
                put_utf8(&b, cp);
                break;
            }
            default:
                p->err = "bad escape";
                esk_strbuf_free(&b);
                return NULL;
            }
        } else {
            if ((unsigned char)c < 0x20) {
                p->err = "control character in string";
                esk_strbuf_free(&b);
                return NULL;
            }
            if (c == '\n')
                p->line++;
            esk_strbuf_putc(&b, c);
        }
    }
    p->err = "unterminated string";
    esk_strbuf_free(&b);
    return NULL;
}

static esk_json *parse_value(parser *p);

static esk_json *parse_number(parser *p)
{
    const char *start = p->s;
    esk_json *j;
    char *endp;
    int is_int = 1;
    if (p->s < p->end && *p->s == '-')
        p->s++;
    if (p->s >= p->end || *p->s < '0' || *p->s > '9') {
        p->err = "bad number";
        return NULL;
    }
    while (p->s < p->end && *p->s >= '0' && *p->s <= '9')
        p->s++;
    if (p->s < p->end && *p->s == '.') {
        is_int = 0;
        p->s++;
        while (p->s < p->end && *p->s >= '0' && *p->s <= '9')
            p->s++;
    }
    if (p->s < p->end && (*p->s == 'e' || *p->s == 'E')) {
        is_int = 0;
        p->s++;
        if (p->s < p->end && (*p->s == '+' || *p->s == '-'))
            p->s++;
        while (p->s < p->end && *p->s >= '0' && *p->s <= '9')
            p->s++;
    }
    j = node(p->a, ESK_JSON_NUMBER);
    if (!j) {
        p->err = "out of memory";
        return NULL;
    }
    j->u.number.text = esk_strndup(p->a, start, (size_t)(p->s - start));
    if (!j->u.number.text) {
        esk_json_free(p->a, j);
        p->err = "out of memory";
        return NULL;
    }
    j->u.number.value = strtod(j->u.number.text, &endp);
    if (is_int) {
        long long v;
        char *e2;
        v = strtoll(j->u.number.text, &e2, 10);
        if (*e2 == 0 && v >= -9223372036854775807LL && v <= 9223372036854775807LL) {
            j->u.number.integer = (int64_t)v;
            j->u.number.is_integer = 1;
        } else if ((size_t)(p->s - start) > 18) {
            /* too large for int64: keep as double only */
            j->u.number.is_integer = 0;
        }
    }
    return j;
}

static esk_json *parse_array(parser *p)
{
    esk_json *arr = node(p->a, ESK_JSON_ARRAY);
    if (!arr) {
        p->err = "out of memory";
        return NULL;
    }
    arr->line = p->line;
    p->s++;
    skip_ws(p);
    if (p->s < p->end && *p->s == ']') {
        p->s++;
        return arr;
    }
    for (;;) {
        esk_json *v = parse_value(p);
        if (!v) {
            esk_json_free(p->a, arr);
            return NULL;
        }
        if (push_item(p->a, arr, v)) {
            esk_json_free(p->a, v);
            esk_json_free(p->a, arr);
            p->err = "out of memory";
            return NULL;
        }
        skip_ws(p);
        if (p->s < p->end && *p->s == ',') {
            p->s++;
            skip_ws(p);
            continue;
        }
        if (p->s < p->end && *p->s == ']') {
            p->s++;
            return arr;
        }
        p->err = "expected ',' or ']'";
        esk_json_free(p->a, arr);
        return NULL;
    }
}

static esk_json *parse_object(parser *p)
{
    esk_json *obj = node(p->a, ESK_JSON_OBJECT);
    if (!obj) {
        p->err = "out of memory";
        return NULL;
    }
    obj->line = p->line;
    p->s++;
    skip_ws(p);
    if (p->s < p->end && *p->s == '}') {
        p->s++;
        return obj;
    }
    for (;;) {
        char *key;
        size_t klen;
        esk_json *v;
        if (p->s >= p->end || *p->s != '"') {
            p->err = "expected a key";
            esk_json_free(p->a, obj);
            return NULL;
        }
        key = parse_string(p, &klen);
        if (!key) {
            esk_json_free(p->a, obj);
            return NULL;
        }
        skip_ws(p);
        if (p->s >= p->end || *p->s != ':') {
            p->err = "expected ':'";
            esk_free(p->a, key, klen + 1);
            esk_json_free(p->a, obj);
            return NULL;
        }
        p->s++;
        v = parse_value(p);
        if (!v) {
            esk_free(p->a, key, klen + 1);
            esk_json_free(p->a, obj);
            return NULL;
        }
        v->key = key;
        if (push_item(p->a, obj, v)) {
            esk_json_free(p->a, v);
            esk_json_free(p->a, obj);
            p->err = "out of memory";
            return NULL;
        }
        skip_ws(p);
        if (p->s < p->end && *p->s == ',') {
            p->s++;
            skip_ws(p);
            continue;
        }
        if (p->s < p->end && *p->s == '}') {
            p->s++;
            return obj;
        }
        p->err = "expected ',' or '}'";
        esk_json_free(p->a, obj);
        return NULL;
    }
}

static esk_json *parse_value(parser *p)
{
    esk_json *j = NULL;
    skip_ws(p);
    if (p->s >= p->end) {
        p->err = "unexpected end of input";
        return NULL;
    }
    if (++p->depth > ESK_JSON_MAX_DEPTH) {
        p->err = "nesting too deep";
        return NULL;
    }
    switch (*p->s) {
    case '{': j = parse_object(p); break;
    case '[': j = parse_array(p); break;
    case '"': {
        size_t len;
        char *s;
        int line = p->line;
        s = parse_string(p, &len);
        if (s) {
            j = node(p->a, ESK_JSON_STRING);
            if (!j) {
                esk_free(p->a, s, len + 1);
                p->err = "out of memory";
            } else {
                j->u.string.data = s;
                j->u.string.len = len;
                j->line = line;
            }
        }
        break;
    }
    case 't':
        if (p->end - p->s >= 4 && !memcmp(p->s, "true", 4)) {
            j = node(p->a, ESK_JSON_BOOL);
            if (j) j->u.boolean = 1;
            p->s += 4;
        } else p->err = "bad literal";
        break;
    case 'f':
        if (p->end - p->s >= 5 && !memcmp(p->s, "false", 5)) {
            j = node(p->a, ESK_JSON_BOOL);
            p->s += 5;
        } else p->err = "bad literal";
        break;
    case 'n':
        if (p->end - p->s >= 4 && !memcmp(p->s, "null", 4)) {
            j = node(p->a, ESK_JSON_NULL);
            p->s += 4;
        } else p->err = "bad literal";
        break;
    default:
        if (*p->s == '-' || (*p->s >= '0' && *p->s <= '9'))
            j = parse_number(p);
        else
            p->err = "unexpected character";
    }
    p->depth--;
    if (j && !j->line)
        j->line = p->line;
    if (!j && !p->err)
        p->err = "out of memory";
    return j;
}

esk_json *esk_json_parse(esk_alloc *a, const char *text, size_t len, int *err_line, const char **err)
{
    parser p;
    esk_json *j;
    memset(&p, 0, sizeof p);
    p.a = a;
    p.s = text;
    p.end = text + len;
    p.line = 1;
    /* skip a UTF-8 BOM */
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        p.s += 3;
    j = parse_value(&p);
    if (j) {
        skip_ws(&p);
        if (p.s != p.end) {
            esk_json_free(a, j);
            j = NULL;
            p.err = "trailing characters";
        }
    }
    if (!j) {
        if (err_line)
            *err_line = p.line;
        if (err)
            *err = p.err ? p.err : "parse error";
    }
    return j;
}

esk_json *esk_json_clone(esk_alloc *a, const esk_json *j)
{
    esk_json *c;
    size_t i;
    if (!j)
        return NULL;
    c = node(a, j->type);
    if (!c)
        return NULL;
    c->line = j->line;
    if (j->key) {
        c->key = esk_strdup(a, j->key);
        if (!c->key) goto fail;
    }
    switch (j->type) {
    case ESK_JSON_BOOL: c->u.boolean = j->u.boolean; break;
    case ESK_JSON_NUMBER:
        c->u.number = j->u.number;
        c->u.number.text = j->u.number.text ? esk_strdup(a, j->u.number.text) : NULL;
        if (j->u.number.text && !c->u.number.text) goto fail;
        break;
    case ESK_JSON_STRING:
        c->u.string.data = esk_strndup(a, j->u.string.data, j->u.string.len);
        if (!c->u.string.data) goto fail;
        c->u.string.len = j->u.string.len;
        break;
    case ESK_JSON_ARRAY:
    case ESK_JSON_OBJECT:
        for (i = 0; i < j->u.array.len; i++) {
            esk_json *child = esk_json_clone(a, j->u.array.items[i]);
            if (!child || push_item(a, c, child)) {
                esk_json_free(a, child);
                goto fail;
            }
        }
        break;
    default: break;
    }
    return c;
fail:
    esk_json_free(a, c);
    return NULL;
}

esk_json *esk_json_new_null(esk_alloc *a) { return node(a, ESK_JSON_NULL); }
esk_json *esk_json_new_bool(esk_alloc *a, int v)
{
    esk_json *j = node(a, ESK_JSON_BOOL);
    if (j) j->u.boolean = v != 0;
    return j;
}
esk_json *esk_json_new_number(esk_alloc *a, double v)
{
    esk_json *j = node(a, ESK_JSON_NUMBER);
    if (j) {
        j->u.number.value = v;
        if (v == floor(v) && fabs(v) < 9.0e15) {
            j->u.number.integer = (int64_t)v;
            j->u.number.is_integer = 1;
        }
    }
    return j;
}
esk_json *esk_json_new_integer(esk_alloc *a, int64_t v)
{
    esk_json *j = node(a, ESK_JSON_NUMBER);
    if (j) {
        j->u.number.value = (double)v;
        j->u.number.integer = v;
        j->u.number.is_integer = 1;
    }
    return j;
}
esk_json *esk_json_new_stringn(esk_alloc *a, const char *s, size_t n)
{
    esk_json *j = node(a, ESK_JSON_STRING);
    if (!j)
        return NULL;
    j->u.string.data = esk_strndup(a, s, n);
    if (!j->u.string.data) {
        esk_json_free(a, j);
        return NULL;
    }
    j->u.string.len = n;
    return j;
}
esk_json *esk_json_new_string(esk_alloc *a, const char *s) { return esk_json_new_stringn(a, s, strlen(s)); }
esk_json *esk_json_new_array(esk_alloc *a) { return node(a, ESK_JSON_ARRAY); }
esk_json *esk_json_new_object(esk_alloc *a) { return node(a, ESK_JSON_OBJECT); }

int esk_json_array_push(esk_alloc *a, esk_json *arr, esk_json *v)
{
    if (!arr || !v || arr->type != ESK_JSON_ARRAY)
        return -1;
    return push_item(a, arr, v);
}

int esk_json_object_set(esk_alloc *a, esk_json *obj, const char *key, esk_json *v)
{
    size_t i;
    if (!obj || !v || obj->type != ESK_JSON_OBJECT)
        return -1;
    esk_strfree(a, v->key);
    v->key = esk_strdup(a, key);
    if (!v->key)
        return -1;
    for (i = 0; i < obj->u.array.len; i++) {
        if (esk_str_eq(obj->u.array.items[i]->key, key)) {
            esk_json_free(a, obj->u.array.items[i]);
            obj->u.array.items[i] = v;
            return 0;
        }
    }
    return push_item(a, obj, v);
}

int esk_json_remove_at(esk_alloc *a, esk_json *j, size_t i)
{
    if (!j || (j->type != ESK_JSON_ARRAY && j->type != ESK_JSON_OBJECT) || i >= j->u.array.len)
        return -1;
    esk_json_free(a, j->u.array.items[i]);
    memmove(&j->u.array.items[i], &j->u.array.items[i + 1], (j->u.array.len - i - 1) * sizeof j->u.array.items[0]);
    j->u.array.len--;
    return 0;
}

int esk_json_remove(esk_alloc *a, esk_json *obj, const char *key)
{
    size_t i;
    if (!obj || obj->type != ESK_JSON_OBJECT)
        return -1;
    for (i = 0; i < obj->u.array.len; i++)
        if (esk_str_eq(obj->u.array.items[i]->key, key))
            return esk_json_remove_at(a, obj, i);
    return -1;
}

#define SETTER(name, T, ctor) \
    int name(esk_alloc *a, esk_json *obj, const char *key, T v) \
    { esk_json *j = ctor(a, v); if (!j) return -1; if (esk_json_object_set(a, obj, key, j)) { esk_json_free(a, j); return -1; } return 0; }
SETTER(esk_json_set_string, const char *, esk_json_new_string)
SETTER(esk_json_set_number, double, esk_json_new_number)
SETTER(esk_json_set_integer, int64_t, esk_json_new_integer)
SETTER(esk_json_set_bool, int, esk_json_new_bool)

int esk_json_set_u64_string(esk_alloc *a, esk_json *obj, const char *key, uint64_t v)
{
    char buf[32];
    esk_snprintf(buf, sizeof buf, "%llu", (unsigned long long)v);
    return esk_json_set_string(a, obj, key, buf);
}

const esk_json *esk_json_get(const esk_json *obj, const char *key)
{
    size_t i;
    if (!obj || obj->type != ESK_JSON_OBJECT || !key)
        return NULL;
    for (i = 0; i < obj->u.array.len; i++)
        if (esk_str_eq(obj->u.array.items[i]->key, key))
            return obj->u.array.items[i];
    return NULL;
}
esk_json *esk_json_get_mut(esk_json *obj, const char *key)
{
    size_t i;
    if (!obj || obj->type != ESK_JSON_OBJECT || !key)
        return NULL;
    for (i = 0; i < obj->u.array.len; i++)
        if (esk_str_eq(obj->u.array.items[i]->key, key))
            return obj->u.array.items[i];
    return NULL;
}
esk_json *esk_json_at_mut(esk_json *j, size_t i)
{
    return j && (j->type == ESK_JSON_ARRAY || j->type == ESK_JSON_OBJECT) && i < j->u.array.len ? j->u.array.items[i] : NULL;
}
size_t esk_json_len(const esk_json *j)
{
    return j && (j->type == ESK_JSON_ARRAY || j->type == ESK_JSON_OBJECT) ? j->u.array.len : 0;
}
const esk_json *esk_json_at(const esk_json *j, size_t i)
{
    return i < esk_json_len(j) ? j->u.array.items[i] : NULL;
}
const char *esk_json_str(const esk_json *j) { return j && j->type == ESK_JSON_STRING ? j->u.string.data : NULL; }
const char *esk_json_get_str(const esk_json *obj, const char *key, const char *def)
{
    const char *s = esk_json_str(esk_json_get(obj, key));
    return s ? s : def;
}
double esk_json_get_num(const esk_json *obj, const char *key, double def)
{
    const esk_json *j = esk_json_get(obj, key);
    return j && j->type == ESK_JSON_NUMBER ? j->u.number.value : def;
}
int64_t esk_json_get_int(const esk_json *obj, const char *key, int64_t def)
{
    const esk_json *j = esk_json_get(obj, key);
    if (!j || j->type != ESK_JSON_NUMBER)
        return def;
    return j->u.number.is_integer ? j->u.number.integer : (int64_t)j->u.number.value;
}
int esk_json_get_bool(const esk_json *obj, const char *key, int def)
{
    const esk_json *j = esk_json_get(obj, key);
    return j && j->type == ESK_JSON_BOOL ? j->u.boolean : def;
}
int esk_json_is(const esk_json *j, esk_json_type t) { return j && j->type == t; }

int esk_json_write_string(esk_strbuf *b, const char *s, size_t n)
{
    size_t i;
    esk_strbuf_putc(b, '"');
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': esk_strbuf_puts(b, "\\\""); break;
        case '\\': esk_strbuf_puts(b, "\\\\"); break;
        case '\n': esk_strbuf_puts(b, "\\n"); break;
        case '\r': esk_strbuf_puts(b, "\\r"); break;
        case '\t': esk_strbuf_puts(b, "\\t"); break;
        case '\b': esk_strbuf_puts(b, "\\b"); break;
        case '\f': esk_strbuf_puts(b, "\\f"); break;
        default:
            if (c < 0x20)
                esk_strbuf_printf(b, "\\u%04x", c);
            else
                esk_strbuf_putc(b, (char)c);
        }
    }
    return esk_strbuf_putc(b, '"');
}

static void newline(esk_strbuf *b, int indent, int level)
{
    int i;
    if (indent <= 0)
        return;
    esk_strbuf_putc(b, '\n');
    for (i = 0; i < indent * level; i++)
        esk_strbuf_putc(b, ' ');
}

static int write_value(esk_strbuf *b, const esk_json *j, int indent, int level)
{
    size_t i;
    if (!j)
        return esk_strbuf_puts(b, "null");
    switch (j->type) {
    case ESK_JSON_NULL: return esk_strbuf_puts(b, "null");
    case ESK_JSON_BOOL: return esk_strbuf_puts(b, j->u.boolean ? "true" : "false");
    case ESK_JSON_NUMBER:
        if (j->u.number.is_integer)
            return esk_strbuf_printf(b, "%lld", (long long)j->u.number.integer);
        if (j->u.number.text)
            return esk_strbuf_puts(b, j->u.number.text);
        if (isnan(j->u.number.value) || isinf(j->u.number.value))
            return esk_strbuf_puts(b, "null");
        return esk_strbuf_printf(b, "%.17g", j->u.number.value);
    case ESK_JSON_STRING: return esk_json_write_string(b, j->u.string.data, j->u.string.len);
    case ESK_JSON_ARRAY:
        esk_strbuf_putc(b, '[');
        for (i = 0; i < j->u.array.len; i++) {
            if (i)
                esk_strbuf_putc(b, ',');
            newline(b, indent, level + 1);
            write_value(b, j->u.array.items[i], indent, level + 1);
        }
        if (j->u.array.len)
            newline(b, indent, level);
        return esk_strbuf_putc(b, ']');
    case ESK_JSON_OBJECT:
        esk_strbuf_putc(b, '{');
        for (i = 0; i < j->u.array.len; i++) {
            const esk_json *m = j->u.array.items[i];
            if (i)
                esk_strbuf_putc(b, ',');
            newline(b, indent, level + 1);
            esk_json_write_string(b, m->key ? m->key : "", m->key ? strlen(m->key) : 0);
            esk_strbuf_puts(b, indent > 0 ? ": " : ":");
            write_value(b, m, indent, level + 1);
        }
        if (j->u.array.len)
            newline(b, indent, level);
        return esk_strbuf_putc(b, '}');
    }
    return -1;
}

int esk_json_write(esk_strbuf *b, const esk_json *j, int indent) { return write_value(b, j, indent, 0); }

char *esk_json_format(esk_alloc *a, const esk_json *j, int indent, size_t *len)
{
    esk_strbuf b;
    esk_strbuf_init(&b, a);
    write_value(&b, j, indent, 0);
    return esk_strbuf_detach(&b, len);
}
