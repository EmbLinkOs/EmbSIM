/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_LOADER_H
#define ESK_LOADER_H
#include "session/session_priv.h"
/* parse a pin reference "INSTANCE.PIN" (the last dot splits; instances may contain dots) */
int esk_split_pin_ref(const char *ref, char *inst, size_t icap, char *pin, size_t pcap);
#endif
