/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_PROJECT_MANIFEST_H
#define ESK_PROJECT_MANIFEST_H
#include "session/session_priv.h"
/* Read-only, one session cut: canonical project, loaded descriptions and native parameters. */
es_status esk_project_manifest(es_session *s, esk_json **out);
#endif
