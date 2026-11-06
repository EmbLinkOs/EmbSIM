/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/machine.h"
#define BASE UINT32_C(0x3ff40000)
typedef struct capture { esk_esp32_uart_frame frames[129];unsigned count;uint64_t last_offset;esk_esp32_uart *u; } capture;
static void emit(void *ctx,const esk_esp32_uart_frame *frame,uint64_t offset32)
{
    capture *c=(capture *)ctx;CHECK(c->count<129);c->frames[c->count++]=*frame;c->last_offset=offset32;
    if(c->u) CHECK_EQ(esk_esp32_uart_receive(c->u,0,0),ES_EINVAL);
}
static uint32_t read_reg(esk_esp32_uart *u,unsigned off)
{
    uint32_t v=0;CHECK_EQ(esk_esp32_uart_read(u,BASE+off,4,&v),ES_OK);return v;
}
static void frame_encoding_and_clock(void)
{
    esk_esp32_uart u;unsigned bits,parity,odd,stop,data,half,n,ones,level,state,total;capture c;
    esk_esp32_uart_init(&u);
    for(bits=5;bits<=8;bits++) for(parity=0;parity<2;parity++) for(odd=0;odd<2;odd++) for(stop=1;stop<=2;stop++) for(data=0;data<256;data++) {
        memset(&c,0,sizeof c);c.u=&u;CHECK_EQ(esk_esp32_uart_configure(&u,1,1,0,0),ES_OK);
        CHECK_EQ(esk_esp32_uart_write(&u,BASE+20,4,2),ES_OK);
        CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,0x08000000u|((bits-5u)<<2)|(parity<<1)|odd|(stop<<4)),ES_OK);
        CHECK_EQ(esk_esp32_uart_write(&u,BASE,4,data),ES_OK);CHECK_EQ(u.tx_count,0);
        total=2u+2u*(bits+parity)+(stop==1 ? 2u : 3u);ones=0;
        for(n=0;n<bits;n++) ones+=(data>>n)&1u;
        for(half=0;half<total;half++) {
            unsigned bit=half/2u;uint32_t status=read_reg(&u,28);
            if(!bit) { level=0;state=1; }
            else if(bit<=bits) { level=(data>>(bit-1u))&1u;state=bit+1u; }
            else if(parity && bit==bits+1u) { level=(ones&1u)^odd;state=10; }
            else { level=1;state=half>=4u+2u*(bits+parity) ? 12u : 11u; }
            CHECK_EQ(status>>31,level);CHECK_EQ((status>>24)&15u,state);
            CHECK_EQ(esk_esp32_uart_advance(&u,1,emit,&c),ES_OK);
            CHECK_EQ(c.count,half+1u==total ? 1u : 0u);
        }
        CHECK_EQ(c.frames[0].data,data&((1u<<bits)-1u));CHECK_EQ(c.frames[0].bits,bits);
        CHECK_EQ(c.frames[0].parity,(ones&1u)^odd);CHECK_EQ(c.frames[0].parity_enabled,parity);
        CHECK_EQ(c.frames[0].stop_half_bits,stop==1 ? 2u : 3u);CHECK_EQ(c.frames[0].divider16,32);
        CHECK_EQ(c.frames[0].apb,1);CHECK_EQ(c.last_offset,32);CHECK_EQ(read_reg(&u,28),UINT32_C(0x80008000));CHECK(read_reg(&u,4)&0x4000u);
    }
}
static void fifos_flow_and_errors(void)
{
    esk_esp32_uart u,before;unsigned round,n;uint32_t value;int irq;capture c;
    memset(&c,0,sizeof c);esk_esp32_uart_init(&u);CHECK_EQ(esk_esp32_uart_configure(&u,1,1,1,0),ES_OK);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,u.conf0|0x8000u),ES_OK);
    for(n=0;n<128;n++) CHECK_EQ(esk_esp32_uart_write(&u,UINT32_C(0x60000000),4,n),ES_OK);
    CHECK_EQ((read_reg(&u,28)>>16)&255u,128);before=u;
    CHECK_EQ(esk_esp32_uart_write(&u,BASE,4,129),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&u,&before,sizeof u),0);
    CHECK_EQ(esk_esp32_uart_inputs(&u,1,0,1),ES_OK);CHECK_EQ(u.tx_count,127);CHECK_EQ(u.active,1);
    CHECK_EQ(esk_esp32_uart_inputs(&u,1,1,1),ES_OK);
    CHECK_EQ(esk_esp32_uart_advance(&u,1000000,emit,&c),ES_OK);CHECK_EQ(c.count,1);CHECK_EQ(u.tx_count,127);
    CHECK_EQ(read_reg(&u,4)&0x4060u,0x60); /* no DONE while queued/CTS-blocked */
    CHECK_EQ(esk_esp32_uart_inputs(&u,1,0,0),ES_OK);
    CHECK_EQ(esk_esp32_uart_advance(&u,1000000,emit,&c),ES_OK);CHECK_EQ(c.count,128);
    for(n=0;n<128;n++) CHECK_EQ(c.frames[n].data,n);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+12,4,UINT32_MAX),ES_OK);CHECK_EQ(read_reg(&u,12),0x7ffffu);
    CHECK_EQ(esk_esp32_uart_irq(&u,&irq),ES_OK);CHECK_EQ(irq,1);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+36,4,127u|(1u<<8)),ES_OK);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+16,4,UINT32_MAX),ES_OK);CHECK_EQ(read_reg(&u,4),2);
    for(round=0;round<3;round++) {
        for(n=0;n<128;n++) CHECK_EQ(esk_esp32_uart_receive(&u,(round+n)&255u,0),ES_OK);
        CHECK_EQ(read_reg(&u,4)&1u,1);CHECK_EQ(esk_esp32_uart_receive(&u,255,0),ES_OK);
        CHECK_EQ(read_reg(&u,4)&16u,16);CHECK_EQ(read_reg(&u,28)&255u,128);
        for(n=0;n<128;n++) CHECK_EQ(read_reg(&u,0),(round+n)&255u);
        CHECK_EQ(read_reg(&u,96),0);value=0xdeadbeef;
        CHECK_EQ(esk_esp32_uart_read(&u,BASE,4,&value),ES_EUNSUPPORTED);CHECK_EQ(value,0xdeadbeefu);
        CHECK_EQ(read_reg(&u,4)&17u,17); /* latched until W1C */
        CHECK_EQ(esk_esp32_uart_write(&u,BASE+16,4,17),ES_OK);CHECK_EQ(read_reg(&u,4)&17u,0);
    }
    CHECK_EQ(esk_esp32_uart_timeout(&u),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,0x0c00001e),ES_OK);
    CHECK_EQ(esk_esp32_uart_receive(&u,0x55,3),ES_OK);CHECK_EQ(u.rx_count,0);CHECK_EQ(read_reg(&u,4)&12u,12);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,0x0800001e),ES_OK);
    CHECK_EQ(esk_esp32_uart_receive(&u,0xa5,3),ES_OK);CHECK_EQ(u.rx_count,1);CHECK_EQ(read_reg(&u,0),0xa5);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+36,4,UINT32_C(0x8100017f)),ES_OK);
    CHECK_EQ(esk_esp32_uart_receive(&u,0,0),ES_OK);CHECK_EQ(esk_esp32_uart_timeout(&u),ES_OK);
    CHECK_EQ(read_reg(&u,4)&256u,256);CHECK_EQ(read_reg(&u,0),0);
}
static void fractional_clock_and_failures(void)
{
    esk_esp32_uart u,v,before;capture c,d;unsigned i;uint32_t value=7;
    memset(&c,0,sizeof c);memset(&d,0,sizeof d);esk_esp32_uart_init(&u);
    CHECK_EQ(esk_esp32_uart_configure(&u,1,1,0,0),ES_OK);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+20,4,0x700002),ES_OK);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE,4,0xaa),ES_OK);v=u;
    for(i=0;i<24;i++) CHECK_EQ(esk_esp32_uart_advance(&u,1,emit,&c),ES_OK);
    CHECK_EQ(c.count,0);CHECK_EQ(esk_esp32_uart_advance(&u,1,emit,&c),ES_OK);CHECK_EQ(c.count,1);
    CHECK_EQ(esk_esp32_uart_advance(&v,25,emit,&d),ES_OK);CHECK_EQ(d.count,1);
    CHECK_EQ(memcmp(&u,&v,sizeof u),0);CHECK_EQ(memcmp(&c.frames[0],&d.frames[0],sizeof c.frames[0]),0);
    CHECK_EQ(c.frames[0].divider16,39);CHECK_EQ(c.last_offset,12);CHECK_EQ(d.last_offset,780);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE,4,1),ES_OK);before=u;
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+20,4,2),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,0x08000018),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_advance(&u,UINT64_MAX,NULL,NULL),ES_EINVAL);
    CHECK_EQ(esk_esp32_uart_receive(&u,0,1),ES_EINVAL);
    CHECK_EQ(esk_esp32_uart_receive(&u,256,0),ES_EINVAL);
    CHECK_EQ(esk_esp32_uart_inputs(&u,2,0,0),ES_EINVAL);CHECK_EQ(memcmp(&u,&before,sizeof u),0);
    CHECK_EQ(esk_esp32_uart_advance(&u,25,NULL,NULL),ES_OK);
    before=u;
    for(i=0;i<28;i++) if(!(UINT32_C(0x0e0480ff)&(1u<<i))) {
        CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,u.conf0|(1u<<i)),ES_EUNSUPPORTED);
        CHECK_EQ(memcmp(&u,&before,sizeof u),0);
    }
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,(u.conf0&~0x30u)|0x30u),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,u.conf0&~0x30u),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+20,4,0),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+36,4,1u<<23),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_read(&u,BASE+16,4,&value),ES_EUNSUPPORTED);CHECK_EQ(value,7);
    CHECK_EQ(esk_esp32_uart_read(&u,BASE+24,4,&value),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+4,4,0),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE,1,0),ES_EINVAL);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+1,4,0),ES_EINVAL);
    CHECK_EQ(memcmp(&u,&before,sizeof u),0);
    CHECK_EQ(esk_esp32_uart_configure(&u,0,0,0,0),ES_OK);
    CHECK_EQ(esk_esp32_uart_read(&u,BASE+4,4,&value),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_uart_advance(&u,0,NULL,NULL),ES_EUNSUPPORTED);
}
static void machine_routing_and_shared_state(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory b[2];uint32_t value;unsigned cpu;
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);
    CHECK_EQ(esk_esp32_machine_uart_configure(m,1,1,0,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_source(m,34,1),ES_EUNSUPPORTED);
    for(cpu=0;cpu<2;cpu++) {
        b[cpu]=esk_esp32_machine_bus(m,cpu);
        CHECK_EQ(b[cpu].write(b[cpu].ctx,0x3ff00104u+cpu*0x114u+34u*4u,4,1),ES_OK);
    }
    CHECK_EQ(b[0].write(b[0].ctx,BASE+12,4,256),ES_OK);
    CHECK_EQ(b[0].write(b[0].ctx,BASE+36,4,UINT32_C(0x81006060)),ES_OK);
    CHECK_EQ(esk_esp32_machine_uart_receive(m,0xa5,0),ES_OK);CHECK_EQ(esk_esp32_machine_uart_timeout(m),ES_OK);
    for(cpu=0;cpu<2;cpu++) {
        CHECK_EQ(b[cpu].read(b[cpu].ctx,0x3ff000f0u+cpu*12u,4,&value),ES_OK);CHECK_EQ(value&4u,4);
    }
    CHECK_EQ(b[1].read(b[1].ctx,BASE+8,4,&value),ES_OK);CHECK_EQ(value,256);
    CHECK_EQ(b[1].read(b[1].ctx,BASE,4,&value),ES_OK);CHECK_EQ(value,0xa5);
    CHECK_EQ(b[1].write(b[1].ctx,BASE+16,4,256),ES_OK);
    CHECK_EQ(b[0].read(b[0].ctx,BASE+8,4,&value),ES_OK);CHECK_EQ(value,0);
    for(cpu=0;cpu<2;cpu++) {
        CHECK_EQ(b[cpu].read(b[cpu].ctx,0x3ff000f0u+cpu*12u,4,&value),ES_OK);CHECK_EQ(value&4u,0);
    }
    CHECK_EQ(esk_esp32_machine_uart_configure(m,0,0,0,0),ES_OK);
    CHECK_EQ(esk_esp32_machine_source(m,34,1),ES_OK);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void fetched_enable_interrupt_and_retention(void)
{
    esk_alloc a;esk_esp32_machine *m=NULL;esk_xtensa_memory b;esk_xtensa_windows *w;
    const unsigned char code[]={0x32,0x62,0};unsigned event;esk_xtensa_fault fault;uint32_t value=0;
    esk_alloc_init(&a,NULL);CHECK_EQ(esk_esp32_machine_create(&a,&m),ES_OK);
    CHECK_EQ(esk_esp32_machine_uart_configure(m,1,1,0,0),ES_OK);b=esk_esp32_machine_bus(m,0);
    CHECK_EQ(b.write(b.ctx,BASE+36,4,0),ES_OK);CHECK_EQ(esk_esp32_machine_uart_receive(m,0xa5,0),ES_OK);
    CHECK_EQ(b.write(b.ctx,0x3ff0018c,4,19),ES_OK);
    CHECK_EQ(esk_esp32_fabric_load(esk_esp32_machine_fabric(m),0,0x40080000,code,sizeof code),ES_OK);
    CHECK_EQ(esk_esp32_machine_configure(m,0,0x40080000),ES_OK);w=esk_esp32_machine_core(m,0);
    w->irq.enabled=1;w->irq.intenable=1u<<19;w->irq.vectors[2]=0x40084000;w->cpu.a[2]=BASE+12;w->cpu.a[3]=1;
    CHECK_EQ(esk_esp32_machine_run_core(m,0,1),ES_OK);
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,0);CHECK_EQ(w->cpu.pc,0x40080003);
    CHECK(w->irq.lines&(1u<<19));
    CHECK_EQ(esk_esp32_machine_step(m,0,&event,&fault),ES_OK);CHECK_EQ(event,12);CHECK_EQ(w->irq.epc[2],0x40080003);
    CHECK_EQ(b.write(b.ctx,BASE+12,4,0),ES_OK);CHECK(!(w->irq.lines&(1u<<19)));
    CHECK_EQ(esk_esp32_machine_app_reset_configure(m,1),ES_OK);
    CHECK_EQ(b.read(b.ctx,BASE,4,&value),ES_OK);CHECK_EQ(value,0xa5);
    CHECK_EQ(b.fetch(b.ctx,BASE,3,&value),ES_EUNSUPPORTED);
    CHECK_EQ(b.compare_exchange(b.ctx,BASE,0,0,&value),ES_EUNSUPPORTED);
    esk_esp32_machine_destroy(m);CHECK_EQ(a.live_bytes,0);
}
static void reset_hold_and_masked_receive(void)
{
    esk_esp32_uart u,before;uint32_t value=0;
    esk_esp32_uart_init(&u);CHECK_EQ(esk_esp32_uart_configure(&u,1,1,1,0),ES_OK);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,0x08048010),ES_OK);
    CHECK_EQ(read_reg(&u,32),0x08048010u);before=u;
    CHECK_EQ(esk_esp32_uart_write(&u,BASE,4,1),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&u,&before,sizeof u),0);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,0x08008010),ES_OK);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE,4,255),ES_OK);CHECK_EQ(u.tx_count,1);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,0x08048010),ES_OK);CHECK_EQ(u.tx_count,0);
    CHECK_EQ(esk_esp32_uart_receive(&u,255,0),ES_OK);CHECK_EQ(read_reg(&u,0),31);
    before=u;CHECK_EQ(esk_esp32_uart_configure(&u,1,1,2,0),ES_EINVAL);CHECK_EQ(memcmp(&u,&before,sizeof u),0);
    CHECK_EQ(esk_esp32_uart_write(&u,BASE+32,4,0x080480d0),ES_OK);
    value=read_reg(&u,28);CHECK(value&(1u<<30));CHECK(value&(1u<<29));
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(frame_encoding_and_clock);RUN(fifos_flow_and_errors);RUN(fractional_clock_and_failures);RUN(machine_routing_and_shared_state);RUN(fetched_enable_interrupt_and_retention);RUN(reset_hold_and_masked_receive);
TEST_MAIN_END
