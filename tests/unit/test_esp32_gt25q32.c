/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/gt25q32.h"
#include "mcu/esp32/fabric.h"
static es_status command(esk_esp32_gt25q32 *f,unsigned char *image,unsigned cmd,int has,uint32_t address,const unsigned char *tx,size_t n,unsigned char *rx,size_t m)
{ return esk_esp32_gt25q32_command(f,image,0x400000,cmd,has,address,tx,n,rx,m); }
static void identity_program_timing_and_erase(void)
{
    esk_esp32_gt25q32 f,before;unsigned char *image=(unsigned char *)malloc(0x400000);unsigned char tx[64],rx[3];
    const unsigned lengths[]={1,2,16,64},cmds[]={0x20,0x52,0xd8,0xc7,0x60},sizes[]={4096,32768,65536,0x400000,0x400000};
    unsigned k,i;CHECK(image!=NULL);if(!image) return;
    memset(image,255,0x400000);esk_esp32_gt25q32_init(&f);
    CHECK_EQ(command(&f,image,0x9f,0,0,NULL,0,rx,3),ES_OK);CHECK_EQ(rx[0],0xc4);CHECK_EQ(rx[1],0x60);CHECK_EQ(rx[2],0x16);
    for(k=0;k<4;k++) {
        uint64_t duration=lengths[k]==1 ? 150u : 1500u;
        memset(image+0x100,255,256);for(i=0;i<64;i++) tx[i]=(unsigned char)(0xf0u^(i&255u));
        CHECK_EQ(command(&f,image,6,0,0,NULL,0,NULL,0),ES_OK);
        CHECK_EQ(command(&f,image,2,1,0x1ff,tx,lengths[k],NULL,0),ES_OK);CHECK_EQ(f.remaining_us,duration);CHECK_EQ(f.wel,0);
        CHECK_EQ(command(&f,image,5,0,0,NULL,0,rx,3),ES_OK);CHECK_EQ(rx[0],1);CHECK_EQ(rx[2],1);
        CHECK_EQ(esk_esp32_gt25q32_advance(&f,image,0x400000,duration-1),ES_OK);CHECK_EQ(image[0x1ff],255);
        CHECK_EQ(esk_esp32_gt25q32_advance(&f,image,0x400000,1),ES_OK);CHECK_EQ(f.remaining_us,0);
        for(i=0;i<256;i++) { unsigned source=(i+1u)&255u;CHECK_EQ(image[0x100u+i],source<lengths[k] ? tx[source] : 255u); }
    }
    for(k=0;k<5;k++) {
        uint64_t duration=k<3 ? 8000u : 16000u;uint32_t address=0x23456,base=k<3 ? address&~(sizes[k]-1u) : 0;
        memset(image,0,0x400000);CHECK_EQ(command(&f,image,6,0,0,NULL,0,NULL,0),ES_OK);
        CHECK_EQ(command(&f,image,cmds[k],k<3,address,NULL,0,NULL,0),ES_OK);CHECK_EQ(f.remaining_us,duration);CHECK_EQ(f.wel,0);
        before=f;CHECK_EQ(command(&f,image,6,0,0,NULL,0,NULL,0),ES_OK);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
        CHECK_EQ(esk_esp32_gt25q32_advance(&f,image,0x400000,duration-1),ES_OK);CHECK_EQ(image[base],0);
        CHECK_EQ(esk_esp32_gt25q32_advance(&f,image,0x400000,UINT64_MAX),ES_OK);
        CHECK_EQ(image[base],255);CHECK_EQ(image[base+sizes[k]-1u],255);if(base) CHECK_EQ(image[base-1u],0);
        if(base+sizes[k]<0x400000u) CHECK_EQ(image[base+sizes[k]],0);
    }
    before=f;CHECK_EQ(command(&f,image,0x82,1,0,NULL,0,NULL,0),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
    free(image);
}
static void explicit_attachment_and_controller(void)
{
    esk_alloc a;esk_esp32_fabric *f=NULL;esk_xtensa_memory bus;uint32_t value=0;uint64_t left=0;
    unsigned char *image=(unsigned char *)malloc(0x400000);CHECK(image!=NULL);if(!image) return;memset(image,255,0x400000);
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_fabric_create(&a,&f),ES_OK);
    CHECK_EQ(esk_esp32_fabric_flash(f,image,0x400000),ES_OK);CHECK_EQ(esk_esp32_fabric_gt25q32_configure(f,1),ES_OK);
    CHECK_EQ(esk_esp32_fabric_w25q32_configure(f,1),ES_EUNSUPPORTED);CHECK_EQ(esk_esp32_fabric_w25q32_configure(f,0),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_fabric_spi1_configure(f,1),ES_OK);bus=esk_esp32_fabric_bus(f,0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42008,4,0x208400),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff4201c,4,0x90000040),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42024,4,0x7000009f),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff4202c,4,23),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42080,4,0xa5000000),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff42000,4,0x40000),ES_OK);
    CHECK_EQ(bus.read(bus.ctx,0x3ff42080,4,&value),ES_OK);CHECK_EQ(value,0xa51660c4u);
    CHECK_EQ(bus.write(bus.ctx,0x3ff4201c,4,0x80000040),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff42024,4,0x70000006),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42000,4,0x40000),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff4201c,4,0xc8000040),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff42024,4,0x70000002),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42004,4,0x123400),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff42028,4,31),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42080,4,0x12345678),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff42000,4,0x40000),ES_OK);
    CHECK_EQ(esk_esp32_fabric_flash_deadline(f,&left),ES_OK);CHECK_EQ(left,1500);
    CHECK_EQ(esk_esp32_fabric_gt25q32_configure(f,0),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_fabric_flash_advance(f,1500),ES_OK);CHECK_EQ(esk_esp32_fabric_gt25q32_configure(f,0),ES_OK);
    CHECK_EQ(esk_esp32_fabric_w25q32_configure(f,1),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff4201c,4,0x90000040),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff42024,4,0x7000009f),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42000,4,0x40000),ES_OK);CHECK_EQ(bus.read(bus.ctx,0x3ff42080,4,&value),ES_OK);CHECK_EQ(value&0xffffffu,0x1640efu);
    esk_esp32_fabric_destroy(f);free(image);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(identity_program_timing_and_erase);RUN(explicit_attachment_and_controller);
TEST_MAIN_END
