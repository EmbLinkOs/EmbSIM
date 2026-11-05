/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_XTENSA_EXECUTE_H
#define ESK_XTENSA_EXECUTE_H
#include "embsim/types.h"
/* Experimental non-windowed execution engine, not an ESP32 SoC/CPU backend.
 * Virtual addresses equal bus addresses: no MMU, protection, caches, special
 * registers, exceptions/interrupts, windows, timing or SMP. Faults are returned
 * to the caller with PC/address/opcode, not dispatched to architectural vectors.
 * Sequential callbacks provide ordering but make no multicore ordering claim.
 * Callback failure MUST mean no completed write. No rollback of MMIO reads.
 * Reads/writes must finish synchronously before returning; posted operations,
 * stale private caches and deferred visibility require a different interface.
 * This serialized provider contract supplies L32AI/S32RI ordering, not a
 * pipeline/cache/coherence model or an asynchronous system-bus barrier.
 * Callbacks must not re-enter or mutate the executing CPU state. Queue its
 * input/clock changes for instruction boundaries; these APIs are not thread-safe. */
typedef struct esk_xtensa_state { uint32_t a[16], pc; } esk_xtensa_state;
enum { ESK_XT_MEM_LOCAL, ESK_XT_MEM_BYPASS, ESK_XT_MEM_WRITETHROUGH, ESK_XT_MEM_WRITEBACK };
typedef struct esk_xtensa_memory {
    void *ctx;
    es_status (*read)(void *ctx, uint32_t address, unsigned bytes, uint32_t *value);
    es_status (*write)(void *ctx, uint32_t address, unsigned bytes, uint32_t value);
    /* Must be one indivisible conditional exchange shared by all bus agents.
     * Return original memory in observed on success, whether matched or not.
     * On failure no write may complete. No read/write fallback is allowed. */
    es_status (*compare_exchange)(void *ctx,uint32_t address,uint32_t expected,uint32_t desired,uint32_t *observed);
    /* Optional instruction-bus access. Null uses read for legacy flat test buses. */
    es_status (*fetch)(void *ctx,uint32_t address,unsigned bytes,uint32_t *value);
    /* Optional pure classification for ATOMCTL-enabled conditional stores.
     * Must classify the complete aligned word without an access/side effect.
     * LOCAL bypasses ATOMCTL; other values select its BY/WT/WB field.
     * Unknown attributes must fail, never silently select bypass. */
    es_status (*atomic_type)(void *ctx,uint32_t address,unsigned *type);
} esk_xtensa_memory;
enum { ESK_XT_OK, ESK_XT_UNSUPPORTED, ESK_XT_TRUNCATED, ESK_XT_FETCH,
       ESK_XT_ALIGN, ESK_XT_LOAD, ESK_XT_STORE, ESK_XT_DIV_ZERO, ESK_XT_ATOMIC };
typedef struct esk_xtensa_fault { unsigned kind; uint32_t pc, address, opcode; } esk_xtensa_fault;
/* Execute supplied instruction bytes; no state commit on failure. */
es_status esk_xtensa_execute(esk_xtensa_state *state, const esk_xtensa_memory *memory,
                             const unsigned char *code, size_t available,
                             size_t *consumed, esk_xtensa_fault *fault);
/* As execute, also reports sequential increment (not a taken branch/jump/call),
 * for hardware loop-end handling. A target equal to PC+width is still taken. */
es_status esk_xtensa_execute_flow(esk_xtensa_state *state, const esk_xtensa_memory *memory,
                                  const unsigned char *code, size_t available,
                                  size_t *consumed, esk_xtensa_fault *fault, unsigned *sequential);
/* Fetch exactly 2/3 bytes through fetch (or read fallback), then execute. */
es_status esk_xtensa_step(esk_xtensa_state *state, const esk_xtensa_memory *memory,
                          esk_xtensa_fault *fault);
#endif
