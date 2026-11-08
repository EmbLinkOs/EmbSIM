/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* W25Q32JV-IQ Rev.G maximum-duration fixture. Not the owner's board part. */
#include "mcu/esp32/w25q32.h"
static const esk_esp32_nor32_spec spec={{0xef,0x40,0x16},3000,400000,1600000,2000000,50000000,0,0};
void esk_esp32_w25q32_init(esk_esp32_w25q32 *f) { esk_esp32_nor32_init(f); }
es_status esk_esp32_w25q32_command(esk_esp32_w25q32 *f,unsigned char *image,size_t size,
    unsigned command,int has_address,uint32_t address,const unsigned char *tx,size_t tx_len,unsigned char *rx,size_t rx_len)
{ return esk_esp32_nor32_command(f,&spec,image,size,command,has_address,address,tx,tx_len,rx,rx_len); }
es_status esk_esp32_w25q32_advance(esk_esp32_w25q32 *f,unsigned char *image,size_t size,uint64_t microseconds)
{ return esk_esp32_nor32_advance(f,image,size,microseconds); }
