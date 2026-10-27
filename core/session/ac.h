/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_SESSION_AC_H
#define ESK_SESSION_AC_H
#include "session/session_priv.h"
/* Isolated linear AC sweep. Unsupported/nonlinear devices are rejected. */
es_status esk_session_ac(es_session *s, const esk_json *request, esk_json **result, char *why,
                         size_t cap);
#endif
