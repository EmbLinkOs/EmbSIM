/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "project/variants.h"
#include "project/loader.h"
#include "project/manifest.h"
#include "util/sha256.h"
#include "util/str.h"
#include "util/version.h"
#include <string.h>

static int bad(char *why, size_t cap, const char *message)
{
    esk_snprintf(why, cap, "%s", message);
    return -1;
}
static int put(es_session *s, esk_json *o, const char *key, esk_json *v)
{
    if (!v) return -1;
    if (esk_json_object_set(s->a, o, key, v)) { esk_json_free(s->a, v); return -1; }
    return 0;
}
static const esk_json *member(const esk_json *sheet, const char *id)
{
    const esk_json *parts = esk_json_get(sheet, "components");
    size_t i;
    for (i = 0; i < esk_json_len(parts); i++) {
        const esk_json *part = esk_json_at(parts, i);
        if (esk_str_eq(esk_json_get_str(part, "id", ""), id)) return part;
    }
    return NULL;
}
static const esk_json *target(const esk_json *project, const char *path)
{
    const esk_json *sheet = project;
    unsigned depth = 0;
    const char *p = path;
    if (!p || !*p || strlen(p) >= 150) return NULL;
    for (;;) {
        const char *dot = strchr(p, '.');
        char id[160];
        const esk_json *part;
        size_t len = dot ? (size_t)(dot - p) : strlen(p);
        const char *ref;
        if (!len || len >= sizeof id || depth++ > 8) return NULL;
        memcpy(id, p, len); id[len] = 0;
        part = member(sheet, id);
        ref = esk_json_get_str(part, "component", "");
        if (!dot) return part && !esk_str_has_prefix(ref, "block/") ? part : NULL;
        if (!esk_str_has_prefix(ref, "block/")) return NULL;
        sheet = esk_json_get(esk_json_get(project, "blocks"), ref + 6);
        if (!sheet) return NULL;
        p = dot + 1;
    }
}

int esk_project_validate_variants(const esk_json *project, char *why, size_t cap)
{
    const esk_json *variants = esk_json_get(project, "variants");
    size_t i, j, k;
    {
        const esk_json *origin = esk_json_get(project, "variant_origin");
        if (origin) {
            const char *text = esk_json_get_str(origin, "json", NULL);
            char hash[72];
            if (!text || strlen(text) != esk_json_get(origin, "json")->u.string.len || strlen(text) > 8388608 || origin->type != ESK_JSON_OBJECT || esk_json_len(origin) != 3 ||
                !esk_json_get_str(origin, "name", NULL) || variants) return bad(why, cap, "Invalid derived variant origin; edit variant definitions in the base project.");
            esk_sha256_text(text, strlen(text), hash);
            if (!esk_str_eq(hash, esk_json_get_str(origin, "sha256", ""))) return bad(why, cap, "Variant base content hash mismatch.");
        }
    }
    if (!variants) return 0;
    if (variants->type != ESK_JSON_OBJECT || esk_json_len(variants) > 32) return bad(why, cap, "Expected at most 32 named assembly variants.");
    for (i = 0; i < esk_json_len(variants); i++) {
        const esk_json *variant = esk_json_at(variants, i), *overrides = esk_json_get(variant, "overrides");
        if (!variant->key || !esk_is_identifier(variant->key) || strlen(variant->key) > 63 || variant->type != ESK_JSON_OBJECT ||
            !overrides || overrides->type != ESK_JSON_OBJECT || esk_json_len(overrides) > 10000 || esk_json_len(variant) > 2 ||
            (esk_json_get(variant, "description") && !esk_json_str(esk_json_get(variant, "description"))))
            return bad(why, cap, "Each variant needs a name, overrides object and optional description.");
        for (j = 0; j < esk_json_len(variant); j++) {
            const char *key = esk_json_at(variant, j)->key;
            if (strcmp(key, "description") && strcmp(key, "overrides")) return bad(why, cap, "Unknown assembly variant definition field.");
        }
        for (j = 0; j < esk_json_len(overrides); j++) {
            const esk_json *change = esk_json_at(overrides, j);
            if (!target(project, change->key)) {
                if (strlen(change->key) > 149) return bad(why, cap, "Variant target exceeds 149 bytes.");
                esk_snprintf(why, cap, "Variant %s: invalid target %s (expected a project-owned physical component).", variant->key, change->key);
                return -1;
            }
            if (change->type != ESK_JSON_OBJECT || !esk_json_len(change) || esk_json_len(change) > 4)
                return bad(why, cap, "A variant override needs population, parameters, component or version.");
            {
                const esk_json *params = esk_json_get(change, "params");
                size_t param_index;
                for (param_index = 0; param_index < esk_json_len(params); param_index++) {
                    const esk_json *v = esk_json_at(params, param_index);
                    const char *key = v->key;
                    if (!key || *key < 'a' || *key > 'z' || strspn(key, "abcdefghijklmnopqrstuvwxyz0123456789_") != strlen(key) ||
                        (v->type != ESK_JSON_STRING && v->type != ESK_JSON_BOOL && v->type != ESK_JSON_NUMBER && v->type != ESK_JSON_ARRAY))
                        return bad(why, cap, "Variant parameters need declared-style names and number, string, boolean or array values.");
                }
            }
            for (k = 0; k < esk_json_len(change); k++) {
                const esk_json *value = esk_json_at(change, k);
                if ((!strcmp(value->key, "populated") && value->type == ESK_JSON_BOOL) ||
                    (!strcmp(value->key, "params") && value->type == ESK_JSON_OBJECT) ||
                    (!strcmp(value->key, "component") && value->type == ESK_JSON_STRING && !esk_str_has_prefix(esk_json_str(value), "block/")) ||
                    (!strcmp(value->key, "version") && (value->type == ESK_JSON_NULL || (value->type == ESK_JSON_STRING && esk_version_satisfies("0.0.0", esk_json_str(value)) >= 0)))) continue;
                return bad(why, cap, "Invalid variant override field or value type.");
            }
        }
    }
    return 0;
}

