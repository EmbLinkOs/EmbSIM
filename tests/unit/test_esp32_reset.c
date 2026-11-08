/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/machine.h"
static void hold_release_and_no_boot_shortcut(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *app,*pro,before;
    unsigned event;esk_xtensa_fault fault;uint32_t v;uint64_t ticks=99;
    unsigned char nop[]={0x3d,0xf0};
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);bus=esk_esp32_machine_bus(m,0);
    app=esk_esp32_machine_core(m,1);pro=esk_esp32_machine_core(m,0);before=*app;
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,2),ES_EINVAL);CHECK_EQ(memcmp(app,&before,sizeof before),0);
    CHECK_EQ(bus.read(bus.ctx,0x3ff0002c,4,&v),ES_EUNSUPPORTED);
    pro->cpu.a[5]=99;before=*pro;app->cpu.a[6]=0x1234;app->irq.ccount=987;app->lcount=9;
    CHECK_EQ(bus.write(bus.ctx,0x3ff00038,4,0x40080000),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00034,4,1),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ffb0000,4,0xdeadbeef),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),1,0x40080000,nop,sizeof nop),ES_OK);
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);
    CHECK_EQ(app->cpu.pc,0x40000400);CHECK_EQ(app->ps,0x1f);CHECK_EQ(app->lcount,0);CHECK_EQ(app->irq.ccount,987);
    CHECK_EQ(app->cpu.a[6],0x1234);CHECK_EQ(memcmp(pro,&before,sizeof before),0);
    before=*app;CHECK_EQ(esk_esp32_machine_configure(m,1,0x40080000),ES_EUNSUPPORTED);
    CHECK_EQ(memcmp(app,&before,sizeof before),0);
    CHECK_EQ(bus.read(bus.ctx,0x3ff0002c,4,&v),ES_OK);CHECK_EQ(v,1);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,19);
    CHECK_EQ(esk_esp32_machine_run_core(m,1,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,22);
    CHECK_EQ(esk_esp32_machine_clock_advance(m,1,100),ES_OK);CHECK_EQ(app->irq.ccount,987);
    CHECK_EQ(esk_esp32_machine_clock_deadline(m,1,&ticks),0);CHECK_EQ(ticks,0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff0002c,4,2),ES_OK); /* low bit releases */
    CHECK_EQ(bus.read(bus.ctx,0x3ff0002c,4,&v),ES_OK);CHECK_EQ(v,0);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,20);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,21);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00034,4,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_EINVAL);CHECK_EQ(fault.kind,ESK_XT_FETCH);
    CHECK_EQ(fault.address,0x40000400);CHECK_EQ(app->cpu.pc,0x40000400);CHECK_EQ(esk_esp32_machine_retired(m,1),0);
    CHECK_EQ(bus.read(bus.ctx,0x3ff00038,4,&v),ES_OK);CHECK_EQ(v,0x40080000);
    CHECK_EQ(bus.read(bus.ctx,0x3ffb0000,4,&v),ES_OK);CHECK_EQ(v,0xdeadbeefu);
    CHECK_EQ(bus.write(bus.ctx,0x3ff0002c,1,1),ES_EINVAL);
    CHECK_EQ(bus.write(bus.ctx,0x3ff0002d,4,1),ES_EINVAL);
    CHECK_EQ(bus.write(bus.ctx,0x3ff0002c,4,UINT32_MAX),ES_OK);
    CHECK_EQ(bus.read(bus.ctx,0x3ff0002c,4,&v),ES_OK);CHECK_EQ(v,1);
    CHECK_EQ(bus.write(bus.ctx,0x3ff0002c,4,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,19); /* fault pause survives reset */
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void self_reset_and_live_source(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *app;
    unsigned char *rom=(unsigned char *)calloc(0x60000,1);unsigned event;esk_xtensa_fault fault;uint32_t v;
    CHECK(rom!=NULL);if(!rom) return;
    /* Original fixture ROM, not Espressif ROM: NOP.N; S32I A2,A3,0. */
    rom[0x400]=0x3d;rom[0x401]=0xf0;rom[0x402]=0x22;rom[0x403]=0x63;
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(esk_esp32_fabric_rom(esk_esp32_machine_fabric(m),0,rom,0x60000),ES_OK);free(rom);
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);app=esk_esp32_machine_core(m,1);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,1),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff0002c,4,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_run_core(m,1,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(app->cpu.pc,0x40000402);
    app->cpu.a[2]=1;app->cpu.a[3]=0x3ff0002c;
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,0);
    CHECK_EQ(app->cpu.pc,0x40000400);CHECK_EQ(app->ps,0x1f);CHECK_EQ(esk_esp32_machine_retired(m,1),2);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,22);
    /* CPU reset cannot acknowledge or reset the peripheral/matrix source. */
    CHECK_EQ(bus.write(bus.ctx,0x3ff002a0,4,19),ES_OK);
    CHECK_EQ(esk_esp32_machine_source(m,34,1),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(app),0);
    CHECK_EQ(bus.read(bus.ctx,0x3ff000fc,4,&v),ES_OK);CHECK_EQ(v,4);
    CHECK_EQ(bus.write(bus.ctx,0x3ff0002c,4,0),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(app),1u<<19);
    app->irq.intenable=1u<<19;
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,0); /* PS reset mask blocks level 2 */
    app->ps=ESK_XT_PS_WOE;
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(event,12);CHECK_EQ(app->cpu.pc,0x40000180);
    CHECK_EQ(app->irq.epc[2],0x40000402);CHECK_EQ(esk_esp32_machine_retired(m,1),3);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void alternate_static_vector_and_remap(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *app;
    unsigned event;esk_xtensa_fault fault;unsigned char nop[]={0x3d,0xf0};uint32_t v;
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),1,0x50000000,nop,sizeof nop),ES_OK);
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,0),ES_OK);app=esk_esp32_machine_core(m,1);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,1),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff0002c,4,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_run_core(m,1,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(app->cpu.pc,0x50000002);
    CHECK_EQ(app->vecbase,0x40000000); /* static selector does not relocate dynamic vectors */
    CHECK_EQ(esk_esp32_fabric_remap(esk_esp32_machine_fabric(m),1,1),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),1,0x40000400,nop,sizeof nop),ES_OK);
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff0002c,4,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,1,&event,&fault),ES_OK);CHECK_EQ(app->cpu.pc,0x40000402);
    CHECK_EQ(esk_esp32_machine_retired(m,1),2);
    CHECK_EQ(bus.fetch(bus.ctx,0x40000400,1,&v),ES_EUNSUPPORTED); /* peer remap did not change PRO view */
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(hold_release_and_no_boot_shortcut);RUN(self_reset_and_live_source);RUN(alternate_static_vector_and_remap);
TEST_MAIN_END
