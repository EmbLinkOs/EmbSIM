/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* catalog.h -- libraries of descriptions (SPEC-014): loading, lookup by id,
 * provenance. The public es_catalog handle is this struct. */
#ifndef ESK_CATALOG_H
#define ESK_CATALOG_H

#include "embsim/session.h"
#include "util/alloc.h"
#include "util/diag.h"
#include "util/json.h"
#include "util/map.h"
#include "util/vec.h"

typedef struct esk_catalog_item {
    char *id;        /* "emblink/resistor" */
    char *kind;      /* component | board | mcu | package | toolchain */
    char *version;
    char *dir;       /* directory holding the description and its files */
    char *path;      /* the description file */
    char *library;   /* library name */
    esk_json *desc;
} esk_catalog_item;

struct es_catalog {
    esk_alloc alloc;
    esk_alloc *a;
    ESK_VEC(esk_catalog_item *) items;
    esk_map by_id;      /* "kind:id" -> item */
    esk_json *library_manifests; /* loaded manifest by root, private dependency declarations */
    esk_json *libraries; /* array of {name, version, path, items} */
};

es_status esk_catalog_init(es_catalog *c);
void esk_catalog_free(es_catalog *c);
/* load a library directory (library.json + components/, boards/, mcus/, toolchains/); diag may be NULL */
es_status esk_catalog_load_library(es_catalog *c, const char *path, esk_diag *diag);
const esk_catalog_item *esk_catalog_find(const es_catalog *c, const char *kind, const char *id);
/* resolve the bundled catalog directory: $EMBSIM_CATALOG, else the compile-time
 * source directory's catalog/, else NULL */
char *esk_catalog_bundled_dir(esk_alloc *a, const char *exe_hint);
/* the user library directory (SPEC-014 §1): $EMBSIM_LIBRARIES, else
 * <user data>/libraries; NULL when neither is known. esk_strfree to release. */
char *esk_catalog_user_dir(esk_alloc *a);
/* load every library directory under `dir` (the user libraries) */
void esk_catalog_load_user_libraries(es_catalog *c, const char *dir, esk_diag *diag);
/* a file of an item, read into memory (symbol.svg etc.), NULL if missing */
char *esk_catalog_read_item_file(const es_catalog *c, const esk_catalog_item *it, const char *rel, size_t *len);
/* JSON listing for catalog.list */
esk_json *esk_catalog_list(const es_catalog *c, const char *kind, const char *category, const char *query);

#endif
