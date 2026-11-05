/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_MACHINE_H
#define ESK_ESP32_MACHINE_H
#include "mcu/esp32/fabric.h"
#include "mcu/xtensa/windows.h"
typedef struct esk_esp32_machine esk_esp32_machine;
/* Experimental two-context execution harness over shared internal memory.
 * Deterministic serialized instruction turns, not a cycle scheduler or full SoC.
 * No DPORT/reset/stall registers, boot ROM, peripherals, interrupt matrix,
 * cache arbitration/coherence or board model. Host controls are debug/test
 * operations, not emulated hardware control registers. No concurrent callers. */
es_status esk_esp32_machine_create(esk_alloc *a,esk_esp32_machine **out);
void esk_esp32_machine_destroy(esk_esp32_machine *m);
esk_esp32_fabric *esk_esp32_machine_fabric(esk_esp32_machine *m);
/* Clear one architecture profile, pause it and set explicit entry. Shared
 * memory and per-core remap survive. This is not a silicon reset sequence. */
es_status esk_esp32_machine_configure(esk_esp32_machine *m,unsigned cpu,uint32_t entry);
/* Mutable profile inspection/setup only between execution turns. */
esk_xtensa_windows *esk_esp32_machine_core(esk_esp32_machine *m,unsigned cpu);
es_status esk_esp32_machine_run_core(esk_esp32_machine *m,unsigned cpu,int run);
/* Paused core: event 19, no fetch. Fault pauses that core at the failing PC.
 * Interrupt/window/wait events retain their execution-engine meanings. */
es_status esk_esp32_machine_step(esk_esp32_machine *m,unsigned cpu,unsigned *event,esk_xtensa_fault *fault);
/* One turn per core, 0 then 1; stop at first fault. Earlier successful turns
 * remain committed. failed_cpu=2 when neither failed. No implicit clock ticks. */
es_status esk_esp32_machine_round(esk_esp32_machine *m,unsigned events[2],unsigned *failed_cpu,esk_xtensa_fault *fault);
uint64_t esk_esp32_machine_retired(const esk_esp32_machine *m,unsigned cpu);
#endif
