/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_XTENSA_WINDOWS_H
#define ESK_XTENSA_WINDOWS_H
#include "mcu/xtensa/execute.h"
#include "mcu/xtensa/interrupts.h"
#define ESK_XT_PS_EXCM UINT32_C(0x10)
#define ESK_XT_PS_WOE UINT32_C(0x40000)
/* Experimental LX6 64-AR register-window execution, NOT SoC support.
 * cpu.a is the editable visible cache; physical ARs synchronize on successful
 * instructions/exception transitions. Init configures an ABI test state, not
 * silicon power-on defaults. Vectors are caller-supplied, not inferred.
 * Selected special registers, SAR and opt-in XEA2-with-DEPC exception entry
 * are implemented. CPU interrupt/timer delivery is opt-in; the SoC interrupt
 * matrix, automatic bus/MMU fault dispatch, complete ISA, MMU/caches and
 * instruction timing remain unsupported. MEMW/ISYNC/ESYNC only serialize
 * the already synchronous functional profile; no asynchronous pipeline claim. */
typedef struct esk_xtensa_windows {
    esk_xtensa_state cpu;
    uint32_t ar[64], ps, base, start, epc1, lbeg, lend, lcount;
    uint32_t sar, exccause, excvaddr, depc, exceptions_enabled;
    uint32_t exception_vectors[3]; /* kernel, user, double; configured explicitly */
    esk_xtensa_interrupts irq;
    uint32_t vectors[6]; /* overflow4, underflow4, overflow8, underflow8, overflow12, underflow12 */
} esk_xtensa_windows;
/* event 0: retired; 1..6: window transition; 7/8/9: kernel/user/double exception;
 * 11..17: accepted interrupt level 1..7; 18: suspended WAITI (step only).
 * Exception entry consumes zero bytes. Window instructions retry after spill/fill;
 * syscall/illegal handlers explicitly decide whether to advance EPC1/DEPC.
 * Architectural exception entries intentionally update saved state and PC.
 * Other failures retain state and return the ordinary fault report. */
/* Explicit exception profile; no SoC vector addresses inferred. Unknown or
 * unimplemented instructions remain unsupported, never silently illegal. */
void esk_xtensa_exception_configure(esk_xtensa_windows *state, const uint32_t vectors[3]);
es_status esk_xtensa_raise_exception(esk_xtensa_windows *state, unsigned cause,
                                     uint32_t address, int has_address, unsigned *event);
void esk_xtensa_windows_init(esk_xtensa_windows *state, uint32_t pc, const uint32_t vectors[6]);
es_status esk_xtensa_window_execute(esk_xtensa_windows *state, const esk_xtensa_memory *memory,
                                    const unsigned char *code, size_t available,
                                    size_t *consumed, unsigned *event, esk_xtensa_fault *fault);
/* Poll configured CPU interrupts before fetch. event 18 means suspended WAITI
 * and no fetch/retirement; explicit clock/input advances can make it runnable. */
es_status esk_xtensa_window_step(esk_xtensa_windows *state, const esk_xtensa_memory *memory,
                                 unsigned *event, esk_xtensa_fault *fault);
#endif
