/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "mcu/esp32/machine.h"
#include <string.h>
struct machine_view { struct esk_esp32_machine *m;unsigned cpu; };
struct esk_esp32_machine {
    esk_alloc *a;esk_esp32_fabric *fabric;
    esk_xtensa_windows cpu[2];uint64_t retired[2];unsigned running[2],executing;
    uint32_t app_clock,app_stall,app_boot,app_reset,app_reset_enabled,app_reset_vector,app_reset_pending;
    esk_esp32_matrix matrix;struct machine_view view[2];
};
/* Never mutate an executing CPU from a bus callback. Synchronize the
 * combinatorial routing only outside execution or after instruction commit.
 * Both cores are updated, including a debug-paused peer, so edge pulses
 * across separate retired writes are latched even while the peer is paused. */
static void sync_inputs(esk_esp32_machine *m)
{
    unsigned cpu,input;const uint32_t internal=UINT32_C(0x200188c0);
    for(cpu=0;cpu<2;cpu++) if(m->cpu[cpu].irq.enabled && !(cpu==1 && m->app_reset_enabled && m->app_reset)) {
        uint32_t levels=esk_esp32_matrix_levels(&m->matrix,cpu);
        for(input=0;input<32;input++) {
            uint32_t bit=UINT32_C(1)<<input;
            if(!(bit&internal)) (void)esk_xtensa_irq_line(&m->cpu[cpu],input,(levels&bit)!=0);
        }
    }
}
static void apply_reset(esk_esp32_machine *m)
{
    if(m->app_reset_pending) {
        (void)esk_xtensa_lx6_reset(&m->cpu[1],m->app_reset_vector);m->app_reset_pending=0;
    }
}
/* APP reset input is opt-in. BOOT_ADDR is consumed by firmware, never used
 * as the hardware reset PC. Architectural changes occur outside callbacks. */
