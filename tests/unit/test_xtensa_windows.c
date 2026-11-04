/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"

static const uint32_t vectors[6]={0x40010000,0x40010040,0x40010080,0x400100c0,0x40010100,0x40010140};
static es_status execute_bus(esk_xtensa_windows *w,const esk_xtensa_memory *memory,uint32_t word,unsigned bytes,unsigned *event)
{
    unsigned char code[3]; size_t used=99; esk_xtensa_fault fault; es_status st;
    code[0]=(unsigned char)(word&255u); code[1]=(unsigned char)((word>>8)&255u);
    code[2]=(unsigned char)((word>>16)&255u);
    st=esk_xtensa_window_execute(w,memory,code,bytes,&used,event,&fault);
    CHECK_EQ(used,st==ES_OK && !*event ? bytes : 0u);
    return st;
}
static es_status execute(esk_xtensa_windows *w,uint32_t word,unsigned bytes,unsigned *event)
{
    return execute_bus(w,NULL,word,bytes,event);
}
static void call_entry_return(void)
{
    unsigned n,base;
    for (base=0;base<16;base++) for (n=1;n<=3;n++) {
        esk_xtensa_windows w; unsigned event; uint32_t caller_sp=0x3ffb0100;
        esk_xtensa_windows_init(&w,0x40000000,vectors);
        w.base=base; w.start=1u<<base; w.cpu.a[0]=0x12345678; w.cpu.a[1]=caller_sp;
        w.cpu.a[n*4+2]=7;
        CHECK_EQ(execute(&w,0x45u|(n<<4),3,&event),ES_OK); CHECK_EQ(event,0);
        CHECK_EQ(w.base,base); CHECK_EQ(w.cpu.pc,0x40000008u);
        CHECK_EQ(w.cpu.a[n*4],(n<<30)|3u); CHECK_EQ((w.ps>>16)&3u,n);
        CHECK_EQ(execute(&w,0x004136,3,&event),ES_OK); /* entry a1,32 */
        CHECK_EQ(w.base,(base+n)&15u); CHECK_EQ(w.cpu.a[1],caller_sp-32u);
        CHECK_EQ(w.cpu.a[0],(n<<30)|3u); CHECK_EQ(w.cpu.a[2],7);
        CHECK_EQ(execute(&w,0x01c222,3,&event),ES_OK); CHECK_EQ(w.cpu.a[2],8);
        CHECK_EQ(execute(&w,0xf01d,2,&event),ES_OK); CHECK_EQ(event,0);
        CHECK_EQ(w.cpu.pc,0x40000003u); CHECK_EQ(w.base,base);
        CHECK_EQ(w.start,1u<<base); CHECK_EQ(w.cpu.a[0],0x12345678);
        CHECK_EQ(w.cpu.a[1],caller_sp); CHECK_EQ(w.cpu.a[n*4+2],8);
    }
}
static void overflow_retry(void)
{
    esk_xtensa_windows w; unsigned event;
    esk_xtensa_windows_init(&w,0x40000000,vectors); w.start=1u|4u|8u;
    CHECK_EQ(execute(&w,0x00a012,3,&event),ES_OK); CHECK_EQ(event,0); /* movi a1,0 */
    CHECK_EQ(execute(&w,0x00a042,3,&event),ES_OK); CHECK_EQ(event,0); /* movi a4,0 */
    w.cpu.a[8]=123;
    CHECK_EQ(execute(&w,0x00a082,3,&event),ES_OK); CHECK_EQ(event,1); /* touch a8 */
    CHECK_EQ(w.base,2); CHECK_EQ(w.cpu.pc,vectors[0]); CHECK_EQ(w.epc1,0x40000006u);
    CHECK_EQ(w.cpu.a[0],123); CHECK_EQ(w.ps&ESK_XT_PS_EXCM,ESK_XT_PS_EXCM);
    CHECK_EQ(w.start,13);
    CHECK_EQ(execute(&w,0x003400,3,&event),ES_OK); /* RFWO */
    CHECK_EQ(w.base,0); CHECK_EQ(w.start,9); CHECK_EQ(w.cpu.pc,0x40000006u);
    CHECK_EQ(w.ps&ESK_XT_PS_EXCM,0); CHECK_EQ(w.cpu.a[8],123);
    CHECK_EQ(execute(&w,0x00a082,3,&event),ES_OK); CHECK_EQ(event,0); CHECK_EQ(w.cpu.a[8],0);
    CHECK_EQ(execute(&w,0x00a0c2,3,&event),ES_OK); CHECK_EQ(event,5); /* touch a12 */
    CHECK_EQ(w.base,3); CHECK_EQ(w.cpu.pc,vectors[4]);
}
static void underflow_retry(void)
{
    esk_xtensa_windows w; unsigned event;
    esk_xtensa_windows_init(&w,0x40001234,vectors); w.base=3; w.start=8;
    w.cpu.a[0]=0x80005678u;
    CHECK_EQ(execute(&w,0xf01d,2,&event),ES_OK); CHECK_EQ(event,4);
    CHECK_EQ(w.base,1); CHECK_EQ(w.cpu.pc,vectors[3]); CHECK_EQ(w.epc1,0x40001234u);
    CHECK_EQ((w.ps>>8)&15u,3); CHECK_EQ(w.start,8);
    w.cpu.a[2]=0xdeadbeef; /* handler restores one caller register */
    CHECK_EQ(execute(&w,0x003500,3,&event),ES_OK); CHECK_EQ(event,0);
    CHECK_EQ(w.base,3); CHECK_EQ(w.start,10); CHECK_EQ(w.cpu.pc,0x40001234u);
    CHECK_EQ(w.ps&ESK_XT_PS_EXCM,0);
    CHECK_EQ(execute(&w,0xf01d,2,&event),ES_OK); CHECK_EQ(event,0);
    CHECK_EQ(w.base,1); CHECK_EQ(w.start,2); CHECK_EQ(w.cpu.pc,0x40005678u);
    CHECK_EQ(w.cpu.a[2],0xdeadbeefu);
}
static void special_and_invalid(void)
{
    esk_xtensa_windows w,before; unsigned event;
    esk_xtensa_windows_init(&w,0x40000000,vectors);
    w.cpu.a[3]=2; CHECK_EQ(execute(&w,0x134830,3,&event),ES_OK); CHECK_EQ(w.base,2);
    CHECK_EQ(execute(&w,0x034820,3,&event),ES_OK); CHECK_EQ(w.cpu.a[2],2);
    CHECK_EQ(execute(&w,0x4080d0,3,&event),ES_OK); CHECK_EQ(w.base,15); /* rotw -3 */
    w.cpu.a[3]=0xabcd; CHECK_EQ(execute(&w,0x134930,3,&event),ES_OK); CHECK_EQ(w.start,0xabcd);
    w.ps=ESK_XT_PS_WOE|64u; before=w;
    CHECK_EQ(execute(&w,0x408000,3,&event),ES_EUNSUPPORTED); CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    w.ps=0; before=w; CHECK_EQ(execute(&w,0x004136,3,&event),ES_EUNSUPPORTED);
    CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    w.ps=ESK_XT_PS_WOE; w.cpu.a[0]=0; before=w;
    CHECK_EQ(execute(&w,0xf01d,2,&event),ES_EUNSUPPORTED); CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    before=w; CHECK_EQ(execute(&w,0xdead00,3,&event),ES_EUNSUPPORTED);
    CHECK_EQ(memcmp(&w,&before,sizeof w),0);
}
static void hardware_loops(void)
{
    esk_xtensa_windows w; unsigned event;
    esk_xtensa_windows_init(&w,0x40000001,vectors); w.cpu.a[2]=3;
    CHECK_EQ(execute(&w,0x018276,3,&event),ES_OK); /* loop a2, PC+5 */
    CHECK_EQ(w.lbeg,0x40000004u); CHECK_EQ(w.lend,0x40000006u); CHECK_EQ(w.lcount,2);
    CHECK_EQ(execute(&w,0xf03d,2,&event),ES_OK); CHECK_EQ(w.cpu.pc,w.lbeg); CHECK_EQ(w.lcount,1);
    CHECK_EQ(execute(&w,0xf03d,2,&event),ES_OK); CHECK_EQ(w.cpu.pc,w.lbeg); CHECK_EQ(w.lcount,0);
    CHECK_EQ(execute(&w,0xf03d,2,&event),ES_OK); CHECK_EQ(w.cpu.pc,w.lend);
    w.cpu.pc=0x40000001; w.cpu.a[3]=0;
    CHECK_EQ(execute(&w,0x019376,3,&event),ES_OK); /* loopnez a3 */
    CHECK_EQ(w.cpu.pc,0x40000006u); CHECK_EQ(w.lcount,UINT32_MAX);
    w.cpu.pc=0x40000001; w.cpu.a[4]=0x80000000u;
    CHECK_EQ(execute(&w,0x01a476,3,&event),ES_OK); /* loopgtz a4 */
    CHECK_EQ(w.cpu.pc,0x40000006u);
    w.cpu.pc=0x40000001; w.cpu.a[2]=0;
    CHECK_EQ(execute(&w,0x018276,3,&event),ES_OK); CHECK_EQ(w.cpu.pc,w.lbeg); CHECK_EQ(w.lcount,UINT32_MAX);
    /* Exception handlers are not controlled by an interrupted loop. */
    w.cpu.pc=w.lend-2; w.lcount=2; w.ps|=ESK_XT_PS_EXCM;
    CHECK_EQ(execute(&w,0xf03d,2,&event),ES_OK); CHECK_EQ(w.cpu.pc,w.lend); CHECK_EQ(w.lcount,2);
    /* Taken branch targets fallthrough PC exactly: it must still exit loop. */
    w.ps&=~ESK_XT_PS_EXCM; w.lbeg=0x40000004; w.lend=0x40000010; w.lcount=5;
    w.cpu.pc=w.lend-3; w.cpu.a[2]=1;
    CHECK_EQ(execute(&w,0xfff256,3,&event),ES_OK); CHECK_EQ(w.cpu.pc,w.lend); CHECK_EQ(w.lcount,5);
    w.cpu.pc=w.lend-3; w.cpu.a[2]=0;
    CHECK_EQ(execute(&w,0xfff256,3,&event),ES_OK); CHECK_EQ(w.cpu.pc,w.lbeg); CHECK_EQ(w.lcount,4);
}
typedef struct test_memory { uint32_t value; unsigned reads,writes,fail; } test_memory;
static es_status mem_read(void *ctx,uint32_t address,unsigned bytes,uint32_t *value)
{
    test_memory *m=(test_memory *)ctx; m->reads++;
    if (m->fail || address!=128 || bytes!=4) return ES_EINVAL;
    *value=m->value; return ES_OK;
}
static es_status mem_write(void *ctx,uint32_t address,unsigned bytes,uint32_t value)
{
    test_memory *m=(test_memory *)ctx; m->writes++;
    if (m->fail || address!=128 || bytes!=4) return ES_EINVAL;
    m->value=value; return ES_OK;
}
static void exception_memory(void)
{
    esk_xtensa_windows w,before; unsigned event;
    test_memory m={0x89abcdef,0,0,0}; esk_xtensa_memory bus={&m,mem_read,mem_write};
    esk_xtensa_windows_init(&w,0x40000000,vectors); w.cpu.a[3]=192; before=w;
    CHECK_EQ(execute_bus(&w,&bus,0x090320,3,&event),ES_EUNSUPPORTED); /* L32E only in EXCM */
    CHECK_EQ(m.reads,0); CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    w.ps|=ESK_XT_PS_EXCM;
    CHECK_EQ(execute_bus(&w,&bus,0x090320,3,&event),ES_OK); CHECK_EQ(w.cpu.a[2],0x89abcdefu);
    w.cpu.a[6]=0x12345678; w.cpu.a[7]=132;
    CHECK_EQ(execute_bus(&w,&bus,0x49f760,3,&event),ES_OK); CHECK_EQ(m.value,0x12345678u);
    before=w; m.fail=1;
    CHECK_EQ(execute_bus(&w,&bus,0x49f760,3,&event),ES_EINVAL);
    CHECK_EQ(memcmp(&w,&before,sizeof w),0); CHECK_EQ(m.value,0x12345678u);
    CHECK_EQ(execute_bus(&w,&bus,0x090320,3,&event),ES_EINVAL);
    CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    w.cpu.a[7]=133; before=w; { unsigned writes=m.writes;
        CHECK_EQ(execute_bus(&w,&bus,0x49f760,3,&event),ES_EINVAL);
        CHECK_EQ(m.writes,writes); CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    }
}
TEST_MAIN_BEGIN
    (void)t_str_eq;
    RUN(call_entry_return); RUN(overflow_retry); RUN(underflow_retry); RUN(special_and_invalid); RUN(hardware_loops); RUN(exception_memory);
TEST_MAIN_END
