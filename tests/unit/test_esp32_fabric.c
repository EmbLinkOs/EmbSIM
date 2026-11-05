/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/fabric.h"
static void address_views(void)
{
    esk_alloc a;esk_esp32_fabric *f=NULL;esk_xtensa_memory pro,app;uint32_t v=0,i,lane;
    unsigned char bytes[8]={1,2,3,4,5,6,7,8};
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_fabric_create(&a,&f),ES_OK);
    pro=esk_esp32_fabric_bus(f,0);app=esk_esp32_fabric_bus(f,1);
    /* Every SRAM1 word and byte lane, not just the two endpoint aliases. */
    for(i=0;i<0x20000;i+=4) {
        uint32_t expected=UINT32_C(0xa5c30000)^i;
        CHECK_EQ(pro.write(pro.ctx,0x3ffe0000+i,4,expected),ES_OK);
        CHECK_EQ(app.read(app.ctx,0x400bfffc-i,4,&v),ES_OK);CHECK_EQ(v,expected);
        for(lane=0;lane<4;lane++) {
            CHECK_EQ(app.fetch(app.ctx,0x400bfffc-i+lane,1,&v),ES_OK);
            CHECK_EQ(v,(expected>>(lane*8))&255u);
        }
    }
    CHECK_EQ(esk_esp32_fabric_load(f,0,0x400a0002,bytes,8),ES_OK);
    CHECK_EQ(pro.fetch(pro.ctx,0x400a0002,3,&v),ES_OK);CHECK_EQ(v,0x030201);
    CHECK_EQ(app.read(app.ctx,0x3ffffffc,4,&v),ES_OK);CHECK_EQ(v>>16,0x0201);
    CHECK_EQ(pro.read(pro.ctx,0x40080000,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(pro.write(pro.ctx,0x40080000,2,1),ES_EUNSUPPORTED);
    CHECK_EQ(pro.read(pro.ctx,0x3ffae001,4,&v),ES_EINVAL);
    CHECK_EQ(pro.fetch(pro.ctx,0x3ffae000,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(pro.fetch(pro.ctx,0x40070000,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(pro.read(pro.ctx,0x3ff44000,4,&v),ES_EUNSUPPORTED);
    CHECK_EQ(pro.fetch(pro.ctx,0x400d0000,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(pro.fetch(pro.ctx,0x40000000,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(pro.write(pro.ctx,0x3ff80000,4,0x12345678),ES_OK);
    CHECK_EQ(pro.fetch(pro.ctx,0x400c0001,3,&v),ES_OK);CHECK_EQ(v,0x123456);
    CHECK_EQ(app.read(app.ctx,0x3ff80000,4,&v),ES_EUNSUPPORTED);
    CHECK_EQ(app.fetch(app.ctx,0x400c0000,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(app.write(app.ctx,0x50000000,4,0x78563412),ES_OK);
    CHECK_EQ(pro.fetch(pro.ctx,0x50000001,3,&v),ES_OK);CHECK_EQ(v,0x785634);
    CHECK_EQ(pro.compare_exchange(pro.ctx,0x50000000,0,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(pro.write(pro.ctx,0x3ffb0000,4,3),ES_OK);
    CHECK_EQ(app.compare_exchange(app.ctx,0x3ffb0000,3,7,&v),ES_OK);CHECK_EQ(v,3);
    CHECK_EQ(pro.compare_exchange(pro.ctx,0x3ffb0000,3,9,&v),ES_OK);CHECK_EQ(v,7);
    CHECK_EQ(pro.read(pro.ctx,0x3ffb0000,4,&v),ES_OK);CHECK_EQ(v,7);
    CHECK_EQ(pro.compare_exchange(pro.ctx,0x40080000,0,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(pro.compare_exchange(pro.ctx,0x3ffb0001,0,1,&v),ES_EINVAL);
    CHECK_EQ(pro.write(pro.ctx,0x50001ffc,4,0x11223344),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(f,0,0x50001ffc,bytes,8),ES_EUNSUPPORTED);
    CHECK_EQ(pro.read(pro.ctx,0x50001ffc,4,&v),ES_OK);CHECK_EQ(v,0x11223344);
    CHECK_EQ(esk_esp32_fabric_load(f,0,0xfffffffc,bytes,8),ES_EINVAL);
    CHECK_EQ(esk_esp32_fabric_load(f,2,0,bytes,8),ES_EINVAL);
    CHECK(esk_esp32_fabric_bus(f,2).read==NULL);
    esk_esp32_fabric_destroy(f);CHECK_EQ(a.live_bytes,0);
}
static void rom_and_remap(void)
{
    esk_alloc a;esk_esp32_fabric *f=NULL;esk_xtensa_memory pro,app;uint32_t v=0;
    unsigned char *rom=(unsigned char *)malloc(0x60000);unsigned char code[4]={1,2,3,4};
    CHECK(rom!=NULL);if(!rom) return;
    memset(rom,0x5a,0x60000);esk_alloc_init(&a,NULL);
    CHECK_EQ(esk_esp32_fabric_create(&a,&f),ES_OK);
    pro=esk_esp32_fabric_bus(f,0);app=esk_esp32_fabric_bus(f,1);
    CHECK_EQ(esk_esp32_fabric_rom(f,0,rom,0x60000),ES_OK);
    CHECK_EQ(esk_esp32_fabric_rom(f,1,rom,0x10000),ES_OK);
    rom[0]=0;CHECK_EQ(pro.fetch(pro.ctx,0x40000000,4,&v),ES_OK);CHECK_EQ(v,0x5a5a5a5a);
    CHECK_EQ(pro.read(pro.ctx,0x3ff90000,1,&v),ES_OK);CHECK_EQ(v,0x5a);
    CHECK_EQ(pro.fetch(pro.ctx,0x3ff90000,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(pro.write(pro.ctx,0x40000000,4,0),ES_EINVAL);
    CHECK_EQ(esk_esp32_fabric_load(f,0,0x40000000,code,4),ES_EINVAL);
    CHECK_EQ(esk_esp32_fabric_rom(f,0,rom,4),ES_EINVAL);
    CHECK_EQ(esk_esp32_fabric_load(f,0,0x400b0000,code,4),ES_OK);
    CHECK_EQ(esk_esp32_fabric_remap(f,0,1),ES_OK);
    CHECK_EQ(pro.fetch(pro.ctx,0x40000000,4,&v),ES_OK);CHECK_EQ(v,0x04030201);
    CHECK_EQ(app.fetch(app.ctx,0x40000000,4,&v),ES_OK);CHECK_EQ(v,0x5a5a5a5a);
    CHECK_EQ(pro.fetch(pro.ctx,0x400b0000,1,&v),ES_EUNSUPPORTED);
    CHECK_EQ(app.fetch(app.ctx,0x400b0000,4,&v),ES_OK);CHECK_EQ(v,0x04030201);
    CHECK_EQ(pro.read(pro.ctx,0x3ffefffc,4,&v),ES_OK);CHECK_EQ(v,0x04030201);
    CHECK_EQ(pro.write(pro.ctx,0x40000000,4,0x99887766),ES_OK);
    CHECK_EQ(app.fetch(app.ctx,0x400b0000,4,&v),ES_OK);CHECK_EQ(v,0x99887766);
    CHECK_EQ(pro.fetch(pro.ctx,0x40008000,1,&v),ES_OK);CHECK_EQ(v,0x5a);
    CHECK_EQ(esk_esp32_fabric_remap(f,0,0),ES_OK);
    CHECK_EQ(pro.fetch(pro.ctx,0x400b0000,4,&v),ES_OK);CHECK_EQ(v,0x99887766);
    esk_esp32_fabric_destroy(f);free(rom);CHECK_EQ(a.live_bytes,0);
}
static void *limited_alloc(void *ctx,size_t n)
{
    unsigned *remaining=(unsigned *)ctx;
    if(!*remaining) return NULL;
    (*remaining)--;return malloc(n);
}
static void *limited_realloc(void *ctx,void *p,size_t n) { (void)ctx;return realloc(p,n); }
static void limited_free(void *ctx,void *p) { (void)ctx;free(p); }
static void allocation_failures(void)
{
    unsigned remaining=0;es_allocator_t hooks={limited_alloc,limited_realloc,limited_free,&remaining};
    esk_alloc a;esk_esp32_fabric *f=NULL;esk_xtensa_memory bus;uint32_t v=0;size_t live;
    unsigned char *rom=(unsigned char *)malloc(0x60000);
    CHECK(rom!=NULL);if(!rom) return;
    memset(rom,0x67,0x60000);esk_alloc_init(&a,&hooks);
    CHECK_EQ(esk_esp32_fabric_create(&a,&f),ES_ENOMEM);CHECK(f==NULL);CHECK_EQ(a.live_bytes,0);
    remaining=2;CHECK_EQ(esk_esp32_fabric_create(&a,&f),ES_OK);
    CHECK_EQ(esk_esp32_fabric_rom(f,0,rom,0x60000),ES_OK);live=a.live_bytes;
    memset(rom,0x89,0x60000);
    CHECK_EQ(esk_esp32_fabric_rom(f,0,rom,0x60000),ES_ENOMEM);CHECK_EQ(a.live_bytes,live);
    bus=esk_esp32_fabric_bus(f,0);
    CHECK_EQ(bus.fetch(bus.ctx,0x40000000,4,&v),ES_OK);CHECK_EQ(v,0x67676767);
    esk_esp32_fabric_destroy(f);free(rom);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;
    RUN(address_views);RUN(rom_and_remap);RUN(allocation_failures);
TEST_MAIN_END
