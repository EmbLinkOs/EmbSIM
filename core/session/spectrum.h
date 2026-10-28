/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_SPECTRUM_H
#define ESK_SPECTRUM_H
#include "session/session_priv.h"
/* Stateless coherent-amplitude FFT of an inspectable captured record. No new
 * circuit execution, PSD, anti-alias filtering or small-signal AC solver. */
es_status esk_session_spectrum(es_session *s, const esk_json *request, esk_json **result, char *why, size_t cap);
#endif
