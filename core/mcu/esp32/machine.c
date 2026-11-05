/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "mcu/esp32/machine.h"
#include <string.h>
struct esk_esp32_machine {
    esk_alloc *a;esk_esp32_fabric *fabric;
    esk_xtensa_windows cpu[2];uint64_t retired[2];unsigned running[2];
};
es_status esk_esp32_machine_create(esk_alloc *a,esk_esp32_machine **out)
{
    esk_esp32_machine *m;es_status st;unsigned i;
    if(!out) return ES_EINVAL;
    *out=NULL;if(!a) return ES_EINVAL;
    m=(esk_esp32_machine *)esk_calloc(a,1,sizeof *m);if(!m) return ES_ENOMEM;
    m->a=a;st=esk_esp32_fabric_create(a,&m->fabric);
    if(st!=ES_OK) { esk_free(a,m,sizeof *m);return st; }
    for(i=0;i<2;i++) esk_xtensa_windows_init(&m->cpu[i],0,NULL);
    *out=m;return ES_OK;
}
void esk_esp32_machine_destroy(esk_esp32_machine *m)
{
    if(!m) return;
    esk_esp32_fabric_destroy(m->fabric);esk_free(m->a,m,sizeof *m);
}
esk_esp32_fabric *esk_esp32_machine_fabric(esk_esp32_machine *m) { return m ? m->fabric : NULL; }
es_status esk_esp32_machine_configure(esk_esp32_machine *m,unsigned cpu,uint32_t entry)
{
    if(!m || cpu>1) return ES_EINVAL;
    esk_xtensa_windows_init(&m->cpu[cpu],entry,NULL);m->running[cpu]=0;m->retired[cpu]=0;return ES_OK;
}
esk_xtensa_windows *esk_esp32_machine_core(esk_esp32_machine *m,unsigned cpu) { return m && cpu<2 ? &m->cpu[cpu] : NULL; }
es_status esk_esp32_machine_run_core(esk_esp32_machine *m,unsigned cpu,int run)
{
    if(!m || cpu>1) return ES_EINVAL;
    m->running[cpu]=run ? 1u : 0u;return ES_OK;
}
es_status esk_esp32_machine_step(esk_esp32_machine *m,unsigned cpu,unsigned *event,esk_xtensa_fault *fault)
{
    esk_xtensa_memory bus;es_status st;
    if(event) *event=0;
    if(fault) memset(fault,0,sizeof *fault);
    if(!m || cpu>1 || !event) return ES_EINVAL;
    if(!m->running[cpu]) { *event=19;return ES_OK; }
    bus=esk_esp32_fabric_bus(m->fabric,cpu);
    st=esk_xtensa_window_step(&m->cpu[cpu],&bus,event,fault);
    if(st!=ES_OK) m->running[cpu]=0;
    else if(!*event) m->retired[cpu]++;
    return st;
}
es_status esk_esp32_machine_round(esk_esp32_machine *m,unsigned events[2],unsigned *failed_cpu,esk_xtensa_fault *fault)
{
    unsigned i;es_status st;
    if(failed_cpu) *failed_cpu=2;
    if(!m || !events || !failed_cpu) return ES_EINVAL;
    events[0]=events[1]=0;
    for(i=0;i<2;i++) {
        st=esk_esp32_machine_step(m,i,&events[i],fault);
        if(st!=ES_OK) { *failed_cpu=i;return st; }
    }
    return ES_OK;
}
uint64_t esk_esp32_machine_retired(const esk_esp32_machine *m,unsigned cpu) { return m && cpu<2 ? m->retired[cpu] : 0; }