es_status esk_project_variant_base(es_session *s, esk_json **base)
{
    const esk_json *origin = esk_json_get(s->project, "variant_origin");
    const char *text = esk_json_get_str(origin, "json", NULL);
    char hash[72];
    *base = NULL;
    if (!s->loaded || !s->project) return ES_ESTATE;
    if (!origin) { *base = esk_json_clone(s->a, s->project); return *base ? ES_OK : ES_ENOMEM; }
    if (!text || strlen(text) != esk_json_get(origin, "json")->u.string.len || strlen(text) > 8388608) return ES_ESCHEMA;
    esk_sha256_text(text, strlen(text), hash);
    if (!esk_str_eq(hash, esk_json_get_str(origin, "sha256", ""))) return ES_ESCHEMA;
    *base = esk_json_parse(s->a, text, strlen(text), NULL, NULL);
    if (!*base || esk_json_get(*base, "variant_origin") || !esk_json_get(esk_json_get(*base, "variants"), esk_json_get_str(origin, "name", ""))) { esk_json_free(s->a, *base); *base = NULL; return ES_ESCHEMA; }
    return ES_OK;
}

/* Substitution is deliberately bounded to the same schematic terminal geometry;
 * changing a package/pin map requires editing the base circuit explicitly. */
static int compatible_replacement(es_session *s, const char *before, const char *after)
{
    const esk_catalog_item *a = esk_catalog_find(s->catalog, "component", before),
                           *b = esk_catalog_find(s->catalog, "component", after);
    const esk_json *as, *bs, *ap, *bp;
    size_t i;
    if (!a || !b) return 1; /* native staged load reports catalog.not_found */
    as = esk_json_get(a->desc, "symbol"); bs = esk_json_get(b->desc, "symbol");
    ap = esk_json_get(as, "pins"); bp = esk_json_get(bs, "pins");
    if (esk_json_get_num(as, "width", 0) != esk_json_get_num(bs, "width", 0) ||
        esk_json_get_num(as, "height", 0) != esk_json_get_num(bs, "height", 0) ||
        esk_json_len(ap) != esk_json_len(bp)) return 0;
    for (i = 0; i < esk_json_len(ap); i++) {
        const esk_json *pin = esk_json_at(ap, i), *other = esk_json_get(bp, pin->key);
        if (!other || esk_json_get_num(pin, "x", 0) != esk_json_get_num(other, "x", 0) ||
            esk_json_get_num(pin, "y", 0) != esk_json_get_num(other, "y", 0)) return 0;
    }
    return 1;
}

/* Clone only branches containing concrete overrides. Repeated sheets retain
 * independent population/parameter values without changing the base definition. */
