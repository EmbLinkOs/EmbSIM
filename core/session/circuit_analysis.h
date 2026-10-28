/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_CIRCUIT_ANALYSIS_H
#define ESK_CIRCUIT_ANALYSIS_H
#include "session/session_priv.h"
/* Ideal observations: resolve a native net, never invent volts from logic. */
esk_net *esk_circuit_measurement(es_session *s, const char *target);
/* Isolated reset-and-uniform-sample capture. No firmware/host stimulus replay,
 * operating-point, frequency-domain, bandwidth or device qualification claim. */
es_status esk_session_transient(es_session *s, const esk_json *request, esk_json **result, char *why, size_t cap);
#endif
