/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"
static es_status execute(esk_xtensa_windows *w,uint32_t word,unsigned *event)
{
    unsigned char code[3];size_t used=99;esk_xtensa_fault fault;es_status st;
    code[0]=(unsigned char)(word&255u);code[1]=(unsigned char)((word>>8)&255u);code[2]=(unsigned char)((word>>16)&255u);
    st=esk_xtensa_window_execute(w,NULL,code,3,&used,event,&fault);
    CHECK_EQ(used,st==ES_OK && !*event ? 3u : 0u);return st;
}
static void reset_fields_and_undefined_policy(void)
{
    unsigned selector,i;
    for(selector=0;selector<2;selector++) {
        esk_xtensa_windows w,before;
        esk_xtensa_windows_init(&w,0x1234,NULL);w.base=5;w.start=0xabc;w.lcount=23;w.lbeg=0x12345678;w.lend=0x87654321;
        w.sar=19;w.scompare1=0xaabbccdd;w.epc1=99;w.depc=88;
        for(i=0;i<64;i++) w.ar[i]=0x100u+i;
        for(i=0;i<16;i++) w.cpu.a[i]=0x200u+i;
        w.irq.enabled=1;w.irq.intenable=0x12345678;w.irq.ccount=987;w.irq.compare[0]=123;
        w.irq.latched=UINT32_MAX;w.irq.lines=UINT32_MAX;w.irq.waiting=1;w.irq.epc[2]=44;w.irq.excsave[2]=33;
        before=w;CHECK_EQ(esk_xtensa_lx6_reset(&w,selector),ES_OK);
        CHECK_EQ(w.cpu.pc,selector ? 0x40000400u : 0x50000000u);CHECK_EQ(w.ps,0x1f);CHECK_EQ(w.lcount,0);
        CHECK_EQ(w.vecbase,0x40000000);CHECK_EQ(w.vecbase_enabled,1);CHECK_EQ(w.exceptions_enabled,1);CHECK_EQ(w.irq.enabled,1);
        CHECK_EQ(w.base,before.base);CHECK_EQ(w.start,before.start);CHECK_EQ(w.lbeg,before.lbeg);CHECK_EQ(w.lend,before.lend);
        CHECK_EQ(w.sar,before.sar);CHECK_EQ(w.scompare1,before.scompare1);CHECK_EQ(w.epc1,before.epc1);CHECK_EQ(w.depc,before.depc);
        CHECK_EQ(w.irq.intenable,before.irq.intenable);CHECK_EQ(w.irq.ccount,before.irq.ccount);CHECK_EQ(w.irq.compare[0],123);
        CHECK_EQ(w.irq.epc[2],44);CHECK_EQ(w.irq.excsave[2],33);CHECK_EQ(w.irq.waiting,0);CHECK_EQ(esk_xtensa_irq_pending(&w),0);
        for(i=0;i<16;i++) { CHECK_EQ(w.cpu.a[i],before.cpu.a[i]);CHECK_EQ(w.ar[(20+i)&63u],before.cpu.a[i]); }
        before=w;CHECK_EQ(esk_xtensa_lx6_reset(&w,2),ES_EINVAL);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    }
}
static void register_access_and_rejection(void)
{
    unsigned reg,k,event;const uint32_t bases[]={0,0x40080000,0x50000000,0xfffffc00};
    for(reg=0;reg<16;reg++) for(k=0;k<sizeof bases/sizeof bases[0];k++) {
        esk_xtensa_windows w,before;
        esk_xtensa_windows_init(&w,0x40081000,NULL);before=w;
        CHECK_EQ(execute(&w,0x03e700u|(reg<<4),&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&before,&w,sizeof w),0);
        CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);w.cpu.a[reg]=bases[k];
        CHECK_EQ(execute(&w,0x13e700u|(reg<<4),&event),ES_OK);CHECK_EQ(event,0);CHECK_EQ(w.vecbase,bases[k]);
        w.cpu.a[reg]=0;
        CHECK_EQ(execute(&w,0x03e700u|(reg<<4),&event),ES_OK);CHECK_EQ(w.cpu.a[reg],bases[k]);
        w.cpu.a[reg]=0x40090000;
        CHECK_EQ(execute(&w,0x61e700u|(reg<<4),&event),ES_OK);CHECK_EQ(w.cpu.a[reg],bases[k]);CHECK_EQ(w.vecbase,0x40090000);
    }
    for(reg=0;reg<16;reg++) for(k=0;k<10;k++) {
        esk_xtensa_windows w,before;unsigned exchange;
        esk_xtensa_windows_init(&w,0x40080000,NULL);CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);
        w.cpu.a[reg]=0x40080000u|(1u<<k);before=w;
        for(exchange=0;exchange<2;exchange++) {
            CHECK_EQ(execute(&w,(exchange ? 0x61e700u : 0x13e700u)|(reg<<4),&event),ES_EUNSUPPORTED);
            CHECK_EQ(memcmp(&w,&before,sizeof w),0);
        }
    }
}
static void relocated_dispatch(void)
{
    unsigned n,event,i;const uint32_t base=0x40088000;uint32_t junk[6]={1,2,3,4,5,6};
    const unsigned inputs[]={0,19,23,24,26,14},levels[]={1,2,3,4,5,7};
    for(n=0;n<3;n++) {
        esk_xtensa_windows w;uint32_t general[3]={1,2,3};
        esk_xtensa_windows_init(&w,0x40081234,NULL);CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);
        CHECK_EQ(esk_xtensa_vecbase_configure(&w,base),ES_OK);esk_xtensa_exception_configure(&w,general);
        w.ps=n==2 ? ESK_XT_PS_EXCM : n==1 ? 32u : 0u;
        CHECK_EQ(esk_xtensa_raise_exception(&w,1,0,0,&event),ES_OK);CHECK_EQ(event,7u+n);
        CHECK_EQ(w.cpu.pc,base+(n==2 ? 0x3c0u : 0x300u+n*64u));
    }
    for(n=1;n<=3;n++) {
        esk_xtensa_windows w;
        esk_xtensa_windows_init(&w,0x40081234,NULL);CHECK_EQ(esk_xtensa_vecbase_configure(&w,base),ES_OK);
        w.start=3u|(n==1 ? 4u : n==2 ? 8u : 0u);w.vectors[(n-1)*2]=99;
        CHECK_EQ(execute(&w,0x00a042,&event),ES_OK);CHECK_EQ(event,n*2u-1u);CHECK_EQ(w.cpu.pc,base+(n-1u)*128u);
        esk_xtensa_windows_init(&w,0x40081234,NULL);CHECK_EQ(esk_xtensa_vecbase_configure(&w,base),ES_OK);
        w.base=3;w.start=8;w.cpu.a[0]=(n<<30)|0x5678u;
        /* Three-byte RETW encoding. */
        CHECK_EQ(execute(&w,0x000090,&event),ES_OK);CHECK_EQ(event,n*2u);CHECK_EQ(w.cpu.pc,base+(n*2u-1u)*64u);
    }
    for(i=0;i<sizeof inputs/sizeof inputs[0];i++) {
        esk_xtensa_windows w;
        esk_xtensa_windows_init(&w,0x40081234,NULL);CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);
        CHECK_EQ(esk_xtensa_vecbase_configure(&w,base),ES_OK);CHECK_EQ(esk_xtensa_irq_configure(&w,junk),ES_OK);
        w.ps=ESK_XT_PS_WOE;w.irq.intenable=1u<<inputs[i];w.irq.vectors[levels[i]]=99;
        CHECK_EQ(esk_xtensa_irq_line(&w,inputs[i],1),ES_OK);CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);
        CHECK_EQ(event,10u+levels[i]);CHECK_EQ(w.cpu.pc,base+(levels[i]==1 ? 0x300u : 0x180u+(levels[i]-2u)*64u));
    }
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(reset_fields_and_undefined_policy);RUN(register_access_and_rejection);RUN(relocated_dispatch);
TEST_MAIN_END
