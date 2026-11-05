/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/machine.h"
static void register_fields_and_suspend(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *app;
    esk_xtensa_fault fault;uint32_t v=99;unsigned event,i;const uint32_t values[]={0,1,2,UINT32_MAX,0x80000000};
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);bus=esk_esp32_machine_bus(m,0);
    for(i=0;i<3;i++) { CHECK_EQ(bus.read(bus.ctx,0x3ff00030u+4u*i,4,&v),ES_OK);CHECK_EQ(v,0); }
    CHECK_EQ(esk_esp32_machine_configure(m,1,0x1234),ES_OK);app=esk_esp32_machine_core(m,1);app->cpu.a[5]=77;
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,19);
    CHECK_EQ(esk_esp32_machine_run_core(m,1,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,20);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00034,4,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,20);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,21);
    CHECK_EQ(app->cpu.pc,0x1234);CHECK_EQ(app->cpu.a[5],77);CHECK_EQ(esk_esp32_machine_retired(m,1),0);
    for(i=0;i<sizeof values/sizeof values[0];i++) {
        CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,values[i]),ES_OK);
        CHECK_EQ(bus.read(bus.ctx,0x3ff00030,4,&v),ES_OK);CHECK_EQ(v,values[i]&1u);
        CHECK_EQ(bus.write(bus.ctx,0x3ff00034,4,values[i]),ES_OK);
        CHECK_EQ(bus.read(bus.ctx,0x3ff00034,4,&v),ES_OK);CHECK_EQ(v,values[i]&1u);
        CHECK_EQ(bus.write(bus.ctx,0x3ff00038,4,values[i]),ES_OK);
        CHECK_EQ(bus.read(bus.ctx,0x3ff00038,4,&v),ES_OK);CHECK_EQ(v,values[i]);CHECK_EQ(app->cpu.pc,0x1234);
    }
    CHECK_EQ(bus.write(bus.ctx,0x3ff00030,2,1),ES_EINVAL);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00031,4,1),ES_EINVAL);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00038,1,1),ES_EINVAL);
    CHECK_EQ(bus.write(bus.ctx,0x3ff0002c,4,0),ES_EUNSUPPORTED);
    CHECK_EQ(bus.read(bus.ctx,0x3ff0002c,4,&v),ES_EUNSUPPORTED);
    CHECK_EQ(bus.fetch(bus.ctx,0x3ff00030,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(bus.compare_exchange(bus.ctx,0x3ff00030,0,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(bus.read(bus.ctx,0x3ff00030,4,&v),ES_OK);CHECK_EQ(v,0);
    CHECK_EQ(bus.read(bus.ctx,0x3ff00038,4,&v),ES_OK);CHECK_EQ(v,0x80000000u);
    CHECK_EQ(esk_esp32_machine_configure(m,1,0x50000000),ES_OK);
    CHECK_EQ(bus.read(bus.ctx,0x3ff00030,4,&v),ES_OK);CHECK_EQ(v,0);
    CHECK_EQ(bus.read(bus.ctx,0x3ff00038,4,&v),ES_OK);CHECK_EQ(v,0x80000000u);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void firmware_self_suspend(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *app,*pro;
    esk_xtensa_fault fault;unsigned event,events[2],failed;uint32_t v;
    unsigned char code[]={0x22,0x63,0,0x22,0x64,0,0x3d,0xf0}; /* stores to A3/A4, NOP */
    unsigned char nops[]={0x3d,0xf0,0x3d,0xf0};
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080000,code,sizeof code),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080100,nops,sizeof nops),ES_OK);
    CHECK_EQ(esk_esp32_machine_configure(m,1,0x40080000),ES_OK);app=esk_esp32_machine_core(m,1);
    CHECK_EQ(esk_esp32_machine_configure(m,0,0x40080100),ES_OK);pro=esk_esp32_machine_core(m,0);
    app->cpu.a[2]=0;app->cpu.a[3]=0x3ff00030;app->cpu.a[4]=0x3ff00034;
    CHECK_EQ(bus.write(bus.ctx,0x3ffb0000,4,0xdeadbeef),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,1),ES_OK);CHECK_EQ(esk_esp32_machine_run_core(m,1,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,0);CHECK_EQ(app->cpu.pc,0x40080003);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,20);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,1),ES_OK);app->cpu.a[2]=1;
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,0);CHECK_EQ(app->cpu.pc,0x40080006);
    CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_round(m,events,&failed,&fault),ES_OK);CHECK_EQ(failed,2);
    CHECK_EQ(events[0],0);CHECK_EQ(events[1],21);CHECK_EQ(pro->cpu.pc,0x40080102);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00034,4,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,0);CHECK_EQ(app->cpu.pc,0x40080008);
    CHECK_EQ(esk_esp32_machine_retired(m,1),3);CHECK_EQ(app->cpu.a[3],0x3ff00030);
    CHECK_EQ(bus.read(bus.ctx,0x3ffb0000,4,&v),ES_OK);CHECK_EQ(v,0xdeadbeefu);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void gated_clock_and_pending_interrupt(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *app,*pro;
    esk_xtensa_fault fault;unsigned event,cpu;uint64_t ticks=99;
    const uint32_t general[3]={0x40081000,0x40081040,0x40081080};
    const uint32_t high[6]={0x40082000,0x40082040,0x40082080,0x400820c0,0x40082100,0x40082140};
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);bus=esk_esp32_machine_bus(m,0);
    for(cpu=0;cpu<2;cpu++) {
        esk_xtensa_windows *w;
        CHECK_EQ(esk_esp32_machine_configure(m,cpu,0x40080000),ES_OK);w=esk_esp32_machine_core(m,cpu);
        esk_xtensa_exception_configure(w,general);CHECK_EQ(esk_xtensa_irq_configure(w,high),ES_OK);
        CHECK_EQ(esk_xtensa_irq_write(w,240,10),ES_OK);
    }
    app=esk_esp32_machine_core(m,1);pro=esk_esp32_machine_core(m,0);app->irq.intenable=1u<<6;
    CHECK_EQ(esk_esp32_machine_run_core(m,1,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_clock_advance(m,1,100),ES_OK);CHECK_EQ(app->irq.ccount,0);
    CHECK_EQ(esk_esp32_machine_clock_deadline(m,1,&ticks),0);CHECK_EQ(ticks,0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,1),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff00034,4,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_clock_deadline(m,1,&ticks),1);CHECK_EQ(ticks,10);
    CHECK_EQ(esk_esp32_machine_clock_advance(m,1,10),ES_OK);CHECK_EQ(app->irq.ccount,10);CHECK_EQ(pro->irq.ccount,0);
    CHECK_EQ(esk_xtensa_irq_pending(app),1u<<6);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,21);CHECK_EQ(app->cpu.pc,0x40080000);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_clock_advance(m,1,UINT64_MAX),ES_OK);CHECK_EQ(app->irq.ccount,10);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00034,4,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,20);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,11);CHECK_EQ(app->cpu.pc,general[0]);
    CHECK_EQ(esk_esp32_machine_retired(m,1),0);CHECK_EQ(app->epc1,0x40080000);
    CHECK_EQ(esk_esp32_machine_clock_advance(m,0,3),ES_OK);CHECK_EQ(pro->irq.ccount,3);CHECK_EQ(app->irq.ccount,10);
    CHECK_EQ(esk_esp32_machine_clock_advance(m,2,1),ES_EINVAL);
    CHECK_EQ(esk_esp32_machine_clock_deadline(m,2,&ticks),0);CHECK_EQ(ticks,0);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(register_fields_and_suspend);RUN(firmware_self_suspend);RUN(gated_clock_and_pending_interrupt);
TEST_MAIN_END
