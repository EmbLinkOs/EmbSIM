/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/fabric.h"
static uint32_t ctrl(unsigned cpu) { return 0x3ff00040u+cpu*24u; }
static uint32_t table(unsigned cpu,unsigned index) { return 0x3ff10000u+cpu*0x2000u+index*4u; }
static void enable(esk_xtensa_memory *bus,unsigned cpu)
{
    CHECK_EQ(bus->write(bus->ctx,ctrl(cpu),4,0),ES_OK);
    CHECK_EQ(bus->write(bus->ctx,ctrl(cpu),4,16),ES_OK);
    CHECK_EQ(bus->write(bus->ctx,ctrl(cpu),4,24),ES_OK);
}
static uint32_t pattern(uint32_t address) { return (address*13u^(address>>8)^(address>>16))&255u; }
static void full_mapping_and_masks(void)
{
    esk_alloc a;esk_esp32_fabric *f=NULL;esk_xtensa_memory bus[2];unsigned cpu,index,k;uint32_t i,v=0;
    unsigned char *image=(unsigned char *)malloc(0x1000000);const uint32_t offsets[]={0x2000,0x7ffe,0xfffc,0xffff};
    CHECK(image!=NULL);if(!image) return;
    for(i=0;i<0x1000000;i++) image[i]=(unsigned char)pattern(i);
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_fabric_create(&a,&f),ES_OK);
    CHECK_EQ(esk_esp32_fabric_flash(f,image,0x1000000),ES_OK);
    image[0]^=255; /* source changes cannot mutate the private backing */
    for(cpu=0;cpu<2;cpu++) {
        bus[cpu]=esk_esp32_fabric_bus(f,cpu);
        CHECK_EQ(bus[cpu].read(bus[cpu].ctx,ctrl(cpu),4,&v),ES_OK);CHECK_EQ(v,16);
        CHECK_EQ(bus[cpu].read(bus[cpu].ctx,ctrl(cpu)+4u,4,&v),ES_OK);CHECK_EQ(v,0x8ff);
        for(index=0;index<256;index++) {
            CHECK_EQ(bus[0].read(bus[0].ctx,table(cpu,index),4,&v),ES_OK);CHECK_EQ(v,0x100);
            CHECK_EQ(bus[0].write(bus[0].ctx,table(cpu,index),4,(index*17u+cpu*31u)&255u),ES_OK);
        }
        CHECK_EQ(bus[0].write(bus[0].ctx,ctrl(cpu)+4u,4,0x8e8),ES_OK);
        CHECK_EQ(bus[0].write(bus[0].ctx,ctrl(cpu),4,24),ES_EUNSUPPORTED);
        CHECK_EQ(bus[0].read(bus[0].ctx,ctrl(cpu),4,&v),ES_OK);CHECK_EQ(v,16);
        enable(&bus[0],cpu);
        CHECK_EQ(bus[0].read(bus[0].ctx,ctrl(cpu),4,&v),ES_OK);CHECK_EQ(v,56);
        for(index=0;index<256;index++) {
            uint32_t base=index<64 ? 0x3f400000u+index*65536u : 0x40000000u+(index-64u)*65536u;
            uint32_t physical=((index*17u+cpu*31u)&255u)*65536u;
            if(index>=64 && index<76) continue; /* on-chip area never redirected */
            for(k=0;k<sizeof offsets/sizeof offsets[0];k++) {
                uint32_t expected=pattern(physical+offsets[k]);
                if(index<64) CHECK_EQ(bus[cpu].read(bus[cpu].ctx,base+offsets[k],1,&v),ES_OK);
                else CHECK_EQ(bus[cpu].fetch(bus[cpu].ctx,base+offsets[k],1,&v),ES_OK);
                CHECK_EQ(v,expected);
            }
            CHECK_EQ(bus[cpu].read(bus[cpu].ctx,base+0xfffc,4,&v),ES_OK);
            CHECK_EQ(v,pattern(physical+0xfffc)|(pattern(physical+0xfffd)<<8)|(pattern(physical+0xfffe)<<16)|(pattern(physical+0xffff)<<24));
            if(index>=76 && index<255) {
                uint32_t next=(((index+1u)*17u+cpu*31u)&255u)*65536u;
                CHECK_EQ(bus[cpu].fetch(bus[cpu].ctx,base+65535u,3,&v),ES_OK);
                CHECK_EQ(v,pattern(physical+65535u)|(pattern(next)<<8)|(pattern(next+1u)<<16));
            }
        }
        for(index=0;index<4;index++) {
            const uint32_t addresses[]={0x3f400000,0x400d0000,0x40400000,0x40800000};
            uint32_t bit=index ? 1u<<(index-1u) : 16u;
            CHECK_EQ(bus[0].write(bus[0].ctx,ctrl(cpu)+4u,4,0x8e8u|bit),ES_OK);
            CHECK_EQ(bus[cpu].read(bus[cpu].ctx,addresses[index],4,&v),ES_EUNSUPPORTED);
            CHECK_EQ(bus[0].write(bus[0].ctx,ctrl(cpu)+4u,4,0x8e8),ES_OK);
        }
        CHECK_EQ(bus[cpu].fetch(bus[cpu].ctx,0x3f400000,1,&v),ES_EUNSUPPORTED);
        CHECK_EQ(bus[cpu].read(bus[cpu].ctx,0x400d0000,1,&v),ES_EUNSUPPORTED);
        CHECK_EQ(bus[cpu].read(bus[cpu].ctx,0x3f400001,4,&v),ES_EINVAL);
        CHECK_EQ(bus[cpu].fetch(bus[cpu].ctx,0x40bfffff,2,&v),ES_EUNSUPPORTED);
        CHECK_EQ(bus[cpu].fetch(bus[cpu].ctx,0x40070000,1,&v),ES_EUNSUPPORTED);
        CHECK_EQ(bus[cpu].fetch(bus[cpu].ctx,0x40000400,1,&v),ES_EUNSUPPORTED);
        CHECK_EQ(bus[cpu].fetch(bus[cpu].ctx,0x40060000,1,&v),ES_EUNSUPPORTED);
        CHECK_EQ(bus[cpu].write(bus[cpu].ctx,0x3f400000,4,0),ES_EUNSUPPORTED);
        CHECK_EQ(bus[cpu].compare_exchange(bus[cpu].ctx,0x3f400000,0,1,&v),ES_EUNSUPPORTED);
        CHECK_EQ(bus[cpu].fetch(bus[cpu].ctx,ctrl(cpu),1,&v),ES_EUNSUPPORTED);
        CHECK_EQ(bus[0].read(bus[0].ctx,table(cpu,77),4,&v),ES_OK);i=v;
        CHECK_EQ(bus[0].write(bus[0].ctx,table(cpu,77),4,9),ES_EUNSUPPORTED);
        CHECK_EQ(bus[0].read(bus[0].ctx,table(cpu,77),4,&v),ES_OK);CHECK_EQ(v,i);
        CHECK_EQ(bus[0].write(bus[0].ctx,ctrl(cpu),4,0x438),ES_EUNSUPPORTED);
        CHECK_EQ(bus[0].write(bus[0].ctx,ctrl(cpu)+4u,4,0x8c8),ES_EUNSUPPORTED);
        CHECK_EQ(bus[0].write(bus[0].ctx,table(cpu,0),1,0),ES_EINVAL);
        CHECK_EQ(bus[0].read(bus[0].ctx,table(cpu,0)+1u,4,&v),ES_EINVAL);
        CHECK_EQ(bus[0].read(bus[0].ctx,table(cpu,256),4,&v),ES_EUNSUPPORTED);
    }
    CHECK_EQ(esk_esp32_fabric_flash(f,image,0x1000000),ES_EUNSUPPORTED);
    CHECK_EQ(bus[0].write(bus[0].ctx,ctrl(0),4,16),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,table(0,77),4,9),ES_OK);
    CHECK_EQ(bus[0].write(bus[0].ctx,table(0,77),4,0x200),ES_EUNSUPPORTED);
    CHECK_EQ(bus[0].write(bus[0].ctx,ctrl(0),4,24),ES_EUNSUPPORTED);
    enable(&bus[0],0);
    CHECK_EQ(bus[0].fetch(bus[0].ctx,0x400d0000,1,&v),ES_OK);CHECK_EQ(v,pattern(9u*65536u));
    CHECK_EQ(bus[1].fetch(bus[1].ctx,0x400d0000,1,&v),ES_OK);CHECK_EQ(v,pattern(((77u*17u+31u)&255u)*65536u));
    CHECK_EQ(bus[0].write(bus[0].ctx,0x40080000,4,0xf03d),ES_OK);
    CHECK_EQ(bus[1].fetch(bus[1].ctx,0x40080000,2,&v),ES_OK);CHECK_EQ(v,0xf03d);
    CHECK_EQ(bus[1].fetch(bus[1].ctx,0x400c0000,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(bus[0].fetch(bus[0].ctx,0x400c0000,1,&v),ES_OK);CHECK_EQ(v,0);
    esk_esp32_fabric_destroy(f);free(image);CHECK_EQ(a.live_bytes,0);
}
static void bounds_and_replacement(void)
{
    esk_alloc a;esk_esp32_fabric *f=NULL;esk_xtensa_memory bus;uint32_t v=0;unsigned char *image=(unsigned char *)malloc(65536);
    CHECK(image!=NULL);if(!image) return;
    memset(image,0x55,65536);esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_fabric_create(&a,&f),ES_OK);bus=esk_esp32_fabric_bus(f,0);
    CHECK_EQ(esk_esp32_fabric_flash(f,image,65535),ES_EINVAL);
    CHECK_EQ(bus.read(bus.ctx,ctrl(0),4,&v),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_fabric_flash(f,image,65536),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,table(0,0),4,0),ES_OK);CHECK_EQ(bus.write(bus.ctx,table(0,1),4,1),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,ctrl(0)+4u,4,0x8ef),ES_OK);enable(&bus,0);
    CHECK_EQ(bus.read(bus.ctx,0x3f400000,4,&v),ES_OK);CHECK_EQ(v,0x55555555);
    v=0xdeadbeef;CHECK_EQ(bus.read(bus.ctx,0x3f410000,4,&v),ES_EUNSUPPORTED);CHECK_EQ(v,0xdeadbeefu);
    CHECK_EQ(esk_esp32_fabric_load(f,0,0x3f400000,image,4),ES_EUNSUPPORTED);
    CHECK_EQ(bus.write(bus.ctx,ctrl(0),4,16),ES_OK);memset(image,0x66,65536);
    CHECK_EQ(esk_esp32_fabric_flash(f,image,65536),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,ctrl(0),4,24),ES_EUNSUPPORTED);enable(&bus,0);
    CHECK_EQ(bus.read(bus.ctx,0x3f400000,4,&v),ES_OK);CHECK_EQ(v,0x66666666);
    esk_esp32_fabric_destroy(f);free(image);CHECK_EQ(a.live_bytes,0);
}
static void *limited_alloc(void *ctx,size_t n) { unsigned *left=(unsigned *)ctx;if(!*left) return NULL;(*left)--;return malloc(n); }
static void *limited_realloc(void *ctx,void *p,size_t n) { (void)ctx;return realloc(p,n); }
static void limited_free(void *ctx,void *p) { (void)ctx;free(p); }
static void allocation_failures(void)
{
    unsigned remaining=1;es_allocator_t hooks={limited_alloc,limited_realloc,limited_free,&remaining};
    esk_alloc a;esk_esp32_fabric *f=NULL;esk_xtensa_memory bus;uint32_t v=0;size_t before;
    unsigned char *image=(unsigned char *)malloc(65536);CHECK(image!=NULL);if(!image) return;memset(image,0x77,65536);
    esk_alloc_init(&a,&hooks);CHECK_EQ(esk_esp32_fabric_create(&a,&f),ES_OK);bus=esk_esp32_fabric_bus(f,0);before=a.live_bytes;
    remaining=1;CHECK_EQ(esk_esp32_fabric_flash(f,image,65536),ES_ENOMEM);CHECK_EQ(a.live_bytes,before);
    CHECK_EQ(bus.read(bus.ctx,ctrl(0),4,&v),ES_EUNSUPPORTED);
    remaining=2;CHECK_EQ(esk_esp32_fabric_flash(f,image,65536),ES_OK);before=a.live_bytes;
    remaining=0;CHECK_EQ(esk_esp32_fabric_flash(f,image,65536),ES_ENOMEM);CHECK_EQ(a.live_bytes,before);
    CHECK_EQ(bus.write(bus.ctx,table(0,0),4,0),ES_OK);CHECK_EQ(bus.write(bus.ctx,ctrl(0)+4u,4,0x8ef),ES_OK);enable(&bus,0);
    CHECK_EQ(bus.read(bus.ctx,0x3f400000,4,&v),ES_OK);CHECK_EQ(v,0x77777777);
    CHECK_EQ(bus.write(bus.ctx,ctrl(0),4,16),ES_OK);before=a.live_bytes;
    CHECK_EQ(esk_esp32_fabric_flash(f,image,65536),ES_ENOMEM);CHECK_EQ(a.live_bytes,before);
    /* Failed replacement cannot dirty an already flushed mapping. */
    CHECK_EQ(bus.write(bus.ctx,ctrl(0),4,24),ES_OK);
    CHECK_EQ(bus.read(bus.ctx,0x3f400000,4,&v),ES_OK);CHECK_EQ(v,0x77777777);
    esk_esp32_fabric_destroy(f);free(image);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(full_mapping_and_masks);RUN(bounds_and_replacement);RUN(allocation_failures);
TEST_MAIN_END
