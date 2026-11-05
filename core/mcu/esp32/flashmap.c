/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Facts: ESP32 TRM v5.9 ch.3/4/12, ESP-IDF v4.4.7 soc/mmu.h and dport_reg.h.
 * Original functional translation implementation, no vendor controller code. */
#include "mcu/esp32/flashmap.h"
#include "mcu/esp32/flashspi.h"
#include <string.h>
struct esk_esp32_flashmap {
    esk_alloc *a;unsigned char *image;size_t size;
    uint32_t table[2][256],ctrl[2],ctrl1[2],dirty[2];
    esk_esp32_flashspi spi1;int spi1_enabled;
};
es_status esk_esp32_flashmap_create(esk_alloc *a,esk_esp32_flashmap **out)
{
    esk_esp32_flashmap *f;unsigned cpu,i;
    if(!out) return ES_EINVAL;
    *out=NULL;if(!a) return ES_EINVAL;
    f=(esk_esp32_flashmap *)esk_calloc(a,1,sizeof *f);if(!f) return ES_ENOMEM;
    f->a=a;esk_esp32_flashspi_init(&f->spi1);
    for(cpu=0;cpu<2;cpu++) {
        f->ctrl[cpu]=0x10;f->ctrl1[cpu]=0x8ff;f->dirty[cpu]=1;
        for(i=0;i<256;i++) f->table[cpu][i]=0x100;
    }
    *out=f;return ES_OK;
}
void esk_esp32_flashmap_destroy(esk_esp32_flashmap *f)
{
    if(!f) return;
    if(f->image) esk_free(f->a,f->image,f->size);
    esk_free(f->a,f,sizeof *f);
}
es_status esk_esp32_flashmap_install(esk_esp32_flashmap *f,const unsigned char *data,size_t len)
{
    unsigned char *copy;
    if(!f || !data || !len || (len&65535u) || len>0x1000000u) return ES_EINVAL;
    if((f->ctrl[0]|f->ctrl[1])&8u) return ES_EUNSUPPORTED;
    copy=(unsigned char *)esk_malloc(f->a,len);if(!copy) return ES_ENOMEM;
    memcpy(copy,data,len);if(f->image) esk_free(f->a,f->image,f->size);
    f->image=copy;f->size=len;f->dirty[0]=f->dirty[1]=1;return ES_OK;
}
es_status esk_esp32_flashmap_spi1_configure(esk_esp32_flashmap *f,int enabled)
{
    if(!f) return ES_EINVAL;
    if(enabled && !f->image) return ES_EUNSUPPORTED;
    f->spi1_enabled=enabled!=0;return ES_OK;
}
es_status esk_esp32_flashmap_spi1_irq(const esk_esp32_flashmap *f,int *asserted)
{
    if(!f || !asserted) return ES_EINVAL;
    if(!f->spi1_enabled) return ES_EUNSUPPORTED;
    *asserted=esk_esp32_flashspi_irq(&f->spi1);return ES_OK;
}
/* kind=0 MMU entry, 1 control, 2 masks. Only PID0/1 entry ranges exist here. */
static es_status decode(uint32_t address,unsigned bytes,unsigned *cpu,unsigned *kind,unsigned *index)
{
    if((address>=0x3ff10000 && address<0x3ff10400)||(address>=0x3ff12000 && address<0x3ff12400)) {
        *cpu=address>=0x3ff12000 ? 1u : 0u;*kind=0;*index=(address-(0x3ff10000u+*cpu*0x2000u))/4;
    } else if((address>=0x3ff00040 && address<0x3ff00048)||(address>=0x3ff00058 && address<0x3ff00060)) {
        *cpu=address>=0x3ff00058 ? 1u : 0u;*kind=address-(0x3ff00040u+*cpu*24u)<4 ? 1u : 2u;*index=0;
    } else return ES_EUNSUPPORTED;
    if(bytes!=4 || (address&3u)) return ES_EINVAL;
    return ES_OK;
}
es_status esk_esp32_flashmap_reg_read(const esk_esp32_flashmap *f,uint32_t address,unsigned bytes,uint32_t *value)
{
    unsigned cpu,kind,index;es_status st;
    if(!f || !value) return ES_EINVAL;
    if(f->spi1_enabled && address>=0x3ff42000 && address<0x3ff42400) return esk_esp32_flashspi_read(&f->spi1,address,bytes,value);
    st=decode(address,bytes,&cpu,&kind,&index);if(st!=ES_OK) return st;
    *value=kind==0 ? f->table[cpu][index] : kind==1 ? f->ctrl[cpu] : f->ctrl1[cpu];return ES_OK;
}
es_status esk_esp32_flashmap_reg_write(esk_esp32_flashmap *f,uint32_t address,unsigned bytes,uint32_t value)
{
    unsigned cpu,kind,index;uint32_t next;es_status st;
    if(!f) return ES_EINVAL;
    if(f->spi1_enabled && address>=0x3ff42000 && address<0x3ff42400) return esk_esp32_flashspi_write(&f->spi1,f->image,f->size,((f->ctrl[0]|f->ctrl[1])&8u)==0,address,bytes,value);
    st=decode(address,bytes,&cpu,&kind,&index);if(st!=ES_OK) return st;
    if(kind==0) {
        if((value&~UINT32_C(0x1ff)) || (f->ctrl[cpu]&8u)) return ES_EUNSUPPORTED;
        f->table[cpu][index]=value;f->dirty[cpu]=1;
    } else if(kind==1) {
        int flush=!(f->ctrl[cpu]&16u) && (value&16u);
        /* Reject unsupported defined RW fields. RO/reserved bits are ignored. */
        if(value&UINT32_C(0x10fc4)) return ES_EUNSUPPORTED;
        if(flush && ((f->ctrl[cpu]|value)&8u)) return ES_EUNSUPPORTED;
        if((value&8u) && f->dirty[cpu]) return ES_EUNSUPPORTED;
        next=(value&24u)|(f->ctrl[cpu]&32u);
        if(!(value&16u)) next&=~32u;
        if(flush) { next|=32u;f->dirty[cpu]=0; }
        f->ctrl[cpu]=next;
    } else {
        /* Preserve unqualified PSRAM/page/power/error-control reset fields. */
        if((value&0x3fe8u)!=(0x8ffu&0x3fe8u)) return ES_EUNSUPPORTED;
        f->ctrl1[cpu]=0x8e8u|(value&0x17u);
    }
    return ES_OK;
}
static es_status resolve(const esk_esp32_flashmap *f,unsigned cpu,uint32_t address,size_t *physical)
{
    uint32_t page,mask,entry;
    if(address>=0x3f400000 && address<0x3f800000) { page=(address-0x3f400000)>>16;mask=16; }
    else if(address>=0x400c2000 && address<0x40c00000) {
        unsigned region=(address-0x40000000)>>22;
        page=64u+((address-0x40000000)>>16);mask=1u<<region;
    } else return ES_EUNSUPPORTED;
    if(!(f->ctrl[cpu]&8u) || (f->ctrl1[cpu]&mask)) return ES_EUNSUPPORTED;
    entry=f->table[cpu][page];if(entry&0x100u) return ES_EUNSUPPORTED;
    *physical=(size_t)(entry&255u)*65536u+(address&65535u);
    if(*physical>=f->size) return ES_EUNSUPPORTED;
    return ES_OK;
}
es_status esk_esp32_flashmap_read(const esk_esp32_flashmap *f,unsigned cpu,uint32_t address,unsigned bytes,int fetch,uint32_t *value)
{
    size_t physical[4];unsigned i;uint32_t result=0;es_status st;int instruction;
    if(!f || cpu>1 || !value) return ES_EINVAL;
    instruction=address>=0x400c2000 && address<0x40c00000;
    if(!instruction && !(address>=0x3f400000 && address<0x3f800000)) return ES_EUNSUPPORTED;
    if(!f->image || (fetch && !instruction)) return ES_EUNSUPPORTED;
    if(bytes<1 || bytes>4 || (!fetch && (bytes==3 || (address&(bytes-1u))))) return ES_EINVAL;
    if(!fetch && instruction && bytes!=4) return ES_EUNSUPPORTED;
    for(i=0;i<bytes;i++) { st=resolve(f,cpu,address+i,&physical[i]);if(st!=ES_OK) return st; }
    for(i=0;i<bytes;i++) result|=(uint32_t)f->image[physical[i]]<<(8*i);
    *value=result;return ES_OK;
}
