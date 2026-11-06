/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/fabric.h"
static uint32_t word(const unsigned char *image,unsigned offset)
{ return (uint32_t)image[offset]|((uint32_t)image[offset+1u]<<8)|((uint32_t)image[offset+2u]<<16)|((uint32_t)image[offset+3u]<<24); }
static void aliases_boundaries_and_remap(void)
{
    esk_alloc a;esk_esp32_fabric *f=NULL;esk_xtensa_memory b[2];unsigned cpu,i,j;uint32_t v,old;
    unsigned char *image=(unsigned char *)malloc(0x70000);const unsigned char code[]={0x36,0x41,0,0};
    const uint32_t offsets[]={0,0x7ffcu,0x8000u,0x5fffcu,0x60000u,0x65c18u,0x6fffcu};
    CHECK(image!=NULL);if(!image) return;
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_fabric_create(&a,&f),ES_OK);
    for(cpu=0;cpu<2;cpu++) b[cpu]=esk_esp32_fabric_bus(f,cpu);
    memset(image,0x5a,0x70000);CHECK_EQ(esk_esp32_fabric_rom(f,0,image,0x60000),ES_OK);
    CHECK_EQ(esk_esp32_fabric_rom(f,1,image,0x10000),ES_OK);
    for(cpu=0;cpu<2;cpu++) {
        v=0xdeadbeef;CHECK_EQ(b[cpu].fetch(b[cpu].ctx,0x40060000,3,&v),ES_EUNSUPPORTED);CHECK_EQ(v,0xdeadbeefu);
        for(i=0;i<0x70000u;i++) image[i]=(unsigned char)((i*13u+(i>>12)+cpu*71u)&255u);
        CHECK_EQ(esk_esp32_fabric_mask_rom(f,cpu,image,0x70000),ES_OK);
        for(j=0;j<sizeof offsets/sizeof offsets[0];j++) {
            CHECK_EQ(b[cpu].fetch(b[cpu].ctx,0x40000000u+offsets[j],4,&v),ES_OK);CHECK_EQ(v,word(image,offsets[j]));
            CHECK_EQ(b[cpu].read(b[cpu].ctx,0x40000000u+offsets[j],4,&v),ES_OK);CHECK_EQ(v,word(image,offsets[j]));
        }
        for(i=0;i<65536u;i+=257u) {
            CHECK_EQ(b[cpu].read(b[cpu].ctx,0x3ff90000u+i,1,&v),ES_OK);CHECK_EQ(v,image[0x60000u+i]);
        }
        CHECK_EQ(b[cpu].fetch(b[cpu].ctx,0x4005ffff,3,&v),ES_OK);
        CHECK_EQ(v,(uint32_t)image[0x5ffff]|((uint32_t)image[0x60000]<<8)|((uint32_t)image[0x60001]<<16));
        old=word(image,0x65c18);image[0x65c18]=0;
        CHECK_EQ(b[cpu].fetch(b[cpu].ctx,0x40065c18,4,&v),ES_OK);CHECK_EQ(v,old);
        v=0xdeadbeef;CHECK_EQ(b[cpu].fetch(b[cpu].ctx,0x4006ffff,2,&v),ES_EUNSUPPORTED);CHECK_EQ(v,0xdeadbeefu);
        CHECK_EQ(b[cpu].fetch(b[cpu].ctx,0x40070000,1,&v),ES_EUNSUPPORTED);
        CHECK_EQ(b[cpu].fetch(b[cpu].ctx,0x3ff90000,3,&v),ES_EUNSUPPORTED);
        CHECK_EQ(b[cpu].read(b[cpu].ctx,0x40060000,1,&v),ES_EUNSUPPORTED);
        CHECK_EQ(b[cpu].read(b[cpu].ctx,0x40060000,2,&v),ES_EUNSUPPORTED);
        CHECK_EQ(b[cpu].read(b[cpu].ctx,0x40060001,4,&v),ES_EINVAL);
        CHECK_EQ(b[cpu].write(b[cpu].ctx,0x40060000,4,0),ES_EINVAL);
        CHECK_EQ(b[cpu].write(b[cpu].ctx,0x3ff90000,1,0),ES_EINVAL);
        CHECK_EQ(b[cpu].compare_exchange(b[cpu].ctx,0x40060000,0,0,&v),ES_EUNSUPPORTED);
        CHECK_EQ(esk_esp32_fabric_load(f,cpu,0x4005ffff,code,sizeof code),ES_EINVAL);
        CHECK_EQ(esk_esp32_fabric_load(f,cpu,0x400b0000,code,sizeof code),ES_OK);
        CHECK_EQ(esk_esp32_fabric_remap(f,cpu,1),ES_OK);
        CHECK_EQ(b[cpu].fetch(b[cpu].ctx,0x40000000,4,&v),ES_OK);CHECK_EQ(v,0x004136u);
        CHECK_EQ(b[cpu].fetch(b[cpu].ctx,0x40065c18,4,&v),ES_OK);CHECK_EQ(v,old);
        CHECK_EQ(esk_esp32_fabric_remap(f,cpu,0),ES_OK);
    }
    CHECK_EQ(b[0].fetch(b[0].ctx,0x40065c18,4,&old),ES_OK);
    CHECK_EQ(b[1].fetch(b[1].ctx,0x40065c18,4,&v),ES_OK);CHECK(v!=old);
    memset(image,0x89,0x70000);
    CHECK_EQ(esk_esp32_fabric_rom(f,0,image,0x60000),ES_OK);
    CHECK_EQ(esk_esp32_fabric_rom(f,1,image,0x10000),ES_OK);
    CHECK_EQ(b[0].fetch(b[0].ctx,0x40065c18,4,&v),ES_OK);CHECK_EQ(v,old);
    CHECK_EQ(b[0].read(b[0].ctx,0x3ff95c18,4,&v),ES_OK);CHECK_EQ(v,old);
    CHECK_EQ(esk_esp32_fabric_mask_rom(f,0,NULL,0),ES_OK);
    CHECK_EQ(b[0].fetch(b[0].ctx,0x40000000,4,&v),ES_OK);CHECK_EQ(v,0x89898989u);
    CHECK_EQ(b[0].read(b[0].ctx,0x3ff90000,4,&v),ES_OK);CHECK_EQ(v,0x89898989u);
    CHECK_EQ(b[0].fetch(b[0].ctx,0x40060000,3,&v),ES_EUNSUPPORTED);
    CHECK_EQ(b[1].fetch(b[1].ctx,0x40065c18,4,&v),ES_OK);
    CHECK_EQ(esk_esp32_fabric_mask_rom(f,1,NULL,0),ES_OK);
    esk_esp32_fabric_destroy(f);free(image);CHECK_EQ(a.live_bytes,0);
}
static void *limited_alloc(void *ctx,size_t n)
{ unsigned *left=(unsigned *)ctx;if(!*left) return NULL;(*left)--;return malloc(n); }
static void *limited_realloc(void *ctx,void *p,size_t n) { (void)ctx;return realloc(p,n); }
static void limited_free(void *ctx,void *p) { (void)ctx;free(p); }
static void atomic_replacement_and_bad_inputs(void)
{
    unsigned remaining=2;es_allocator_t hooks={limited_alloc,limited_realloc,limited_free,&remaining};
    esk_alloc a;esk_esp32_fabric *f=NULL;esk_xtensa_memory b;uint32_t v;size_t live;
    unsigned char *image=(unsigned char *)malloc(0x70000);CHECK(image!=NULL);if(!image) return;
    memset(image,0x67,0x70000);esk_alloc_init(&a,&hooks);CHECK_EQ(esk_esp32_fabric_create(&a,&f),ES_OK);
    b=esk_esp32_fabric_bus(f,0);CHECK_EQ(esk_esp32_fabric_mask_rom(f,0,image,0x70000),ES_OK);live=a.live_bytes;
    memset(image,0x89,0x70000);CHECK_EQ(esk_esp32_fabric_mask_rom(f,0,image,0x70000),ES_ENOMEM);CHECK_EQ(a.live_bytes,live);
    CHECK_EQ(esk_esp32_fabric_mask_rom(f,2,image,0x70000),ES_EINVAL);
    CHECK_EQ(esk_esp32_fabric_mask_rom(f,0,image,0x60000),ES_EINVAL);
    CHECK_EQ(esk_esp32_fabric_mask_rom(f,0,image,0),ES_EINVAL);
    CHECK_EQ(esk_esp32_fabric_mask_rom(f,0,NULL,0x70000),ES_EINVAL);
    CHECK_EQ(b.fetch(b.ctx,0x40065c18,4,&v),ES_OK);CHECK_EQ(v,0x67676767u);
    CHECK_EQ(b.read(b.ctx,0x3ff95c18,4,&v),ES_OK);CHECK_EQ(v,0x67676767u);
    CHECK_EQ(esk_esp32_fabric_mask_rom(f,0,NULL,0),ES_OK);CHECK_EQ(a.live_bytes,live-0x70000u);
    CHECK_EQ(b.fetch(b.ctx,0x40065c18,4,&v),ES_EUNSUPPORTED);
    remaining=1;CHECK_EQ(esk_esp32_fabric_mask_rom(f,1,image,0x70000),ES_OK);
    esk_esp32_fabric_destroy(f);free(image);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(aliases_boundaries_and_remap);RUN(atomic_replacement_and_bad_inputs);
TEST_MAIN_END
