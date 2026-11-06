/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_MATRIX_H
#define ESK_ESP32_MATRIX_H
#include "embsim/types.h"
/* Original ESP32 interrupt matrix, TRM v5.9 ch.8/12. The 69 source slots
 * include reserved 41/42, which are rejected. GPIO slots 22/23 are core-local;
 * all other implemented sources are shared, including four software sources.
 * Peripheral generators and PID hardware are absent: external levels and PID
 * NMI-mask inputs must be supplied explicitly. No APB latency, pulse-width,
 * reset-domain or electrical timing qualification. Serialized calls only. */
typedef struct esk_esp32_matrix {
    uint32_t map[2][69],sources[3],gpio[2],ipi[4],nmi_mask[2];
} esk_esp32_matrix;
/* Documented map-register reset field=16 (disconnected); sources/IPC low. */
void esk_esp32_matrix_init(esk_esp32_matrix *m);
/* Shared peripheral input; reject GPIO, software-owned and reserved slots. */
es_status esk_esp32_matrix_source(esk_esp32_matrix *m,unsigned source,int asserted);
es_status esk_esp32_matrix_gpio(esk_esp32_matrix *m,unsigned cpu,int nmi,int asserted);
es_status esk_esp32_matrix_nmi_mask(esk_esp32_matrix *m,unsigned cpu,int asserted);
/* Raw source status is independent of mapping/masking. */
uint32_t esk_esp32_matrix_status(const esk_esp32_matrix *m,unsigned cpu,unsigned group);
/* OR-combined CPU peripheral-input levels. Internal CPU inputs never driven. */
uint32_t esk_esp32_matrix_levels(const esk_esp32_matrix *m,unsigned cpu);
/* DPORT 0x3ff000dc..0x3ff00328: IPC, status and defined map registers only.
 * Qualified profile requires aligned 32-bit data accesses. RO status writes
 * are ignored; reserved bits read zero and are ignored on writes. Unknown
 * addresses/holes remain unsupported. No instruction fetch or MMIO CAS. */
es_status esk_esp32_matrix_read(const esk_esp32_matrix *m,uint32_t address,unsigned bytes,uint32_t *value);
es_status esk_esp32_matrix_write(esk_esp32_matrix *m,uint32_t address,unsigned bytes,uint32_t value);
#endif
