/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "project/manifest.h"
#include "project/hierarchy.h"
#include "util/sha256.h"
#include "util/str.h"
#include <string.h>

/* Consumes value even when insertion fails. */
static int put(es_session *s, esk_json *object, const char *key, esk_json *value)
{
    if (!value)
        return -1;
    if (esk_json_object_set(s->a, object, key, value)) {
        esk_json_free(s->a, value);
        return -1;
    }
    return 0;
}

/* Include the exact hashed bytes, so verification does not depend on another
 * JSON serializer's numeric spelling or member-order conventions. */
static esk_json *document(es_session *s, const esk_json *value)
{
    esk_json *out;
    char *text, hash[72];
    size_t len = 0;
    text = esk_json_format(s->a, value, 0, &len);
    if (!text)
        return NULL;
    esk_sha256_text(text, len, hash);
    out = esk_json_new_object(s->a);
    if (out && (esk_json_set_string(s->a, out, "sha256", hash) ||
                put(s, out, "json", esk_json_new_stringn(s->a, text, len)))) {
        esk_json_free(s->a, out);
        out = NULL;
    }
    esk_free(s->a, text, len + 1);
    return out;
}

/* Include every loaded model description, keeping colliding generated descriptions
 * distinct even when their public identifier is the same. */
static es_status describe(es_session *s, const esk_json *description, const char *kind,
                          const char *ref, char key[512], esk_json *catalog)
{
    esk_json *doc;
    const esk_json *previous;
    char base[400];
    key[0] = 0;
    if (!description)
        return ES_OK;
    kind = esk_json_get_str(description, "kind", kind);
    ref = esk_json_get_str(description, "id", ref);
    if (strlen(kind) + strlen(ref) + 1u >= sizeof base)
        return ES_ELIMIT;
    esk_snprintf(base, sizeof base, "%s:%s", kind, ref);
    doc = document(s, description);
    if (!doc)
        return ES_ENOMEM;
    previous = esk_json_get(catalog, base);
    if (previous &&
        strcmp(esk_json_get_str(previous, "sha256", ""), esk_json_get_str(doc, "sha256", "")))
        esk_snprintf(key, 512, "%s@%s", base, esk_json_get_str(doc, "sha256", "") + 7);
    else
        esk_snprintf(key, 512, "%s", base);
    if (esk_json_get(catalog, key)) {
        esk_json_free(s->a, doc);
        return ES_OK;
    }
    return put(s, catalog, key, doc) ? ES_ENOMEM : ES_OK;
}

static es_status add_model(es_session *s, const esk_comp *c, esk_json *models, esk_json *catalog)
{
    char key[512];
    esk_json *row;
    es_status st = describe(s, c->desc, "component", c->kind, key, catalog);
    if (st != ES_OK)
        return st;
    row = esk_json_new_object(s->a);
    if (!row)
        return ES_ENOMEM;
    if (esk_json_set_string(s->a, row, "path", c->id) ||
        esk_json_set_string(s->a, row, "reference", c->kind) ||
        esk_json_set_string(s->a, row, "model", c->ops->name) ||
        esk_json_set_string(s->a, row, "catalog", key) ||
        put(s, row, "params",
            c->params ? esk_json_clone(s->a, c->params) : esk_json_new_object(s->a)) ||
        esk_json_array_push(s->a, models, row)) {
        esk_json_free(s->a, row);
        return ES_ENOMEM;
    }
    return ES_OK;
}

static es_status add_instance(es_session *s, const esk_json *entry, const char *sheet,
                              const char *kind, esk_json *instances, esk_json *catalog)
{
    const char *id = esk_json_get_str(entry, "id", "");
    const char *ref = esk_json_get_str(entry, !strcmp(kind, "board") ? "board" : "component", "");
    const char *requested = esk_json_get_str(entry, "version", NULL);
    const esk_json *description = NULL;
    esk_comp *component = NULL;
    esk_json *row;
    char path[160], key[512];
    size_t i;
    es_status st;
    if (strlen(sheet) + strlen(id) + (*sheet ? 1u : 0u) >= sizeof path ||
        strlen(kind) + strlen(ref) + 1u >= sizeof key)
        return ES_ELIMIT;
    esk_snprintf(path, sizeof path, "%s%s%s", sheet, *sheet ? "." : "", id);
    esk_snprintf(key, sizeof key, "%s:%s", kind, ref);
    if (!strcmp(kind, "board")) {
        for (i = 0; i < s->boards.len; i++)
            if (!strcmp(s->boards.data[i]->id, id)) {
                description = s->boards.data[i]->item->desc;
                break;
            }
    } else {
        component = esk_engine_comp(&s->eng, path);
        if (component)
            description = component->desc;
    }
    st = describe(s, description, kind, ref, key, catalog);
    if (st != ES_OK)
        return st;
    row = esk_json_new_object(s->a);
    if (!row)
        return ES_ENOMEM;
    if (esk_json_set_string(s->a, row, "path", path) ||
        esk_json_set_string(s->a, row, "sheet", sheet) ||
        esk_json_set_string(s->a, row, "id", id) || esk_json_set_string(s->a, row, "kind", kind) ||
        esk_json_set_string(s->a, row, "reference", ref) ||
        esk_json_set_string(s->a, row, "catalog", key) ||
        esk_json_set_string(s->a, row, "resolution",
                            component     ? "native"
                            : description ? "board-assembly"
                                          : "unavailable") ||
        (requested && esk_json_set_string(s->a, row, "requested_version", requested)) ||
        (component && esk_json_set_string(s->a, row, "model", component->ops->name)) ||
        put(s, row, "params",
            component && component->params ? esk_json_clone(s->a, component->params)
                                           : esk_json_new_object(s->a)) ||
        esk_json_array_push(s->a, instances, row)) {
        esk_json_free(s->a, row);
        return ES_ENOMEM;
    }
    return ES_OK;
}

