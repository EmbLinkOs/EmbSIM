/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/machine.h"
#define CFG UINT32_C(0x3ff5f068)
#define VALUE UINT32_C(0x3ff5f06c)
#define START UINT32_C(0x80000000)
static uint32_t read_reg(esk_esp32_rtc_cal *c,uint32_t address)
{ uint32_t v=0;CHECK_EQ(esk_esp32_rtc_cal_read(c,address,4,&v),ES_OK);return v; }
static void one_shot_sources_and_boundaries(void)
{
    const unsigned sizes[]={1,2,3,8,255,1024,32767};esk_esp32_rtc_cal c,before;
    unsigned source,n,p;uint32_t expected,control,ticks;
    esk_esp32_rtc_cal_init(&c);
    for(source=0;source<3;source++) for(n=0;n<sizeof sizes/sizeof sizes[0];n++) {
        control=(sizes[n]<<16)|(source<<13);
        CHECK_EQ(esk_esp32_rtc_cal_configure(&c,1,control,0x1234),ES_OK);
        CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,control|START),ES_OK);expected=0;
        for(p=0;p<sizes[n];p++) {
            before=c;CHECK_EQ(esk_esp32_rtc_cal_period(&c,(source+1u)%3u,99),ES_OK);CHECK_EQ(memcmp(&c,&before,sizeof c),0);
            ticks=1u+(p%1023u);expected+=ticks;
            CHECK_EQ(esk_esp32_rtc_cal_period(&c,source,ticks),ES_OK);
            if(p+1u<sizes[n]) {
                CHECK_EQ(read_reg(&c,CFG)&0x8000u,0);CHECK_EQ(read_reg(&c,VALUE),0x1234u<<7);
                before=c;CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,control|START|0x8000u|255u),ES_OK);
                CHECK_EQ(memcmp(&c,&before,sizeof c),0);
            }
        }
        CHECK_EQ(read_reg(&c,VALUE),expected<<7);CHECK_EQ(read_reg(&c,CFG),control|START|0x8000u);
        before=c;CHECK_EQ(esk_esp32_rtc_cal_period(&c,source,100),ES_OK);CHECK_EQ(memcmp(&c,&before,sizeof c),0);
        CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,control),ES_OK);CHECK_EQ(read_reg(&c,CFG)&0x8000u,0x8000u);
        CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,control|START),ES_OK);CHECK_EQ(read_reg(&c,CFG)&0x8000u,0);
    }
}
static void cycling_restart_stop_and_readonly(void)
{
    esk_esp32_rtc_cal c,before;unsigned round;uint32_t control=(1u<<16)|(1u<<13)|0x1000u,last=123;
    esk_esp32_rtc_cal_init(&c);CHECK_EQ(esk_esp32_rtc_cal_configure(&c,1,control,last),ES_OK);
    for(round=0;round<16;round++) {
        CHECK_EQ(esk_esp32_rtc_cal_period(&c,1,100u+round),ES_OK);
        CHECK_EQ(read_reg(&c,VALUE),(100u+round)<<7);CHECK_EQ(read_reg(&c,CFG)&0x8000u,0);
        last=100u+round;before=c;
        CHECK_EQ(esk_esp32_rtc_cal_write(&c,VALUE,4,UINT32_MAX),ES_OK);CHECK_EQ(memcmp(&c,&before,sizeof c),0);
    }
    CHECK_EQ(esk_esp32_rtc_cal_period(&c,0,100),ES_OK);
    control=(1u<<16)|(2u<<13)|0x1000u;CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,control),ES_OK);
    CHECK_EQ(c.periods,0);CHECK_EQ(c.ticks,0);CHECK_EQ(read_reg(&c,VALUE),last<<7);
    CHECK_EQ(esk_esp32_rtc_cal_period(&c,2,80),ES_OK);CHECK_EQ(esk_esp32_rtc_cal_period(&c,2,81),ES_OK);
    CHECK_EQ(read_reg(&c,VALUE),81u<<7);
    CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,control&~UINT32_C(0x1000)),ES_OK);before=c;
    CHECK_EQ(esk_esp32_rtc_cal_period(&c,2,1000),ES_OK);CHECK_EQ(memcmp(&c,&before,sizeof c),0);
}
static void overflow_invalid_requests_and_abort(void)
{
    esk_esp32_rtc_cal c,before;uint32_t v=0xdeadbeef;
    esk_esp32_rtc_cal_init(&c);CHECK_EQ(esk_esp32_rtc_cal_configure(&c,1,START|(2u<<16),7),ES_OK);
    CHECK_EQ(esk_esp32_rtc_cal_period(&c,0,0x1ffffff),ES_OK);before=c;
    CHECK_EQ(esk_esp32_rtc_cal_period(&c,0,1),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&c,&before,sizeof c),0);
    CHECK_EQ(esk_esp32_rtc_cal_period(&c,0,0),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&c,&before,sizeof c),0);
    CHECK_EQ(esk_esp32_rtc_cal_configure(&c,1,START|(1u<<16),7),ES_OK);
    CHECK_EQ(esk_esp32_rtc_cal_period(&c,0,0x1ffffff),ES_OK);CHECK_EQ(read_reg(&c,VALUE),UINT32_C(0xffffff80));
    CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,2u<<16),ES_OK);
    CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,START|(2u<<16)),ES_OK);
    CHECK_EQ(esk_esp32_rtc_cal_period(&c,0,5),ES_OK);before=c;
    CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,START|(3u<<16)),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,START|(2u<<16)|(1u<<13)),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,START|(2u<<16)|0x1000u),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,START),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,(2u<<16)|0x1000u),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,3u<<13),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_rtc_cal_configure(&c,1,0x13000,0x2000000),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_rtc_cal_configure(&c,1,0x1b000,0),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_rtc_cal_period(&c,3,0),ES_EINVAL);
    CHECK_EQ(esk_esp32_rtc_cal_read(&c,CFG,1,&v),ES_EINVAL);CHECK_EQ(v,0xdeadbeefu);
    CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG+1u,4,0),ES_EINVAL);
    CHECK_EQ(esk_esp32_rtc_cal_read(&c,0x3ff60068,4,&v),ES_EUNSUPPORTED);
    CHECK_EQ(memcmp(&c,&before,sizeof c),0);
    CHECK_EQ(esk_esp32_rtc_cal_write(&c,CFG,4,0),ES_OK);CHECK_EQ(c.active,0);CHECK_EQ(c.ticks,0);
    CHECK_EQ(read_reg(&c,VALUE),UINT32_C(0xffffff80));
    CHECK_EQ(esk_esp32_rtc_cal_configure(&c,0,0,0),ES_OK);
    CHECK_EQ(esk_esp32_rtc_cal_read(&c,CFG,4,&v),ES_EUNSUPPORTED);
}
static void native_bus_and_cpu_reset(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory b[2];unsigned p;uint32_t v;
    const unsigned char code[]={0x32,0x62,0};esk_xtensa_windows *w;esk_xtensa_fault fault;unsigned event;
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);
    for(p=0;p<2;p++) b[p]=esk_esp32_machine_bus(m,p);
    CHECK_EQ(esk_esp32_machine_rtc_cal_configure(m,1,2u<<16,0),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080000,code,sizeof code),ES_OK);
    CHECK_EQ(esk_esp32_machine_configure(m,0,0x40080000),ES_OK);w=esk_esp32_machine_core(m,0);
    w->cpu.a[2]=CFG;w->cpu.a[3]=START|(2u<<16);CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,0);
    CHECK_EQ(esk_esp32_machine_rtc_cal_period(m,0,250),ES_OK);
    CHECK_EQ(b[1].read(b[1].ctx,CFG,4,&v),ES_OK);CHECK_EQ(v&0x8000u,0);
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_rtc_cal_period(m,0,251),ES_OK);
    CHECK_EQ(b[1].read(b[1].ctx,VALUE,4,&v),ES_OK);CHECK_EQ(v,501u<<7);
    CHECK_EQ(b[0].read(b[0].ctx,CFG,4,&v),ES_OK);CHECK_EQ(v&0x8000u,0x8000u);
    CHECK_EQ(b[0].fetch(b[0].ctx,CFG,3,&v),ES_EUNSUPPORTED);
    CHECK_EQ(b[0].compare_exchange(b[0].ctx,CFG,0,0,&v),ES_EUNSUPPORTED);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(one_shot_sources_and_boundaries);RUN(cycling_restart_stop_and_readonly);
    RUN(overflow_invalid_requests_and_abort);RUN(native_bus_and_cpu_reset);
TEST_MAIN_END
