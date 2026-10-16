/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* json.h -- a small JSON parser (to a tree) and writer (ADR-006). Objects
 * keep key order; numbers keep their text and an exact 64-bit integer when
 * they have one (times in picoseconds are strings, but limits and counts are
 * integers). Depth is bounded (CS-17). */
#ifndef ESK_JSON_H
#define ESK_JSON_H

#include <stdint.h>
#include "util/alloc.h"
#include "util/str.h"

typedef enum esk_json_type {
    ESK_JSON_NULL,
    ESK_JSON_BOOL,
    ESK_JSON_NUMBER,
    ESK_JSON_STRING,
    ESK_JSON_ARRAY,
    ESK_JSON_OBJECT
} esk_json_type;

typedef struct esk_json esk_json;
struct esk_json {
    esk_json_type type;
    int line;             /* 1-based line of the value's first character */
    char *key;            /* for object members */
    union {
        int boolean;
        struct {
            double value;
            int64_t integer;
            int is_integer;
            char *text;
        } number;
        struct {
            char *data;
            size_t len;
        } string;
        struct {
            esk_json **items;
            size_t len, cap;
        } array; /* arrays and objects */
    } u;
};

#define ESK_JSON_MAX_DEPTH 64

/* Parse `len` bytes; on failure returns NULL and fills *err_line and *err (static text). */
esk_json *esk_json_parse(esk_alloc *a, const char *text, size_t len, int *err_line, const char **err);
void esk_json_free(esk_alloc *a, esk_json *j);
esk_json *esk_json_clone(esk_alloc *a, const esk_json *j);

/* constructors */
esk_json *esk_json_new_null(esk_alloc *a);
esk_json *esk_json_new_bool(esk_alloc *a, int v);
esk_json *esk_json_new_number(esk_alloc *a, double v);
esk_json *esk_json_new_integer(esk_alloc *a, int64_t v);
esk_json *esk_json_new_string(esk_alloc *a, const char *s);
esk_json *esk_json_new_stringn(esk_alloc *a, const char *s, size_t n);
esk_json *esk_json_new_array(esk_alloc *a);
esk_json *esk_json_new_object(esk_alloc *a);
/* append; the child is owned by the parent afterwards; returns 0 or -1 */
int esk_json_array_push(esk_alloc *a, esk_json *arr, esk_json *v);
int esk_json_object_set(esk_alloc *a, esk_json *obj, const char *key, esk_json *v); /* replaces an existing key */
/* convenience setters: return 0 or -1 */
/* remove item `i` of an array or object, or the member `key` of an
 * object, freeing it: 0, or -1 when there is none */
int esk_json_remove_at(esk_alloc *a, esk_json *j, size_t i);
int esk_json_remove(esk_alloc *a, esk_json *obj, const char *key);
int esk_json_set_string(esk_alloc *a, esk_json *obj, const char *key, const char *v);
int esk_json_set_number(esk_alloc *a, esk_json *obj, const char *key, double v);
int esk_json_set_integer(esk_alloc *a, esk_json *obj, const char *key, int64_t v);
int esk_json_set_bool(esk_alloc *a, esk_json *obj, const char *key, int v);
int esk_json_set_u64_string(esk_alloc *a, esk_json *obj, const char *key, uint64_t v); /* times */

/* accessors; all tolerate NULL and wrong types by returning NULL / defaults */
const esk_json *esk_json_get(const esk_json *obj, const char *key);
size_t esk_json_len(const esk_json *j); /* arrays and objects */
/* the same, for changing in place */
esk_json *esk_json_get_mut(esk_json *obj, const char *key);
esk_json *esk_json_at_mut(esk_json *j, size_t i);
const esk_json *esk_json_at(const esk_json *j, size_t i);
const char *esk_json_str(const esk_json *j);               /* NULL unless a string */
const char *esk_json_get_str(const esk_json *obj, const char *key, const char *def);
double esk_json_get_num(const esk_json *obj, const char *key, double def);
int64_t esk_json_get_int(const esk_json *obj, const char *key, int64_t def);
int esk_json_get_bool(const esk_json *obj, const char *key, int def);
int esk_json_is(const esk_json *j, esk_json_type t);

/* writer: compact or indented; appends to a strbuf */
int esk_json_write(esk_strbuf *b, const esk_json *j, int indent);
int esk_json_write_string(esk_strbuf *b, const char *s, size_t n); /* a quoted, escaped string */
/* format as a fresh string (free with esk_free(a, p, len + 1)) */
char *esk_json_format(esk_alloc *a, const esk_json *j, int indent, size_t *len);

#endif
