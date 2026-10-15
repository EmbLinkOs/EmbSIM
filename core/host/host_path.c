/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* host_path.c -- the path helpers of host.h that need no OS call. They know
 * the platform's separators and root forms, nothing else. */
#include <string.h>
#include "host/host.h"

#ifdef _WIN32
#define ESK_PATH_SEP '\\'
#else
#define ESK_PATH_SEP '/'
#endif

static int is_sep(char c)
{
#ifdef _WIN32
    return c == '\\' || c == '/';
#else
    return c == '/';
#endif
}

static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* The length of the root prefix of `p`: "/" or "" on POSIX; on Windows "C:\",
 * "C:" (drive-relative), "\\" (UNC or root-relative) or "". *absolute is
 * set when the prefix anchors the path (a ".." cannot climb above it). */
static size_t root_prefix(const char *p, int *absolute)
{
    *absolute = 0;
#ifdef _WIN32
    if (is_alpha(p[0]) && p[1] == ':') {
        if (is_sep(p[2])) {
            *absolute = 1;
            return 3;
        }
        return 2;
    }
    if (is_sep(p[0])) {
        *absolute = 1;
        return is_sep(p[1]) ? 2 : 1;
    }
    return 0;
#else
    ES_UNUSED(is_alpha);
    if (p[0] == '/') {
        *absolute = 1;
        return 1;
    }
    return 0;
#endif
}

static int is_absolute(const char *p)
{
    int abs_;
    size_t n = root_prefix(p, &abs_);
    return n > 0 && abs_;
}

char *esk_host_dirname(esk_alloc *a, const char *path)
{
    size_t n, prefix;
    int abs_;
    if (!path)
        return NULL;
    n = strlen(path);
    prefix = root_prefix(path, &abs_);
    /* drop trailing separators, then the last component */
    while (n > prefix && is_sep(path[n - 1]))
        n--;
    while (n > prefix && !is_sep(path[n - 1]))
        n--;
    while (n > prefix && is_sep(path[n - 1]))
        n--;
    if (n == 0)
        return esk_strdup(a, ".");
    return esk_strndup(a, path, n);
}

char *esk_host_path_join(esk_alloc *a, const char *base, const char *rel)
{
    esk_strbuf raw, out;
    const char *src;
    const char **parts;
    size_t nparts = 0, i, prefix, start, cap;
    int absolute;
    char *result;

    if (!rel)
        rel = "";
    if (!base)
        base = "";
    esk_strbuf_init(&raw, a);
    if (is_absolute(rel) || !*base) {
        esk_strbuf_puts(&raw, rel);
    } else {
        esk_strbuf_puts(&raw, base);
        if (*rel) {
            esk_strbuf_putc(&raw, ESK_PATH_SEP);
            esk_strbuf_puts(&raw, rel);
        }
    }
    if (raw.oom) {
        esk_strbuf_free(&raw);
        return NULL;
    }
    src = raw.data ? raw.data : "";
    prefix = root_prefix(src, &absolute);

    /* split the remainder into components; the array is bounded by the
     * number of separators plus one (CS-17) */
    cap = 2;
    for (i = prefix; src[i]; i++)
        if (is_sep(src[i]))
            cap++;
    parts = (const char **)esk_malloc(a, cap * sizeof *parts);
    if (!parts) {
        esk_strbuf_free(&raw);
        return NULL;
    }
    /* NUL-terminate the components in place: raw is ours */
    start = prefix;
    for (i = prefix;; i++) {
        if (src[i] == 0 || is_sep(src[i])) {
            size_t len = i - start;
            const char *part = src + start;
            int end = src[i] == 0;
            raw.data[i] = 0;
            if (len == 0 || (len == 1 && part[0] == '.')) {
                /* empty or "." : nothing */
            } else if (len == 2 && part[0] == '.' && part[1] == '.') {
                if (nparts > 0 && strcmp(parts[nparts - 1], "..") != 0)
                    nparts--;
                else if (!absolute)
                    parts[nparts++] = part;
                /* else: ".." above the root is dropped */
            } else {
                parts[nparts++] = part;
            }
            if (end)
                break;
            start = i + 1;
        }
    }

    esk_strbuf_init(&out, a);
    esk_strbuf_append(&out, src, prefix);
#ifdef _WIN32
    for (i = 0; i < prefix; i++)
        if (out.data[i] == '/')
            out.data[i] = '\\';
#endif
    for (i = 0; i < nparts; i++) {
        if (i > 0)
            esk_strbuf_putc(&out, ESK_PATH_SEP);
        esk_strbuf_puts(&out, parts[i]);
    }
    if (out.len == 0)
        esk_strbuf_putc(&out, '.');
    esk_free(a, parts, cap * sizeof *parts);
    esk_strbuf_free(&raw);
    if (out.oom) {
        esk_strbuf_free(&out);
        return NULL;
    }
    {
        size_t len;
        result = esk_strbuf_detach(&out, &len);
    }
    return result;
}
