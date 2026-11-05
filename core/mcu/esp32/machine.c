/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "mcu/esp32/machine.h"
#include <string.h>
struct machine_view { struct esk_esp32_machine *m;unsigned cpu; };
struct esk_esp32_machine {
    esk_alloc *a;esk_esp32_fabric *fabric;
    esk_xtensa_windows cpu[2];uint64_t retired[2];unsigned running[2],executing;
    uint32_t app_clock,app_stall,app_boot,app_reset,app_reset_enabled,app_reset_vector,app_reset_pending;
    unsigned spi1_owned,efuse_owned,uart_owned;
    uint32_t rtc_enabled,rtc_state,rtc_store[8];
    esk_esp32_gpio gpio;esk_esp32_uart uart;
    esk_esp32_matrix matrix;struct machine_view view[2];
};
/* Never mutate an executing CPU from a bus callback. Synchronize the
 * combinatorial routing only outside execution or after instruction commit.
 * Both cores are updated, including a debug-paused peer, so edge pulses
 * across separate retired writes are latched even while the peer is paused. */
static void sync_inputs(esk_esp32_machine *m)
{
    unsigned cpu,input;int asserted=0;const uint32_t internal=UINT32_C(0x200188c0);
    if(esk_esp32_fabric_spi1_irq(m->fabric,&asserted)==ES_OK) {
        (void)esk_esp32_matrix_source(&m->matrix,29,asserted);m->spi1_owned=1;
    } else if(m->spi1_owned) {
        (void)esk_esp32_matrix_source(&m->matrix,29,0);m->spi1_owned=0;
    }
    if(esk_esp32_fabric_efuse_irq(m->fabric,&asserted)==ES_OK) {
        (void)esk_esp32_matrix_source(&m->matrix,44,asserted);m->efuse_owned=1;
    } else if(m->efuse_owned) {
        (void)esk_esp32_matrix_source(&m->matrix,44,0);m->efuse_owned=0;
    }
    if(esk_esp32_uart_irq(&m->uart,&asserted)==ES_OK) {
        (void)esk_esp32_matrix_source(&m->matrix,34,asserted);m->uart_owned=1;
    } else if(m->uart_owned) {
        (void)esk_esp32_matrix_source(&m->matrix,34,0);m->uart_owned=0;
    }
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
        if(m->rtc_enabled) {
            m->app_reset_vector=(m->rtc_state>>12)&1u;
            m->rtc_state=(m->rtc_state&~UINT32_C(0xfc0))|(14u<<6);
        }
        (void)esk_xtensa_lx6_reset(&m->cpu[1],m->app_reset_vector);m->app_reset_pending=0;
    }
}
/* Return 0=reset state, 1..8=retention, -1=unqualified/hole. */
static int rtc_register(const esk_esp32_machine *m,uint32_t address)
{
    if(!m->rtc_enabled) return -1;
    if(address>=0x3ff48034 && address<0x3ff48038) return 0;
    if(address>=0x3ff4804c && address<0x3ff4805c) return 1+(int)((address-0x3ff4804c)/4u);
    if(address>=0x3ff480b0 && address<0x3ff480c0) return 5+(int)((address-0x3ff480b0)/4u);
    return -1;
}
es_status esk_esp32_machine_rtc_configure(esk_esp32_machine *m,uint32_t reset_state,const uint32_t *retention,size_t count)
{
    if(!m || m->executing || (!retention && count)) return ES_EINVAL;
    if(!retention && !count) { m->rtc_enabled=0;return ES_OK; }
    if(count!=8 || (reset_state&~UINT32_C(0x3fff))) return ES_EINVAL;
    memcpy(m->rtc_store,retention,sizeof m->rtc_store);m->rtc_state=reset_state;m->rtc_enabled=1;return ES_OK;
}
/* APP reset input is opt-in. BOOT_ADDR is consumed by firmware, never used
 * as the hardware reset PC. Architectural changes occur outside callbacks. */
