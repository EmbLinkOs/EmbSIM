/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_FLASHMAP_H
#define ESK_ESP32_FLASHMAP_H
#include "util/alloc.h"
/* Read-only original-ESP32 flash/MMU functional profile, PID0/1 normal mode.
 * Per-core 256-entry tables, 64K pages, access masks and explicit flush/enable
 * sequence. No cache tags/replacement/latency, physical SPI/JEDEC programming,
 * PSRAM, high PIDs, lock/special/page modes, pool reuse or fault IRQ modeling.
 * To keep immutable-image reads qualified without cache-coherence guesses,
 * table edits require the destination cache disabled and a fresh flush before
 * enabling. Flash replacement requires both caches disabled. Unsupported modes
 * fail unchanged. Initial invalid entries are fixture policy, not silicon reset.
 * No concurrent callers or re-entry. */
typedef struct esk_esp32_flashmap esk_esp32_flashmap;
es_status esk_esp32_flashmap_create(esk_alloc *a,esk_esp32_flashmap **out);
void esk_esp32_flashmap_destroy(esk_esp32_flashmap *f);
/* Complete provided backing extent: 1..256 whole 64K pages, copied privately.
 * No chip identity/capacity is inferred and no missing bytes are fabricated. */
es_status esk_esp32_flashmap_install(esk_esp32_flashmap *f,const unsigned char *data,size_t len);
es_status esk_esp32_flashmap_reg_read(const esk_esp32_flashmap *f,uint32_t address,unsigned bytes,uint32_t *value);
es_status esk_esp32_flashmap_reg_write(esk_esp32_flashmap *f,uint32_t address,unsigned bytes,uint32_t value);
es_status esk_esp32_flashmap_read(const esk_esp32_flashmap *f,unsigned cpu,uint32_t address,unsigned bytes,int fetch,uint32_t *value);
#endif
