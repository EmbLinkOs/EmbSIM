/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* build.h -- a machine from an MCU description and its register description. */
#ifndef ESK_MCU_BUILD_H
#define ESK_MCU_BUILD_H

#include "mcu/mach.h"
#include "util/json.h"

/* Fill an initialised machine: memory, aliases, the core, the register file
 * (`regs` may be NULL) and the models. `hse_hz` is the board's external
 * oscillator (0: none). Errors name the problem in `why`. Host hooks should
 * be set before, so the binding's diagnostics reach the session. */
es_status esk_mach_build(struct esk_mach *m, const esk_json *mcu, const esk_json *regs, u32 hse_hz,
                         char *why, size_t cap);

#endif