static es_status control_read(const esk_esp32_machine *m,uint32_t address,unsigned bytes,uint32_t *value)
{
    int reg=rtc_register(m,address);
    if(reg>=0) {
        if(!value || bytes!=4 || (address&3u)) return ES_EINVAL;
        *value=reg ? m->rtc_store[(unsigned)reg-1u] : m->rtc_state;return ES_OK;
    }
    if(address<0x3ff0002c || address>0x3ff0003b) return ES_EUNSUPPORTED;
    if(address<0x3ff00030 && !m->app_reset_enabled) return ES_EUNSUPPORTED;
    if(!value || bytes!=4 || (address&3u)) return ES_EINVAL;
    *value=address==0x3ff0002c ? m->app_reset : address==0x3ff00030 ? m->app_clock : address==0x3ff00034 ? m->app_stall : m->app_boot;
    return ES_OK;
}
static es_status control_write(esk_esp32_machine *m,uint32_t address,unsigned bytes,uint32_t value)
{
    int reg=rtc_register(m,address);
    if(reg>=0) {
        if(bytes!=4 || (address&3u)) return ES_EINVAL;
        if(reg) m->rtc_store[(unsigned)reg-1u]=value;
        else m->rtc_state=(m->rtc_state&0xfffu)|(value&0x3000u);
        return ES_OK;
    }
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
    st=esk_esp32_uart_read(&v->m->uart,address,bytes,value);
    if(st!=ES_EUNSUPPORTED) { if(st==ES_OK && !v->m->executing) sync_inputs(v->m);return st; }
    st=esk_esp32_gpio_read(&v->m->gpio,address,bytes,value);if(st!=ES_EUNSUPPORTED) return st;
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
    st=esk_esp32_uart_write(&v->m->uart,address,bytes,value);
    if(st!=ES_EUNSUPPORTED) { if(st==ES_OK && !v->m->executing) sync_inputs(v->m);return st; }
    st=esk_esp32_gpio_write(&v->m->gpio,address,bytes,value);if(st!=ES_EUNSUPPORTED) return st;
    st=esk_esp32_matrix_write(&v->m->matrix,address,bytes,value);
    if(st!=ES_EUNSUPPORTED) {
        if(st==ES_OK && !v->m->executing) sync_inputs(v->m);
        return st;
    }
    ram=esk_esp32_fabric_bus(v->m->fabric,v->cpu);st=ram.write(ram.ctx,address,bytes,value);
    if(st==ES_OK && !v->m->executing) sync_inputs(v->m);
    return st;
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
static es_status bus_atomic_type(void *ctx,uint32_t address,unsigned *type)
{
    struct machine_view *v=(struct machine_view *)ctx;esk_xtensa_memory ram=esk_esp32_fabric_bus(v->m->fabric,v->cpu);
    return ram.atomic_type(ram.ctx,address,type);
}
esk_xtensa_memory esk_esp32_machine_bus(esk_esp32_machine *m,unsigned cpu)
{
    esk_xtensa_memory bus={NULL,NULL,NULL,NULL,NULL,NULL};
    if(m && cpu<2) { bus.ctx=&m->view[cpu];bus.read=bus_read;bus.write=bus_write;bus.fetch=bus_fetch;bus.atomic_type=bus_atomic_type;bus.compare_exchange=bus_cas; }
    return bus;
}
es_status esk_esp32_machine_source(esk_esp32_machine *m,unsigned source,int asserted)
{
    es_status st;int asserted_now=0;if(!m || m->executing) return ES_EINVAL;
    if(source==29) {
        if(esk_esp32_fabric_spi1_irq(m->fabric,&asserted_now)==ES_OK) return ES_EUNSUPPORTED;
        if(m->spi1_owned) sync_inputs(m);
    }
    if(source==44) {
        if(esk_esp32_fabric_efuse_irq(m->fabric,&asserted_now)==ES_OK) return ES_EUNSUPPORTED;
        if(m->efuse_owned) sync_inputs(m);
    }
    if(source==34) {
        if(esk_esp32_uart_irq(&m->uart,&asserted_now)==ES_OK) return ES_EUNSUPPORTED;
        if(m->uart_owned) sync_inputs(m);
    }
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
    if(m->rtc_enabled) m->rtc_state=(m->rtc_state&~UINT32_C(0x1000))|(vector_input<<12);
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
    esk_esp32_matrix_init(&m->matrix);esk_esp32_gpio_init(&m->gpio);esk_esp32_uart_init(&m->uart);
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

es_status esk_esp32_machine_gpio_configure(esk_esp32_machine *m,const uint32_t *config,size_t count)
{ return !m || m->executing ? ES_EINVAL : esk_esp32_gpio_configure(&m->gpio,config,count); }
es_status esk_esp32_machine_gpio_input(esk_esp32_machine *m,unsigned pin,int known,int high)
{ return !m || m->executing ? ES_EINVAL : esk_esp32_gpio_input(&m->gpio,pin,known,high); }
es_status esk_esp32_machine_gpio_output(const esk_esp32_machine *m,unsigned pin,esk_esp32_gpio_pad *pad)
{ return !m || m->executing ? ES_EINVAL : esk_esp32_gpio_output(&m->gpio,pin,pad); }
es_status esk_esp32_machine_gpio_auto_hold_event(esk_esp32_machine *m)
{ return !m || m->executing ? ES_EINVAL : esk_esp32_gpio_auto_hold_event(&m->gpio); }

es_status esk_esp32_machine_uart_configure(esk_esp32_machine *m,int on,int rxd,int cts,int dsr)
{
    es_status st;if(!m || m->executing) return ES_EINVAL;
    st=esk_esp32_uart_configure(&m->uart,on,rxd,cts,dsr);if(st==ES_OK) sync_inputs(m);return st;
}
es_status esk_esp32_machine_uart_inputs(esk_esp32_machine *m,int rxd,int cts,int dsr)
{
    es_status st;if(!m || m->executing) return ES_EINVAL;
    st=esk_esp32_uart_inputs(&m->uart,rxd,cts,dsr);if(st==ES_OK) sync_inputs(m);return st;
}
es_status esk_esp32_machine_uart_receive(esk_esp32_machine *m,uint32_t data,unsigned flags)
{
    es_status st;if(!m || m->executing) return ES_EINVAL;
    st=esk_esp32_uart_receive(&m->uart,data,flags);if(st==ES_OK) sync_inputs(m);return st;
}
es_status esk_esp32_machine_uart_timeout(esk_esp32_machine *m)
{
    es_status st;if(!m || m->executing) return ES_EINVAL;
    st=esk_esp32_uart_timeout(&m->uart);if(st==ES_OK) sync_inputs(m);return st;
}
es_status esk_esp32_machine_uart_advance(esk_esp32_machine *m,uint64_t ticks,esk_esp32_uart_emit emit,void *ctx)
{
    es_status st;if(!m || m->executing) return ES_EINVAL;
    m->executing=1;st=esk_esp32_uart_advance(&m->uart,ticks,emit,ctx);m->executing=0;
    if(st==ES_OK) sync_inputs(m);
    return st;
}
