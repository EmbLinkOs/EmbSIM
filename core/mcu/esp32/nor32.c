/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Shared 4MiB standard-SPI subset; each named adapter supplies its own
 * qualified identity, durations and WEL policy. No vendor code copied. */
#include "mcu/esp32/nor32.h"
#include <string.h>
void esk_esp32_nor32_init(esk_esp32_nor32 *f) { if(f) memset(f,0,sizeof *f); }
es_status esk_esp32_nor32_command(esk_esp32_nor32 *f,const esk_esp32_nor32_spec *spec,unsigned char *image,size_t size,
    unsigned command,int has_address,uint32_t address,const unsigned char *tx,size_t tx_len,unsigned char *rx,size_t rx_len)
{
    uint64_t duration=0;uint32_t length=0,base=address;size_t i;
    if(!f || !spec || !image || size!=0x400000u || (tx_len && !tx) || (rx_len && !rx)) return ES_EINVAL;
    /* Validate shape before any storage, WEL or pending-operation change. */
    switch(command) {
    case 3:
        if(!has_address || tx_len || !rx_len || address>=size || rx_len>size-address) return ES_EUNSUPPORTED;
        break;
    case 5:
        if(has_address || tx_len || !rx_len) return ES_EUNSUPPORTED;
        break;
    case 0x9f:
        if(has_address || tx_len || rx_len!=3) return ES_EUNSUPPORTED;
        break;
    case 6:case 4:
        if(has_address || tx_len || rx_len) return ES_EUNSUPPORTED;
        break;
    case 2:
        if(!has_address || !tx_len || tx_len>256 || rx_len || address>=size) return ES_EUNSUPPORTED;
        length=(uint32_t)tx_len;duration=tx_len==1 && spec->program_byte_us ? spec->program_byte_us : spec->program_us;break;
    case 0x20:case 0x52:case 0xd8:
        if(!has_address || tx_len || rx_len || address>=size) return ES_EUNSUPPORTED;
        length=command==0x20 ? 4096u : command==0x52 ? 32768u : 65536u;
        base=address&~(length-1u);duration=command==0x20 ? spec->erase4_us : command==0x52 ? spec->erase32_us : spec->erase64_us;break;
    case 0xc7:case 0x60:
        if(has_address || tx_len || rx_len) return ES_EUNSUPPORTED;
        base=0;length=0x400000;duration=spec->erasechip_us;break;
    default:return ES_EUNSUPPORTED;
    }
    if(command==5) { memset(rx,(f->remaining_us ? 1 : 0)|(f->wel ? 2 : 0),rx_len);return ES_OK; }
    if(f->remaining_us) {
        /* Busy ignores further non-status instructions. Undefined receive-line
         * values are not represented by invented pulled-up or retained bytes. */
        return rx_len ? ES_EUNSUPPORTED : ES_OK;
    }
    if(command==3) { memcpy(rx,image+address,rx_len);return ES_OK; }
    if(command==0x9f) { memcpy(rx,spec->jedec,3);return ES_OK; }
    if(command==6) { f->wel=1;return ES_OK; }
    if(command==4) { f->wel=0;return ES_OK; }
    if(!f->wel) return ES_OK; /* Hardware ignores writes without WEL. */
    if(command==2) for(i=0;i<tx_len;i++) f->data[i]=tx[i];
    if(spec->clear_wel_at_start) f->wel=0;
    f->operation=command;f->address=base;f->length=length;f->remaining_us=duration;
    return ES_OK;
}
es_status esk_esp32_nor32_advance(esk_esp32_nor32 *f,unsigned char *image,size_t size,uint64_t microseconds)
{
    uint32_t i;
    if(!f || !image || size!=0x400000u) return ES_EINVAL;
    if(!f->remaining_us) return ES_OK;
    if(microseconds<f->remaining_us) { f->remaining_us-=microseconds;return ES_OK; }
    if(f->operation==2) {
        uint32_t page=f->address&~UINT32_C(255),offset=f->address&255u;
        for(i=0;i<f->length;i++) {
            uint32_t at=page+((offset+i)&255u);image[at]=(unsigned char)(image[at]&f->data[i]);
        }
    } else memset(image+f->address,255,f->length);
    f->remaining_us=0;f->wel=0;f->operation=0;f->address=f->length=0;
    return ES_OK;
}