static es_status materialize(es_session *s, esk_json *project, esk_json *sheet, const char *path,
                              const esk_json *overrides, unsigned depth, size_t *budget, unsigned *serial)
{
    esk_json *parts = esk_json_get_mut(sheet, "components");
    size_t i, j;
    if (depth > 8) return ES_ELIMIT;
    for (i = 0; i < esk_json_len(parts); i++) {
        esk_json *part = esk_json_at_mut(parts, i);
        const char *id = esk_json_get_str(part, "id", ""), *ref = esk_json_get_str(part, "component", "");
        char full[160];
        const esk_json *change;
        if (!*budget) return ES_ELIMIT;
        (*budget)--;
        if (strlen(path) + strlen(id) + (*path ? 1u : 0u) >= sizeof full) return ES_ELIMIT;
        esk_snprintf(full, sizeof full, "%s%s%s", path, *path ? "." : "", id);
        if (esk_str_has_prefix(ref, "block/")) {
            int affected = 0;
            for (j = 0; j < esk_json_len(overrides); j++) {
                const char *key = esk_json_at(overrides, j)->key;
                if (!strncmp(key, full, strlen(full)) && key[strlen(full)] == '.') { affected = 1; break; }
            }
            if (affected) {
                esk_json *blocks = esk_json_get_mut(project, "blocks");
                esk_json *clone = esk_json_clone(s->a, esk_json_get(blocks, ref + 6));
                char name[64], reference[80];
                es_status st;
                if (!clone) return ES_ENOMEM;
                do { esk_snprintf(name, sizeof name, "variant_sheet_%u", ++*serial); } while (esk_json_get(blocks, name));
                if (esk_json_len(blocks) >= 256) { esk_json_free(s->a, clone); return ES_ELIMIT; }
                if (put(s, blocks, name, clone)) return ES_ENOMEM;
                esk_snprintf(reference, sizeof reference, "block/%s", name);
                if (esk_json_set_string(s->a, part, "component", reference)) return ES_ENOMEM;
                st = materialize(s, project, clone, full, overrides, depth + 1, budget, serial);
                if (st != ES_OK) return st;
            }
            continue;
        }
        change = esk_json_get(overrides, full);
        if (esk_json_get_str(change, "component", NULL)) {
            if (!compatible_replacement(s, ref, esk_json_get_str(change, "component", ""))) return ES_EUNSUPPORTED;
            esk_json_remove(s->a, part, "params"); /* replacement starts from its own defaults */
        }
        for (j = 0; j < esk_json_len(change); j++) {
            const esk_json *value = esk_json_at(change, j);
            if (!strcmp(value->key, "params")) {
                esk_json *params = esk_json_get_mut(part, "params");
                size_t k;
                if (!params) {
                    if (put(s, part, "params", esk_json_new_object(s->a))) return ES_ENOMEM;
                    params = esk_json_get_mut(part, "params");
                }
                for (k = 0; k < esk_json_len(value); k++) {
                    const esk_json *param = esk_json_at(value, k);
                    if (put(s, params, param->key, esk_json_clone(s->a, param))) return ES_ENOMEM;
                }
            } else if (value->type == ESK_JSON_NULL) esk_json_remove(s->a, part, value->key);
            else if (put(s, part, value->key, esk_json_clone(s->a, value))) return ES_ENOMEM;
        }
    }
    return ES_OK;
}

/* Reuse concrete-path materialization without deriving a saved assembly artifact.
 * Analysis callers own validation and never replace s->project. */
es_status esk_project_override(es_session *s, const esk_json *overrides, esk_json **result)
{
    size_t budget = 10000;
    unsigned serial = 0;
    es_status st;
    *result = esk_json_clone(s->a, s->project);
    if (!*result) return ES_ENOMEM;
    st = materialize(s, *result, *result, "", overrides, 0, &budget, &serial);
    if (st != ES_OK) { esk_json_free(s->a, *result); *result = NULL; return st; }
    esk_json_remove(s->a, *result, "variants");
    esk_json_remove(s->a, *result, "variant_origin");
    return ES_OK;
}

