/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/machine.h"
#define GPIO UINT32_C(0x3ff44000)
#define ISO UINT32_C(0x3ff48088)
#define HOLD UINT32_C(0x3ff48474)
static const unsigned pins[]={3,1,6,7,8,9,10,11,5,16,17,18,19,21,22,23};
static const unsigned bits[]={0,1,2,3,4,5,6,7,8,9,10,11,12,14,15,16};
static void write_reg(esk_esp32_gpio *g,uint32_t a,uint32_t v)
{ CHECK_EQ(esk_esp32_gpio_write(g,a,4,v),ES_OK); }
static uint32_t read_reg(esk_esp32_gpio *g,uint32_t a)
{ uint32_t v=0;CHECK_EQ(esk_esp32_gpio_read(g,a,4,&v),ES_OK);return v; }
static int drive(esk_esp32_gpio *g,unsigned pin)
{ esk_esp32_gpio_pad p;CHECK_EQ(esk_esp32_gpio_output(g,pin,&p),ES_OK);return p.drive; }
static void all_mapped_pads_and_live_inputs(void)
{
    esk_esp32_gpio g;esk_esp32_gpio_pad pad;uint32_t cfg[14],bit,otherbit,mask;unsigned n,oe,od,high,other;
    for(n=0;n<16;n++) for(oe=0;oe<2;oe++) for(od=0;od<2;od++) for(high=0;high<2;high++) {
        bit=UINT32_C(1)<<pins[n];other=pins[n]==1 ? 3u : 1u;otherbit=UINT32_C(1)<<other;mask=UINT32_C(1)<<bits[n];
        memset(cfg,0,sizeof cfg);cfg[0]=bit|otherbit;cfg[2]=bit;cfg[4]=od ? bit : 0u;cfg[6]=od ? 0u : bit;
        cfg[8]=od ? bit : 0u;cfg[10]=(high ? bit : 0u)|otherbit;cfg[12]=(oe ? bit : 0u)|otherbit;
        esk_esp32_gpio_init(&g);CHECK_EQ(esk_esp32_gpio_configure(&g,cfg,14),ES_OK);
        write_reg(&g,ISO,g.iso&~UINT32_C(0x4000));write_reg(&g,HOLD,mask);
        CHECK_EQ(read_reg(&g,HOLD),mask);CHECK_EQ(drive(&g,pins[n]),oe && !(od && high) ? (high ? 1 : 0) : -1);
        write_reg(&g,GPIO+4,high ? 0u : bit);write_reg(&g,GPIO+0x20,(oe ? 0u : bit)|otherbit);
        write_reg(&g,GPIO+0x88u+pins[n]*4u,od ? 0u : 4u);
        CHECK_EQ(read_reg(&g,GPIO+4),high ? 0u : bit);
        CHECK_EQ(drive(&g,pins[n]),oe && !(od && high) ? (high ? 1 : 0) : -1);CHECK_EQ(drive(&g,other),0);
        CHECK_EQ(esk_esp32_gpio_output(&g,pins[n],&pad),ES_OK);CHECK_EQ(pad.input_enabled,1);
        CHECK_EQ(pad.pull_up,od);CHECK_EQ(pad.pull_down,od ? 0u : 1u);
        if(!oe) {
            CHECK_EQ(esk_esp32_gpio_input(&g,pins[n],1,0),ES_OK);CHECK_EQ(read_reg(&g,GPIO+0x3c),0);
            CHECK_EQ(esk_esp32_gpio_input(&g,pins[n],1,1),ES_OK);CHECK_EQ(read_reg(&g,GPIO+0x3c),bit);
        }
        write_reg(&g,HOLD,0);
        CHECK_EQ(drive(&g,pins[n]),!oe && !(!od && !high) ? (high ? 0 : 1) : -1);
    }
}
static void overlapping_causes_and_force_unhold(void)
{
    esk_esp32_gpio g;uint32_t cfg[14]={0};cfg[0]=10;cfg[10]=8;cfg[12]=10;
    esk_esp32_gpio_init(&g);CHECK_EQ(esk_esp32_gpio_configure(&g,cfg,14),ES_OK);
    write_reg(&g,HOLD,2);write_reg(&g,GPIO+4,10);CHECK_EQ(drive(&g,1),1); /* force-unhold reset input */
    write_reg(&g,GPIO+4,8);write_reg(&g,ISO,g.iso&~UINT32_C(0x4000));CHECK_EQ(drive(&g,1),0);
    write_reg(&g,GPIO+4,10);CHECK_EQ(drive(&g,1),0);
    write_reg(&g,ISO,g.iso|0x8000u);write_reg(&g,GPIO+4,2);CHECK_EQ(drive(&g,3),1);
    write_reg(&g,HOLD,0);CHECK_EQ(drive(&g,1),0);write_reg(&g,HOLD,2);
    write_reg(&g,ISO,g.iso&~UINT32_C(0x8000));CHECK_EQ(drive(&g,1),0);CHECK_EQ(drive(&g,3),0);
    write_reg(&g,ISO,g.iso|0x4000u);CHECK_EQ(drive(&g,1),1);CHECK_EQ(read_reg(&g,HOLD),2);
    write_reg(&g,GPIO+4,0);write_reg(&g,ISO,g.iso&~UINT32_C(0x4000));write_reg(&g,GPIO+4,2);
    CHECK_EQ(drive(&g,1),0);write_reg(&g,HOLD,0);CHECK_EQ(drive(&g,1),1);
    /* Auto-hold and local hold share the same latch, too. */
    write_reg(&g,GPIO+4,0);write_reg(&g,HOLD,2);write_reg(&g,ISO,g.iso|0x800u);
    CHECK_EQ(esk_esp32_gpio_auto_hold_event(&g),ES_OK);write_reg(&g,GPIO+4,10);
    write_reg(&g,HOLD,0);CHECK_EQ(drive(&g,1),0);write_reg(&g,HOLD,2);
    write_reg(&g,ISO,g.iso|0x400u);CHECK_EQ(drive(&g,1),0);CHECK_EQ(drive(&g,3),1);
    CHECK_EQ(read_reg(&g,ISO)&0x200u,0);write_reg(&g,HOLD,0);CHECK_EQ(drive(&g,1),1);
}
static void unsupported_bits_and_unresolved_inputs(void)
{
    esk_esp32_gpio g,before;uint32_t cfg[14]={0},v=0xdeadbeef;esk_esp32_gpio_pad pad;unsigned b;
    cfg[0]=2;cfg[2]=2;esk_esp32_gpio_init(&g);CHECK_EQ(esk_esp32_gpio_configure(&g,cfg,14),ES_OK);
    write_reg(&g,ISO,g.iso&~UINT32_C(0x4000));before=g;
    for(b=0;b<32;b++) if(b==13 || b>16) {
        CHECK_EQ(esk_esp32_gpio_write(&g,HOLD,4,2u|(UINT32_C(1)<<b)),ES_EUNSUPPORTED);
        CHECK_EQ(memcmp(&g,&before,sizeof g),0);
    }
    CHECK_EQ(esk_esp32_gpio_write(&g,HOLD,1,2),ES_EINVAL);CHECK_EQ(esk_esp32_gpio_write(&g,HOLD+1u,4,2),ES_EINVAL);
    CHECK_EQ(memcmp(&g,&before,sizeof g),0);write_reg(&g,HOLD,1); /* unmapped route, known pad control */
    CHECK_EQ(esk_esp32_gpio_output(&g,3,&pad),ES_EUNSUPPORTED);
    write_reg(&g,HOLD,2);CHECK_EQ(esk_esp32_gpio_read(&g,GPIO+0x3c,4,&v),ES_EUNSUPPORTED);CHECK_EQ(v,0xdeadbeefu);
    CHECK_EQ(esk_esp32_gpio_input(&g,1,1,1),ES_OK);CHECK_EQ(read_reg(&g,GPIO+0x3c),2);
    CHECK_EQ(esk_esp32_gpio_input(&g,1,0,0),ES_OK);v=0xdeadbeef;
    CHECK_EQ(esk_esp32_gpio_read(&g,GPIO+0x3c,4,&v),ES_EUNSUPPORTED);CHECK_EQ(v,0xdeadbeefu);
}
static void fetched_hold_shared_buses_and_reset(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus[2];esk_xtensa_windows *w;uint32_t cfg[14]={0},v;
    const unsigned char code[]={0x32,0x62,0};esk_esp32_gpio_pad pad;unsigned event;esk_xtensa_fault fault;
    cfg[0]=2;cfg[12]=2;esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);
    CHECK_EQ(esk_esp32_machine_gpio_configure(m,cfg,14),ES_OK);bus[0]=esk_esp32_machine_bus(m,0);bus[1]=esk_esp32_machine_bus(m,1);
    CHECK_EQ(bus[0].write(bus[0].ctx,ISO,4,0xaaaa1000),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080000,code,sizeof code),ES_OK);
    CHECK_EQ(esk_esp32_machine_configure(m,0,0x40080000),ES_OK);w=esk_esp32_machine_core(m,0);
    w->cpu.a[2]=HOLD;w->cpu.a[3]=2;CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,0);
    CHECK_EQ(bus[1].read(bus[1].ctx,HOLD,4,&v),ES_OK);CHECK_EQ(v,2);
    CHECK_EQ(bus[1].write(bus[1].ctx,GPIO+4,4,2),ES_OK);CHECK_EQ(esk_esp32_machine_gpio_output(m,1,&pad),ES_OK);CHECK_EQ(pad.drive,0);
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_gpio_output(m,1,&pad),ES_OK);CHECK_EQ(pad.drive,0);
    CHECK_EQ(bus[0].fetch(bus[0].ctx,HOLD,3,&v),ES_EUNSUPPORTED);
    CHECK_EQ(bus[0].compare_exchange(bus[0].ctx,HOLD,0,0,&v),ES_EUNSUPPORTED);
    CHECK_EQ(bus[1].write(bus[1].ctx,HOLD,4,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_gpio_output(m,1,&pad),ES_OK);CHECK_EQ(pad.drive,1);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(all_mapped_pads_and_live_inputs);RUN(overlapping_causes_and_force_unhold);
    RUN(unsupported_bits_and_unresolved_inputs);RUN(fetched_hold_shared_buses_and_reset);
TEST_MAIN_END
