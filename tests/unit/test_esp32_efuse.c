/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/efuse.h"
#include "mcu/esp32/machine.h"
static uint32_t data_reg(unsigned i) { return 0x3ff5a000u+(i<7 ? i*4u : 0x38u+(i-7u)*4u); }
static uint32_t stage_reg(unsigned i) { return 0x3ff5a000u+(i<7 ? 0x1cu+i*4u : 0x98u+(i-7u)*4u); }
static void supplied_images_and_protection(void)
{
    esk_esp32_efuse f,before;uint32_t image[31],value=0;unsigned i,mask,scheme;int irq=77;
    esk_esp32_efuse_init(&f);CHECK_EQ(esk_esp32_efuse_irq(&f,&irq),ES_EUNSUPPORTED);CHECK_EQ(irq,77);
    CHECK_EQ(esk_esp32_efuse_read(&f,data_reg(0),4,&value),ES_EUNSUPPORTED);
    for(mask=0;mask<16;mask++) for(scheme=0;scheme<4;scheme+=3) {
        for(i=0;i<31;i++) image[i]=0xf1230000u+i;
        image[0]=(image[0]&~UINT32_C(0xf0000))|(mask<<16);image[6]=0xfffffffcu|scheme;
        CHECK_EQ(esk_esp32_efuse_configure(&f,image,31),ES_OK);
        for(i=0;i<31;i++) {
            uint32_t wanted=image[i];
            if(i>=7 && (mask&(1u<<((i-7u)/8u)))) wanted=0;
            if((mask&8u) && i==5) wanted&=0xfffffff;
            if((mask&8u) && i==6) wanted&=~UINT32_C(0x403);
            CHECK_EQ(esk_esp32_efuse_read(&f,data_reg(i),4,&value),ES_OK);CHECK_EQ(value,wanted);
            CHECK_EQ(esk_esp32_efuse_write(&f,data_reg(i),4,0),ES_OK);
            CHECK_EQ(esk_esp32_efuse_read(&f,data_reg(i),4,&value),ES_OK);CHECK_EQ(value,wanted);
            CHECK_EQ(esk_esp32_efuse_write(&f,stage_reg(i),4,~image[i]),ES_OK);
            CHECK_EQ(esk_esp32_efuse_read(&f,stage_reg(i),4,&value),ES_OK);CHECK_EQ(value,~image[i]);
        }
        before=f;image[6]=1;CHECK_EQ(esk_esp32_efuse_configure(&f,image,31),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
        image[6]=2;CHECK_EQ(esk_esp32_efuse_configure(&f,image,31),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
    }
}
static void reload_interrupt_and_failures(void)
{
    esk_esp32_efuse f,before;uint32_t image[31],value=0;unsigned i;int irq=0;
    for(i=0;i<31;i++) image[i]=0x12340000u+i;
    image[6]&=~3u;
    esk_esp32_efuse_init(&f);CHECK_EQ(esk_esp32_efuse_configure(&f,image,31),ES_OK);image[1]=0;
    CHECK_EQ(esk_esp32_efuse_read(&f,data_reg(1),4,&value),ES_OK);CHECK_EQ(value,0x12340001);
    before=f;CHECK_EQ(esk_esp32_efuse_write(&f,0x3ff5a104,4,1),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
    CHECK_EQ(esk_esp32_efuse_write(&f,0x3ff5a0fc,4,0x5aa5),ES_OK);
    CHECK_EQ(esk_esp32_efuse_write(&f,0x3ff5a104,4,1),ES_OK);
    CHECK_EQ(esk_esp32_efuse_read(&f,0x3ff5a108,4,&value),ES_OK);CHECK_EQ(value,1);
    CHECK_EQ(esk_esp32_efuse_read(&f,0x3ff5a10c,4,&value),ES_OK);CHECK_EQ(value,0);
    CHECK_EQ(esk_esp32_efuse_write(&f,0x3ff5a110,4,1),ES_OK);CHECK_EQ(esk_esp32_efuse_irq(&f,&irq),ES_OK);CHECK_EQ(irq,1);
    CHECK_EQ(esk_esp32_efuse_write(&f,0x3ff5a114,4,1),ES_OK);CHECK_EQ(esk_esp32_efuse_irq(&f,&irq),ES_OK);CHECK_EQ(irq,0);
    CHECK_EQ(esk_esp32_efuse_read(&f,data_reg(1),4,&value),ES_OK);CHECK_EQ(value,0x12340001);
    before=f;CHECK_EQ(esk_esp32_efuse_write(&f,0x3ff5a0fc,4,0x5a5a),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
    CHECK_EQ(esk_esp32_efuse_write(&f,0x3ff5a104,4,2),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
    CHECK_EQ(esk_esp32_efuse_write(&f,0x3ff5a104,4,3),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
    CHECK_EQ(esk_esp32_efuse_configure(&f,image,30),ES_EINVAL);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
    CHECK_EQ(esk_esp32_efuse_read(&f,0x3ff5a120,4,&value),ES_EUNSUPPORTED);
    value=0xdeadbeef;CHECK_EQ(esk_esp32_efuse_read(&f,0x3ff5a114,4,&value),ES_EUNSUPPORTED);CHECK_EQ(value,0xdeadbeefu);
    CHECK_EQ(esk_esp32_efuse_read(&f,data_reg(1),1,&value),ES_EINVAL);CHECK_EQ(value,0xdeadbeefu);
    CHECK_EQ(esk_esp32_efuse_configure(&f,NULL,0),ES_OK);CHECK_EQ(esk_esp32_efuse_read(&f,data_reg(1),4,&value),ES_EUNSUPPORTED);
}
static void native_bus_and_reset_retention(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_esp32_fabric *f;uint32_t image[31]={0},value=0;int irq;
    const unsigned char code[]={0x32,0x62,0};esk_xtensa_windows *w;unsigned event;esk_xtensa_fault fault;
    image[1]=0x12345678;esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);f=esk_esp32_machine_fabric(m);
    CHECK_EQ(esk_esp32_fabric_efuse_configure(f,image,31),ES_OK);bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(bus.read(bus.ctx,data_reg(1),4,&value),ES_OK);CHECK_EQ(value,0x12345678);
    CHECK_EQ(bus.fetch(bus.ctx,data_reg(1),3,&value),ES_EUNSUPPORTED);
    CHECK_EQ(bus.compare_exchange(bus.ctx,data_reg(1),0,0,&value),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_machine_source(m,44,1),ES_EUNSUPPORTED);
    CHECK_EQ(bus.write(bus.ctx,0x3ff001b4,4,19),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff5a110,4,1),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff5a0fc,4,0x5aa5),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(f,0,0x40080000,code,sizeof code),ES_OK);
    CHECK_EQ(esk_esp32_machine_configure(m,0,0x40080000),ES_OK);w=esk_esp32_machine_core(m,0);
    w->irq.enabled=1;w->irq.intenable=1u<<19;w->irq.vectors[2]=0x40084000;w->cpu.a[2]=0x3ff5a104;w->cpu.a[3]=1;
    CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);
    CHECK_EQ(event,0);CHECK_EQ(w->cpu.pc,0x40080003);CHECK(w->irq.lines&(1u<<19));
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,12);CHECK_EQ(w->irq.epc[2],0x40080003);
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);CHECK_EQ(esk_esp32_fabric_efuse_irq(f,&irq),ES_OK);CHECK_EQ(irq,1);
    CHECK_EQ(bus.read(bus.ctx,data_reg(1),4,&value),ES_OK);CHECK_EQ(value,0x12345678);
    CHECK_EQ(bus.write(bus.ctx,0x3ff5a114,4,1),ES_OK);CHECK(!(w->irq.lines&(1u<<19)));
    CHECK_EQ(esk_esp32_fabric_efuse_configure(f,NULL,0),ES_OK);CHECK_EQ(esk_esp32_machine_source(m,44,1),ES_OK);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(supplied_images_and_protection);RUN(reload_interrupt_and_failures);RUN(native_bus_and_reset_retention);
TEST_MAIN_END
