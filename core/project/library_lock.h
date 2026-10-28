/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_LIBRARY_LOCK_H
#define ESK_LIBRARY_LOCK_H
#include "session/session_priv.h"
/* Complete non-hidden library trees plus the effective catalog selection.
 * These calls never advance time or change the project/catalog. */
es_status esk_library_lock_capture(es_session *s, esk_json **out);
es_status esk_library_lock_check(es_session *s, const esk_json *lock, esk_json **report);
es_status esk_library_lock_enforce(es_session *s, const esk_json *project);
#endif