es_status esk_project_variant_preview(es_session *s, const char *name, esk_json **result)
{
    esk_json *base = NULL, *effective = NULL, *out = NULL, *origin;
    const esk_json *variant;
    char *text = NULL, hash[72], why[256];
    size_t len = 0, budget = 10000;
    unsigned serial = 0;
    es_status st = esk_project_variant_base(s, &base);
    *result = NULL;
    if (st != ES_OK) return st;
    if (esk_project_validate_variants(base, why, sizeof why)) { st = ES_ESCHEMA; goto done; }
    variant = esk_json_get(esk_json_get(base, "variants"), name);
    if (!variant) { st = ES_ENOTFOUND; goto done; }
    text = esk_json_format(s->a, base, 0, &len);
    if (!text) { st = ES_ENOMEM; goto done; }
    if (len > 8388608) { st = ES_ELIMIT; goto done; }
    esk_sha256_text(text, len, hash);
    effective = esk_json_clone(s->a, base);
    if (!effective) { st = ES_ENOMEM; goto done; }
    st = materialize(s, effective, effective, "", esk_json_get(variant, "overrides"), 0, &budget, &serial);
    if (st != ES_OK) goto done;
    /* Definitions remain the source base's; cloned effective paths are not new targets. */
    esk_json_remove(s->a, effective, "variants");
    if (put(s, effective, "variant_origin", esk_json_new_object(s->a))) { st = ES_ENOMEM; goto done; }
    origin = esk_json_get_mut(effective, "variant_origin");
    if (esk_json_set_string(s->a, origin, "name", name) || esk_json_set_string(s->a, origin, "json", text) ||
        esk_json_set_string(s->a, origin, "sha256", hash)) { st = ES_ENOMEM; goto done; }
    out = esk_json_new_object(s->a);
    if (!out || esk_json_set_string(s->a, out, "base_sha256", hash)) { st = ES_ENOMEM; goto done; }
    if (put(s, out, "project", effective)) { effective = NULL; st = ES_ENOMEM; goto done; }
    effective = NULL;
    *result = out; out = NULL;
done:
    esk_free(s->a, text, len + 1);
    esk_json_free(s->a, base); esk_json_free(s->a, effective); esk_json_free(s->a, out);
    return st;
}


es_status esk_project_variant_review(es_session *s, const char *name, esk_json **result)
{
    es_session *temporary = NULL;
    es_session_config_t cfg;
    esk_json *preview = NULL;
    es_status st, checked;
    if (name) st = esk_project_variant_preview(s, name, &preview);
    else {
        esk_json *base = NULL;
        st = esk_project_variant_base(s, &base);
        if (st == ES_OK) {
            char *text, hash[72];
            size_t len = 0;
            text = esk_json_format(s->a, base, 0, &len);
            preview = esk_json_new_object(s->a);
            if (!text || !preview) st = ES_ENOMEM;
            if (st == ES_OK) {
                esk_sha256_text(text, len, hash);
                if (esk_json_set_string(s->a, preview, "base_sha256", hash)) st = ES_ENOMEM;
                else { int inserted = put(s, preview, "project", base); base = NULL; if (inserted) st = ES_ENOMEM; }
            }
            esk_free(s->a, text, len + 1);
        }
        esk_json_free(s->a, base);
    }
    memset(&cfg, 0, sizeof cfg);
    *result = NULL;
    if (st != ES_OK) { esk_json_free(s->a, preview); return st; }
    cfg.catalog = s->catalog;
    cfg.policy = s->eng.policy == ESK_POLICY_FUNCTIONAL ? "functional" : s->eng.policy == ESK_POLICY_TIMED ? "timed" : "electrical";
    st = es_session_create(&cfg, &temporary);
    if (st != ES_OK) { esk_json_free(s->a, preview); return st; }
    checked = esk_project_load(temporary, esk_json_get(preview, "project"), s->base_dir);
    if (checked == ES_OK) checked = esk_session_do_reset(temporary);
    if (esk_json_set_bool(s->a, preview, "valid", checked == ES_OK && !esk_diag_count(&temporary->diag, ES_SEV_ERROR))) st = ES_ENOMEM;
    {
        esk_json *diags = esk_diag_to_json(&temporary->diag);
        if (!diags || put(s, preview, "diagnostics", esk_json_clone(s->a, diags))) st = ES_ENOMEM;
        esk_json_free(temporary->a, diags);
    }
    if (checked == ES_OK) {
        esk_json *manifest = NULL;
        checked = esk_project_manifest(temporary, &manifest);
        if (checked == ES_OK && put(s, preview, "manifest", esk_json_clone(s->a, manifest))) checked = ES_ENOMEM;
        esk_json_free(temporary->a, manifest);
        if (checked != ES_OK) st = checked;
    }
    es_session_destroy(temporary);
    if (st == ES_OK) *result = preview;
    else esk_json_free(s->a, preview);
    return st;
}
