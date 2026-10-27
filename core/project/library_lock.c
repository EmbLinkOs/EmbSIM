/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "project/library_lock.h"
#include "host/host.h"
#include "util/sha256.h"
#include "util/str.h"
#include <string.h>

#define LOCK_FILES 20000u
#define LOCK_BYTES UINT64_C(134217728)

static int put(es_session *s, esk_json *o, const char *key, esk_json *v)
{
    if (!v) return -1;
    if (esk_json_object_set(s->a, o, key, v)) { esk_json_free(s->a, v); return -1; }
    return 0;
}

static es_status problem(es_session *s, const char *code, const char *path, const char *message)
{
    esk_diag_raise(&s->diag, ES_SEV_ERROR, code, s->eng.sched.now, "library", path,
                   NULL, 0, "%s", message);
    return ES_ESCHEMA;
}

static es_status tree(es_session *s, const char *root, const char *rel, esk_json *files,
                      unsigned depth, size_t *count, uint64_t *bytes)
{
    char *path = esk_host_path_join(s->a, root, rel);
    esk_json *names = NULL;
    size_t i;
    es_status st;
    if (!path) return ES_ENOMEM;
    if (depth > 16 || esk_host_path_type(path, NULL) != 2) {
        esk_strfree(s->a, path);
        return problem(s, "library.lock.path", rel, "Library tree must contain regular files and directories only, with depth at most 16.");
    }
    st = esk_host_list_dir(s->a, path, &names);
    esk_strfree(s->a, path);
    for (i = 0; st == ES_OK && i < esk_json_len(names); i++) {
        const char *name = esk_json_str(esk_json_at(names, i));
        char *next, *full;
        uint64_t size = 0;
        int type;
        if (!name || name[0] == '.') continue; /* same distribution scope as install */
        if (strchr(name, '\\') || strchr(name, '/') || strchr(name, ':')) {
            st = problem(s, "library.lock.path", name, "Library filenames must be portable relative path segments.");
            break;
        }
        next = *rel ? esk_host_path_join(s->a, rel, name) : esk_strdup(s->a, name);
        full = next ? esk_host_path_join(s->a, root, next) : NULL;
        if (!next || !full) st = ES_ENOMEM;
        else if (strlen(next) > 1024) st = problem(s, "library.lock.path", next, "Library relative paths must not exceed 1024 bytes.");
        else {
            char *p;
            for (p = next; *p; p++) if (*p == '\\') *p = '/';
            type = esk_host_path_type(full, &size);
            if (type == 2) st = tree(s, root, next, files, depth + 1, count, bytes);
            else if (type != 1) st = problem(s, "library.lock.path", next, "Links, reparse points and special files cannot be locked.");
            else if (!*count || size > *bytes) st = problem(s, "library.lock.limit", next, "Library lock exceeds 20,000 files or 128 MiB total.");
            else {
                char *text = NULL, hash[72];
                size_t len = 0;
                (*count)--;
                st = esk_host_read_file(s->a, full, &text, &len);
                if (st == ES_OK && (len != size || len > *bytes)) st = ES_EIO;
                if (st == ES_OK) {
                    *bytes -= len;
                    esk_sha256_text(text, len, hash);
                    if (esk_json_set_string(s->a, files, next, hash)) st = ES_ENOMEM;
                }
                esk_free(s->a, text, len + 1);
            }
        }
        esk_strfree(s->a, next);
        esk_strfree(s->a, full);
    }
    esk_json_free(s->a, names);
    return st;
}

static int equal(const esk_json *a, const esk_json *b)
{
    size_t i;
    if (!a || !b || a->type != b->type) return a == b;
    if (a->type == ESK_JSON_STRING) return !strcmp(esk_json_str(a), esk_json_str(b));
    if (a->type == ESK_JSON_NUMBER) {
        if (a->u.number.is_integer && b->u.number.is_integer) return a->u.number.integer == b->u.number.integer;
        return a->u.number.value == b->u.number.value;
    }
    if (a->type == ESK_JSON_BOOL) return a->u.boolean == b->u.boolean;
    if (esk_json_len(a) != esk_json_len(b)) return 0;
    for (i = 0; i < esk_json_len(a); i++) {
        const esk_json *v = esk_json_at(a, i);
        if (!equal(v, a->type == ESK_JSON_OBJECT ? esk_json_get(b, v->key) : esk_json_at(b, i))) return 0;
    }
    return 1;
}

