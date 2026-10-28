/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_VERSION_RULES_H
#define ESK_VERSION_RULES_H
#include "session/session_priv.h"
es_status esk_project_require_version(es_session *s, const esk_catalog_item *item,
                                      const esk_json *entry, const char *subject);
es_status esk_project_require_libraries(es_session *s);
#endif
