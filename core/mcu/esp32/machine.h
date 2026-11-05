/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_MACHINE_H
#define ESK_ESP32_MACHINE_H
#include "mcu/esp32/fabric.h"
#include "mcu/esp32/matrix.h"
#include "mcu/xtensa/windows.h"
typedef struct esk_esp32_machine esk_esp32_machine;
/* Experimental two-context execution harness over shared internal memory.
 * Deterministic serialized instruction turns, not a cycle scheduler or full SoC.
 * No vendor boot ROM, peripheral generators,
 * cache arbitration/coherence or board model. Host controls are debug/test
 * operations, not emulated hardware control registers. No concurrent callers. */
es_status esk_esp32_machine_create(esk_alloc *a,esk_esp32_machine **out);
void esk_esp32_machine_destroy(esk_esp32_machine *m);
esk_esp32_fabric *esk_esp32_machine_fabric(esk_esp32_machine *m);
/* APP clock enable/stall initialize to documented zero; explicitly enable
 * 0x3ff00030 before executing APP. 0x3ff00034 stalls/resumes without reset.
 * 0x3ff00038 is the ROM-consumed boot address, not a direct PC setter.
 * Reset register 0x3ff0002c is opt-in through the API below. These functional controls
 * do not qualify clock-domain synchronizers, retention or physical latency. */
/* Unified data bus includes interrupt-matrix and APP-control MMIO; fetch uses instruction
 * memory and CAS remains SRAM-only.
 * Direct host bus calls are permitted only between turns. Matrix peripheral
 * inputs belong exclusively to the machine: use source/GPIO inputs below,
 * not irq_line on CPU peripheral input numbers. Internal timer/software and
 * profiling inputs remain CPU-local. No API is re-entrant or thread-safe. */
esk_xtensa_memory esk_esp32_machine_bus(esk_esp32_machine *m,unsigned cpu);
es_status esk_esp32_machine_source(esk_esp32_machine *m,unsigned source,int asserted);
es_status esk_esp32_machine_gpio_irq(esk_esp32_machine *m,unsigned cpu,int nmi,int asserted);
es_status esk_esp32_machine_nmi_mask(esk_esp32_machine *m,unsigned cpu,int asserted);
/* Opt into the supported APP reset-input profile, asserting reset now.
 * Select static vector input 0/1 explicitly; no board strap inference. DPORT
 * 0x3ff0002c bit 0 then holds/releases APP. Assertion applies the supported
 * CPU reset subset after instruction commit (or immediately for host calls).
 * RAM/ROM/remap/matrix/clock/stall/BOOT_ADDR/peer state and diagnostic retired
 * totals survive; undefined CPU-register choices are documented in lx6_reset.
 * Held reset reports event 22 and suppresses clock/deadline/IRQ delivery. This
 * is not power-on/RTC/watchdog reset-domain, actual ROM or silicon timing proof. */
es_status esk_esp32_machine_app_reset_configure(esk_esp32_machine *m,unsigned vector_input);
/* Supply clock ticks through the gate-aware machine boundary. APP ticks are
 * dropped while its clock is disabled or reset is held. Enabled-clock ticks
 * still advance the functional counter during stall/WAITI/debug pause. No wall-time conversion,
 * synchronizer latency or physical debug-clock policy is implied. Direct
 * low-level clock_advance(core) is a debug bypass for gate/reset inputs. */
es_status esk_esp32_machine_clock_advance(esk_esp32_machine *m,unsigned cpu,uint64_t ticks);
int esk_esp32_machine_clock_deadline(const esk_esp32_machine *m,unsigned cpu,uint64_t *ticks);
/* Clear one architecture profile, pause it and set explicit entry. Shared
 * memory, matrix, APP controls and per-core remap survive.
 * This is not a silicon reset sequence. APP fixture configuration is rejected
 * after opting into its reset-input profile; PRO fixture setup stays available. */
es_status esk_esp32_machine_configure(esk_esp32_machine *m,unsigned cpu,uint32_t entry);
/* Mutable profile inspection/setup only between execution turns. */
esk_xtensa_windows *esk_esp32_machine_core(esk_esp32_machine *m,unsigned cpu);
es_status esk_esp32_machine_run_core(esk_esp32_machine *m,unsigned cpu,int run);
/* Paused core: 19; APP reset held: 22; clock disabled: 20; stalled: 21. No fetch
 * or interrupt delivery/retirement while suspended. Host pause takes priority
 * over held reset, then clock gate, then stall. Matrix levels remain outside
 * the CPU; delivery is suppressed while reset is held. Gated pulse sampling
 * remains unqualified.
 * Fault pauses that core at the failing PC.
 * Interrupt/window/wait events retain their execution-engine meanings. */
es_status esk_esp32_machine_step(esk_esp32_machine *m,unsigned cpu,unsigned *event,esk_xtensa_fault *fault);
/* One turn per core, 0 then 1; stop at first fault. Earlier successful turns
 * remain committed. failed_cpu=2 when neither failed. No implicit clock ticks. */
es_status esk_esp32_machine_round(esk_esp32_machine *m,unsigned events[2],unsigned *failed_cpu,esk_xtensa_fault *fault);
uint64_t esk_esp32_machine_retired(const esk_esp32_machine *m,unsigned cpu);
#endif
