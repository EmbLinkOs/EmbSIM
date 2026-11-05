/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"

static const uint32_t vectors[3]={0x40020000,0x40020040,0x40020080};
static es_status execute(esk_xtensa_windows *w,uint32_t word,unsigned *event)
{
    unsigned char code[3];size_t used=99;esk_xtensa_fault fault;es_status st;
    code[0]=(unsigned char)(word&255u);code[1]=(unsigned char)((word>>8)&255u);code[2]=(unsigned char)((word>>16)&255u);
    st=esk_xtensa_window_execute(w,NULL,code,3,&used,event,&fault);
    CHECK_EQ(used,st==ES_OK && !*event ? 3u : 0u);return st;
}
static void shifts(void)
{
    esk_xtensa_windows w,before;unsigned event,i;
    esk_xtensa_windows_init(&w,0x40000000,NULL);
    for (i=0;i<32;i++) {
        uint32_t value=0x89abcdefu;
        w.cpu.a[2]=i;CHECK_EQ(execute(&w,0x401200,&event),ES_OK);CHECK_EQ(w.sar,32u-i);
        w.cpu.a[7]=value;CHECK_EQ(execute(&w,0xa16700,&event),ES_OK);CHECK_EQ(w.cpu.a[6],value<<i);
        w.cpu.a[3]=i;CHECK_EQ(execute(&w,0x400300,&event),ES_OK);CHECK_EQ(w.sar,i);
        w.cpu.a[9]=value;CHECK_EQ(execute(&w,0x918090,&event),ES_OK);CHECK_EQ(w.cpu.a[8],value>>i);
        w.cpu.a[11]=value;CHECK_EQ(execute(&w,0xb1a0b0,&event),ES_OK);
        CHECK_EQ(w.cpu.a[10],(value>>i)|(i ? UINT32_MAX<<(32u-i) : 0u));
    }
    for (i=0;i<=32;i++) {
        uint64_t joined=UINT64_C(0x1234567889abcdef);
        w.cpu.a[3]=i;CHECK_EQ(execute(&w,0x130330,&event),ES_OK);
        w.cpu.a[13]=0x12345678;w.cpu.a[14]=0x89abcdef;
        CHECK_EQ(execute(&w,0x81cde0,&event),ES_OK);CHECK_EQ(w.cpu.a[12],(uint32_t)(joined>>i));
    }
    w.cpu.a[3]=32;CHECK_EQ(execute(&w,0x130330,&event),ES_OK);
    CHECK_EQ(execute(&w,0x918090,&event),ES_OK);CHECK_EQ(w.cpu.a[8],0);
    CHECK_EQ(execute(&w,0xb1a0b0,&event),ES_OK);CHECK_EQ(w.cpu.a[10],UINT32_MAX);
    CHECK_EQ(execute(&w,0xa16700,&event),ES_OK);CHECK_EQ(w.cpu.a[6],0x89abcdefu);
    w.cpu.a[3]=0;CHECK_EQ(execute(&w,0x130330,&event),ES_OK);
    CHECK_EQ(execute(&w,0xa16700,&event),ES_OK);CHECK_EQ(w.cpu.a[6],0);
    w.cpu.a[3]=127;CHECK_EQ(execute(&w,0x130330,&event),ES_OK);CHECK_EQ(w.sar,63);
    before=w;CHECK_EQ(execute(&w,0x918090,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    w.cpu.a[4]=64;CHECK_EQ(execute(&w,0x610340,&event),ES_OK);CHECK_EQ(w.sar,0);CHECK_EQ(w.cpu.a[4],63);
    CHECK_EQ(execute(&w,0x030320,&event),ES_OK);CHECK_EQ(w.cpu.a[2],0);
    w.cpu.a[4]=7;CHECK_EQ(execute(&w,0x402400,&event),ES_OK);CHECK_EQ(w.sar,24);
    w.cpu.a[5]=7;CHECK_EQ(execute(&w,0x403500,&event),ES_OK);CHECK_EQ(w.sar,8);
    CHECK_EQ(execute(&w,0x404f10,&event),ES_OK);CHECK_EQ(w.sar,31);
}
static void nested_exceptions(void)
{
    esk_xtensa_windows w,before;unsigned event;
    esk_xtensa_windows_init(&w,0x40001234,NULL);before=w;
    CHECK_EQ(execute(&w,0x005000,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    esk_xtensa_exception_configure(&w,vectors);w.excvaddr=0x12345678;w.cpu.a[2]=42;
    CHECK_EQ(execute(&w,0x005000,&event),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.cpu.pc,vectors[0]);
    CHECK_EQ(w.epc1,0x40001234u);CHECK_EQ(w.exccause,1);CHECK_EQ(w.excvaddr,0x12345678u);
    CHECK_EQ(w.cpu.a[2],42);CHECK_EQ(w.ps&ESK_XT_PS_EXCM,ESK_XT_PS_EXCM);
    CHECK_EQ(execute(&w,0x005000,&event),ES_OK);CHECK_EQ(event,9);CHECK_EQ(w.cpu.pc,vectors[2]);
    CHECK_EQ(w.depc,vectors[0]);CHECK_EQ(w.epc1,0x40001234u);
    w.cpu.a[3]=w.depc+3;CHECK_EQ(execute(&w,0x13c030,&event),ES_OK); /* WSR DEPC */
    CHECK_EQ(execute(&w,0x003200,&event),ES_OK);CHECK_EQ(w.cpu.pc,vectors[0]+3u);
    CHECK_EQ(w.ps&ESK_XT_PS_EXCM,ESK_XT_PS_EXCM);
    w.cpu.a[6]=w.epc1+3;CHECK_EQ(execute(&w,0x13b160,&event),ES_OK);
    CHECK_EQ(execute(&w,0x003000,&event),ES_OK);CHECK_EQ(w.cpu.pc,0x40001237u);
    CHECK_EQ(w.ps&ESK_XT_PS_EXCM,0);
    w.ps|=32u;CHECK_EQ(execute(&w,0x005000,&event),ES_OK);CHECK_EQ(event,8);CHECK_EQ(w.cpu.pc,vectors[1]);
    /* Explicit address-bearing injection preserves EPC1 during a double fault. */
    { uint32_t epc=w.epc1;
        CHECK_EQ(esk_xtensa_raise_exception(&w,9,0xdeadbeef,1,&event),ES_OK);
        CHECK_EQ(event,9);CHECK_EQ(w.epc1,epc);CHECK_EQ(w.exccause,9);CHECK_EQ(w.excvaddr,0xdeadbeefu);
    }
    before=w;CHECK_EQ(esk_xtensa_raise_exception(&w,64,0,0,&event),ES_EINVAL);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    before=w;CHECK_EQ(execute(&w,0x0a2340,&event),ES_EUNSUPPORTED);CHECK_EQ(event,0);
    CHECK_EQ(memcmp(&w,&before,sizeof w),0); /* valid, unimplemented ADD.S is not an illegal opcode */
}
static void privilege_and_sync(void)
{
    esk_xtensa_windows w;unsigned event;uint32_t old;
    esk_xtensa_windows_init(&w,0x40000000,NULL);w.ps|=3u;old=w.ps;
    CHECK_EQ(execute(&w,0x006750,&event),ES_OK);CHECK_EQ(w.cpu.a[5],old);CHECK_EQ(w.ps&15u,7);
    CHECK_EQ(execute(&w,0x0020c0,&event),ES_OK);CHECK_EQ(execute(&w,0x002000,&event),ES_OK);
    CHECK_EQ(execute(&w,0x002020,&event),ES_OK);
    esk_xtensa_exception_configure(&w,vectors);w.ps|=64u|32u;
    old=w.cpu.pc;CHECK_EQ(execute(&w,0x006750,&event),ES_OK);CHECK_EQ(event,8);
    CHECK_EQ(w.epc1,old);CHECK_EQ(w.exccause,8);CHECK_EQ(w.cpu.pc,vectors[1]);
    /* User SAR access is legal and does not become a privilege exception. */
    w.ps&=~ESK_XT_PS_EXCM;w.cpu.a[3]=5;
    CHECK_EQ(execute(&w,0x130330,&event),ES_OK);CHECK_EQ(event,0);CHECK_EQ(w.sar,5);
}
static void operand_windows_and_illegal(void)
{
    esk_xtensa_windows w,before;unsigned event;
    static const uint32_t win[6]={0x40010000,0x40010040,0x40010080,0x400100c0,0x40010100,0x40010140};
    esk_xtensa_windows_init(&w,0x40000000,win);w.start=7;
    CHECK_EQ(execute(&w,0x404f10,&event),ES_OK);CHECK_EQ(event,0);CHECK_EQ(w.sar,31);
    CHECK_EQ(execute(&w,0x401c00,&event),ES_OK);CHECK_EQ(event,1); /* SSL a12 references a high group */
    CHECK_EQ(w.epc1,0x40000003u);CHECK_EQ(w.sar,31);
    esk_xtensa_windows_init(&w,0x40001234,NULL);esk_xtensa_exception_configure(&w,vectors);
    CHECK_EQ(execute(&w,0,&event),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.exccause,0);CHECK_EQ(w.epc1,0x40001234u);
    CHECK_EQ(execute(&w,0xf06d,&event),ES_OK);CHECK_EQ(event,9);CHECK_EQ(w.exccause,0);CHECK_EQ(w.depc,vectors[0]);
    before=w;CHECK_EQ(execute(&w,0x0a2340,&event),ES_EUNSUPPORTED);CHECK_EQ(event,0);
    CHECK_EQ(memcmp(&w,&before,sizeof w),0);
}
static es_status code_read(void *ctx,uint32_t address,unsigned bytes,uint32_t *value)
{
    const unsigned char *data=(const unsigned char *)ctx;unsigned i;uint32_t offset;
    if (address<0x1000 || bytes>4) return ES_EINVAL;
    offset=address-0x1000;
    if (offset>=256 || bytes>256-offset) return ES_EINVAL;
    *value=0;for(i=0;i<bytes;i++) *value|=(uint32_t)data[offset+i]<<(8*i);
    return ES_OK;
}
static void fetched_syscall_program(void)
{
    /* Assembled: movi.n a2,7; syscall; addi.n a2,a2,1; nop.n.
     * Handler: rsr a3,epc1; addi.n a3,a3,3; wsr a3,epc1; rfe. */
    static const unsigned char program[]={0x0c,0x72,0,0x50,0,0x1b,0x22,0x3d,0xf0};
    static const unsigned char handler[]={0x30,0xb1,3,0x3b,0x33,0x30,0xb1,0x13,0,0x30,0};
    static const uint32_t evectors[3]={0x1040,0x1080,0x10c0};
    unsigned char data[256]={0};esk_xtensa_windows w;esk_xtensa_memory bus={data,code_read,NULL,NULL,NULL,NULL};
    esk_xtensa_fault fault;unsigned event,steps=0,entries=0;
    memcpy(data,program,sizeof program);memcpy(data+64,handler,sizeof handler);
    esk_xtensa_windows_init(&w,0x1000,NULL);esk_xtensa_exception_configure(&w,evectors);
    while(w.cpu.pc!=0x1007 && steps<20) {
        CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);
        if (event) { CHECK_EQ(event,7);entries++; }
        steps++;
    }
    CHECK_EQ(steps,7);CHECK_EQ(entries,1);CHECK_EQ(w.cpu.a[2],8);CHECK_EQ(w.cpu.pc,0x1007u);
    CHECK_EQ(w.epc1,0x1005u);CHECK_EQ(w.ps&ESK_XT_PS_EXCM,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;
    RUN(shifts);RUN(nested_exceptions);RUN(privilege_and_sync);RUN(operand_windows_and_illegal);RUN(fetched_syscall_program);
TEST_MAIN_END
