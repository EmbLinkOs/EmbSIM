/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/flashspi.h"
#include "mcu/esp32/machine.h"
static es_status write_reg(esk_esp32_flashspi *s,const unsigned char *image,size_t size,unsigned reg,uint32_t value)
{ return esk_esp32_flashspi_write(s,image,size,1,0x3ff42000u+reg,4,value); }
static void setup(esk_esp32_flashspi *s)
{
    esk_esp32_flashspi_init(s);
    CHECK_EQ(write_reg(s,NULL,0,8,0x208400),ES_OK);
    CHECK_EQ(write_reg(s,NULL,0,0x1c,0xd0000040),ES_OK);
    CHECK_EQ(write_reg(s,NULL,0,0x24,0x70000003),ES_OK);
}
static void all_lengths_and_buffers(void)
{
    unsigned char image[1024];unsigned i,n,high,turn;uint32_t value=0;
    for(i=0;i<sizeof image;i++) image[i]=(unsigned char)((i*17u+(i>>8))&255u);
    for(high=0;high<2;high++) for(n=1;n<=64u-high*32u;n++) for(turn=0;turn<4;turn++) {
        esk_esp32_flashspi s;unsigned first=high*32u,address=997u-n;
        setup(&s);for(i=0;i<16;i++) CHECK_EQ(write_reg(&s,image,sizeof image,0x80u+i*4u,0xa5a5a5a5),ES_OK);
        CHECK_EQ(write_reg(&s,image,sizeof image,0x1c,0xd0000040u|(high<<24)),ES_OK);
        CHECK_EQ(write_reg(&s,image,sizeof image,4,(address<<8)|(turn*85u)),ES_OK);
        CHECK_EQ(write_reg(&s,image,sizeof image,0x2c,n*8u-1u),ES_OK);
        CHECK_EQ(write_reg(&s,image,sizeof image,0,0x40000),ES_OK);CHECK(esk_esp32_flashspi_irq(&s));
        for(i=0;i<64;i++) {
            uint32_t wanted=i>=first && i<first+n ? image[address+i-first] : 0xa5u;
            CHECK_EQ((s.buffer[i/4u]>>((i&3u)*8u))&255u,wanted);
        }
        CHECK_EQ(esk_esp32_flashspi_read(&s,0x3ff42000,4,&value),ES_OK);CHECK_EQ(value,0);
        CHECK_EQ(s.slave,0x800210);
        CHECK_EQ(write_reg(&s,image,sizeof image,0x38,0x200),ES_OK);CHECK(!esk_esp32_flashspi_irq(&s));
        CHECK_EQ(write_reg(&s,image,sizeof image,0x38,0x10),ES_OK);CHECK(!esk_esp32_flashspi_irq(&s));
        CHECK_EQ(write_reg(&s,image,sizeof image,0x38,0x210),ES_OK);CHECK(esk_esp32_flashspi_irq(&s));
    }
}
static void failures_are_atomic(void)
{
    esk_esp32_flashspi s,before;unsigned char image[64];unsigned i;uint32_t value=0xdeadbeef;
    const unsigned regs[]={8,0x18,0x1c,0x20,0x34,0x38,0x10,0x100};
    const uint32_t values[]={0,0,0xd00000c0,0,7,0x40000000,0,0};
    memset(image,0x5a,sizeof image);setup(&s);
    CHECK_EQ(write_reg(&s,image,sizeof image,0x2c,511),ES_OK);
    for(i=0;i<sizeof regs/sizeof regs[0];i++) {
        before=s;CHECK_EQ(write_reg(&s,image,sizeof image,regs[i],values[i]),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&s,&before,sizeof s),0);
    }
    for(i=0;i<32;i++) if(i!=18) {
        before=s;CHECK_EQ(write_reg(&s,image,sizeof image,0,UINT32_C(1)<<i),i<16 ? ES_OK : ES_EUNSUPPORTED);
        CHECK_EQ(memcmp(&s,&before,sizeof s),0);
    }
    before=s;CHECK_EQ(esk_esp32_flashspi_write(&s,image,sizeof image,0,0x3ff42000,4,0x40000),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&s,&before,sizeof s),0);
    CHECK_EQ(write_reg(&s,image,sizeof image,0,0x40000),ES_OK);
    CHECK_EQ((s.slave>>23)&15u,1);
    for(i=0;i<31;i++) CHECK_EQ(write_reg(&s,image,sizeof image,0,0x40000),ES_OK);
    CHECK_EQ((s.slave>>23)&15u,0);
    CHECK_EQ(write_reg(&s,image,sizeof image,4,1u<<8),ES_OK);before=s;
    CHECK_EQ(write_reg(&s,image,sizeof image,0,0x40000),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&s,&before,sizeof s),0);
    CHECK_EQ(write_reg(&s,image,sizeof image,4,0),ES_OK);
    CHECK_EQ(write_reg(&s,image,sizeof image,0x2c,512),ES_OK);before=s;
    CHECK_EQ(write_reg(&s,image,sizeof image,0,0x40000),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&s,&before,sizeof s),0);
    CHECK_EQ(write_reg(&s,image,sizeof image,0x24,0x7000009f),ES_OK);before=s;
    CHECK_EQ(write_reg(&s,image,sizeof image,0,0x40000),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&s,&before,sizeof s),0);
    CHECK_EQ(esk_esp32_flashspi_read(&s,0x3ff42001,4,&value),ES_EINVAL);CHECK_EQ(value,0xdeadbeefu);
    CHECK_EQ(esk_esp32_flashspi_read(&s,0x3ff42000,1,&value),ES_EINVAL);CHECK_EQ(value,0xdeadbeefu);
    CHECK_EQ(esk_esp32_flashspi_read(&s,0x3ff43000,4,&value),ES_EUNSUPPORTED);CHECK_EQ(value,0xdeadbeefu);
    CHECK_EQ(esk_esp32_flashspi_read(NULL,0,4,&value),ES_EINVAL);
    CHECK_EQ(esk_esp32_flashspi_write(NULL,NULL,0,0,0,4,0),ES_EINVAL);
}
static void native_bus_irq_and_opt_in(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_esp32_fabric *f;esk_xtensa_memory bus[2];uint32_t value=0;int irq=77;
    unsigned char *image=(unsigned char *)malloc(65536);unsigned i;esk_xtensa_windows *cpu;
    CHECK(image!=NULL);if(!image) return;for(i=0;i<65536;i++) image[i]=(unsigned char)(i&255u);
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);f=esk_esp32_machine_fabric(m);
    CHECK_EQ(esk_esp32_fabric_spi1_configure(f,1),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_fabric_spi1_irq(f,&irq),ES_EUNSUPPORTED);CHECK_EQ(irq,77);
    CHECK_EQ(esk_esp32_fabric_flash(f,image,65536),ES_OK);
    bus[0]=esk_esp32_machine_bus(m,0);bus[1]=esk_esp32_machine_bus(m,1);
    CHECK_EQ(bus[0].read(bus[0].ctx,0x3ff42000,4,&value),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_fabric_spi1_configure(f,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_source(m,29,1),ES_EUNSUPPORTED);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff00178,4,19),ES_OK); /* SPI1 -> PRO level 2 */
    cpu=esk_esp32_machine_core(m,0);cpu->irq.enabled=1;
    esk_esp32_machine_core(m,1)->irq.enabled=1;
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff0028c,4,19),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff42008,4,0x208400),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff4201c,4,0xd0000040),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff42024,4,0x70000003),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff4202c,4,31),ES_OK);
    CHECK_EQ(bus[1].write(bus[1].ctx,0x3ff42004,4,0x1234ff),ES_OK);
    CHECK_EQ(bus[1].write(bus[1].ctx,0x3ff42000,4,0x40000),ES_OK);
    CHECK_EQ(bus[0].read(bus[0].ctx,0x3ff42080,4,&value),ES_OK);CHECK_EQ(value,0x37363534);
    CHECK_EQ(esk_esp32_fabric_spi1_irq(f,&irq),ES_OK);CHECK_EQ(irq,1);CHECK(cpu->irq.lines&(1u<<19));
    CHECK(esk_esp32_machine_core(m,1)->irq.lines&(1u<<19));
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);
    CHECK_EQ(bus[0].read(bus[0].ctx,0x3ff42080,4,&value),ES_OK);CHECK_EQ(value,0x37363534);
    CHECK_EQ(esk_esp32_fabric_spi1_irq(f,&irq),ES_OK);CHECK_EQ(irq,1);
    CHECK_EQ(bus[0].read(bus[0].ctx,0x3ff000ec,4,&value),ES_OK);CHECK(value&(1u<<29));
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff42038,4,0x200),ES_OK);CHECK(!(cpu->irq.lines&(1u<<19)));
    CHECK_EQ(bus[1].fetch(bus[1].ctx,0x3ff42080,3,&value),ES_EUNSUPPORTED);
    CHECK_EQ(bus[1].compare_exchange(bus[1].ctx,0x3ff42080,0,0,&value),ES_EUNSUPPORTED);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff10000,4,0),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff00040,4,0),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff00040,4,16),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff00040,4,24),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff42000,4,0x40000),ES_EUNSUPPORTED);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff00040,4,16),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff12000,4,0),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff00058,4,0),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff00058,4,16),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff00058,4,24),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff42000,4,0x40000),ES_EUNSUPPORTED);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff00058,4,16),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,0x3ff42000,4,0x40000),ES_OK);
    CHECK(cpu->irq.lines&(1u<<19));
    CHECK_EQ(esk_esp32_fabric_spi1_configure(f,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_source(m,29,0),ES_OK);CHECK(!(cpu->irq.lines&(1u<<19)));
    CHECK_EQ(esk_esp32_machine_source(m,29,1),ES_OK);CHECK(cpu->irq.lines&(1u<<19));
    esk_esp32_machine_destroy(m);free(image);CHECK_EQ(a.live_bytes,0);
}
static void instruction_commit_then_interrupt(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_esp32_fabric *f;esk_xtensa_windows *w;
    esk_xtensa_memory bus;uint32_t value=0;unsigned event=99;esk_xtensa_fault fault;
    const unsigned char code[3]={0x32,0x62,0}; /* s32i a3,a2,0 */
    unsigned char *image=(unsigned char *)malloc(65536);CHECK(image!=NULL);if(!image) return;memset(image,0x5a,65536);
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);f=esk_esp32_machine_fabric(m);
    CHECK_EQ(esk_esp32_fabric_flash(f,image,65536),ES_OK);CHECK_EQ(esk_esp32_fabric_spi1_configure(f,1),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(f,0,0x40080000,code,sizeof code),ES_OK);
    CHECK_EQ(esk_esp32_machine_configure(m,0,0x40080000),ES_OK);w=esk_esp32_machine_core(m,0);
    w->irq.enabled=1;w->irq.intenable=1u<<19;w->irq.vectors[2]=0x40084000;
    w->cpu.a[2]=0x3ff42000;w->cpu.a[3]=0x40000;
    bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00178,4,19),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42008,4,0x208400),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff4201c,4,0xd0000040),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42024,4,0x70000003),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff4202c,4,31),ES_OK);
    CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,0);
    CHECK_EQ(w->cpu.pc,0x40080003);CHECK_EQ(w->cpu.a[3],0x40000);CHECK_EQ(esk_esp32_machine_retired(m,0),1);
    CHECK(w->irq.lines&(1u<<19));
    CHECK_EQ(bus.read(bus.ctx,0x3ff42080,4,&value),ES_OK);CHECK_EQ(value,0x5a5a5a5a);
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,12);
    CHECK_EQ(w->cpu.pc,0x40084000);CHECK_EQ(w->irq.epc[2],0x40080003);CHECK_EQ(esk_esp32_machine_retired(m,0),1);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42038,4,0x200),ES_OK);CHECK(!(w->irq.lines&(1u<<19)));
    esk_esp32_machine_destroy(m);free(image);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(all_lengths_and_buffers);RUN(failures_are_atomic);RUN(native_bus_irq_and_opt_in);RUN(instruction_commit_then_interrupt);
TEST_MAIN_END
