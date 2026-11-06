/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_NOR32_H
#define ESK_ESP32_NOR32_H
#include "embsim/types.h"
/* Internal common protocol subset, not a generic qualified flash part. Named
 * adapters define identity/timing/WEL semantics; their headers state omissions. */
typedef struct esk_esp32_nor32 {
    uint64_t remaining_us;
    uint32_t wel,operation,address,length;
    unsigned char data[256];
} esk_esp32_nor32;
typedef struct esk_esp32_nor32_spec {
    unsigned char jedec[3];
    uint64_t program_us,erase4_us,erase32_us,erase64_us,erasechip_us;
    uint64_t program_byte_us; /* zero uses the page-program bound */
    int clear_wel_at_start;
} esk_esp32_nor32_spec;
void esk_esp32_nor32_init(esk_esp32_nor32 *f);
es_status esk_esp32_nor32_command(esk_esp32_nor32 *f,const esk_esp32_nor32_spec *spec,unsigned char *image,size_t size,
    unsigned command,int has_address,uint32_t address,const unsigned char *tx,size_t tx_len,unsigned char *rx,size_t rx_len);
es_status esk_esp32_nor32_advance(esk_esp32_nor32 *f,unsigned char *image,size_t size,uint64_t microseconds);
#endif
