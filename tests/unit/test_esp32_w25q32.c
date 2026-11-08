/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/w25q32.h"
#include "mcu/esp32/machine.h"
static es_status command(esk_esp32_w25q32 *f,unsigned char *image,unsigned cmd,int addr,uint32_t address,const unsigned char *tx,size_t n,unsigned char *rx,size_t m)
{ return esk_esp32_w25q32_command(f,image,0x400000,cmd,addr,address,tx,n,rx,m); }
static void page_program_and_latches(void)
{
    unsigned char *image=(unsigned char *)malloc(0x400000);unsigned char tx[256],rx[3];unsigned offset,k,i;
    const unsigned lengths[]={1,2,16,64,255,256};esk_esp32_w25q32 f;
    CHECK(image!=NULL);if(!image) return;memset(image,255,0x400000);
    for(offset=0;offset<256;offset++) for(k=0;k<sizeof lengths/sizeof lengths[0];k++) {
        unsigned n=lengths[k];uint32_t base=offset&1u ? 0x3fff00u : 0x100u;
        memset(image+base,255,256);image[base-1]=0x5a;
        for(i=0;i<n;i++) tx[i]=(unsigned char)((i*17u+offset)&255u);
        esk_esp32_w25q32_init(&f);CHECK_EQ(command(&f,image,6,0,0,NULL,0,NULL,0),ES_OK);
        CHECK_EQ(command(&f,image,2,1,base+offset,tx,n,NULL,0),ES_OK);CHECK_EQ(f.remaining_us,3000);
        CHECK_EQ(command(&f,image,5,0,0,NULL,0,rx,3),ES_OK);CHECK_EQ(rx[0],3);CHECK_EQ(rx[2],3);
        CHECK_EQ(command(&f,image,4,0,0,NULL,0,NULL,0),ES_OK);CHECK_EQ(f.wel,1); /* Ignored during BUSY. */
        CHECK_EQ(esk_esp32_w25q32_advance(&f,image,0x400000,2999),ES_OK);CHECK_EQ(f.remaining_us,1);
        for(i=0;i<256;i++) CHECK_EQ(image[base+i],255);
        CHECK_EQ(esk_esp32_w25q32_advance(&f,image,0x400000,1),ES_OK);CHECK_EQ(f.remaining_us,0);CHECK_EQ(f.wel,0);
        for(i=0;i<256;i++) { unsigned source=(i-offset)&255u;CHECK_EQ(image[base+i],source<n ? tx[source] : 255u); }
        CHECK_EQ(image[base-1],0x5a);
        /* Program cannot turn a previously cleared bit back into one. */
        CHECK_EQ(command(&f,image,6,0,0,NULL,0,NULL,0),ES_OK);memset(tx,255,n);
        CHECK_EQ(command(&f,image,2,1,base+offset,tx,n,NULL,0),ES_OK);
        CHECK_EQ(esk_esp32_w25q32_advance(&f,image,0x400000,UINT64_MAX),ES_OK);
        for(i=0;i<256;i++) { unsigned source=(i-offset)&255u;CHECK_EQ(image[base+i],source<n ? ((source*17u+offset)&255u) : 255u); }
    }
    free(image);
}
static void erase_sizes_and_ignored_commands(void)
{
    unsigned char *image=(unsigned char *)malloc(0x400000),*expected=(unsigned char *)malloc(0x400000);
    const unsigned cmds[]={0x20,0x52,0xd8,0xc7,0x60};const uint64_t times[]={400000,1600000,2000000,50000000,50000000};
    const unsigned sizes[]={4096,32768,65536,0x400000,0x400000};unsigned k;esk_esp32_w25q32 f,before;unsigned char rx[3]={0xa5,0xa5,0xa5},tx=0;
    CHECK(image!=NULL && expected!=NULL);if(!image || !expected) { free(image);free(expected);return; }
    for(k=0;k<5;k++) {
        uint32_t address=0x12345,base=k<3 ? address&~(sizes[k]-1u) : 0;
        memset(image,0x33,0x400000);memcpy(expected,image,0x400000);esk_esp32_w25q32_init(&f);before=f;
        CHECK_EQ(command(&f,image,cmds[k],k<3,address,NULL,0,NULL,0),ES_OK);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
        CHECK_EQ(command(&f,image,6,0,0,NULL,0,NULL,0),ES_OK);
        CHECK_EQ(command(&f,image,cmds[k],k<3,address,NULL,0,NULL,0),ES_OK);CHECK_EQ(f.remaining_us,times[k]);before=f;
        CHECK_EQ(command(&f,image,2,1,0,&tx,1,NULL,0),ES_OK);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
        CHECK_EQ(command(&f,image,3,1,0,NULL,0,rx,1),ES_EUNSUPPORTED);CHECK_EQ(rx[0],0xa5);
        CHECK_EQ(command(&f,image,0x9f,0,0,NULL,0,rx,3),ES_EUNSUPPORTED);CHECK_EQ(rx[1],0xa5);
        CHECK_EQ(esk_esp32_w25q32_advance(&f,image,0x400000,times[k]-1),ES_OK);CHECK_EQ(memcmp(image,expected,0x400000),0);
        CHECK_EQ(esk_esp32_w25q32_advance(&f,image,0x400000,1),ES_OK);memset(expected+base,255,sizes[k]);
        CHECK_EQ(memcmp(image,expected,0x400000),0);CHECK_EQ(f.wel,0);
    }
    esk_esp32_w25q32_init(&f);before=f;
    CHECK_EQ(command(&f,image,2,1,0,&tx,1,NULL,0),ES_OK);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
    CHECK_EQ(command(&f,image,6,0,0,NULL,0,NULL,0),ES_OK);CHECK_EQ(command(&f,image,4,0,0,NULL,0,NULL,0),ES_OK);CHECK_EQ(f.wel,0);
    CHECK_EQ(command(&f,image,0x9f,0,0,NULL,0,rx,3),ES_OK);CHECK_EQ(rx[0],0xef);CHECK_EQ(rx[1],0x40);CHECK_EQ(rx[2],0x16);
    before=f;CHECK_EQ(command(&f,image,3,1,0x3fffff,NULL,0,rx,2),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
    CHECK_EQ(command(&f,image,0x35,0,0,NULL,0,rx,1),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&f,&before,sizeof f),0);
    CHECK_EQ(command(&f,image,2,1,0x400000,&tx,1,NULL,0),ES_EUNSUPPORTED);
    CHECK_EQ(command(&f,image,2,1,0,NULL,1,NULL,0),ES_EINVAL);
    CHECK_EQ(esk_esp32_w25q32_advance(NULL,image,0x400000,0),ES_EINVAL);
    free(image);free(expected);
}
static void spi_command(esk_xtensa_memory *bus,unsigned cmd,int address,uint32_t at,unsigned tx_bytes,unsigned rx_bytes)
{
    uint32_t user=0x80000040u|(address ? 0x40000000u : 0u)|(tx_bytes ? 0x8000000u : 0u)|(rx_bytes ? 0x10000000u : 0u);
    CHECK_EQ(bus->write(bus->ctx,0x3ff4201c,4,user),ES_OK);
    CHECK_EQ(bus->write(bus->ctx,0x3ff42024,4,0x70000000u|cmd),ES_OK);
    CHECK_EQ(bus->write(bus->ctx,0x3ff42004,4,(at<<8)|255u),ES_OK);
    CHECK_EQ(bus->write(bus->ctx,0x3ff42028,4,tx_bytes ? tx_bytes*8u-1u : 0u),ES_OK);
    CHECK_EQ(bus->write(bus->ctx,0x3ff4202c,4,rx_bytes ? rx_bytes*8u-1u : 0u),ES_OK);
    CHECK_EQ(bus->write(bus->ctx,0x3ff42000,4,0x40000),ES_OK);
}
static void flash_time_reset_and_cache_integration(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_esp32_fabric *f;esk_xtensa_memory bus;uint32_t value=0;uint64_t left=77;
    unsigned char *image=(unsigned char *)malloc(0x400000);CHECK(image!=NULL);if(!image) return;memset(image,255,0x400000);
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);f=esk_esp32_machine_fabric(m);
    CHECK_EQ(esk_esp32_fabric_flash_deadline(f,&left),ES_EUNSUPPORTED);CHECK_EQ(left,77);
    CHECK_EQ(esk_esp32_fabric_w25q32_configure(f,1),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_fabric_flash(f,image,65536),ES_OK);
    CHECK_EQ(esk_esp32_fabric_w25q32_configure(f,1),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_fabric_flash(f,image,0x400000),ES_OK);CHECK_EQ(esk_esp32_fabric_w25q32_configure(f,1),ES_OK);
    CHECK_EQ(esk_esp32_fabric_spi1_configure(f,1),ES_OK);bus=esk_esp32_machine_bus(m,0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42008,4,0x208400),ES_OK);
    spi_command(&bus,6,0,0,0,0);CHECK_EQ(bus.write(bus.ctx,0x3ff42080,4,0x12345678),ES_OK);
    spi_command(&bus,2,1,0x100,4,0);CHECK_EQ(esk_esp32_fabric_flash_deadline(f,&left),ES_OK);CHECK_EQ(left,3000);
    spi_command(&bus,5,0,0,0,1);CHECK_EQ(bus.read(bus.ctx,0x3ff42080,4,&value),ES_OK);CHECK_EQ(value&255u,3);
    CHECK_EQ(esk_esp32_fabric_w25q32_configure(f,0),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_fabric_flash(f,image,0x400000),ES_EUNSUPPORTED);
    CHECK_EQ(bus.write(bus.ctx,0x3ff10000,4,0),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff00044,4,0x8ef),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00040,4,0),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff00040,4,16),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00040,4,24),ES_EUNSUPPORTED); /* Flush while busy is insufficient. */
    CHECK_EQ(esk_esp32_fabric_flash_advance(f,2999),ES_OK);CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);
    CHECK_EQ(esk_esp32_fabric_flash_deadline(f,&left),ES_OK);CHECK_EQ(left,1);
    CHECK_EQ(esk_esp32_fabric_flash_advance(f,1),ES_OK);CHECK_EQ(esk_esp32_fabric_flash_deadline(f,&left),ES_OK);CHECK_EQ(left,0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00040,4,24),ES_EUNSUPPORTED); /* Needs fresh post-completion flush. */
    spi_command(&bus,5,0,0,0,1);CHECK_EQ(bus.read(bus.ctx,0x3ff42080,4,&value),ES_OK);CHECK_EQ(value&255u,0);
    spi_command(&bus,3,1,0x100,0,4);CHECK_EQ(bus.read(bus.ctx,0x3ff42080,4,&value),ES_OK);CHECK_EQ(value,0x12345678);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00040,4,0),ES_OK);CHECK_EQ(bus.write(bus.ctx,0x3ff00040,4,16),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00040,4,24),ES_OK);CHECK_EQ(bus.read(bus.ctx,0x3f400100,4,&value),ES_OK);CHECK_EQ(value,0x12345678);
    CHECK_EQ(bus.write(bus.ctx,0x3ff00040,4,16),ES_OK);
    /* High MOSI and high MISO buffers use separate byte lanes. */
    spi_command(&bus,6,0,0,0,0);
    CHECK_EQ(bus.write(bus.ctx,0x3ff420a0,4,0x1234cdab),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff4201c,4,0xca000040),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42024,4,0x70000002),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42004,4,0x2ffff),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42028,4,15),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42000,4,0x40000),ES_OK);
    CHECK_EQ(esk_esp32_fabric_flash_advance(f,3000),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff420a0,4,0),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff4201c,4,0xd1000040),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42024,4,0x70000003),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff4202c,4,7),ES_OK);
    CHECK_EQ(bus.write(bus.ctx,0x3ff42000,4,0x40000),ES_OK);
    CHECK_EQ(bus.read(bus.ctx,0x3ff420a0,4,&value),ES_OK);CHECK_EQ(value,0xab);
    CHECK_EQ(bus.read(bus.ctx,0x3ff42080,4,&value),ES_OK);CHECK_EQ(value,0x12345678);
    spi_command(&bus,3,1,0x200,0,1);CHECK_EQ(bus.read(bus.ctx,0x3ff42080,4,&value),ES_OK);CHECK_EQ(value&255u,0xcd);
    CHECK_EQ(esk_esp32_fabric_w25q32_configure(f,0),ES_OK);
    left=77;CHECK_EQ(esk_esp32_fabric_flash_deadline(f,&left),ES_EUNSUPPORTED);CHECK_EQ(left,77);
    esk_esp32_machine_destroy(m);free(image);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(page_program_and_latches);RUN(erase_sizes_and_ignored_commands);RUN(flash_time_reset_and_cache_integration);
TEST_MAIN_END
