/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_GT25Q32_H
#define ESK_ESP32_GT25Q32_H
#include "mcu/esp32/nor32.h"
/* Giantec GT25Q32A-U V2.5 unprotected 4MiB standard-SPI subset, matching
 * owner's reported C4 60 16 but not authenticating package/suffix/die revision.
 * 03/05/06/04/9f/02/20/52/d8/c7/60, PP 1..256 bytes with page wrap/bit clear.
 * Uses independently supplied microseconds and datasheet MAXIMUM durations:
 * one-byte PP 150us (tBP), longer PP 1500us (tPP), sector/32K/64K
 * erase 8000us, chip erase 16000us. WEL clears on
 * acceptance as a deterministic early-clear fixture choice within the stated
 * unspecified pre-completion interval, not measured WEL timing. Array commits
 * at completion; SR1 upper bits explicitly fixture-unprotected zero.
 * Not complete: 1KB mini-sector erase (tMSE maximum unspecified in the inspected
 * timing tables), fast/dual/quad/QPI, long frames, array-end wrap/aliases,
 * protection/status2/3/SFDP/OTP/suspend/reset/power/edges/voltage/endurance/
 * intermediate-cell or power-failure behavior. Unsupported operations fail
 * explicitly; busy reads do not invent floating-line data. Serialized only. */
typedef esk_esp32_nor32 esk_esp32_gt25q32;
void esk_esp32_gt25q32_init(esk_esp32_gt25q32 *f);
es_status esk_esp32_gt25q32_command(esk_esp32_gt25q32 *f,unsigned char *image,size_t size,
    unsigned command,int has_address,uint32_t address,const unsigned char *tx,size_t tx_len,unsigned char *rx,size_t rx_len);
es_status esk_esp32_gt25q32_advance(esk_esp32_gt25q32 *f,unsigned char *image,size_t size,uint64_t microseconds);
#endif
