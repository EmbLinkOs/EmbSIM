/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_FABRIC_H
#define ESK_ESP32_FABRIC_H
#include "mcu/xtensa/execute.h"
#include "util/alloc.h"
typedef struct esk_esp32_fabric esk_esp32_fabric;
/* Experimental static internal-memory profile, not the full SoC bus. SRAM0
 * non-cache 128K, SRAM1 128K (reversed instruction words), SRAM2 200K, RTC
 * fast/slow 8K each. RTC fast is PRO-only. No cache/MMU/MPU, flash, peripherals,
 * DMA, RTC power/retention or physical reset qualification. RAM starts zero only
 * for deterministic setup. Serialized single-thread calls only; no re-entry. */
es_status esk_esp32_fabric_create(esk_alloc *a,esk_esp32_fabric **out);
void esk_esp32_fabric_destroy(esk_esp32_fabric *f);
/* Optional externally provided ROM0/ROM1, exact 384K/64K. No ROM is fabricated. */
es_status esk_esp32_fabric_rom(esk_esp32_fabric *f,unsigned which,const unsigned char *data,size_t len);
/* Host-controlled boot-remap bit, not a DPORT register model. */
es_status esk_esp32_fabric_remap(esk_esp32_fabric *f,unsigned cpu,int enabled);
/* All-or-nothing RAM image write through the selected core's address view. */
es_status esk_esp32_fabric_load(esk_esp32_fabric *f,unsigned cpu,uint32_t address,const unsigned char *data,size_t len);
/* Fetch and data accesses differ: instruction-only views permit data words,
 * not narrow data operations. CAS is limited to SRAM1/2 data views. */
esk_xtensa_memory esk_esp32_fabric_bus(esk_esp32_fabric *f,unsigned cpu);
#endif
