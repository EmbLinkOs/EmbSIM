/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_SWEEP_H
#define ESK_SWEEP_H
#include "session/session_priv.h"
/* Independent reset-and-sample runs, NOT a DC operating-point solver.
 * Circuit-only electrical policy; no firmware, external stimuli or host I/O. */
es_status esk_session_sweep(es_session *s, const esk_json *request, esk_json **result, char *why, size_t cap);
#endif
