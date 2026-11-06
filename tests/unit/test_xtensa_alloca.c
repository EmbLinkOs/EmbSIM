/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"
static es_status execute(esk_xtensa_windows *w,uint32_t word,unsigned *event)
{
    unsigned char code[3];size_t used=99;esk_xtensa_fault fault;es_status st;
    code[0]=(unsigned char)(word&255u);code[1]=(unsigned char)((word>>8)&255u);code[2]=(unsigned char)((word>>16)&255u);
    st=esk_xtensa_window_execute(w,NULL,code,3,&used,event,&fault);
    CHECK_EQ(used,st==ES_OK && !*event ? 3u : 0u);
    if(st!=ES_OK) { CHECK_EQ(fault.kind,ESK_XT_UNSUPPORTED);CHECK_EQ(fault.pc,w->cpu.pc);CHECK_EQ(fault.opcode,word); }
    return st;
}
static uint32_t window_bit(unsigned base,unsigned distance) { return UINT32_C(1)<<((base-distance)&15u); }
static void caller_windows_and_exception_modes(void)
{
    unsigned base,pattern,mode,event,i;const uint32_t vectors[3]={0x40081000,0x40081400,0x40081800};
    for(base=0;base<16;base++) for(pattern=0;pattern<8;pattern++) for(mode=0;mode<8;mode++) {
        esk_xtensa_windows w;uint32_t old_ps=ESK_XT_PS_WOE|((mode&1u) ? ESK_XT_PS_EXCM : 0u)|((mode&2u) ? 32u : 0u)|((mode&4u) ? 64u : 0u);
        esk_xtensa_windows_init(&w,0x40080000,NULL);esk_xtensa_exception_configure(&w,vectors);
        w.base=base;w.start=window_bit(base,0);w.ps=old_ps;w.cpu.a[1]=0x3ffd0000;w.cpu.a[2]=0x3ffc0000;
        for(i=0;i<3;i++) if(pattern&(1u<<i)) w.start|=window_bit(base,i+1);
        w.excvaddr=0xdeadbeef;w.epc1=0x11223344;w.depc=0x55667788;
        CHECK_EQ(execute(&w,0x001210,&event),ES_OK);CHECK_EQ(w.base,base);CHECK_EQ(w.excvaddr,0xdeadbeefu);
        if(pattern) {
            CHECK_EQ(event,0);CHECK_EQ(w.cpu.pc,0x40080003);CHECK_EQ(w.cpu.a[1],0x3ffc0000);CHECK_EQ(w.ps,old_ps);
        } else {
            unsigned index=(mode&1u) ? 2u : (mode&2u) ? 1u : 0u;
            CHECK_EQ(event,7u+index);CHECK_EQ(w.cpu.pc,vectors[index]);CHECK_EQ(w.exccause,5);CHECK_EQ(w.cpu.a[1],0x3ffd0000);
            CHECK_EQ(w.ps,old_ps|ESK_XT_PS_EXCM);
            CHECK_EQ(w.epc1,(mode&1u) ? 0x11223344u : 0x40080000u);
            CHECK_EQ(w.depc,(mode&1u) ? 0x40080000u : 0x55667788u);
        }
    }
}
static void operands_and_failure_priority(void)
{
    unsigned dest,src,event;const uint32_t general[3]={0x40081000,0x40081400,0x40081800},windows[6]={0x40082000,0,0,0,0,0};
    for(dest=0;dest<16;dest++) for(src=0;src<16;src++) {
        esk_xtensa_windows w;unsigned i;uint32_t original[16];
        esk_xtensa_windows_init(&w,0x40080000,NULL);w.start|=window_bit(0,1);
        for(i=0;i<16;i++) original[i]=w.cpu.a[i]=0x12345678u+i;
        CHECK_EQ(execute(&w,0x001000u|(src<<8)|(dest<<4),&event),ES_OK);CHECK_EQ(event,0);
        for(i=0;i<16;i++) CHECK_EQ(w.cpu.a[i],i==dest ? original[src] : original[i]);
    }
    {
        esk_xtensa_windows w,before;
        esk_xtensa_windows_init(&w,0x40080000,NULL);w.cpu.a[1]=100;w.cpu.a[2]=50;before=w;
        CHECK_EQ(execute(&w,0x001210,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
        esk_xtensa_exception_configure(&w,general);
        CHECK_EQ(execute(&w,0x001210,&event),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.cpu.a[1],100);
        /* A handler supplies the missing caller window and retries through RFE.
         * This exercises retry, not a complete ABI alloca handler. */
        w.start|=window_bit(w.base,1);
        CHECK_EQ(execute(&w,0x003000,&event),ES_OK);CHECK_EQ(w.cpu.pc,0x40080000);
        CHECK_EQ(execute(&w,0x001210,&event),ES_OK);CHECK_EQ(w.cpu.a[1],50);CHECK_EQ(w.cpu.pc,0x40080003);
        esk_xtensa_windows_init(&w,0x40080000,windows);esk_xtensa_exception_configure(&w,general);
        w.start=7;w.cpu.a[1]=100;w.cpu.a[8]=50;
        CHECK_EQ(execute(&w,0x001810,&event),ES_OK);CHECK_EQ(event,1);CHECK_EQ(w.exccause,0);CHECK_EQ(w.epc1,0x40080000);
        CHECK_EQ(w.ar[1],100); /* Overflow precedes alloca; no SP move. */
    }
}
static void loop_commit(void)
{
    esk_xtensa_windows w;unsigned event;const uint32_t general[3]={0x40081000,0x40081400,0x40081800};
    esk_xtensa_windows_init(&w,0x40080000,NULL);w.start|=window_bit(0,1);
    w.cpu.a[2]=123;w.lbeg=0x4007fff0;w.lend=0x40080003;w.lcount=2;
    CHECK_EQ(execute(&w,0x001210,&event),ES_OK);CHECK_EQ(event,0);CHECK_EQ(w.cpu.a[1],123);
    CHECK_EQ(w.cpu.pc,w.lbeg);CHECK_EQ(w.lcount,1);
    esk_xtensa_windows_init(&w,0x40080000,NULL);esk_xtensa_exception_configure(&w,general);
    w.cpu.a[1]=456;w.cpu.a[2]=123;w.lbeg=0x4007fff0;w.lend=0x40080003;w.lcount=2;
    CHECK_EQ(execute(&w,0x001210,&event),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.cpu.a[1],456);
    CHECK_EQ(w.cpu.pc,general[0]);CHECK_EQ(w.lcount,2);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(caller_windows_and_exception_modes);RUN(operands_and_failure_priority);RUN(loop_commit);
TEST_MAIN_END
