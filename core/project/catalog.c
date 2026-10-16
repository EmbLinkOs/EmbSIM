/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "project/catalog.h"
#include <stdlib.h>
#include <string.h>
#include "host/host.h"
#include "util/str.h"

#ifndef EMBSIM_CATALOG_DIR
#define EMBSIM_CATALOG_DIR ""
#endif

es_status esk_catalog_init(es_catalog *c)
{
    memset(c, 0, sizeof *c);
    esk_alloc_init(&c->alloc, NULL);
    c->a = &c->alloc;
    esk_map_init(&c->by_id, c->a);
    c->libraries = esk_json_new_array(c->a);
    return c->libraries ? ES_OK : ES_ENOMEM;
}

static void item_free(es_catalog *c, esk_catalog_item *it)
{
    if (!it)
        return;
    esk_strfree(c->a, it->id);
    esk_strfree(c->a, it->kind);
    esk_strfree(c->a, it->version);
    esk_strfree(c->a, it->dir);
    esk_strfree(c->a, it->path);
    esk_strfree(c->a, it->library);
    esk_json_free(c->a, it->desc);
    esk_free(c->a, it, sizeof *it);
}

void esk_catalog_free(es_catalog *c)
{
    size_t i;
    for (i = 0; i < c->items.len; i++)
        item_free(c, c->items.data[i]);
    esk_vec_free(c->a, &c->items);
    esk_map_free(&c->by_id);
    esk_json_free(c->a, c->libraries);
    c->libraries = NULL;
}

static void diag(esk_diag *d, es_severity_t sev, const char *code, const char *file, const char *fmt, const char *arg)
{
    if (d)
        esk_diag_raise(d, sev, code, 0, "file", file, file, 0, fmt, arg);
}

static es_status add_item(es_catalog *c, const char *kind, const char *path, const char *library, esk_diag *d, int *count)
{
    char *text;
    size_t len;
    esk_json *j;
    int line;
    const char *err, *id, *jkind;
    esk_catalog_item *it;
    char key[256];
    es_status st = esk_host_read_file(c->a, path, &text, &len);
    if (st != ES_OK)
        return st;
    j = esk_json_parse(c->a, text, len, &line, &err);
    esk_free(c->a, text, len + 1);
    if (!j) {
        diag(d, ES_SEV_ERROR, "schema.parse", path, "%s", err);
        return ES_EPARSE;
    }
    id = esk_json_get_str(j, "id", NULL);
    jkind = esk_json_get_str(j, "kind", NULL);
    if (!id || !jkind || strcmp(jkind, kind) != 0) {
        diag(d, ES_SEV_ERROR, "schema.missing_field", path, "%s", "id and kind are required");
        esk_json_free(c->a, j);
        return ES_ESCHEMA;
    }
    esk_snprintf(key, sizeof key, "%s:%s", kind, id);
    if (esk_map_has(&c->by_id, key)) {
        esk_json_free(c->a, j); /* first library wins */
        return ES_OK;
    }
    it = (esk_catalog_item *)esk_calloc(c->a, 1, sizeof *it);
    if (!it) {
        esk_json_free(c->a, j);
        return ES_ENOMEM;
    }
    it->id = esk_strdup(c->a, id);
    it->kind = esk_strdup(c->a, kind);
    it->version = esk_strdup(c->a, esk_json_get_str(j, "version", "0.0.0"));
    it->path = esk_strdup(c->a, path);
    it->dir = esk_host_dirname(c->a, path);
    it->library = esk_strdup(c->a, library);
    it->desc = j;
    if (!it->id || !it->kind || !it->version || !it->path || !it->dir || !it->library ||
        esk_vec_push(c->a, &c->items, it) || esk_map_set(&c->by_id, key, it)) {
        item_free(c, it);
        return ES_ENOMEM;
    }
    (*count)++;
    return ES_OK;
}

