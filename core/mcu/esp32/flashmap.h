/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_FLASHMAP_H
#define ESK_ESP32_FLASHMAP_H
#include "util/alloc.h"
/* Default read-only original-ESP32 flash/MMU functional profile, PID0/1 normal mode.
 * Per-core 256-entry tables, 64K pages, access masks and explicit flush/enable
 * sequence. No cache tags/replacement/latency, physical SPI/pin timing, full flash protocol/protection,
 * PSRAM, high PIDs, lock/special/page modes, pool reuse or fault IRQ modeling.
 * To keep immutable-image reads qualified without cache-coherence guesses,
 * table edits require the destination cache disabled and a fresh flush before
 * enabling. Flash replacement requires both caches disabled. Unsupported modes
 * fail unchanged. Optional SPI1 USER reads use the same immutable backing
 * under the separate flashspi.h contract. An explicitly attached W25Q32 profile
 * adds timed program/erase; its header lists the deliberately bounded subset. Initial invalid entries are fixture
 * policy, not silicon reset.
 * No concurrent callers or re-entry. */
typedef struct esk_esp32_flashmap esk_esp32_flashmap;
es_status esk_esp32_flashmap_create(esk_alloc *a,esk_esp32_flashmap **out);
void esk_esp32_flashmap_destroy(esk_esp32_flashmap *f);
/* Complete provided backing extent: 1..256 whole 64K pages, copied privately.
 * No chip identity/capacity is inferred and no missing bytes are fabricated. */
es_status esk_esp32_flashmap_install(esk_esp32_flashmap *f,const unsigned char *data,size_t len);
/* Explicit immutable SPI1 USER 03h read attachment; see flashspi.h. */
es_status esk_esp32_flashmap_spi1_configure(esk_esp32_flashmap *f,int enabled);
/* Explicit W25Q32JV-IQ unprotected 4MiB profile, not a board-part assertion.
 * Both caches disabled; cannot detach or replace backing while busy. */
es_status esk_esp32_flashmap_w25q32_configure(esk_esp32_flashmap *f,int enabled);
/* Giantec subset described by gt25q32.h; mutually exclusive with Winbond. */
es_status esk_esp32_flashmap_gt25q32_configure(esk_esp32_flashmap *f,int enabled);
es_status esk_esp32_flashmap_flash_advance(esk_esp32_flashmap *f,uint64_t microseconds);
es_status esk_esp32_flashmap_flash_deadline(const esk_esp32_flashmap *f,uint64_t *microseconds);
es_status esk_esp32_flashmap_spi1_irq(const esk_esp32_flashmap *f,int *asserted);
es_status esk_esp32_flashmap_reg_read(const esk_esp32_flashmap *f,uint32_t address,unsigned bytes,uint32_t *value);
es_status esk_esp32_flashmap_reg_write(esk_esp32_flashmap *f,uint32_t address,unsigned bytes,uint32_t value);
es_status esk_esp32_flashmap_read(const esk_esp32_flashmap *f,unsigned cpu,uint32_t address,unsigned bytes,int fetch,uint32_t *value);
#endif
