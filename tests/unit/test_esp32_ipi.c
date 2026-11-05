/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/machine.h"
static const uint32_t general[3]={0x40081000,0x40081040,0x40081080};
static const uint32_t high[6]={0x40082000,0x40082040,0x40082080,0x400820c0,0x40082100,0x40082140};
static void profile(esk_esp32_machine *m,unsigned cpu,uint32_t entry)
{
    esk_xtensa_windows *w;
    CHECK_EQ(esk_esp32_machine_configure(m,cpu,entry),ES_OK);
    w=esk_esp32_machine_core(m,cpu);esk_xtensa_exception_configure(w,general);
    CHECK_EQ(esk_xtensa_irq_configure(w,high),ES_OK);
}
static void firmware_signal_and_acknowledge(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *pro,*app;
    unsigned event;esk_xtensa_fault fault;uint32_t value=0;
    unsigned char sender[]={0x22,0x63,0x00,0x3d,0xf0}; /* S32I A2,A3,0; NOP.N */
    unsigned char receiver[]={0x00,0x70,0x00,0x3d,0xf0}; /* WAITI 0; NOP.N */
    unsigned char handler[]={0x22,0x63,0x00,0x10,0x32,0x00}; /* S32I A2,A3,0; RFI 2 */
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080000,sender,sizeof sender),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080010,receiver,sizeof receiver),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),1,high[0],handler,sizeof handler),ES_OK);
    profile(m,0,0x40080000);profile(m,1,0x40080010);pro=esk_esp32_machine_core(m,0);app=esk_esp32_machine_core(m,1);
    pro->cpu.a[2]=1;pro->cpu.a[3]=0x3ff000dc;app->cpu.a[2]=0;app->cpu.a[3]=0x3ff000dc;
    app->irq.intenable=1u<<19;
    CHECK_EQ(bus.write(bus.ctx,0x3ff00278,4,19),ES_OK);
    CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);CHECK_EQ(esk_esp32_machine_run_core(m,1,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(app->irq.waiting,1);
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,0);
    CHECK_EQ(pro->cpu.pc,0x40080003);CHECK_EQ(esk_xtensa_irq_pending(pro),0);
    CHECK_EQ(esk_xtensa_irq_pending(app),1u<<19);
    CHECK_EQ(bus.read(bus.ctx,0x3ff000ec,4,&value),ES_OK);CHECK_EQ(value,1u<<24);
    CHECK_EQ(bus.read(bus.ctx,0x3ff000f8,4,&value),ES_OK);CHECK_EQ(value,1u<<24);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,12);
    CHECK_EQ(app->cpu.pc,high[0]);CHECK_EQ(app->irq.epc[2],0x40080013);CHECK_EQ(app->irq.waiting,0);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,0);
    CHECK_EQ(esk_xtensa_irq_pending(app),0);
    CHECK_EQ(bus.read(bus.ctx,0x3ff000dc,4,&value),ES_OK);CHECK_EQ(value,0);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(app->cpu.pc,0x40080013);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(app->cpu.pc,0x40080015);
    CHECK_EQ(esk_esp32_machine_retired(m,0),1);CHECK_EQ(esk_esp32_machine_retired(m,1),4);
    CHECK_EQ(bus.fetch(bus.ctx,0x3ff000dc,1,&value),ES_EUNSUPPORTED);
    CHECK_EQ(bus.compare_exchange(bus.ctx,0x3ff000dc,0,1,&value),ES_EUNSUPPORTED);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void paused_edges_and_shared_levels(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *pro,*app;unsigned event;esk_xtensa_fault fault;
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);bus=esk_esp32_machine_bus(m,0);
    profile(m,0,0x40080000);profile(m,1,0x40080000);pro=esk_esp32_machine_core(m,0);app=esk_esp32_machine_core(m,1);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00278,4,10),ES_OK);app->irq.intenable=1u<<10;
    /* Host writes between turns, while APP is debug-paused: edge survives low. */
    CHECK_EQ(bus.write(bus.ctx,0x3ff000dc,4,1),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff000dc,4,0),ES_OK);
    CHECK_EQ(app->irq.lines&(1u<<10),0);CHECK_EQ(esk_xtensa_irq_pending(app),1u<<10);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,19);
    CHECK_EQ(esk_esp32_machine_run_core(m,1,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,11);
    CHECK_EQ(esk_xtensa_irq_write(app,227,1u<<10),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(app),0);
    /* Matrix cannot drive or clear an internal compare input. */
    CHECK_EQ(esk_xtensa_irq_line(pro,6,1),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff0018c,4,19),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff00190,4,19),ES_OK);
    CHECK_EQ(esk_esp32_machine_source(m,34,1),ES_OK);CHECK_EQ(esk_esp32_machine_source(m,35,1),ES_OK);
    CHECK_EQ(esk_xtensa_irq_pending(pro),(1u<<19)|(1u<<6));
    CHECK_EQ(esk_esp32_machine_source(m,34,0),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(pro),(1u<<19)|(1u<<6));
    CHECK_EQ(esk_esp32_machine_source(m,35,0),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(pro),1u<<6);
    /* Core-specific GPIO and NMI-mask inputs do not affect the peer. */
    CHECK_EQ(bus.write(bus.ctx,0x3ff0015c,4,14),ES_OK);
    CHECK_EQ(esk_esp32_machine_nmi_mask(m,0,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_gpio_irq(m,0,0,1),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(pro),1u<<6);
    CHECK_EQ(esk_esp32_machine_nmi_mask(m,0,0),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(pro),(1u<<14)|(1u<<6));
    CHECK_EQ(esk_xtensa_irq_pending(app),0);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void self_signal_and_edge_or(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *pro;
    unsigned event;esk_xtensa_fault fault;
    unsigned char stores[]={0x22,0x63,0x00,0x22,0x63,0x00};
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080000,stores,sizeof stores),ES_OK);
    profile(m,0,0x40080000);pro=esk_esp32_machine_core(m,0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00164,4,10),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff00168,4,10),ES_OK);
    pro->cpu.a[2]=1;pro->cpu.a[3]=0x3ff000dc;
    CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);
    /* IRQ changes from our own MMIO store must survive instruction commit. */
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,0);
    CHECK_EQ(pro->cpu.pc,0x40080003);CHECK_EQ(esk_xtensa_irq_pending(pro),1u<<10);
    CHECK_EQ(esk_xtensa_irq_write(pro,227,1u<<10),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(pro),0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff000e0,4,1),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(pro),0);
    pro->cpu.a[2]=0;
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(pro->cpu.pc,0x40080006);
    CHECK_EQ(pro->irq.lines,1u<<10);CHECK_EQ(esk_xtensa_irq_pending(pro),0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff000e0,4,0),ES_OK);CHECK_EQ(pro->irq.lines,0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff000dc,4,1),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(pro),1u<<10);
    CHECK_EQ(esk_esp32_machine_retired(m,0),2);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(firmware_signal_and_acknowledge);RUN(paused_edges_and_shared_levels);RUN(self_signal_and_edge_or);
TEST_MAIN_END
