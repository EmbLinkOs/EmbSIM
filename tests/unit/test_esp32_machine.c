/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/machine.h"
static void shared_execution(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_esp32_fabric *f;esk_xtensa_memory bus;
    esk_xtensa_windows *one,*two;unsigned event,events[2],failed;esk_xtensa_fault fault;uint32_t value;
    /* S32C1I A2,A3,0 followed by NOP.N. Independently assembled ESP32 opcode. */
    unsigned char code[]={0x22,0xe3,0x00,0x3d,0xf0};
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);
    f=esk_esp32_machine_fabric(m);bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,1),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(f,0,0x40080000,code,sizeof code),ES_OK);
    CHECK_EQ(esk_esp32_machine_configure(m,0,0x40080000),ES_OK);
    CHECK_EQ(esk_esp32_machine_configure(m,1,0x40080000),ES_OK);
    one=esk_esp32_machine_core(m,0);two=esk_esp32_machine_core(m,1);
    one->cpu.a[3]=two->cpu.a[3]=0x3ffb0000;one->cpu.a[2]=1;two->cpu.a[2]=2;
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,19);
    CHECK_EQ(one->cpu.pc,0x40080000);CHECK_EQ(esk_esp32_machine_retired(m,0),0);
    CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);CHECK_EQ(esk_esp32_machine_run_core(m,1,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_round(m,events,&failed,&fault),ES_OK);CHECK_EQ(failed,2);
    CHECK_EQ(events[0],0);CHECK_EQ(events[1],0);CHECK_EQ(one->cpu.a[2],0);CHECK_EQ(two->cpu.a[2],1);
    CHECK_EQ(bus.read(bus.ctx,0x3ffb0000,4,&value),ES_OK);CHECK_EQ(value,1);
    CHECK_EQ(esk_esp32_machine_retired(m,0),1);CHECK_EQ(esk_esp32_machine_retired(m,1),1);
    /* Explicit retry on CPU1: its comparison register is private. */
    two->scompare1=1;two->cpu.a[2]=2;two->cpu.pc=0x40080000;
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);
    CHECK_EQ(one->scompare1,0);CHECK_EQ(two->scompare1,1);
    CHECK_EQ(bus.read(bus.ctx,0x3ffb0000,4,&value),ES_OK);CHECK_EQ(value,2);
    CHECK_EQ(esk_esp32_machine_round(m,events,&failed,&fault),ES_OK);
    CHECK_EQ(one->cpu.pc,0x40080005);CHECK_EQ(two->cpu.pc,0x40080005);
    /* CPU0 fault stops the round before CPU1, then stays paused. */
    CHECK(esk_esp32_machine_round(m,events,&failed,&fault)!=ES_OK);CHECK_EQ(failed,0);
    CHECK_EQ(two->cpu.pc,0x40080005);
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,19);
    CHECK_EQ(esk_esp32_machine_configure(m,0,0x40080003),ES_OK);
    CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);
    CHECK(esk_esp32_machine_round(m,events,&failed,&fault)!=ES_OK);CHECK_EQ(failed,1);
    CHECK_EQ(one->cpu.pc,0x40080005);CHECK_EQ(esk_esp32_machine_retired(m,0),1);
    CHECK_EQ(bus.read(bus.ctx,0x3ffb0000,4,&value),ES_OK);CHECK_EQ(value,2);
    CHECK_EQ(esk_esp32_machine_step(m,2,&event,&fault),ES_EINVAL);
    CHECK(esk_esp32_machine_core(m,2)==NULL);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void independent_wait_and_clock(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_windows *one,*two;
    unsigned events[2],failed,event;esk_xtensa_fault fault;unsigned i;
    uint32_t general[3]={0x40081000,0x40081040,0x40081080};
    uint32_t high[6]={0x40082000,0x40082040,0x40082080,0x400820c0,0x40082100,0x40082140};
    unsigned char wait[]={0x00,0x70,0x00}; /* WAITI 0 */
    unsigned char nops[]={0x3d,0xf0,0x3d,0xf0,0x3d,0xf0};
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080000,wait,sizeof wait),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080010,nops,sizeof nops),ES_OK);
    for(i=0;i<2;i++) {
        esk_xtensa_windows *w;
        CHECK_EQ(esk_esp32_machine_configure(m,i,i ? 0x40080010u : 0x40080000u),ES_OK);
        w=esk_esp32_machine_core(m,i);esk_xtensa_exception_configure(w,general);
        CHECK_EQ(esk_xtensa_irq_configure(w,high),ES_OK);w->irq.intenable=1u<<6;
        CHECK_EQ(esk_xtensa_irq_write(w,240,10),ES_OK);
        CHECK_EQ(esk_esp32_machine_run_core(m,i,1),ES_OK);
    }
    one=esk_esp32_machine_core(m,0);two=esk_esp32_machine_core(m,1);
    { esk_xtensa_memory bus=esk_esp32_machine_bus(m,0);CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,1),ES_OK); }
    CHECK_EQ(esk_esp32_machine_round(m,events,&failed,&fault),ES_OK);
    CHECK_EQ(one->irq.waiting,1);CHECK_EQ(two->irq.waiting,0);
    CHECK_EQ(esk_esp32_machine_round(m,events,&failed,&fault),ES_OK);
    CHECK_EQ(events[0],18);CHECK_EQ(events[1],0);
    CHECK_EQ(one->cpu.pc,0x40080003);CHECK_EQ(two->cpu.pc,0x40080014);
    CHECK_EQ(esk_esp32_machine_retired(m,0),1);CHECK_EQ(esk_esp32_machine_retired(m,1),2);
    CHECK_EQ(one->irq.ccount,0);CHECK_EQ(two->irq.ccount,0);
    CHECK_EQ(esk_xtensa_clock_advance(one,10),ES_OK);
    CHECK_EQ(two->irq.ccount,0);CHECK_EQ(esk_xtensa_irq_pending(two),0);
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,11);
    CHECK_EQ(one->irq.waiting,0);CHECK_EQ(one->epc1,0x40080003);
    CHECK_EQ(one->cpu.pc,general[0]);CHECK_EQ(two->cpu.pc,0x40080014);
    CHECK_EQ(esk_esp32_machine_retired(m,0),1);
    /* Reconfigure only CPU0; peer clock, registers and RAM survive. */
    two->cpu.a[5]=123;
    CHECK_EQ(esk_esp32_machine_configure(m,0,0x40080000),ES_OK);
    CHECK_EQ(one->irq.enabled,0);CHECK_EQ(two->irq.enabled,1);CHECK_EQ(two->cpu.a[5],123);
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,19);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;
    RUN(shared_execution);RUN(independent_wait_and_clock);
TEST_MAIN_END
