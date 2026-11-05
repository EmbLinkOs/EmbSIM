/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ESP32 TRM v5.9 chapter 5; ESP-IDF v4.4.7 soc/efuse_reg.h. Original code. */
#include "mcu/esp32/efuse.h"
#include <string.h>
void esk_esp32_efuse_init(esk_esp32_efuse *f)
{
    if(!f) return;
    memset(f,0,sizeof *f);f->clock=0x4052;f->conf=0x10000;f->dac=40;f->date=0x16042600;
}
static void reload(esk_esp32_efuse *f)
{
    unsigned block;uint32_t disabled=(f->image[0]>>16)&15u;
    memcpy(f->read,f->image,sizeof f->read);
    for(block=0;block<3;block++) if(disabled&(1u<<block)) memset(f->read+7u+block*8u,0,8*sizeof(uint32_t));
    if(disabled&8u) { f->read[5]&=UINT32_C(0x0fffffff);f->read[6]&=~UINT32_C(0x403); }
}
es_status esk_esp32_efuse_configure(esk_esp32_efuse *f,const uint32_t *words,size_t count)
{
    esk_esp32_efuse next;
    if(!f || (!words && count)) return ES_EINVAL;
    if(!words && !count) { esk_esp32_efuse_init(f);return ES_OK; }
    if(count!=31) return ES_EINVAL;
    if((words[6]&3u)!=0 && (words[6]&3u)!=3u) return ES_EUNSUPPORTED;
    esk_esp32_efuse_init(&next);memcpy(next.image,words,sizeof next.image);next.enabled=1;reload(&next);*f=next;return ES_OK;
}
/* kind 0=read data, 1=write staging, 2=controller. */
static es_status decode(const esk_esp32_efuse *f,uint32_t address,unsigned bytes,unsigned *kind,unsigned *index)
{
    unsigned off;
    if(address<0x3ff5a000 || address>=0x3ff5b000 || !f->enabled) return ES_EUNSUPPORTED;
    if(bytes!=4 || (address&3u)) return ES_EINVAL;
    off=address-0x3ff5a000;
    if(off<=0x18) { *kind=0;*index=off/4; }
    else if(off<=0x34) { *kind=1;*index=(off-0x1c)/4; }
    else if(off<=0x94) { *kind=0;*index=7u+(off-0x38)/4; }
    else if(off<=0xf4) { *kind=1;*index=7u+(off-0x98)/4; }
    else { *kind=2;*index=off; }
    return ES_OK;
}
es_status esk_esp32_efuse_read(const esk_esp32_efuse *f,uint32_t address,unsigned bytes,uint32_t *value)
{
    unsigned kind,index;es_status st;
    if(!f || !value) return ES_EINVAL;
    st=decode(f,address,bytes,&kind,&index);if(st!=ES_OK) return st;
    if(kind<2) { *value=kind ? f->write[index] : f->read[index];return ES_OK; }
    switch(index) {
    case 0xf8:*value=f->clock;break;case 0xfc:*value=f->conf;break;
    case 0x100:if(f->pending) return ES_EUNSUPPORTED;*value=0;break;
    case 0x104:*value=f->pending;break;case 0x11c:*value=0;break;
    case 0x108:*value=f->raw;break;case 0x10c:*value=f->raw&f->enable;break;
    case 0x110:*value=f->enable;break;case 0x118:*value=f->dac;break;case 0x1fc:*value=f->date;break;
    default:return ES_EUNSUPPORTED;
    }
    return ES_OK;
}
es_status esk_esp32_efuse_write(esk_esp32_efuse *f,uint32_t address,unsigned bytes,uint32_t value)
{
    unsigned kind,index;es_status st;
    if(!f) return ES_EINVAL;
    st=decode(f,address,bytes,&kind,&index);if(st!=ES_OK) return st;
    if(kind==0) return ES_OK; /* TRM software-read registers are RO. */
    if(kind==1) { if(f->pending) return ES_EUNSUPPORTED;f->write[index]=value;return ES_OK; }
    if(f->pending && (index==0xf8 || index==0xfc || index==0x118 || (index==0x104 && (value&3u)))) return ES_EUNSUPPORTED;
    switch(index) {
    case 0xf8:f->clock=value&0x1ffffu;break;
    case 0xfc:
        /* Only inactive reset/zero and the documented READ mode qualify.
         * FORCE_NO_WR_RD_DIS interactions with programming are unqualified. */
        value&=0x1ffffu;
        if(value!=0 && value!=0x10000u && value!=0x5aa5u) return ES_EUNSUPPORTED;
        f->conf=value;break;
    case 0x104:
        value&=3u;if(!value) break;
        if(value!=1 || f->conf!=0x5aa5u) return ES_EUNSUPPORTED;
        f->pending=1;break;
    case 0x110:f->enable=value&3u;break;
    case 0x114:f->raw&=~(value&3u);break;
    case 0x118:if(value&256u) return ES_EUNSUPPORTED;f->dac=value&255u;break;
    case 0x1fc:f->date=value;break;
    case 0x100:case 0x108:case 0x10c:case 0x11c:break;
    default:return ES_EUNSUPPORTED;
    }
    return ES_OK;
}
es_status esk_esp32_efuse_irq(const esk_esp32_efuse *f,int *asserted)
{
    if(!f || !asserted) return ES_EINVAL;
    if(!f->enabled) return ES_EUNSUPPORTED;
    *asserted=(f->raw&f->enable)!=0;return ES_OK;
}

es_status esk_esp32_efuse_read_complete(esk_esp32_efuse *f)
{
    if(!f) return ES_EINVAL;
    if(!f->enabled || !f->pending) return ES_EUNSUPPORTED;
    reload(f);f->pending=0;f->raw|=1;return ES_OK;
}
