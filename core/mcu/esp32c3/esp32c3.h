/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* esp32c3.h -- the ESP32-C3's peripheral models (ESP32-C3 Technical
 * Reference Manual v1.4), over the register file converted from
 * Espressif's esp32c3.svd.
 *
 *   intmtx.c  the interrupt matrix (62 sources mapped onto CPU interrupts
 *             1-31) and the CPU's interrupt controller: enable, type,
 *             priority, threshold, the pending state the core claims */
#ifndef ESK_MCU_ESP32C3_H
#define ESK_MCU_ESP32C3_H

#include "mcu/rf.h"

/* the interrupt matrix's sources (TRM table 8.3-1) */
#define ESP32C3_NSOURCES 62

/* a peripheral's interrupt source `src` (0-61) is at `level` */
void esk_esp32c3_source(struct esk_mach *m, int src, int level);

extern const struct esk_dev_ops esk_esp32c3_intmtx_ops;
void *esk_esp32c3_intmtx_create(struct esk_mach *s, struct esk_rf_periph *p);

#endif
