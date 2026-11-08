/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Register facts: ESP32 TRM v5.9 and ESP-IDF v4.4.7 soc/dport_reg.h.
 * Original implementation; no vendor controller code linked. */
#include "mcu/esp32/matrix.h"
#include <string.h>
#define INTERNAL UINT32_C(0x200188c0) /* inputs 6,7,11,15,16,29 */
static int valid_source(unsigned source) { return source<69 && source!=41 && source!=42; }
void esk_esp32_matrix_init(esk_esp32_matrix *m)
{
    unsigned cpu,source;
    if(!m) return;
    memset(m,0,sizeof *m);
    for(cpu=0;cpu<2;cpu++) for(source=0;source<69;source++) m->map[cpu][source]=16;
}
es_status esk_esp32_matrix_source(esk_esp32_matrix *m,unsigned source,int asserted)
{
    uint32_t bit;
    if(!m || !valid_source(source) || (source>=22 && source<=27)) return ES_EINVAL;
    bit=UINT32_C(1)<<(source%32);
    if(asserted) m->sources[source/32]|=bit;else m->sources[source/32]&=~bit;
    return ES_OK;
}
es_status esk_esp32_matrix_gpio(esk_esp32_matrix *m,unsigned cpu,int nmi,int asserted)
{
    uint32_t bit=nmi ? 2u : 1u;
    if(!m || cpu>1) return ES_EINVAL;
    if(asserted) m->gpio[cpu]|=bit;else m->gpio[cpu]&=~bit;
    return ES_OK;
}
es_status esk_esp32_matrix_nmi_mask(esk_esp32_matrix *m,unsigned cpu,int asserted)
{
    if(!m || cpu>1) return ES_EINVAL;
    m->nmi_mask[cpu]=asserted ? 1u : 0u;return ES_OK;
}
uint32_t esk_esp32_matrix_status(const esk_esp32_matrix *m,unsigned cpu,unsigned group)
{
    uint32_t result;unsigned i;
    if(!m || cpu>1 || group>2) return 0;
    result=m->sources[group];
    if(!group) {
        result|=m->gpio[cpu]<<22;
        for(i=0;i<4;i++) result|=m->ipi[i]<<(24+i);
    }
    return result;
}
uint32_t esk_esp32_matrix_levels(const esk_esp32_matrix *m,unsigned cpu)
{
    uint32_t result=0,status[3];unsigned i;
    if(!m || cpu>1) return 0;
    for(i=0;i<3;i++) status[i]=esk_esp32_matrix_status(m,cpu,i);
    for(i=0;i<69;i++) if(valid_source(i) && (status[i/32]&(UINT32_C(1)<<(i%32)))) {
        uint32_t bit=UINT32_C(1)<<(m->map[cpu][i]&31u);
        if(!(bit&INTERNAL)) result|=bit;
    }
    if(m->nmi_mask[cpu]) result&=~UINT32_C(0x4000);
    return result;
}
/* Return kind 1=IPC, 2=status, 3=map. Reserved map slots remain holes. */
static es_status decode(uint32_t address,unsigned bytes,unsigned *kind,unsigned *cpu,unsigned *index)
{
    uint32_t offset;
    if(address<0x3ff000dc || address>0x3ff0032b) return ES_EUNSUPPORTED;
    if(bytes!=4 || (address&3u)) return ES_EINVAL;
    offset=address-0x3ff00000;
    if(offset<0xec) { *kind=1;*index=(offset-0xdc)/4;*cpu=0; }
    else if(offset<0x104) { *kind=2;*cpu=(offset-0xec)/12;*index=((offset-0xec)%12)/4; }
    else {
        *kind=3;*cpu=offset<0x218 ? 0u : 1u;
        *index=(offset-(*cpu ? 0x218u : 0x104u))/4;
        if(!valid_source(*index)) return ES_EUNSUPPORTED;
    }
    return ES_OK;
}
es_status esk_esp32_matrix_read(const esk_esp32_matrix *m,uint32_t address,unsigned bytes,uint32_t *value)
{
    unsigned kind,cpu,index;es_status st;
    if(!m || !value) return ES_EINVAL;
    st=decode(address,bytes,&kind,&cpu,&index);if(st!=ES_OK) return st;
    *value=kind==1 ? m->ipi[index] : kind==2 ? esk_esp32_matrix_status(m,cpu,index) : m->map[cpu][index];return ES_OK;
}
es_status esk_esp32_matrix_write(esk_esp32_matrix *m,uint32_t address,unsigned bytes,uint32_t value)
{
    unsigned kind,cpu,index;es_status st;
    if(!m) return ES_EINVAL;
    st=decode(address,bytes,&kind,&cpu,&index);if(st!=ES_OK) return st;
    if(kind==1) m->ipi[index]=value&1u;
    else if(kind==3) m->map[cpu][index]=value&31u;
    return ES_OK;
}
