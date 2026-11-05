/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_XTENSA_WINDOWS_H
#define ESK_XTENSA_WINDOWS_H
#include "mcu/xtensa/execute.h"
#include "mcu/xtensa/interrupts.h"
#define ESK_XT_PS_MASK UINT32_C(0x00070fff)
#define ESK_XT_PS_EXCM UINT32_C(0x10)
#define ESK_XT_PS_WOE UINT32_C(0x40000)
/* Experimental LX6 64-AR register-window execution, NOT SoC support.
 * cpu.a is the editable visible cache; physical ARs synchronize on successful
 * instructions/exception transitions. Init configures an ABI test state, not
 * silicon power-on defaults. Legacy vectors are caller-supplied; explicit
 * reset/relocation profiles use pinned original-ESP32 vector configuration.
 * MOVSP checks the preceding three windows and raises configured AllocaCause
 * without changing SP; firmware owns base-save-area relocation and retry.
 * Selected special registers, SAR and opt-in XEA2-with-DEPC exception entry
 * are implemented. CPU interrupt/timer delivery is opt-in; the SoC interrupt
 * matrix, automatic bus/MMU fault dispatch, complete ISA, MMU/caches and
 * instruction timing remain unsupported. MEMW/ISYNC/ESYNC only serialize
 * the already synchronous functional profile; no asynchronous pipeline claim. */
typedef struct esk_xtensa_windows {
    esk_xtensa_state cpu;
    uint32_t ar[64], ps, base, start, epc1, lbeg, lend, lcount;
    uint32_t sar, exccause, excvaddr, depc, scompare1, exceptions_enabled;
    uint32_t exception_vectors[3]; /* kernel, user, double; configured explicitly */
    esk_xtensa_interrupts irq;
    uint32_t vectors[6]; /* overflow4, underflow4, overflow8, underflow8, overflow12, underflow12 */
    uint32_t vecbase,vecbase_enabled;
    uint32_t threadptr,misc[4],prid,prid_enabled;
    uint32_t atomctl,atomctl_enabled;
} esk_xtensa_windows;
/* event 0: retired; 1..6: window transition; 7/8/9: kernel/user/double exception;
 * 11..17: accepted interrupt level 1..7; 18: suspended WAITI (step only).
 * Exception entry consumes zero bytes. Window instructions retry after spill/fill;
 * syscall/illegal handlers explicitly decide whether to advance EPC1/DEPC.
 * Architectural exception entries intentionally update saved state and PC.
 * Other failures retain state and return the ordinary fault report. */
/* THREADPTR UR231 and MISC0..3 SR244..247 are per-core, full-width state.
 * Init zeros them only as fixture setup; supported CPU reset preserves their
 * undefined architectural values as the declared deterministic policy.
 * PRID SR235 is read-only and unsupported until explicit reset-pin fixture
 * configuration. This supplies a constant board/SoC input, not a guessed ESP32
 * silicon ID or a live pin-waveform model; it survives supported CPU reset. */
es_status esk_xtensa_prid_configure(esk_xtensa_windows *state,uint32_t value);
/* Opt-in conditional-store ATOMCTL SR99, six bits, reset 0x28.
 * RSR/WSR/XSR privileged; reserved upper-bit writes fail unchanged.
 * S32C1I requires bus atomic_type: LOCAL ignores fields; 0 raises cause 3,
 * 1 uses indivisible RCW callback; internal cache (2) / reserved (3) fail.
 * Cache/internal-operation, Exclusive Access, bus waveforms/timing unqualified.
 * Legacy ABI fixtures remain disabled until opt-in; CPU reset enables it. */
es_status esk_xtensa_atomctl_configure(esk_xtensa_windows *state);
/* Explicit ESP32 relocatable-vector profile. Only 1024-aligned bases are
 * qualified; unaligned/lock-bit writes fail unchanged rather than guessing
 * silicon masking. Aligned RSR/WSR/XSR.VECBASE use SR231 when enabled.
 * This sets locations, not exception/IRQ enables. Legacy fixture vectors stay
 * in effect until explicitly enabled; while active, VECBASE owns locations. */
es_status esk_xtensa_vecbase_configure(esk_xtensa_windows *state,uint32_t base);
/* Reset the supported original-ESP32 CPU subset, on an initialized profile.
 * Explicit reset-vector input: 0=0x50000000, 1=0x40000400. PS=0x1f,
 * LCOUNT=0, VECBASE=0x40000000; exception/IRQ profiles enabled and WAITI
 * canceled. Undefined architectural registers retain fixture values; pending
 * IRQ state is chosen zero and external levels must be re-driven. These are
 * deterministic choices, NOT silicon retention guarantees. Missing ISA/debug/
 * MMU/cache reset state, pin selection and power/reset timing stay unqualified. */
es_status esk_xtensa_lx6_reset(esk_xtensa_windows *state,unsigned vector_input);
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
