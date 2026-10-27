/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_VARIANTS_H
#define ESK_VARIANTS_H
#include "session/session_priv.h"
es_status esk_project_override(es_session *s, const esk_json *overrides, esk_json **result);
/* Pure project transformations; caller stages the resulting native project. */
int esk_project_validate_variants(const esk_json *project, char *why, size_t cap);
es_status esk_project_variant_base(es_session *s, esk_json **base);
es_status esk_project_variant_preview(es_session *s, const char *name, esk_json **result);
es_status esk_project_variant_review(es_session *s, const char *name, esk_json **result);
#endif
