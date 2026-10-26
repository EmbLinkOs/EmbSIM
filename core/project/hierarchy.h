/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_HIERARCHY_H
#define ESK_HIERARCHY_H
#include "session/session_priv.h"
es_status esk_project_blocks_prepare(es_session *s);
void esk_project_blocks_free(es_session *s);
const esk_catalog_item *esk_project_component(const es_session *s, const char *id);
esk_json *esk_project_hierarchy(es_session *s, const char *trace);
#endif
