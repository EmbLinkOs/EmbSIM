/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Interrupt configuration facts checked against Espressif's ESP32 toolchain
 * core-isa.h and XEA2 semantics. Original controller implementation. */
#include "mcu/xtensa/windows.h"
#include <string.h>

#define SOFTWARE UINT32_C(0x20000080)
#define EDGE UINT32_C(0x50400400)
#define NMI UINT32_C(0x00004000)
static const unsigned timer_input[3]={6,15,16};
static const uint32_t levels[8]={0,0x000637ff,0x00380000,0x28c08800,0x53000000,0x84010000,0,0x00004000};

es_status esk_xtensa_irq_configure(struct esk_xtensa_windows *w,const uint32_t high_vectors[6])
{
    unsigned i;
    if (!w || !high_vectors || !w->exceptions_enabled) return ES_EINVAL;
    memset(&w->irq,0,sizeof w->irq);w->irq.enabled=1;
    for(i=0;i<6;i++) w->irq.vectors[i+2]=high_vectors[i];
    if(w->vecbase_enabled) (void)esk_xtensa_vecbase_configure(w,w->vecbase);
    return ES_OK;
}
es_status esk_xtensa_irq_line(struct esk_xtensa_windows *w,unsigned input,int asserted)
{
    uint32_t mask;
    if (!w || !w->irq.enabled || input>=32) return ES_EINVAL;
    mask=UINT32_C(1)<<input;
    if (mask&SOFTWARE) return ES_EINVAL;
    if (asserted) {
        if (!(w->irq.lines&mask) && (mask&(EDGE|NMI))) w->irq.latched|=mask;
        w->irq.lines|=mask;
    } else w->irq.lines&=~mask;
    return ES_OK;
}
uint32_t esk_xtensa_irq_pending(const struct esk_xtensa_windows *w)
{
    if (!w || !w->irq.enabled) return 0;
    return (w->irq.lines&~(SOFTWARE|EDGE|NMI))|(w->irq.latched&(SOFTWARE|EDGE|NMI));
}
es_status esk_xtensa_irq_poll(struct esk_xtensa_windows *w,unsigned *event)
{
    uint32_t pending,eligible,mask;unsigned threshold,level;
    if (event) *event=0;
    if (!w || !event) return ES_EINVAL;
    if (!w->irq.enabled) return ES_OK;
    pending=esk_xtensa_irq_pending(w);threshold=w->ps&15u;
    if ((w->ps&ESK_XT_PS_EXCM) && threshold<3) threshold=3; /* ESP32 EXCMLEVEL */
    eligible=pending&w->irq.intenable;
    if (pending&NMI) level=7;
    else {
        for(level=5;level>0;level--) if (level>threshold && (eligible&levels[level])) break;
        if (!level) return ES_OK;
    }
    mask=level==7 ? NMI : eligible&levels[level];
    if (level==1) {
        es_status st=esk_xtensa_raise_exception(w,4,0,0,event);
        if (st!=ES_OK) return st;
    } else {
        w->irq.epc[level]=w->cpu.pc;w->irq.eps[level]=w->ps;
        w->ps=(w->ps&~15u)|level|ESK_XT_PS_EXCM;w->cpu.pc=w->vecbase_enabled ? w->vecbase+0x180u+(level-2u)*64u : w->irq.vectors[level];
    }
    if(w->memctl_enabled) memset(w->loopbuf_valid,0,sizeof w->loopbuf_valid);
    if (level==7) w->irq.latched&=~NMI;
    w->irq.waiting=0;w->irq.last_level=level;w->irq.last_pending=mask;
    *event=10u+level;return ES_OK;
}
es_status esk_xtensa_irq_return(struct esk_xtensa_windows *w,unsigned level)
{
    if (!w || !w->irq.enabled || level<2 || level>7) return ES_EUNSUPPORTED;
    w->ps=w->irq.eps[level]&ESK_XT_PS_MASK&~(w->region_enabled ? UINT32_C(0xc0) : 0u);w->cpu.pc=w->irq.epc[level];return ES_OK;
}
int esk_xtensa_irq_special(unsigned reg)
{
    return (reg>=178 && reg<=183)||(reg>=194 && reg<=199)||(reg>=209 && reg<=215)||(reg>=226 && reg<=228)||reg==234||(reg>=240 && reg<=242);
}
es_status esk_xtensa_irq_read(const struct esk_xtensa_windows *w,unsigned reg,uint32_t *value)
{
    if (!w || !w->irq.enabled || !value) return ES_EUNSUPPORTED;
    if (reg>=178 && reg<=183) *value=w->irq.epc[reg-176];
    else if (reg>=194 && reg<=199) *value=w->irq.eps[reg-192]&~(w->region_enabled ? UINT32_C(0xc0) : 0u);
    else if (reg>=209 && reg<=215) *value=w->irq.excsave[reg-208];
    else if (reg==226) *value=esk_xtensa_irq_pending(w);
    else if (reg==228) *value=w->irq.intenable;
    else if (reg==234) *value=w->irq.ccount;
    else if (reg>=240 && reg<=242) *value=w->irq.compare[reg-240];
    else return ES_EUNSUPPORTED;
    return ES_OK;
}
es_status esk_xtensa_irq_write(struct esk_xtensa_windows *w,unsigned reg,uint32_t value)
{
    if (!w || !w->irq.enabled) return ES_EUNSUPPORTED;
    if (reg>=178 && reg<=183) w->irq.epc[reg-176]=value;
    else if (reg>=194 && reg<=199) w->irq.eps[reg-192]=value&~(w->region_enabled ? UINT32_C(0xc0) : 0u);
    else if (reg>=209 && reg<=215) w->irq.excsave[reg-208]=value;
    else if (reg==226) w->irq.latched|=value&SOFTWARE;
    else if (reg==227) w->irq.latched&=~(value&(SOFTWARE|EDGE));
    else if (reg==228) w->irq.intenable=value;
    else if (reg==234) w->irq.ccount=value;
    else if (reg>=240 && reg<=242) {
        w->irq.compare[reg-240]=value;w->irq.lines&=~(UINT32_C(1)<<timer_input[reg-240]);
    }
    else return ES_EUNSUPPORTED;
    return ES_OK;
}

static uint64_t distance(uint32_t now,uint32_t compare)
{
    uint32_t d=compare-now;return d ? (uint64_t)d : UINT64_C(0x100000000);
}
es_status esk_xtensa_clock_advance(struct esk_xtensa_windows *w,uint64_t ticks)
{
    unsigned i;
    if (!w || !w->irq.enabled) return ES_EUNSUPPORTED;
    for(i=0;i<3;i++) if(ticks>=distance(w->irq.ccount,w->irq.compare[i]))
        w->irq.lines|=UINT32_C(1)<<timer_input[i];
    w->irq.ccount+=(uint32_t)(ticks&UINT64_C(0xffffffff));return ES_OK;
}
int esk_xtensa_clock_deadline(const struct esk_xtensa_windows *w,uint64_t *ticks)
{
    unsigned i;uint64_t best=UINT64_C(0x100000001);
    if(ticks) *ticks=0;
    if(!w || !w->irq.enabled || !ticks) return 0;
    for(i=0;i<3;i++) if(!(w->irq.lines&(UINT32_C(1)<<timer_input[i]))) {
        uint64_t d=distance(w->irq.ccount,w->irq.compare[i]);if(d<best) best=d;
    }
    if(best>UINT64_C(0x100000000)) return 0;
    *ticks=best;return 1;
}