/* Refuse a lock of new disk bytes over stale in-memory model descriptions. */
static es_status check_description(es_session *s, const esk_catalog_item *item)
{
    char *text = NULL;
    size_t len = 0;
    esk_json *doc = NULL;
    es_status st = esk_host_read_file(s->a, item->path, &text, &len);
    if (st == ES_OK) doc = esk_json_parse(s->a, text, len, NULL, NULL);
    if (st == ES_OK && (!doc || !equal(doc, item->desc)))
        st = problem(s, "library.lock.stale", item->id, "Loaded description differs from disk. Start a new session before capturing a lock.");
    esk_json_free(s->a, doc);
    esk_free(s->a, text, len + 1);
    return st;
}

es_status esk_library_lock_capture(es_session *s, esk_json **out)
{
    esk_json *root = NULL, *libraries, *catalog, *paths = NULL;
    size_t i, count = LOCK_FILES;
    uint64_t bytes = LOCK_BYTES;
    es_status st = ES_OK;
    *out = NULL;
    if (!esk_json_len(s->catalog->libraries) || esk_json_len(s->catalog->libraries) > 256 || s->catalog->items.len > LOCK_FILES)
        return problem(s, "library.lock.limit", "catalog", "A library lock requires 1-256 loaded libraries and at most 20,000 catalog selections.");
    root = esk_json_new_object(s->a);
    paths = esk_json_new_object(s->a);
    if (!root || !paths || esk_json_set_integer(s->a, root, "schema_version", 1) ||
        esk_json_set_string(s->a, root, "kind", "library_lock") ||
        put(s, root, "libraries", esk_json_new_object(s->a)) ||
        put(s, root, "catalog", esk_json_new_object(s->a))) { st = ES_ENOMEM; goto done; }
    libraries = esk_json_get_mut(root, "libraries");
    catalog = esk_json_get_mut(root, "catalog");
    for (i = 0; st == ES_OK && i < esk_json_len(s->catalog->libraries); i++) {
        const esk_json *info = esk_json_at(s->catalog->libraries, i);
        const char *name = esk_json_get_str(info, "name", ""), *path = esk_json_get_str(info, "path", "");
        const char *previous = esk_json_get_str(paths, name, NULL);
        esk_json *entry, *files;
        if (previous) {
            if (strcmp(path, previous)) st = problem(s, "library.lock.ambiguous", name, "Two loaded library roots have the same name; use unique library names.");
            continue;
        }
        entry = esk_json_new_object(s->a);
        if (!entry || put(s, libraries, name, entry)) { st = ES_ENOMEM; break; }
        if (esk_json_set_string(s->a, paths, name, path) ||
            esk_json_set_string(s->a, entry, "version", esk_json_get_str(info, "version", "")) ||
            put(s, entry, "files", esk_json_new_object(s->a))) { st = ES_ENOMEM; break; }
        {
            char *file = esk_host_path_join(s->a, path, "library.json"), *text = NULL;
            esk_json *manifest = NULL;
            size_t len = 0;
            st = file ? esk_host_read_file(s->a, file, &text, &len) : ES_ENOMEM;
            if (st == ES_OK) manifest = esk_json_parse(s->a, text, len, NULL, NULL);
            if (st == ES_OK && (!manifest || !esk_str_eq(esk_json_get_str(manifest, "name", ""), name) ||
                !esk_str_eq(esk_json_get_str(manifest, "version", ""), esk_json_get_str(info, "version", ""))))
                st = problem(s, "library.lock.stale", name, "Loaded library identity differs from disk. Start a new session before capturing a lock.");
            esk_json_free(s->a, manifest);
            esk_free(s->a, text, len + 1);
            esk_strfree(s->a, file);
            if (st != ES_OK) break;
        }
        files = esk_json_get_mut(entry, "files");
        st = tree(s, path, "", files, 0, &count, &bytes);
        if (st == ES_OK && !esk_json_get(files, "library.json"))
            st = problem(s, "library.lock.manifest", name, "A lockable library requires library.json.");
    }
    for (i = 0; st == ES_OK && i < s->catalog->items.len; i++) {
        const esk_catalog_item *item = s->catalog->items.data[i];
        char key[512], hash[72], *text;
        size_t len = 0;
        esk_json *entry;
        if (strlen(item->kind) + strlen(item->id) + 1 >= sizeof key) { st = ES_ELIMIT; break; }
        st = check_description(s, item);
        if (st != ES_OK) break;
        text = esk_json_format(s->a, item->desc, 0, &len);
        if (!text) { st = ES_ENOMEM; break; }
        esk_sha256_text(text, len, hash);
        esk_free(s->a, text, len + 1);
        esk_snprintf(key, sizeof key, "%s:%s", item->kind, item->id);
        entry = esk_json_new_object(s->a);
        if (!entry || put(s, catalog, key, entry)) { st = ES_ENOMEM; break; }
        if (esk_json_set_string(s->a, entry, "library", item->library) ||
            esk_json_set_string(s->a, entry, "version", item->version) ||
            esk_json_set_string(s->a, entry, "sha256", hash)) { st = ES_ENOMEM; break; }
    }
done:
    esk_json_free(s->a, paths);
    if (st != ES_OK) esk_json_free(s->a, root);
    else *out = root;
    return st;
}