/* scan <lib>/<sub>/<entry>/<file> and nested vendor dirs for mcus */
static void scan(es_catalog *c, const char *root, const char *sub, const char *kind, const char *file,
                 const char *library, esk_diag *d, int *count, int depth)
{
    char *dir = esk_host_path_join(c->a, root, sub);
    esk_json *names = NULL;
    size_t i;
    if (!dir)
        return;
    if (esk_host_is_dir(dir) && esk_host_list_dir(c->a, dir, &names) == ES_OK) {
        for (i = 0; i < esk_json_len(names); i++) {
            const char *name = esk_json_str(esk_json_at(names, i));
            char *entry, *path;
            if (!name || name[0] == '.')
                continue;
            entry = esk_host_path_join(c->a, dir, name);
            if (!entry)
                continue;
            path = esk_host_path_join(c->a, entry, file);
            if (path && esk_host_file_exists(path)) {
                add_item(c, kind, path, library, d, count);
                if (!strcmp(kind, "mcu")) {
                    /* packages beside the mcu */
                    esk_json *pk = NULL;
                    size_t k;
                    if (esk_host_list_dir(c->a, entry, &pk) == ES_OK) {
                        for (k = 0; k < esk_json_len(pk); k++) {
                            const char *pn = esk_json_str(esk_json_at(pk, k));
                            size_t pl = pn ? strlen(pn) : 0;
                            if (pn && pl > 5 && !strcmp(pn + pl - 5, ".json") && strcmp(pn, file)) {
                                char *pp = esk_host_path_join(c->a, entry, pn);
                                if (pp) {
                                    add_item(c, "package", pp, library, NULL, count);
                                    esk_strfree(c->a, pp);
                                }
                            }
                        }
                        esk_json_free(c->a, pk);
                    }
                }
            } else if (esk_host_is_dir(entry) && depth < 3) {
                scan(c, entry, "", kind, file, library, d, count, depth + 1);
            }
            esk_strfree(c->a, path);
            esk_strfree(c->a, entry);
        }
        esk_json_free(c->a, names);
    }
    esk_strfree(c->a, dir);
}

es_status esk_catalog_load_library(es_catalog *c, const char *path, esk_diag *d)
{
    char *lib_json = esk_host_path_join(c->a, path, "library.json");
    char *text = NULL;
    size_t len = 0;
    esk_json *lib = NULL;
    const char *name = "unnamed";
    int count = 0;
    esk_json *info;
    size_t i;
    if (!lib_json)
        return ES_ENOMEM;
    if (!esk_host_is_dir(path)) {
        esk_strfree(c->a, lib_json);
        if (d)
            esk_diag_raise(d, ES_SEV_ERROR, "catalog.not_found", 0, "file", path, path, 0, "library directory not found");
        return ES_ENOTFOUND;
    }
    if (esk_host_read_file(c->a, lib_json, &text, &len) == ES_OK) {
        int line;
        const char *err;
        lib = esk_json_parse(c->a, text, len, &line, &err);
        esk_free(c->a, text, len + 1);
        if (!lib)
            diag(d, ES_SEV_ERROR, "schema.parse", lib_json, "%s", err);
        else
            name = esk_json_get_str(lib, "name", name);
    }
    /* toolchains first: cheap; then the rest */
    {
        char *libname = esk_strdup(c->a, name);
        scan(c, path, "toolchains", "toolchain", "", libname, d, &count, 0); /* handled below */
        scan(c, path, "components", "component", "component.json", libname, d, &count, 0);
        scan(c, path, "boards", "board", "board.json", libname, d, &count, 0);
        scan(c, path, "mcus", "mcu", "mcu.json", libname, d, &count, 0);
        /* toolchains are flat files */
        {
            char *tdir = esk_host_path_join(c->a, path, "toolchains");
            esk_json *names = NULL;
            if (tdir && esk_host_is_dir(tdir) && esk_host_list_dir(c->a, tdir, &names) == ES_OK) {
                for (i = 0; i < esk_json_len(names); i++) {
                    const char *n = esk_json_str(esk_json_at(names, i));
                    size_t nl = n ? strlen(n) : 0;
                    if (n && nl > 5 && !strcmp(n + nl - 5, ".json")) {
                        char *p = esk_host_path_join(c->a, tdir, n);
                        if (p) {
                            add_item(c, "toolchain", p, libname, d, &count);
                            esk_strfree(c->a, p);
                        }
                    }
                }
                esk_json_free(c->a, names);
            }
            esk_strfree(c->a, tdir);
        }
        esk_strfree(c->a, libname);
    }
    info = esk_json_new_object(c->a);
    if (info) {
        esk_json_set_string(c->a, info, "name", name);
        esk_json_set_string(c->a, info, "version", lib ? esk_json_get_str(lib, "version", "0.0.0") : "0.0.0");
        esk_json_set_string(c->a, info, "path", path);
        esk_json_set_integer(c->a, info, "items", count);
        esk_json_array_push(c->a, c->libraries, info);
    }
    esk_json_free(c->a, lib);
    esk_strfree(c->a, lib_json);
    return ES_OK;
}