static es_status visit(es_session *s, const esk_json *sheet, const char *path, esk_json *instances,
                       esk_json *catalog, unsigned depth, size_t *budget)
{
    const esk_json *parts = esk_json_get(sheet, "components");
    size_t i;
    if (depth > 8)
        return ES_ELIMIT;
    if (!*path) {
        const esk_json *boards = esk_json_get(sheet, "boards");
        for (i = 0; i < esk_json_len(boards); i++) {
            es_status st;
            if (!*budget)
                return ES_ELIMIT;
            (*budget)--;
            st = add_instance(s, esk_json_at(boards, i), path, "board", instances, catalog);
            if (st != ES_OK)
                return st;
        }
    }
    for (i = 0; i < esk_json_len(parts); i++) {
        const esk_json *entry = esk_json_at(parts, i);
        const char *ref = esk_json_get_str(entry, "component", "");
        es_status st;
        if (!*budget)
            return ES_ELIMIT;
        (*budget)--;
        if (esk_str_has_prefix(ref, "block/")) {
            char next[160];
            const esk_json *child = esk_json_get(esk_json_get(s->project, "blocks"), ref + 6);
            const char *id = esk_json_get_str(entry, "id", "");
            if (!child)
                return ES_ESCHEMA;
            if (strlen(path) + strlen(id) + (*path ? 1u : 0u) >= sizeof next)
                return ES_ELIMIT;
            esk_snprintf(next, sizeof next, "%s%s%s", path, *path ? "." : "", id);
            st = visit(s, child, next, instances, catalog, depth + 1, budget);
        } else {
            /* A catalog package is one assembly item; its simulation primitives
             * are deliberately not extra physical packages in a BOM. */
            st = add_instance(s, entry, path, "component", instances, catalog);
        }
        if (st != ES_OK)
            return st;
    }
    return ES_OK;
}

es_status esk_project_manifest(es_session *s, esk_json **out)
{
    esk_json *root, *instances, *catalog;
    es_status st;
    size_t budget = 10000, i;
    *out = NULL;
    if (!s->loaded || !s->project)
        return ES_ESTATE;
    root = esk_json_new_object(s->a);
    if (!root)
        return ES_ENOMEM;
    if (esk_json_set_integer(s->a, root, "schema_version", 1) ||
        esk_json_set_string(s->a, root, "kind", "engineering_manifest") ||
        esk_json_set_string(s->a, root, "source", "native") ||
        esk_json_set_string(s->a, root, "engine", es_version().string) ||
        esk_json_set_integer(s->a, root, "abi", es_abi_version()) ||
        esk_json_set_u64_string(s->a, root, "now_ps", s->eng.sched.now) ||
        esk_json_set_string(s->a, root, "policy",
                            s->eng.policy == ESK_POLICY_FUNCTIONAL ? "functional"
                            : s->eng.policy == ESK_POLICY_TIMED    ? "timed"
                                                                   : "electrical") ||
        put(s, root, "project", document(s, s->project)) ||
        put(s, root, "libraries",
            s->catalog->libraries ? esk_json_clone(s->a, s->catalog->libraries)
                                  : esk_json_new_array(s->a)) ||
        put(s, root, "instances", esk_json_new_array(s->a)) ||
        put(s, root, "models", esk_json_new_array(s->a)) ||
        put(s, root, "catalog", esk_json_new_object(s->a))) {
        esk_json_free(s->a, root);
        return ES_ENOMEM;
    }
    instances = esk_json_get_mut(root, "instances");
    catalog = esk_json_get_mut(root, "catalog");
    st = visit(s, s->project, "", instances, catalog, 0, &budget);
    if (st != ES_OK) {
        esk_json_free(s->a, root);
        return st;
    }
    for (i = 0; i < s->eng.comps.len; i++) {
        st = add_model(s, s->eng.comps.data[i], esk_json_get_mut(root, "models"), catalog);
        if (st != ES_OK) {
            esk_json_free(s->a, root);
            return st;
        }
    }
    *out = document(s, root);
    esk_json_free(s->a, root);
    if (!*out)
        return ES_ENOMEM;
    if (esk_json_set_integer(s->a, *out, "schema_version", 1) ||
        esk_json_set_string(s->a, *out, "kind", "engineering_manifest")) {
        esk_json_free(s->a, *out);
        *out = NULL;
        return ES_ENOMEM;
    }
    return ES_OK;
}
