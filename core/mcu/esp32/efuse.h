/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_EFUSE_H
#define ESK_ESP32_EFUSE_H
#include "embsim/types.h"
/* Opt-in original-ESP32 eFuse read/reload profile. The caller supplies ALL 31
 * uncoded mapped data words (BLK0 seven, BLK1..3 eight each). No factory identity,
 * MAC, keys, calibration or security values are invented. Coding None (0/3)
 * only; 3/4 and Repeat attach fail. RD_DIS masks protected blocks/fields.
 * RDATA is read-only; WDATA stages bits but PGM and write opcodes explicitly
 * fail: no burn/OTP, write-protection, ECC, analog timing, supply failure,
 * global reset or hidden-hardware consumer qualification. READ_CMD completes
 * synchronously in qualified read mode, sets raw completion, and IRQ=raw&enable.
 * CLK/DAC fields are configuration storage, not physical waveform timing.
 * init/attach is host fixture setup; CPU-only reset does not reset this device.
 * All calls serialized, no re-entry. Missing/unselected profile fails explicitly. */
typedef struct esk_esp32_efuse {
    uint32_t image[31],read[31],write[31];
    uint32_t enabled,clock,conf,raw,enable,dac,date;
} esk_esp32_efuse;
void esk_esp32_efuse_init(esk_esp32_efuse *f);
es_status esk_esp32_efuse_configure(esk_esp32_efuse *f,const uint32_t *words,size_t count);
es_status esk_esp32_efuse_read(const esk_esp32_efuse *f,uint32_t address,unsigned bytes,uint32_t *value);
es_status esk_esp32_efuse_write(esk_esp32_efuse *f,uint32_t address,unsigned bytes,uint32_t value);
/* ES_EUNSUPPORTED when profile disabled; output unchanged on failure. */
es_status esk_esp32_efuse_irq(const esk_esp32_efuse *f,int *asserted);
#endif
