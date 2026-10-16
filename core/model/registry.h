/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* registry.h -- the model registry (SPEC-005 §10): primitive names to
 * esk_model_ops, for digital primitives and for the analog devices wrapped
 * as components. Built per session (CS-18: no mutable globals). */
#ifndef ESK_REGISTRY_H
#define ESK_REGISTRY_H

#include "engine/engine.h"
#include "util/map.h"

typedef struct esk_registry {
    esk_alloc *a;
    esk_map by_name;             /* name -> esk_model_ops* */
    esk_model_ops *wrappers;     /* generated ops for analog devices */
    size_t nwrappers;
} esk_registry;

es_status esk_registry_init(esk_registry *r, esk_alloc *a);
void esk_registry_free(esk_registry *r);
const esk_model_ops *esk_registry_find(const esk_registry *r, const char *name);
/* the digital primitives (digital.c) */
const esk_model_ops *const *esk_digital_models(size_t *n);
/* a JSON array of {name, kind: "digital"|"electrical"} */
esk_json *esk_registry_list(const esk_registry *r);

#endif
