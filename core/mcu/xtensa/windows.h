/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_XTENSA_WINDOWS_H
#define ESK_XTENSA_WINDOWS_H
#include "mcu/xtensa/execute.h"
#define ESK_XT_PS_EXCM UINT32_C(0x10)
#define ESK_XT_PS_WOE UINT32_C(0x40000)
/* Experimental LX6 64-AR register-window execution, NOT SoC support.
 * cpu.a is the editable visible cache; physical ARs synchronize on successful
 * instructions/exception transitions. Init configures an ABI test state, not
 * silicon power-on defaults. Vectors are caller-supplied, not inferred.
 * Only PS/WINDOWBASE/WINDOWSTART/EPC1 and loop special registers are implemented.
 * Generic exceptions, interrupts, MMU/caches and timing remain unsupported. */
typedef struct esk_xtensa_windows {
    esk_xtensa_state cpu;
    uint32_t ar[64], ps, base, start, epc1, lbeg, lend, lcount;
    uint32_t vectors[6]; /* overflow4, underflow4, overflow8, underflow8, overflow12, underflow12 */
} esk_xtensa_windows;
/* event 0: an instruction retired; 1..6: window exception transition. On a
 * window event consumed is zero (retry instruction after spill/fill handler).
 * Window exceptions intentionally change state, including base, PS and PC.
 * Other failures retain state and return the ordinary fault report. */
void esk_xtensa_windows_init(esk_xtensa_windows *state, uint32_t pc, const uint32_t vectors[6]);
es_status esk_xtensa_window_execute(esk_xtensa_windows *state, const esk_xtensa_memory *memory,
                                    const unsigned char *code, size_t available,
                                    size_t *consumed, unsigned *event, esk_xtensa_fault *fault);
es_status esk_xtensa_window_step(esk_xtensa_windows *state, const esk_xtensa_memory *memory,
                                 unsigned *event, esk_xtensa_fault *fault);
#endif
