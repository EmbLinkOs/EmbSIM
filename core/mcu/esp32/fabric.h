/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_FABRIC_H
#define ESK_ESP32_FABRIC_H
#include "mcu/xtensa/execute.h"
#include "util/alloc.h"
typedef struct esk_esp32_fabric esk_esp32_fabric;
/* Experimental static internal-memory profile, not the full SoC bus. SRAM0
 * non-cache 128K, SRAM1 128K (reversed instruction words), SRAM2 200K, RTC
 * fast/slow 8K each. RTC fast is PRO-only. Optional read-only flash mapping
 * and an opt-in SPI1 read path are described below. No full cache/MMU/MPU,
 * other peripherals/DMA, RTC power/retention
 * or physical reset qualification. RAM starts zero only
 * for deterministic setup. Serialized single-thread calls only; no re-entry. */
es_status esk_esp32_fabric_create(esk_alloc *a,esk_esp32_fabric **out);
void esk_esp32_fabric_destroy(esk_esp32_fabric *f);
/* Optional externally provided ROM0/ROM1, exact 384K/64K. No ROM is fabricated. */
es_status esk_esp32_fabric_rom(esk_esp32_fabric *f,unsigned which,const unsigned char *data,size_t len);
/* Opt into copied read-only external flash: whole 64K pages, at most 16MB.
 * Adds PID0/1 normal-mode MMU tables and a bounded cache-control/flush profile.
 * Tables start invalid by fixture policy. Edit while target cache disabled,
 * flush via clear/set FLUSH_ENA, then enable. No cache tags/timing, SPI writes,
 * PSRAM/high-PID/lock/special modes or physical flash-part identity claim.
 * Replacement requires both caches disabled and preserves mappings; allocation
 * failure leaves the old image/state intact. Host calls only between turns. */
es_status esk_esp32_fabric_flash(esk_esp32_fabric *f,const unsigned char *data,size_t len);
/* Opt-in SPI1 USER 03h read path to the installed immutable backing. Both
 * caches must be disabled during transfers. No chip ID/program/erase/timing.
 * Enabling/disabling preserves SPI state; CPU-only reset also preserves it. */
es_status esk_esp32_fabric_spi1_configure(esk_esp32_fabric *f,int enabled);
/* ES_EUNSUPPORTED means disabled/unowned; output is unchanged on failure. */
es_status esk_esp32_fabric_spi1_irq(const esk_esp32_fabric *f,int *asserted);
/* Host-controlled boot-remap bit, not a DPORT register model. */
es_status esk_esp32_fabric_remap(esk_esp32_fabric *f,unsigned cpu,int enabled);
/* All-or-nothing RAM image write through the selected core's address view. */
es_status esk_esp32_fabric_load(esk_esp32_fabric *f,unsigned cpu,uint32_t address,const unsigned char *data,size_t len);
/* Fetch and data accesses differ: instruction-only views permit data words,
 * not narrow data operations. CAS is limited to SRAM1/2 data views. */
esk_xtensa_memory esk_esp32_fabric_bus(esk_esp32_fabric *f,unsigned cpu);
#endif
