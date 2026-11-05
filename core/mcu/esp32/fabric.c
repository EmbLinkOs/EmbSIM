/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Address facts: ESP32 TRM v5.9, section 3.3.2. No vendor code copied. */
#include "mcu/esp32/fabric.h"
#include "mcu/esp32/flashmap.h"
#include "mcu/esp32/efuse.h"
#include <string.h>
struct view { struct esk_esp32_fabric *f;unsigned cpu; };
struct esk_esp32_fabric {
    esk_alloc *a;
    unsigned char sram0[0x20000],sram1[0x20000],sram2[0x32000],fast[0x2000],slow[0x2000];
    unsigned char *rom[2];esk_esp32_flashmap *flash;
    esk_esp32_efuse efuse;
    unsigned remap[2];struct view view[2];
};
struct location { unsigned char *byte;int readonly,word_only,executable,atomic; };
static int within(uint32_t addr,uint32_t base,uint32_t size) { return addr>=base && addr-base<size; }
static es_status locate(struct view *v,uint32_t addr,struct location *p)
{
    esk_esp32_fabric *f=v->f;uint32_t offset;
    memset(p,0,sizeof *p);
    if(within(addr,0x3ffae000,0x32000)) { p->byte=f->sram2+(addr-0x3ffae000);p->atomic=1; }
    else if(within(addr,0x3ffe0000,0x20000)) { p->byte=f->sram1+(addr-0x3ffe0000);p->atomic=1; }
    else if(within(addr,0x40080000,0x20000)) { p->byte=f->sram0+(addr-0x40080000);p->word_only=p->executable=1; }
    else if(within(addr,0x400a0000,0x20000)) {
        if(f->remap[v->cpu] && within(addr,0x400b0000,0x8000)) return ES_EUNSUPPORTED;
        offset=addr-0x400a0000;p->byte=f->sram1+(0x1fffcu-(offset&~3u))+(offset&3u);
        p->word_only=p->executable=1;
    } else if(within(addr,0x40000000,0x60000)) {
        if(f->remap[v->cpu] && addr<0x40008000) {
            offset=0x10000u+(addr-0x40000000);p->byte=f->sram1+(0x1fffcu-(offset&~3u))+(offset&3u);
        } else { if(!f->rom[0]) return ES_EUNSUPPORTED;p->byte=f->rom[0]+(addr-0x40000000);p->readonly=1; }
        p->word_only=p->executable=1;
    } else if(within(addr,0x3ff90000,0x10000)) {
        if(!f->rom[1]) return ES_EUNSUPPORTED;
        p->byte=f->rom[1]+(addr-0x3ff90000);p->readonly=1;
    } else if(within(addr,0x3ff80000,0x2000)||within(addr,0x400c0000,0x2000)) {
        if(v->cpu) return ES_EUNSUPPORTED;
        offset=addr<0x40000000 ? addr-0x3ff80000 : addr-0x400c0000;
        p->byte=f->fast+offset;p->word_only=addr>=0x40000000;p->executable=p->word_only;
    } else if(within(addr,0x50000000,0x2000)) { p->byte=f->slow+(addr-0x50000000);p->executable=1; }
    else return ES_EUNSUPPORTED;
    return ES_OK;
}
static es_status access(struct view *v,uint32_t address,unsigned bytes,int fetch,int write,uint32_t *value)
{
    struct location at[4];unsigned i;uint32_t result=0;
    if(!v || !value || (bytes<1 || bytes>4 || (!fetch && bytes==3)) || (uint64_t)address+bytes>UINT64_C(0x100000000)) return ES_EINVAL;
    if(!fetch && (address&(bytes-1u))) return ES_EINVAL;
    for(i=0;i<bytes;i++) {
        es_status st=locate(v,address+i,&at[i]);if(st!=ES_OK) return st;
        if(fetch && !at[i].executable) return ES_EUNSUPPORTED;
        if(!fetch && at[i].word_only && bytes!=4) return ES_EUNSUPPORTED;
        if(write && at[i].readonly) return ES_EINVAL;
    }
    if(write) for(i=0;i<bytes;i++) *at[i].byte=(unsigned char)((*value>>(8*i))&255u);
    else { for(i=0;i<bytes;i++) result|=(uint32_t)*at[i].byte<<(8*i);*value=result; }
    return ES_OK;
}
static es_status read_bus(void *ctx,uint32_t a,unsigned n,uint32_t *v)
{
    struct view *view=(struct view *)ctx;es_status st;
    if(!view) return ES_EINVAL;
    st=esk_esp32_efuse_read(&view->f->efuse,a,n,v);if(st!=ES_EUNSUPPORTED) return st;
    if(view->f->flash) {
        st=esk_esp32_flashmap_reg_read(view->f->flash,a,n,v);if(st!=ES_EUNSUPPORTED) return st;
        st=esk_esp32_flashmap_read(view->f->flash,view->cpu,a,n,0,v);if(st!=ES_EUNSUPPORTED) return st;
    }
    return access(view,a,n,0,0,v);
}
static es_status fetch_bus(void *ctx,uint32_t a,unsigned n,uint32_t *v)
{
    struct view *view=(struct view *)ctx;es_status st;
    if(!view) return ES_EINVAL;
    if(view->f->flash) { st=esk_esp32_flashmap_read(view->f->flash,view->cpu,a,n,1,v);if(st!=ES_EUNSUPPORTED) return st; }
    return access(view,a,n,1,0,v);
}
static es_status write_bus(void *ctx,uint32_t a,unsigned n,uint32_t value)
{
    struct view *view=(struct view *)ctx;es_status st;
    if(!view) return ES_EINVAL;
    st=esk_esp32_efuse_write(&view->f->efuse,a,n,value);if(st!=ES_EUNSUPPORTED) return st;
    if(view->f->flash) { st=esk_esp32_flashmap_reg_write(view->f->flash,a,n,value);if(st!=ES_EUNSUPPORTED) return st; }
    return access(view,a,n,0,1,&value);
}
static es_status cas_bus(void *ctx,uint32_t address,uint32_t expected,uint32_t desired,uint32_t *observed)
{
    struct view *v=(struct view *)ctx;struct location at;uint32_t old;es_status st;
    if(!v || !observed || (address&3u)) return ES_EINVAL;
    st=locate(v,address,&at);if(st!=ES_OK) return st;
    if(!at.atomic) return ES_EUNSUPPORTED;
    st=read_bus(ctx,address,4,&old);if(st!=ES_OK) return st;
    /* All fabric calls are serialized, so no other writer can intervene. */
    if(old==expected) { st=write_bus(ctx,address,4,desired);if(st!=ES_OK) return st; }
    *observed=old;return ES_OK;
}
es_status esk_esp32_fabric_create(esk_alloc *a,esk_esp32_fabric **out)
{
    esk_esp32_fabric *f;unsigned i;
    if(!out) return ES_EINVAL;
    *out=NULL;
    if(!a) return ES_EINVAL;
    f=(esk_esp32_fabric *)esk_calloc(a,1,sizeof *f);if(!f) return ES_ENOMEM;
    f->a=a;esk_esp32_efuse_init(&f->efuse);for(i=0;i<2;i++) { f->view[i].f=f;f->view[i].cpu=i; }*out=f;return ES_OK;
}
void esk_esp32_fabric_destroy(esk_esp32_fabric *f)
{
    if(!f) return;
    if(f->rom[0]) esk_free(f->a,f->rom[0],0x60000);
    if(f->rom[1]) esk_free(f->a,f->rom[1],0x10000);
    esk_esp32_flashmap_destroy(f->flash);
    esk_free(f->a,f,sizeof *f);
}
es_status esk_esp32_fabric_rom(esk_esp32_fabric *f,unsigned which,const unsigned char *data,size_t len)
{
    unsigned char *copy;size_t expected=which ? 0x10000u : 0x60000u;
    if(!f || which>1 || !data || len!=expected) return ES_EINVAL;
    copy=(unsigned char *)esk_malloc(f->a,len);if(!copy) return ES_ENOMEM;
    memcpy(copy,data,len);if(f->rom[which]) esk_free(f->a,f->rom[which],len);f->rom[which]=copy;return ES_OK;
}
es_status esk_esp32_fabric_flash(esk_esp32_fabric *f,const unsigned char *data,size_t len)
{
    esk_esp32_flashmap *created=NULL;es_status st;
    if(!f || !data || !len || (len&65535u) || len>0x1000000u) return ES_EINVAL;
    if(f->flash) return esk_esp32_flashmap_install(f->flash,data,len);
    st=esk_esp32_flashmap_create(f->a,&created);if(st!=ES_OK) return st;
    st=esk_esp32_flashmap_install(created,data,len);
    if(st!=ES_OK) { esk_esp32_flashmap_destroy(created);return st; }
    f->flash=created;return ES_OK;
}
es_status esk_esp32_fabric_spi1_configure(esk_esp32_fabric *f,int enabled)
{
    if(!f) return ES_EINVAL;
    return f->flash ? esk_esp32_flashmap_spi1_configure(f->flash,enabled) : ES_EUNSUPPORTED;
}
es_status esk_esp32_fabric_spi1_irq(const esk_esp32_fabric *f,int *asserted)
{
    if(!f || !asserted) return ES_EINVAL;
    return f->flash ? esk_esp32_flashmap_spi1_irq(f->flash,asserted) : ES_EUNSUPPORTED;
}
es_status esk_esp32_fabric_w25q32_configure(esk_esp32_fabric *f,int enabled)
{
    if(!f) return ES_EINVAL;
    return f->flash ? esk_esp32_flashmap_w25q32_configure(f->flash,enabled) : ES_EUNSUPPORTED;
}
es_status esk_esp32_fabric_gt25q32_configure(esk_esp32_fabric *f,int enabled)
{
    if(!f) return ES_EINVAL;
    return f->flash ? esk_esp32_flashmap_gt25q32_configure(f->flash,enabled) : ES_EUNSUPPORTED;
}
es_status esk_esp32_fabric_flash_advance(esk_esp32_fabric *f,uint64_t microseconds)
{
    if(!f) return ES_EINVAL;
    return f->flash ? esk_esp32_flashmap_flash_advance(f->flash,microseconds) : ES_EUNSUPPORTED;
}
es_status esk_esp32_fabric_flash_deadline(const esk_esp32_fabric *f,uint64_t *microseconds)
{
    if(!f || !microseconds) return ES_EINVAL;
    return f->flash ? esk_esp32_flashmap_flash_deadline(f->flash,microseconds) : ES_EUNSUPPORTED;
}
es_status esk_esp32_fabric_efuse_configure(esk_esp32_fabric *f,const uint32_t *words,size_t count)
{ return f ? esk_esp32_efuse_configure(&f->efuse,words,count) : ES_EINVAL; }
es_status esk_esp32_fabric_efuse_irq(const esk_esp32_fabric *f,int *asserted)
{ return f ? esk_esp32_efuse_irq(&f->efuse,asserted) : ES_EINVAL; }
es_status esk_esp32_fabric_remap(esk_esp32_fabric *f,unsigned cpu,int enabled)
{
    if(!f || cpu>1) return ES_EINVAL;
    f->remap[cpu]=enabled ? 1u : 0u;return ES_OK;
}
es_status esk_esp32_fabric_load(esk_esp32_fabric *f,unsigned cpu,uint32_t addr,const unsigned char *data,size_t len)
{
    size_t i;struct location at;es_status st;
    if(!f || cpu>1 || (!data && len) || len>UINT64_C(0x100000000)-(uint64_t)addr) return ES_EINVAL;
    for(i=0;i<len;i++) { st=locate(&f->view[cpu],addr+(uint32_t)i,&at);if(st!=ES_OK) return st;if(at.readonly) return ES_EINVAL; }
    for(i=0;i<len;i++) { (void)locate(&f->view[cpu],addr+(uint32_t)i,&at);*at.byte=data[i]; }
    return ES_OK;
}
static es_status atomic_type(void *ctx,uint32_t address,unsigned *type)
{
    struct view *v=(struct view *)ctx;struct location at;es_status st;
    if(!v || !type || (address&3u)) return ES_EINVAL;
    st=locate(v,address,&at);if(st!=ES_OK || !at.atomic) return ES_EUNSUPPORTED;
    /* Qualified writable SRAM2/SRAM1 lies inside the core's configured
     * DataRAM0 window. No external cacheability/PSRAM type is guessed. */
    *type=ESK_XT_MEM_LOCAL;return ES_OK;
}
esk_xtensa_memory esk_esp32_fabric_bus(esk_esp32_fabric *f,unsigned cpu)
{
    esk_xtensa_memory bus={NULL,NULL,NULL,NULL,NULL,NULL};
    if(f && cpu<2) { bus.ctx=&f->view[cpu];bus.read=read_bus;bus.write=write_bus;bus.compare_exchange=cas_bus;bus.fetch=fetch_bus;bus.atomic_type=atomic_type; }
    return bus;
}

es_status esk_esp32_fabric_efuse_read_complete(esk_esp32_fabric *f)
{ return f ? esk_esp32_efuse_read_complete(&f->efuse) : ES_EINVAL; }
