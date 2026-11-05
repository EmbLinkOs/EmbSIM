/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Register facts: ESP32 TRM v5.9 ch.20 and ESP-IDF v4.4.7 spi_reg.h.
 * SPI USER address alignment: that SDK's spi_flash_ll_set_usr_address. */
#include "mcu/esp32/flashspi.h"
#include <string.h>
void esk_esp32_flashspi_init(esk_esp32_flashspi *s)
{
    if(!s) return;
    memset(s,0,sizeof *s);s->ctrl=0x20a400;s->user=0x80000040;s->user1=0x5c000007;s->user2=0x70000000;s->slave=0x200;
}
static es_status decode(uint32_t address,unsigned bytes,unsigned *offset)
{
    if(address<0x3ff42000 || address>=0x3ff42400) return ES_EUNSUPPORTED;
    if(bytes!=4 || (address&3u)) return ES_EINVAL;
    *offset=address-0x3ff42000;return ES_OK;
}
es_status esk_esp32_flashspi_read(const esk_esp32_flashspi *s,uint32_t address,unsigned bytes,uint32_t *value)
{
    unsigned offset;es_status st;
    if(!s || !value) return ES_EINVAL;
    st=decode(address,bytes,&offset);if(st!=ES_OK) return st;
    if(offset>=0x80 && offset<0xc0) { *value=s->buffer[(offset-0x80)/4];return ES_OK; }
    switch(offset) {
    case 0:*value=0;break;case 4:*value=s->addr;break;case 8:*value=s->ctrl;break;
    case 0x18:*value=0x80003043;break;case 0x1c:*value=s->user;break;case 0x20:*value=s->user1;break;
    case 0x24:*value=s->user2;break;case 0x28:*value=s->mosi;break;case 0x2c:*value=s->miso;break;
    case 0x34:*value=6;break;case 0x38:*value=s->slave;break;
    default:return ES_EUNSUPPORTED;
    }
    return ES_OK;
}
static es_status transfer(esk_esp32_flashspi *s,const unsigned char *image,size_t size,int caches_disabled)
{
    uint32_t address=s->addr>>8,bits=s->miso+1u;unsigned first=(s->user&0x1000000u) ? 32u : 0u,i;
    /* No state changes until every command, mode and extent check passes. */
    if(!caches_disabled || !image || (s->ctrl&0x2000u) ||
       (s->user&~UINT32_C(0x1000000))!=0xd0000040u || s->user2!=0x70000003u ||
       bits%8u || bits>512u-first*8u || (size_t)address>size || bits/8u>size-(size_t)address)
        return ES_EUNSUPPORTED;
    for(i=0;i<bits/8u;i++) {
        unsigned byte=first+i,shift=(byte&3u)*8u;
        s->buffer[byte/4u]=(s->buffer[byte/4u]&~(UINT32_C(255)<<shift))|((uint32_t)image[address+i]<<shift);
    }
    s->slave=(s->slave&~UINT32_C(0x7800000))|((((s->slave>>23)+1u)&15u)<<23)|16u;
    return ES_OK;
}
es_status esk_esp32_flashspi_write(esk_esp32_flashspi *s,const unsigned char *image,size_t size,int caches_disabled,uint32_t address,unsigned bytes,uint32_t value)
{
    unsigned offset;es_status st;
    if(!s) return ES_EINVAL;
    st=decode(address,bytes,&offset);if(st!=ES_OK) return st;
    if(offset>=0x80 && offset<0xc0) { s->buffer[(offset-0x80)/4]=value;return ES_OK; }
    switch(offset) {
    case 0:
        if(value&UINT32_C(0xfffb0000)) return ES_EUNSUPPORTED;
        return (value&0x40000u) ? transfer(s,image,size,caches_disabled) : ES_OK;
    case 4:s->addr=value;break;
    case 8:
        if((value&UINT32_C(0x7f0dc00))!=0x208400u) return ES_EUNSUPPORTED;
        s->ctrl=0x208400u|(value&0x2000u);break;
    case 0x18:if(value!=0x80003043u) return ES_EUNSUPPORTED;break;
    case 0x1c:
        /* Preserve unqualified timing/slave/byte-order fields at reset values. */
        if((value&UINT32_C(0x0efffcf1))!=0x40u) return ES_EUNSUPPORTED;
        s->user=(value&UINT32_C(0xf1000000))|0x40u;break;
    case 0x20:
        /* SDK and register comments disagree on address-length writability.
         * Only unchanged 24-bit configuration is qualified in this profile. */
        if((value>>26)!=23u) return ES_EUNSUPPORTED;
        s->user1=0x5c000000u|(value&255u);break;
    case 0x24:s->user2=value&UINT32_C(0xf000ffff);break;
    case 0x28:s->mosi=value&UINT32_C(0xffffff);break;
    case 0x2c:s->miso=value&UINT32_C(0xffffff);break;
    case 0x34:if((value&UINT32_C(0x600039e7))!=6u) return ES_EUNSUPPORTED;break;
    case 0x38:
        if(value&UINT32_C(0xf8000def)) return ES_EUNSUPPORTED;
        s->slave=(s->slave&UINT32_C(0x7800000))|(value&0x210u);break;
    default:return ES_EUNSUPPORTED;
    }
    return ES_OK;
}
int esk_esp32_flashspi_irq(const esk_esp32_flashspi *s) { return s && (s->slave&0x210u)==0x210u; }
