/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "project/version_rules.h"
#include "util/version.h"
#include "util/str.h"
#include <string.h>

es_status esk_project_require_version(es_session *s, const esk_catalog_item *item,
                                      const esk_json *entry, const char *subject)
{
    const esk_json *request = esk_json_get(entry, "version");
    const char *range, *actual;
    int match;
    if (!request) return ES_OK;
    range = esk_json_str(request);
    actual = item && item->version ? item->version : "";
    match = esk_version_satisfies(actual, range);
    if (match == 1) return ES_OK;
    esk_diag_raise(&s->diag, ES_SEV_ERROR, match < 0 ? "catalog.version.invalid" : "catalog.version.mismatch",
                   s->eng.sched.now, "component", subject, s->project_path, 0,
                   "%s requests version %s; first-match catalog selection is %s. %s",
                   item ? item->id : subject, range ? range : "(not a string)", *actual ? actual : "(unversioned project block)",
                   match < 0 ? "Malformed/unsupported version or range." : "Install a compatible library; selection is never silently changed.");
    return ES_ESCHEMA;
}

es_status esk_project_require_libraries(es_session *s)
{
    size_t i, j, k;
    const esk_json *libs = s->catalog->libraries;
    for (i = 0; i < esk_json_len(libs); i++) {
        const esk_json *lib = esk_json_at(libs, i);
        const char *path = esk_json_get_str(lib, "path", ""), *name = esk_json_get_str(lib, "name", "");
        const esk_json *manifest = esk_json_get(s->catalog->library_manifests, path);
        const esk_json *depends = esk_json_get(manifest, "depends");
        if (depends && depends->type != ESK_JSON_ARRAY) {
            esk_diag_raise(&s->diag, ES_SEV_ERROR, "library.dependency.invalid", 0, "library", name, NULL, 0,
                           "Library depends must be an array of named requirements.");
            return ES_ESCHEMA;
        }
        for (j = 0; j < esk_json_len(depends); j++) {
            const esk_json *dep = esk_json_at(depends, j), *selected = NULL;
            const char *target = esk_json_get_str(dep, "name", "");
            const esk_json *constraint = esk_json_get(dep, "version");
            int match;
            for (k = 0; k < esk_json_len(libs); k++) {
                const esk_json *candidate = esk_json_at(libs, k);
                if (!esk_str_eq(esk_json_get_str(candidate, "name", ""), target)) continue;
                if (selected && !esk_str_eq(esk_json_get_str(selected, "path", ""), esk_json_get_str(candidate, "path", ""))) {
                    esk_diag_raise(&s->diag, ES_SEV_ERROR, "library.dependency.ambiguous", 0, "library", name, NULL, 0,
                                   "Dependency %s has multiple loaded library roots; use a single explicit revision.", target);
                    return ES_ESCHEMA;
                }
                selected = candidate;
            }
            match = !selected || !*target ? 0 : constraint ? esk_version_satisfies(esk_json_get_str(selected, "version", ""), esk_json_str(constraint)) : 1;
            if (match != 1) {
                esk_diag_raise(&s->diag, ES_SEV_ERROR, !selected ? "library.dependency.missing" : match < 0 ? "library.dependency.invalid" : "library.dependency.mismatch",
                               0, "library", name, NULL, 0, "%s requires %s %s; loaded version is %s.", name, *target ? target : "(missing name)",
                               constraint ? esk_json_str(constraint) ? esk_json_str(constraint) : "(invalid range)" : "(any version)",
                               selected ? esk_json_get_str(selected, "version", "") : "(not installed)");
                return ES_ESCHEMA;
            }
        }
    }
    return ES_OK;
}
