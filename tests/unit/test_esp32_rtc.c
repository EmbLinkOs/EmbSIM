/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/machine.h"
static uint32_t store_address(unsigned i) { return i<4 ? 0x3ff4804cu+i*4u : 0x3ff480b0u+(i-4u)*4u; }
static void reset_snapshot_and_fields(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;uint32_t stores[8],value=0;unsigned pro,app,flags,i;
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(bus.read(bus.ctx,0x3ff48034,4,&value),ES_EUNSUPPORTED);
    for(i=0;i<8;i++) stores[i]=0xa5000000u+i;
    for(pro=0;pro<64;pro++) for(app=0;app<64;app++) {
        uint32_t causes=pro|(app<<6);
        CHECK_EQ(esk_esp32_machine_rtc_configure(m,causes|0x3000u,stores,8),ES_OK);
        for(flags=0;flags<4;flags++) {
            CHECK_EQ(bus.write(bus.ctx,0x3ff48034,4,0xffffcfffu|(flags<<12)),ES_OK);
            CHECK_EQ(bus.read(bus.ctx,0x3ff48034,4,&value),ES_OK);CHECK_EQ(value,causes|(flags<<12));
        }
    }
    CHECK_EQ(esk_esp32_machine_rtc_configure(m,0x3041,stores,8),ES_OK);
    CHECK_EQ(esk_esp32_machine_rtc_configure(m,0x4000,stores,8),ES_EINVAL);
    CHECK_EQ(bus.read(bus.ctx,0x3ff48034,4,&value),ES_OK);CHECK_EQ(value,0x3041);
    CHECK_EQ(esk_esp32_machine_rtc_configure(m,0,NULL,8),ES_EINVAL);
    CHECK_EQ(esk_esp32_machine_rtc_configure(m,0,stores,7),ES_EINVAL);
    for(i=0;i<8;i++) {
        CHECK_EQ(bus.read(bus.ctx,store_address(i),4,&value),ES_OK);CHECK_EQ(value,stores[i]);
        CHECK_EQ(bus.write(bus.ctx,store_address(i),4,~stores[i]),ES_OK);
        CHECK_EQ(bus.read(bus.ctx,store_address(i),4,&value),ES_OK);CHECK_EQ(value,~stores[i]);
        CHECK_EQ(bus.read(bus.ctx,store_address(i),1,&value),ES_EINVAL);
        CHECK_EQ(bus.write(bus.ctx,store_address(i)+1,4,0),ES_EINVAL);
    }
    value=0xdeadbeef;CHECK_EQ(bus.read(bus.ctx,0x3ff48038,4,&value),ES_EUNSUPPORTED);CHECK_EQ(value,0xdeadbeef);
    for(i=0;i<0x200;i+=4) {
        uint32_t address=0x3ff48000u+i;
        if(i==0x34 || (i>=0x4c && i<=0x58) || (i>=0xb0 && i<=0xbc)) continue;
        value=0xdeadbeef;CHECK_EQ(bus.read(bus.ctx,address,4,&value),ES_EUNSUPPORTED);CHECK_EQ(value,0xdeadbeefu);
        CHECK_EQ(bus.write(bus.ctx,address,4,0),ES_EUNSUPPORTED);
    }
    CHECK_EQ(bus.fetch(bus.ctx,0x3ff48034,3,&value),ES_EUNSUPPORTED);
    CHECK_EQ(bus.compare_exchange(bus.ctx,0x3ff48034,0,0,&value),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_machine_rtc_configure(m,0,NULL,0),ES_OK);
    CHECK_EQ(bus.read(bus.ctx,store_address(0),4,&value),ES_EUNSUPPORTED);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void cause_vector_and_retention_on_app_reset(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus[2];esk_xtensa_windows *app,*pro;uint32_t stores[8],value;unsigned flags,i;
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);
    for(i=0;i<8;i++) stores[i]=0x12340000u+i;
    CHECK_EQ(esk_esp32_machine_rtc_configure(m,0x3041,stores,8),ES_OK);stores[0]=0;
    bus[0]=esk_esp32_machine_bus(m,0);bus[1]=esk_esp32_machine_bus(m,1);
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);app=esk_esp32_machine_core(m,1);pro=esk_esp32_machine_core(m,0);
    pro->cpu.a[3]=0x98765432;
    for(flags=0;flags<4;flags++) {
        CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff48034,4,flags<<12),ES_OK);
        CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff0002c,4,0),ES_OK);
        CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff0002c,4,1),ES_OK);
        CHECK_EQ(app->cpu.pc,(flags&1u) ? 0x40000400u : 0x50000000u);
        CHECK_EQ(bus[1].read(bus[1].ctx,0x3ff48034,4,&value),ES_OK);CHECK_EQ(value,(flags<<12)|(14u<<6)|1u);
        CHECK_EQ(pro->cpu.a[3],0x98765432);
        for(i=0;i<8;i++) { CHECK_EQ(bus[1].read(bus[1].ctx,store_address(i),4,&value),ES_OK);CHECK_EQ(value,0x12340000u+i); }
    }
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void fetched_reset_commit(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *w;uint32_t stores[8]={0},value=0;
    const unsigned char code[]={0x32,0x62,0};unsigned event;esk_xtensa_fault fault;
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);
    CHECK_EQ(esk_esp32_machine_rtc_configure(m,0x3041,stores,8),ES_OK);
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff0002c,4,0),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff00030,4,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_configure(m,0,0x40080000),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080000,code,sizeof code),ES_OK);
    w=esk_esp32_machine_core(m,0);w->cpu.a[2]=0x3ff0002c;w->cpu.a[3]=1;
    CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,0);CHECK_EQ(w->cpu.pc,0x40080003);
    CHECK_EQ(bus.read(bus.ctx,0x3ff48034,4,&value),ES_OK);CHECK_EQ((value>>6)&63u,14);
    CHECK_EQ(esk_esp32_machine_retired(m,0),1);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(reset_snapshot_and_fields);RUN(cause_vector_and_retention_on_app_reset);RUN(fetched_reset_commit);
TEST_MAIN_END