static int difference(es_session *s, esk_json *changes, const char *scope, const char *key, const char *change)
{
    esk_json *entry = esk_json_new_object(s->a);
    if (!entry) return -1;
    if (esk_json_set_string(s->a, entry, "scope", scope) || esk_json_set_string(s->a, entry, "path", key) ||
        esk_json_set_string(s->a, entry, "change", change) || esk_json_array_push(s->a, changes, entry)) {
        esk_json_free(s->a, entry); return -1;
    }
    return 0;
}

static int compare(es_session *s, esk_json *changes, const char *scope, const esk_json *was, const esk_json *now)
{
    size_t i;
    if (!was || was->type != ESK_JSON_OBJECT) return difference(s, changes, scope, "", "invalid");
    for (i = 0; i < esk_json_len(was); i++) {
        const esk_json *v = esk_json_at(was, i), *other = esk_json_get(now, v->key);
        if (!equal(v, other) && difference(s, changes, scope, v->key, other ? "changed" : "removed")) return -1;
    }
    for (i = 0; i < esk_json_len(now); i++) {
        const esk_json *v = esk_json_at(now, i);
        if (!esk_json_get(was, v->key) && difference(s, changes, scope, v->key, "added")) return -1;
    }
    return 0;
}

es_status esk_library_lock_check(es_session *s, const esk_json *lock, esk_json **report)
{
    esk_json *current = NULL, *result = NULL, *changes;
    es_status st = esk_library_lock_capture(s, &current);
    size_t i;
    *report = NULL;
    if (st != ES_OK) return st;
    result = esk_json_new_object(s->a);
    if (!result || put(s, result, "changes", esk_json_new_array(s->a))) { st = ES_ENOMEM; goto done; }
    changes = esk_json_get_mut(result, "changes");
    if (!lock || lock->type != ESK_JSON_OBJECT || esk_json_len(lock) != 4 ||
        esk_json_get_int(lock, "schema_version", 0) != 1 || !esk_str_eq(esk_json_get_str(lock, "kind", ""), "library_lock")) {
        if (difference(s, changes, "lock", "", "invalid")) st = ES_ENOMEM;
    } else {
        const esk_json *was = esk_json_get(lock, "libraries"), *now = esk_json_get(current, "libraries");
        if (compare(s, changes, "libraries", was, now) || compare(s, changes, "catalog", esk_json_get(lock, "catalog"), esk_json_get(current, "catalog"))) st = ES_ENOMEM;
        /* Expand library changes into useful file paths as well. */
        for (i = 0; st == ES_OK && i < esk_json_len(was); i++) {
            const esk_json *lib = esk_json_at(was, i), *actual = esk_json_get(now, lib->key);
            if (actual && compare(s, changes, lib->key, esk_json_get(lib, "files"), esk_json_get(actual, "files"))) st = ES_ENOMEM;
        }
    }
    if (st == ES_OK && esk_json_set_bool(s->a, result, "ok", esk_json_len(changes) == 0)) st = ES_ENOMEM;
done:
    esk_json_free(s->a, current);
    if (st != ES_OK) esk_json_free(s->a, result);
    else *report = result;
    return st;
}

es_status esk_library_lock_enforce(es_session *s, const esk_json *project)
{
    const esk_json *lock = esk_json_get(project, "library_lock");
    esk_json *report = NULL;
    es_status st;
    if (!lock) return ES_OK;
    st = esk_library_lock_check(s, lock, &report);
    if (st == ES_OK && !esk_json_get_bool(report, "ok", 0)) {
        const esk_json *first = esk_json_at(esk_json_get(report, "changes"), 0);
        st = problem(s, "library.lock.drift", esk_json_get_str(first, "path", ""),
                     "Library inputs differ from the saved lock. Review the changes before explicitly replacing or removing the lock.");
    }
    esk_json_free(s->a, report);
    return st;
}
