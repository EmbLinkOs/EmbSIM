/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/gpio.h"
#include "mcu/esp32/machine.h"
#define GPIO_BASE UINT32_C(0x3ff44000)
#define DIG_ISO UINT32_C(0x3ff48088)
static void fixture_config(uint32_t config[14])
{
    memset(config,0,14*sizeof *config);config[0]=0x0effffff;config[1]=255;config[2]=config[0];config[3]=255;
}
static void bank_latches_and_projection(void)
{
    esk_esp32_gpio g;esk_esp32_gpio_pad pad;uint32_t config[14],value;unsigned pin,oe,high,od,bank;
    fixture_config(config);esk_esp32_gpio_init(&g);CHECK_EQ(esk_esp32_gpio_configure(&g,config,14),ES_OK);
    for(pin=0;pin<40;pin++) {
        uint32_t bit=UINT32_C(1)<<(pin%32);bank=pin/32;
        if(!(config[bank]&bit)) continue;
        for(oe=0;oe<2;oe++) for(high=0;high<2;high++) for(od=0;od<2;od++) {
            CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+(bank ? 0x10u : 4u),4,high ? bit : 0u),ES_OK);
            CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+(bank ? 0x2cu : 0x20u),4,oe ? bit : 0u),ES_OK);
            CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+0x88u+pin*4u,4,od<<2),ES_OK);
            CHECK_EQ(esk_esp32_gpio_output(&g,pin,&pad),ES_OK);
            CHECK_EQ(pad.drive,oe && pin<34 && !(od && high) ? (high ? 1 : 0) : -1);
            CHECK_EQ(esk_esp32_gpio_read(&g,GPIO_BASE+(bank ? 0x10u : 4u),4,&value),ES_OK);CHECK_EQ(value,high ? bit : 0u);
            CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+(bank ? 0x14u : 8u),4,bit),ES_OK);
            CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+(bank ? 0x18u : 0xcu),4,bit),ES_OK);
            CHECK_EQ(esk_esp32_gpio_read(&g,GPIO_BASE+(bank ? 0x10u : 4u),4,&value),ES_OK);CHECK_EQ(value,0);
        }
    }
    CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+0x10,4,UINT32_MAX),ES_OK);
    CHECK_EQ(esk_esp32_gpio_read(&g,GPIO_BASE+0x10,4,&value),ES_OK);CHECK_EQ(value,255);
}
static void resolved_inputs_and_unchanged_failures(void)
{
    esk_esp32_gpio g,before;esk_esp32_gpio_pad pad;uint32_t config[14]={0},value=0xdeadbeef;
    config[0]=(1u<<2)|(1u<<4);config[2]=config[0];config[4]=1u<<2;config[6]=1u<<4;
    esk_esp32_gpio_init(&g);CHECK_EQ(esk_esp32_gpio_configure(&g,config,14),ES_OK);
    CHECK_EQ(esk_esp32_gpio_read(&g,GPIO_BASE+0x3c,4,&value),ES_EUNSUPPORTED);CHECK_EQ(value,0xdeadbeefu);
    CHECK_EQ(esk_esp32_gpio_input(&g,2,1,1),ES_OK);CHECK_EQ(esk_esp32_gpio_input(&g,4,1,0),ES_OK);
    CHECK_EQ(esk_esp32_gpio_read(&g,GPIO_BASE+0x3c,4,&value),ES_OK);CHECK_EQ(value,1u<<2);
    CHECK_EQ(esk_esp32_gpio_output(&g,2,&pad),ES_OK);CHECK_EQ(pad.pull_up,1);CHECK_EQ(pad.drive,-1);
    CHECK_EQ(esk_esp32_gpio_input(&g,2,0,0),ES_OK);before=g;
    CHECK_EQ(esk_esp32_gpio_write(&g,DIG_ISO,4,g.iso^(1u<<18)),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&g,&before,sizeof g),0);
    CHECK_EQ(esk_esp32_gpio_write(&g,DIG_ISO,4,g.iso|0xc000u),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&g,&before,sizeof g),0);
    CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+0x90,4,0x80),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&g,&before,sizeof g),0);
    CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+4,1,1),ES_EINVAL);CHECK_EQ(memcmp(&g,&before,sizeof g),0);
    CHECK_EQ(esk_esp32_gpio_read(&g,GPIO_BASE+8,4,&value),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_gpio_output(&g,0,&pad),ES_EUNSUPPORTED);
    config[1]=1u<<2;config[5]=1u<<2;CHECK_EQ(esk_esp32_gpio_configure(&g,config,14),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&g,&before,sizeof g),0);
}
static void hold_release_and_auto_latch(void)
{
    esk_esp32_gpio g;esk_esp32_gpio_pad pad;uint32_t config[14]={0},value;unsigned cycle;
    config[0]=1u<<2;esk_esp32_gpio_init(&g);CHECK_EQ(esk_esp32_gpio_configure(&g,config,14),ES_OK);
    CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+0x20,4,1u<<2),ES_OK);
    for(cycle=0;cycle<16;cycle++) {
        CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+4,4,0),ES_OK);
        CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+0x90,4,0),ES_OK);
        CHECK_EQ(esk_esp32_gpio_write(&g,DIG_ISO,4,(g.iso&~UINT32_C(0x4000))|0x8000u),ES_OK);
        CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+4,4,1u<<2),ES_OK);
        CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+0x90,4,4),ES_OK);
        CHECK_EQ(esk_esp32_gpio_output(&g,2,&pad),ES_OK);CHECK_EQ(pad.drive,0);
        CHECK_EQ(esk_esp32_gpio_write(&g,DIG_ISO,4,(g.iso&~UINT32_C(0x8000))|0x4000u),ES_OK);
        CHECK_EQ(esk_esp32_gpio_output(&g,2,&pad),ES_OK);CHECK_EQ(pad.drive,-1); /* high open drain */
    }
    CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+0x90,4,0),ES_OK);
    CHECK_EQ(esk_esp32_gpio_write(&g,DIG_ISO,4,(g.iso&~UINT32_C(0x4000))|0x800u),ES_OK);
    CHECK_EQ(esk_esp32_gpio_auto_hold_event(&g),ES_OK);
    { esk_esp32_gpio before=g;
      CHECK_EQ(esk_esp32_gpio_write(&g,DIG_ISO,4,(g.iso^UINT32_C(0x40000))|0x400u),ES_EUNSUPPORTED);
      CHECK_EQ(memcmp(&g,&before,sizeof g),0); }
    CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+4,4,0),ES_OK);
    CHECK_EQ(esk_esp32_gpio_output(&g,2,&pad),ES_OK);CHECK_EQ(pad.drive,1);
    CHECK_EQ(esk_esp32_gpio_read(&g,DIG_ISO,4,&value),ES_OK);CHECK(value&0x200u);CHECK(!(value&0x400u));
    CHECK_EQ(esk_esp32_gpio_write(&g,DIG_ISO,4,value|0x400u),ES_OK);
    CHECK_EQ(esk_esp32_gpio_output(&g,2,&pad),ES_OK);CHECK_EQ(pad.drive,0);
    CHECK_EQ(esk_esp32_gpio_read(&g,DIG_ISO,4,&value),ES_OK);CHECK(!(value&0x600u));
}
static void fetched_gpio_and_cpu_reset(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *w;esk_esp32_gpio_pad pad;
    uint32_t config[14]={0};unsigned event;esk_xtensa_fault fault;const unsigned char code[]={0x32,0x62,0};
    config[0]=1u<<2;esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);
    CHECK_EQ(esk_esp32_machine_gpio_configure(m,config,14),ES_OK);bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(bus.write(bus.ctx,GPIO_BASE+0x20,4,1u<<2),ES_OK);
    CHECK_EQ(esk_esp32_machine_configure(m,0,0x40080000),ES_OK);w=esk_esp32_machine_core(m,0);w->cpu.a[2]=GPIO_BASE+4;w->cpu.a[3]=1u<<2;
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080000,code,sizeof code),ES_OK);
    CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);
    CHECK_EQ(event,0);CHECK_EQ(esk_esp32_machine_gpio_output(m,2,&pad),ES_OK);CHECK_EQ(pad.drive,1);
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_gpio_output(m,2,&pad),ES_OK);CHECK_EQ(pad.drive,1);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void explicit_snapshots_and_fixed_route(void)
{
    esk_esp32_gpio g,before;esk_esp32_gpio_pad pad;uint32_t config[14]={0},value=0;
    config[0]=1u<<2;config[10]=1u<<2;config[12]=1u<<2;
    esk_esp32_gpio_init(&g);CHECK_EQ(esk_esp32_gpio_configure(&g,config,14),ES_OK);config[10]=0;
    CHECK_EQ(esk_esp32_gpio_output(&g,2,&pad),ES_OK);CHECK_EQ(pad.drive,1);
    CHECK_EQ(esk_esp32_gpio_read(&g,GPIO_BASE+0x3c,4,&value),ES_OK);CHECK_EQ(value,0); /* input buffer disabled */
    CHECK_EQ(esk_esp32_gpio_read(&g,GPIO_BASE+0x538,4,&value),ES_OK);CHECK_EQ(value,256);
    CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+0x538,4,256),ES_OK);before=g;
    CHECK_EQ(esk_esp32_gpio_write(&g,GPIO_BASE+0x538,4,257),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&g,&before,sizeof g),0);
    config[11]=256;CHECK_EQ(esk_esp32_gpio_configure(&g,config,14),ES_EINVAL);CHECK_EQ(memcmp(&g,&before,sizeof g),0);
    CHECK_EQ(esk_esp32_gpio_configure(&g,config,13),ES_EINVAL);CHECK_EQ(memcmp(&g,&before,sizeof g),0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(bank_latches_and_projection);RUN(resolved_inputs_and_unchanged_failures);RUN(hold_release_and_auto_latch);RUN(fetched_gpio_and_cpu_reset);RUN(explicit_snapshots_and_fixed_route);
TEST_MAIN_END