const esk_catalog_item *esk_catalog_find(const es_catalog *c, const char *kind, const char *id)
{
    char key[256];
    if (!kind || !id)
        return NULL;
    esk_snprintf(key, sizeof key, "%s:%s", kind, id);
    return (const esk_catalog_item *)esk_map_get(&c->by_id, key);
}

char *esk_catalog_bundled_dir(esk_alloc *a, const char *exe_hint)
{
    const char *env = getenv("EMBSIM_CATALOG");
    static const char *const rel[] = { "../catalog", "../../catalog", "../../../catalog", "../share/embsim/catalog" };
    size_t i;
    if (env && *env && esk_host_is_dir(env))
        return esk_strdup(a, env);
    if (exe_hint) {
        char *dir = esk_host_dirname(a, exe_hint);
        for (i = 0; dir && i < ES_COUNTOF(rel); i++) {
            char *cand = esk_host_path_join(a, dir, rel[i]);
            char *lib = cand ? esk_host_path_join(a, cand, "library.json") : NULL;
            int ok = lib && esk_host_file_exists(lib);
            esk_strfree(a, lib);
            if (ok) {
                esk_strfree(a, dir);
                return cand;
            }
            esk_strfree(a, cand);
        }
        esk_strfree(a, dir);
    }
    if (EMBSIM_CATALOG_DIR[0] && esk_host_is_dir(EMBSIM_CATALOG_DIR))
        return esk_strdup(a, EMBSIM_CATALOG_DIR);
    return NULL;
}

char *esk_catalog_read_item_file(const es_catalog *c, const esk_catalog_item *it, const char *rel, size_t *len)
{
    char *path = esk_host_path_join(c->a, it->dir, rel);
    char *text = NULL;
    size_t n = 0;
    if (!path)
        return NULL;
    if (esk_host_read_file(c->a, path, &text, &n) != ES_OK)
        text = NULL;
    esk_strfree(c->a, path);
    if (len)
        *len = n;
    return text;
}

static int contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle), i, k;
    if (!hay)
        return 0;
    for (i = 0; hay[i]; i++) {
        for (k = 0; k < n && hay[i + k]; k++) {
            char a = hay[i + k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b)
                break;
        }
        if (k == n)
            return 1;
    }
    return 0;
}

esk_json *esk_catalog_list(const es_catalog *c, const char *kind, const char *category, const char *query)
{
    esk_json *arr = esk_json_new_array(c->a);
    size_t i;
    if (!arr)
        return NULL;
    for (i = 0; i < c->items.len; i++) {
        const esk_catalog_item *it = c->items.data[i];
        const char *cat = esk_json_get_str(it->desc, "category", "");
        const char *name = esk_json_get_str(it->desc, "name", it->id);
        esk_json *o, *tiers;
        const esk_json *fid;
        if (kind && strcmp(kind, it->kind))
            continue;
        if (category && strcmp(category, cat))
            continue;
        if (query && !contains_ci(it->id, query) && !contains_ci(name, query) && !contains_ci(esk_json_get_str(it->desc, "description", ""), query)) {
            const esk_json *kw = esk_json_get(it->desc, "keywords");
            size_t k;
            int hit = 0;
            for (k = 0; k < esk_json_len(kw); k++)
                if (contains_ci(esk_json_str(esk_json_at(kw, k)), query))
                    hit = 1;
            if (!hit)
                continue;
        }
        o = esk_json_new_object(c->a);
        if (!o)
            break;
        esk_json_set_string(c->a, o, "id", it->id);
        esk_json_set_string(c->a, o, "kind", it->kind);
        esk_json_set_string(c->a, o, "version", it->version);
        esk_json_set_string(c->a, o, "name", name);
        esk_json_set_string(c->a, o, "category", cat);
        esk_json_set_string(c->a, o, "description", esk_json_get_str(it->desc, "description", ""));
        esk_json_set_string(c->a, o, "library", it->library);
        tiers = esk_json_new_array(c->a);
        fid = esk_json_get(it->desc, "fidelity");
        if (tiers) {
            static const char *const names[] = { "functional", "timed", "electrical" };
            size_t t;
            for (t = 0; t < 3; t++) {
                const esk_json *tier = esk_json_get(fid, names[t]);
                if (tier && tier->type == ESK_JSON_OBJECT)
                    esk_json_array_push(c->a, tiers, esk_json_new_string(c->a, names[t]));
            }
            esk_json_object_set(c->a, o, "fidelity_tiers", tiers);
        }
        esk_json_array_push(c->a, arr, o);
    }
    return arr;
}
