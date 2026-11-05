/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_W25Q32_H
#define ESK_ESP32_W25Q32_H
#include "embsim/types.h"
/* Explicit W25Q32JV-IQ unprotected 4MiB storage fixture, NOT identified board
 * hardware. Qualified standard-SPI subset: 03/05/06/04/9f/02/20/52/d8/c7/60.
 * PP accepts 1..256 bytes; longer frames and array-end read/address aliases
 * are unqualified and rejected. Timed PP/erase use Rev.G datasheet MAXIMUM durations and supplied microseconds,
 * never instruction counts. Storage commits at completion; intermediate cells,
 * power interruption, endurance/disturb, SPI edges, fast/dual/quad, protection
 * writes, suspend/resume, status2/3, security/OTP, reset/power and SFDP omitted.
 * SR1 upper bits are explicitly fixture-configured unprotected zero. WEL/BUSY
 * follow the command lifecycle. Ignored busy reads have unqualified electrical
 * output, so fail explicitly instead of fabricating bytes. Caller owns storage
 * and serializes all calls; initialize before use. No actual-board BOM claim. */
typedef struct esk_esp32_w25q32 {
    uint64_t remaining_us;
    uint32_t wel,operation,address,length;
    unsigned char data[256];
} esk_esp32_w25q32;
void esk_esp32_w25q32_init(esk_esp32_w25q32 *f);
es_status esk_esp32_w25q32_command(esk_esp32_w25q32 *f,unsigned char *image,size_t size,
    unsigned command,int has_address,uint32_t address,const unsigned char *tx,size_t tx_len,unsigned char *rx,size_t rx_len);
/* Returns ES_OK; commits at the deadline, keeping memory private until then.
 * Excess time is discarded; one operation can be outstanding. */
es_status esk_esp32_w25q32_advance(esk_esp32_w25q32 *f,unsigned char *image,size_t size,uint64_t microseconds);
#endif
