/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_FLASHSPI_H
#define ESK_ESP32_FLASHSPI_H
#include "mcu/xtensa/execute.h"
/* Opt-in SPI1 single-line USER 03h read profile, CS0, 24-bit address,
 * byte-sized data in the W0..15 buffer, both caches disabled. The attachment
 * is explicitly provided immutable flash storage, not an identified chip.
 * Functional synchronous completion, no pin waveforms, arbiter/clock timing,
 * DMA, dedicated commands, fast/dual/quad, program/erase/status/ID/security,
 * global peripheral reset or power model. Unsupported modes fail unchanged.
 * Only clocked receive bytes are qualified; other buffer bytes retain fixture
 * values, not a silicon retention guarantee. Serialized calls only. */
typedef struct esk_esp32_flashspi {
    uint32_t addr,ctrl,user,user1,user2,mosi,miso,slave,buffer[16];
} esk_esp32_flashspi;
void esk_esp32_flashspi_init(esk_esp32_flashspi *s);
es_status esk_esp32_flashspi_read(const esk_esp32_flashspi *s,uint32_t address,unsigned bytes,uint32_t *value);
es_status esk_esp32_flashspi_write(esk_esp32_flashspi *s,const unsigned char *image,size_t size,int caches_disabled,uint32_t address,unsigned bytes,uint32_t value);
int esk_esp32_flashspi_irq(const esk_esp32_flashspi *s);
#endif