static es_status control_read(const esk_esp32_machine *m,uint32_t address,unsigned bytes,uint32_t *value)
{
    if(address<0x3ff0002c || address>0x3ff0003b) return ES_EUNSUPPORTED;
    if(address<0x3ff00030 && !m->app_reset_enabled) return ES_EUNSUPPORTED;
    if(!value || bytes!=4 || (address&3u)) return ES_EINVAL;
    *value=address==0x3ff0002c ? m->app_reset : address==0x3ff00030 ? m->app_clock : address==0x3ff00034 ? m->app_stall : m->app_boot;
    return ES_OK;
}
static es_status control_write(esk_esp32_machine *m,uint32_t address,unsigned bytes,uint32_t value)
{
    if(address<0x3ff0002c || address>0x3ff0003b) return ES_EUNSUPPORTED;
    if(address<0x3ff00030 && !m->app_reset_enabled) return ES_EUNSUPPORTED;
    if(bytes!=4 || (address&3u)) return ES_EINVAL;
    if(address==0x3ff0002c) {
        if((value&1u) && !m->app_reset) m->app_reset_pending=1;
        m->app_reset=value&1u;
    } else if(address==0x3ff00030) m->app_clock=value&1u;
    else if(address==0x3ff00034) m->app_stall=value&1u;
    else m->app_boot=value;
    return ES_OK;
}
static es_status bus_read(void *ctx,uint32_t address,unsigned bytes,uint32_t *value)
{
    struct machine_view *v=(struct machine_view *)ctx;esk_xtensa_memory ram;es_status st;
    st=control_read(v->m,address,bytes,value);if(st!=ES_EUNSUPPORTED) return st;
    st=esk_esp32_matrix_read(&v->m->matrix,address,bytes,value);
    if(st!=ES_EUNSUPPORTED) return st;
    ram=esk_esp32_fabric_bus(v->m->fabric,v->cpu);return ram.read(ram.ctx,address,bytes,value);
}
static es_status bus_write(void *ctx,uint32_t address,unsigned bytes,uint32_t value)
{
    struct machine_view *v=(struct machine_view *)ctx;esk_xtensa_memory ram;es_status st;
    st=control_write(v->m,address,bytes,value);
    if(st!=ES_EUNSUPPORTED) {
        if(st==ES_OK && !v->m->executing) { apply_reset(v->m);sync_inputs(v->m); }
        return st;
    }
    st=esk_esp32_matrix_write(&v->m->matrix,address,bytes,value);
    if(st!=ES_EUNSUPPORTED) {
        if(st==ES_OK && !v->m->executing) sync_inputs(v->m);
        return st;
    }
    ram=esk_esp32_fabric_bus(v->m->fabric,v->cpu);return ram.write(ram.ctx,address,bytes,value);
}
static es_status bus_fetch(void *ctx,uint32_t address,unsigned bytes,uint32_t *value)
{
    struct machine_view *v=(struct machine_view *)ctx;esk_xtensa_memory ram=esk_esp32_fabric_bus(v->m->fabric,v->cpu);
    return ram.fetch(ram.ctx,address,bytes,value);
}
static es_status bus_cas(void *ctx,uint32_t address,uint32_t expected,uint32_t desired,uint32_t *observed)
{
    struct machine_view *v=(struct machine_view *)ctx;esk_xtensa_memory ram=esk_esp32_fabric_bus(v->m->fabric,v->cpu);
    return ram.compare_exchange(ram.ctx,address,expected,desired,observed);
}
esk_xtensa_memory esk_esp32_machine_bus(esk_esp32_machine *m,unsigned cpu)
{
    esk_xtensa_memory bus={NULL,NULL,NULL,NULL,NULL};
    if(m && cpu<2) { bus.ctx=&m->view[cpu];bus.read=bus_read;bus.write=bus_write;bus.fetch=bus_fetch;bus.compare_exchange=bus_cas; }
    return bus;
}
es_status esk_esp32_machine_source(esk_esp32_machine *m,unsigned source,int asserted)
{
    es_status st;if(!m || m->executing) return ES_EINVAL;
    st=esk_esp32_matrix_source(&m->matrix,source,asserted);if(st==ES_OK) sync_inputs(m);return st;
}
es_status esk_esp32_machine_gpio_irq(esk_esp32_machine *m,unsigned cpu,int nmi,int asserted)
{
    es_status st;if(!m || m->executing) return ES_EINVAL;
    st=esk_esp32_matrix_gpio(&m->matrix,cpu,nmi,asserted);if(st==ES_OK) sync_inputs(m);return st;
}
es_status esk_esp32_machine_nmi_mask(esk_esp32_machine *m,unsigned cpu,int asserted)
{
    es_status st;if(!m || m->executing) return ES_EINVAL;
    st=esk_esp32_matrix_nmi_mask(&m->matrix,cpu,asserted);if(st==ES_OK) sync_inputs(m);return st;
}
es_status esk_esp32_machine_app_reset_configure(esk_esp32_machine *m,unsigned vector_input)
{
    if(!m || vector_input>1 || m->executing) return ES_EINVAL;
    m->app_reset_enabled=1;m->app_reset_vector=vector_input;m->app_reset=1;m->app_reset_pending=1;
    apply_reset(m);return ES_OK;
}
es_status esk_esp32_machine_clock_advance(esk_esp32_machine *m,unsigned cpu,uint64_t ticks)
{
    if(!m || cpu>1 || m->executing) return ES_EINVAL;
    if(!m->cpu[cpu].irq.enabled) return ES_EUNSUPPORTED;
    if(cpu==1 && (!m->app_clock || (m->app_reset_enabled && m->app_reset))) return ES_OK;
    return esk_xtensa_clock_advance(&m->cpu[cpu],ticks);
}
int esk_esp32_machine_clock_deadline(const esk_esp32_machine *m,unsigned cpu,uint64_t *ticks)
{
    if(ticks) *ticks=0;
    if(!m || cpu>1 || !ticks || (cpu==1 && (!m->app_clock || (m->app_reset_enabled && m->app_reset)))) return 0;
    return esk_xtensa_clock_deadline(&m->cpu[cpu],ticks);
}
es_status esk_esp32_machine_create(esk_alloc *a,esk_esp32_machine **out)
{
    esk_esp32_machine *m;es_status st;unsigned i;
    if(!out) return ES_EINVAL;
    *out=NULL;if(!a) return ES_EINVAL;
    m=(esk_esp32_machine *)esk_calloc(a,1,sizeof *m);if(!m) return ES_ENOMEM;
    m->a=a;st=esk_esp32_fabric_create(a,&m->fabric);
    if(st!=ES_OK) { esk_free(a,m,sizeof *m);return st; }
    esk_esp32_matrix_init(&m->matrix);
    for(i=0;i<2;i++) { esk_xtensa_windows_init(&m->cpu[i],0,NULL);m->view[i].m=m;m->view[i].cpu=i; }
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
    if(cpu==1 && m->app_reset_enabled) return ES_EUNSUPPORTED;
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
    if(cpu==1 && m->app_reset_enabled && m->app_reset) { *event=22;return ES_OK; }
    if(cpu==1 && (!m->app_clock || m->app_stall)) { *event=!m->app_clock ? 20u : 21u;return ES_OK; }
    sync_inputs(m);bus=esk_esp32_machine_bus(m,cpu);m->executing=1;
    st=esk_xtensa_window_step(&m->cpu[cpu],&bus,event,fault);
    m->executing=0;apply_reset(m);sync_inputs(m);
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
