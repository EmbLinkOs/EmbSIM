/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/machine.h"
static uint32_t base(unsigned port)
{ return port==0 ? UINT32_C(0x3ff40000) : port==1 ? UINT32_C(0x3ff50000) : UINT32_C(0x3ff6e000); }
static uint32_t fifo(unsigned port)
{ return port==0 ? UINT32_C(0x60000000) : port==1 ? UINT32_C(0x60010000) : UINT32_C(0x6002e000); }
static uint32_t rd(esk_esp32_uart_bank *b,unsigned port,unsigned offset)
{ uint32_t v=0;CHECK_EQ(esk_esp32_uart_bank_read(b,base(port)+offset,4,&v),ES_OK);return v; }
static void wr(esk_esp32_uart_bank *b,unsigned port,unsigned offset,uint32_t v)
{ CHECK_EQ(esk_esp32_uart_bank_write(b,base(port)+offset,4,v),ES_OK); }
typedef struct burst { unsigned count,seed;uint64_t last; } burst;
static void emit(void *ctx,const esk_esp32_uart_frame *f,uint64_t offset32)
{
    burst *c=(burst *)ctx;CHECK_EQ(f->data,(c->seed+c->count)&255u);CHECK_EQ(f->bits,8);
    CHECK(offset32>c->last);c->last=offset32;c->count++;
}
static void all_ports_and_bounded_allocations(void)
{
    esk_esp32_uart_bank b,before;unsigned port,tx,rx,n,txcap,rxcap;burst c;uint32_t status,counts;
    for(port=0;port<3;port++) for(tx=1;tx<=8u-port;tx++) for(rx=1;rx<=5u-port;rx++) {
        esk_esp32_uart_bank_init(&b);CHECK_EQ(esk_esp32_uart_bank_configure(&b,port,1,1,1,0),ES_OK);
        wr(&b,port,20,2);wr(&b,port,32,0x0800801c);wr(&b,port,88,(tx<<7)|(rx<<3));
        CHECK_EQ(rd(&b,port,88),(tx<<7)|(rx<<3));txcap=tx*128u;rxcap=rx*128u;
        for(n=0;n<txcap;n++) CHECK_EQ(esk_esp32_uart_bank_write(&b,fifo(port),4,0xdead0000u|n),ES_OK);
        status=rd(&b,port,28);counts=rd(&b,port,100);
        CHECK_EQ(((status>>16)&255u)|(((counts>>3)&7u)<<8),txcap);
        before=b;CHECK_EQ(esk_esp32_uart_bank_write(&b,fifo(port),4,0),ES_EUNSUPPORTED);
        CHECK_EQ(memcmp(&b,&before,sizeof b),0);
        CHECK_EQ(esk_esp32_uart_inputs(&b.port[port],1,0,0),ES_OK);memset(&c,0,sizeof c);
        CHECK_EQ(esk_esp32_uart_advance(&b.port[port],1000000,emit,&c),ES_OK);CHECK_EQ(c.count,txcap);
        CHECK_EQ(c.last,(uint64_t)txcap*640u);
        for(n=0;n<rxcap;n++) CHECK_EQ(esk_esp32_uart_receive(&b.port[port],n&255u,0),ES_OK);
        status=rd(&b,port,28);counts=rd(&b,port,100);
        CHECK_EQ((status&255u)|((counts&7u)<<8),rxcap);
        CHECK_EQ(esk_esp32_uart_receive(&b.port[port],0,0),ES_OK);CHECK(rd(&b,port,4)&16u);
        for(n=0;n<rxcap;n++) CHECK_EQ(rd(&b,port,0),n&255u);
        CHECK_EQ(rd(&b,port,96),0);
    }
}
static void shared_overlap_and_preserved_ram(void)
{
    esk_esp32_uart_bank b;unsigned p,n;burst c;uint8_t bytes[1024];uint32_t v;
    esk_esp32_uart_bank_init(&b);
    for(p=0;p<3;p++) { CHECK_EQ(esk_esp32_uart_bank_configure(&b,p,1,1,1,0),ES_OK);wr(&b,p,32,0x0800801c);wr(&b,p,20,2); }
    wr(&b,0,88,0x108);wr(&b,1,0,0x55);wr(&b,2,0,0x77);
    for(n=0;n<256;n++) wr(&b,0,0,n);
    CHECK_EQ(b.ram[128],128);CHECK_EQ(b.ram[256],0x77);
    memset(&c,0,sizeof c);c.seed=128;
    CHECK_EQ(esk_esp32_uart_inputs(&b.port[1],1,0,0),ES_OK);
    CHECK_EQ(esk_esp32_uart_advance(&b.port[1],100,emit,&c),ES_OK);CHECK_EQ(c.count,1);
    c.count=0;c.last=0;c.seed=0x77;
    CHECK_EQ(esk_esp32_uart_inputs(&b.port[2],1,0,0),ES_OK);
    CHECK_EQ(esk_esp32_uart_advance(&b.port[2],100,emit,&c),ES_OK);CHECK_EQ(c.count,1);
    /* Peripheral attach and reset clear pointers, never invent RAM zeroing. */
    memcpy(bytes,b.ram,sizeof bytes);CHECK_EQ(esk_esp32_uart_bank_configure(&b,0,1,1,1,0),ES_OK);
    CHECK_EQ(memcmp(bytes,b.ram,sizeof bytes),0);wr(&b,0,32,0x0804001c);
    CHECK_EQ(memcmp(bytes,b.ram,sizeof bytes),0);
    wr(&b,0,88,0x90); /* RX0 two blocks overlaps RX1. */
    CHECK_EQ(esk_esp32_uart_receive(&b.port[1],0x55,0),ES_OK);
    for(n=0;n<129;n++) CHECK_EQ(esk_esp32_uart_receive(&b.port[0],n&255u,0),ES_OK);
    CHECK_EQ(b.ram[512],128);CHECK_EQ(rd(&b,1,0),128);
    for(n=0;n<129;n++) CHECK_EQ(rd(&b,0,0),n&255u);
    v=0xa5;CHECK_EQ(esk_esp32_uart_bank_read(&b,base(0),4,&v),ES_EUNSUPPORTED);CHECK_EQ(v,0xa5u);
}
static void coupled_reset_and_unchanged_failures(void)
{
    esk_esp32_uart_bank b,before;unsigned p,n;uint8_t bytes[1024];
    esk_esp32_uart_bank_init(&b);
    for(p=0;p<3;p++) {
        CHECK_EQ(esk_esp32_uart_bank_configure(&b,p,1,1,1,0),ES_OK);wr(&b,p,32,0x0800801c);
        for(n=0;n<5;n++) wr(&b,p,0,0x40u+p*8u+n);
    }
    memcpy(bytes,b.ram,sizeof bytes);before=b;
    CHECK_EQ(esk_esp32_uart_configure(&b.port[1],1,1,0,0),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_bank_write(&b,base(1)+32u,4,0x0806801c),ES_EUNSUPPORTED);
    CHECK_EQ(memcmp(&b,&before,sizeof b),0);
    CHECK_EQ(esk_esp32_uart_write(&b.port[1],base(1)+32u,4,0x0804801c),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_bank_write(&b,base(2)+32u,4,0x0804801c),ES_EUNSUPPORTED);
    CHECK_EQ(memcmp(&b,&before,sizeof b),0);
    wr(&b,1,32,0x0804801c);CHECK_EQ(b.port[0].tx_count,5);CHECK_EQ(b.port[1].tx_count,0);CHECK_EQ(b.port[2].tx_count,0);
    CHECK_EQ(rd(&b,2,32)&0x40000u,0);CHECK_EQ(b.port[2].external_tx_reset,1);
    CHECK_EQ(memcmp(bytes,b.ram,sizeof bytes),0);before=b;
    CHECK_EQ(esk_esp32_uart_bank_write(&b,fifo(2),4,0xaa),ES_EUNSUPPORTED);
    CHECK_EQ(memcmp(&b,&before,sizeof b),0);wr(&b,1,32,0x0800801c);
    wr(&b,2,0,0xaa);CHECK_EQ(esk_esp32_uart_inputs(&b.port[2],1,0,0),ES_OK);before=b;
    CHECK_EQ(esk_esp32_uart_bank_write(&b,base(1)+32u,4,0x0804801c),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_bank_write(&b,base(1)+32u,1,0x0804801c),ES_EINVAL);
    CHECK_EQ(memcmp(&b,&before,sizeof b),0);
    CHECK_EQ(esk_esp32_uart_advance(&b.port[2],100000,NULL,NULL),ES_OK);
    wr(&b,1,32,0x0804801c);wr(&b,1,32,0x0800801c);
    wr(&b,1,0,0xbb);CHECK_EQ(esk_esp32_uart_inputs(&b.port[1],1,0,0),ES_OK);before=b;
    CHECK_EQ(esk_esp32_uart_bank_write(&b,base(1)+32u,4,0x0804801c),ES_EUNSUPPORTED);
    CHECK_EQ(memcmp(&b,&before,sizeof b),0);
    for(p=0;p<3;p++) {
        before=b;CHECK_EQ(esk_esp32_uart_bank_write(&b,base(p)+32u,4,b.port[p].conf0|0x20000u),ES_EUNSUPPORTED);
        CHECK_EQ(memcmp(&b,&before,sizeof b),0);
    }
}
static void high_thresholds_and_allocation_rejection(void)
{
    esk_esp32_uart_bank b,before;unsigned p,n;uint32_t value=0;
    esk_esp32_uart_bank_init(&b);CHECK_EQ(esk_esp32_uart_bank_configure(&b,0,1,1,0,0),ES_OK);
    wr(&b,0,88,(4u<<3)|(1u<<7)|(1u<<25));wr(&b,0,36,127);
    for(n=0;n<255;n++) CHECK_EQ(esk_esp32_uart_receive(&b.port[0],n,0),ES_OK);
    CHECK_EQ(rd(&b,0,4)&1u,0);CHECK_EQ(esk_esp32_uart_receive(&b.port[0],255,0),ES_OK);
    CHECK_EQ(rd(&b,0,4)&1u,1);CHECK_EQ(rd(&b,0,28)&255u,0);CHECK_EQ(rd(&b,0,100)&7u,1);
    before=b;CHECK_EQ(esk_esp32_uart_bank_write(&b,base(0)+88u,4,0x88),ES_EUNSUPPORTED);
    CHECK_EQ(memcmp(&b,&before,sizeof b),0);
    for(p=0;p<3;p++) {
        CHECK_EQ(esk_esp32_uart_bank_configure(&b,p,1,1,1,0),ES_OK);before=b;
        CHECK_EQ(esk_esp32_uart_bank_write(&b,base(p)+88u,4,0x80),ES_EUNSUPPORTED);
        CHECK_EQ(esk_esp32_uart_bank_write(&b,base(p)+88u,4,8),ES_EUNSUPPORTED);
        CHECK_EQ(esk_esp32_uart_bank_write(&b,base(p)+88u,4,((9u-p)<<7)|8u),ES_EUNSUPPORTED);
        CHECK_EQ(esk_esp32_uart_bank_write(&b,base(p)+88u,4,((6u-p)<<3)|128u),ES_EUNSUPPORTED);
        CHECK_EQ(esk_esp32_uart_bank_write(&b,base(p)+88u,4,0x89),ES_EUNSUPPORTED);
        CHECK_EQ(esk_esp32_uart_bank_write(&b,base(p)+88u,4,0x200088),ES_EUNSUPPORTED);
        CHECK_EQ(memcmp(&b,&before,sizeof b),0);
        wr(&b,p,32,0x0800801c);wr(&b,p,0,1);before=b;
        CHECK_EQ(esk_esp32_uart_bank_write(&b,base(p)+88u,4,0x108),ES_EUNSUPPORTED);
        CHECK_EQ(memcmp(&b,&before,sizeof b),0);
    }
    CHECK_EQ(esk_esp32_uart_bank_configure(&b,0,1,1,1,0),ES_OK);
    wr(&b,0,32,0x0800801c);wr(&b,0,88,(4u<<7)|8u|(2u<<28));wr(&b,0,36,0);
    for(n=0;n<256;n++) wr(&b,0,0,n);
    wr(&b,0,16,2);CHECK_EQ(rd(&b,0,4)&2u,0);
    CHECK_EQ(esk_esp32_uart_inputs(&b.port[0],1,0,0),ES_OK);CHECK_EQ(rd(&b,0,4)&2u,2);
    CHECK_EQ(b.port[0].tx_count,255);
    before=b;CHECK_EQ(esk_esp32_uart_bank_configure(&b,3,1,1,0,0),ES_EINVAL);
    CHECK_EQ(esk_esp32_uart_bank_read(&b,base(0)+4u,1,&value),ES_EINVAL);
    CHECK_EQ(esk_esp32_uart_bank_read(&b,0x3ff60000,4,&value),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_bank_read(&b,base(0),4,NULL),ES_EINVAL);
    CHECK_EQ(memcmp(&b,&before,sizeof b),0);
}
static void machine_interrupt_or_and_independent_ports(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory bus;esk_xtensa_windows *w[2];unsigned p,cpu;uint32_t v;
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);bus=esk_esp32_machine_bus(m,0);
    for(cpu=0;cpu<2;cpu++) {
        w[cpu]=esk_esp32_machine_core(m,cpu);w[cpu]->irq.enabled=1;
        for(p=0;p<3;p++) CHECK_EQ(bus.write(bus.ctx,0x3ff00104u+cpu*0x114u+(34u+p)*4u,4,19),ES_OK);
    }
    for(p=0;p<3;p++) {
        CHECK_EQ(esk_esp32_machine_uart_port_configure(m,p,1,1,0,0),ES_OK);
        CHECK_EQ(esk_esp32_machine_source(m,34u+p,1),ES_EUNSUPPORTED);
        CHECK_EQ(bus.write(bus.ctx,base(p)+36u,4,0),ES_OK);CHECK_EQ(bus.write(bus.ctx,base(p)+12u,4,1),ES_OK);
        CHECK_EQ(esk_esp32_machine_uart_port_receive(m,p,0x40u+p,0),ES_OK);
    }
    for(p=0;p<3;p++) {
        for(cpu=0;cpu<2;cpu++) CHECK(w[cpu]->irq.lines&(1u<<19));
        CHECK_EQ(bus.read(bus.ctx,base(p),4,&v),ES_OK);CHECK_EQ(v,0x40u+p);
        CHECK_EQ(bus.write(bus.ctx,base(p)+16u,4,1),ES_OK);
    }
    for(cpu=0;cpu<2;cpu++) CHECK_EQ(w[cpu]->irq.lines&(1u<<19),0);
    CHECK_EQ(esk_esp32_machine_uart_port_receive(m,3,0,0),ES_EINVAL);
    CHECK_EQ(esk_esp32_machine_uart_port_advance(m,3,0,NULL,NULL),ES_EINVAL);
    for(p=0;p<3;p++) {
        CHECK_EQ(esk_esp32_machine_uart_port_configure(m,p,0,0,0,0),ES_OK);
        CHECK_EQ(esk_esp32_machine_source(m,34u+p,1),ES_OK);
    }
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(all_ports_and_bounded_allocations);RUN(shared_overlap_and_preserved_ram);
    RUN(coupled_reset_and_unchanged_failures);RUN(high_thresholds_and_allocation_rejection);RUN(machine_interrupt_or_and_independent_ports);
TEST_MAIN_END
