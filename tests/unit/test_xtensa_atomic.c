/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"
/* Serialized fixture bus only: the provider's callback contract, not this
 * fixture, supplies indivisibility for real concurrent bus agents. */
typedef struct bus_state { uint32_t value,address,expected,desired;unsigned calls,reads,writes,fail; } bus_state;
static es_status read_mem(void *ctx,uint32_t address,unsigned bytes,uint32_t *value)
{
    bus_state *b=(bus_state *)ctx;(void)address;(void)bytes;b->reads++;*value=b->value;return ES_OK;
}
static es_status write_mem(void *ctx,uint32_t address,unsigned bytes,uint32_t value)
{
    bus_state *b=(bus_state *)ctx;(void)address;(void)bytes;b->writes++;b->value=value;return ES_OK;
}
static es_status cas(void *ctx,uint32_t address,uint32_t expected,uint32_t desired,uint32_t *observed)
{
    bus_state *b=(bus_state *)ctx;b->calls++;b->address=address;b->expected=expected;b->desired=desired;
    *observed=0xdeadbeef;
    if(address!=256) return ES_EUNSUPPORTED;
    if(b->fail) return ES_EINVAL;
    *observed=b->value;if(b->value==expected) b->value=desired;return ES_OK;
}
static es_status execute(esk_xtensa_windows *w,const esk_xtensa_memory *bus,uint32_t word,unsigned *event,esk_xtensa_fault *f)
{
    unsigned char code[3];size_t used=99;es_status st;
    code[0]=(unsigned char)(word&255u);code[1]=(unsigned char)((word>>8)&255u);code[2]=(unsigned char)((word>>16)&255u);
    st=esk_xtensa_window_execute(w,bus,code,3,&used,event,f);
    CHECK_EQ(used,st==ES_OK && !*event ? 3u : 0u);return st;
}
static void compare_and_alias(void)
{
    bus_state b={0};esk_xtensa_memory bus={&b,read_mem,write_mem,cas,NULL};
    esk_xtensa_windows one,two;unsigned event;esk_xtensa_fault f;
    esk_xtensa_windows_init(&one,0x40000000,NULL);esk_xtensa_windows_init(&two,0x40001000,NULL);
    one.cpu.a[3]=256;one.cpu.a[2]=1;two.cpu.a[3]=256;two.cpu.a[2]=2;
    CHECK_EQ(execute(&one,&bus,0x00e322,&event,&f),ES_OK);CHECK_EQ(one.cpu.a[2],0);CHECK_EQ(b.value,1);
    CHECK_EQ(execute(&two,&bus,0x00e322,&event,&f),ES_OK);CHECK_EQ(two.cpu.a[2],1);CHECK_EQ(b.value,1);
    two.scompare1=1;two.cpu.a[2]=2;
    CHECK_EQ(execute(&two,&bus,0x00e322,&event,&f),ES_OK);CHECK_EQ(two.cpu.a[2],1);CHECK_EQ(b.value,2);
    CHECK_EQ(b.calls,3);CHECK_EQ(b.reads,0);CHECK_EQ(b.writes,0);
    two.scompare1=2;two.cpu.a[3]=256;
    CHECK_EQ(execute(&two,&bus,0x00e332,&event,&f),ES_OK);CHECK_EQ(b.value,256);CHECK_EQ(two.cpu.a[3],2);
    CHECK_EQ(b.address,256);CHECK_EQ(b.expected,2);CHECK_EQ(b.desired,256);
    two.cpu.a[3]=0xfffffd04;two.cpu.a[2]=7;two.scompare1=256;
    CHECK_EQ(execute(&two,&bus,0xffe322,&event,&f),ES_OK);CHECK_EQ(b.address,256);CHECK_EQ(b.value,7);
    bus.read=NULL;bus.write=NULL;two.cpu.a[3]=256;two.cpu.a[2]=8;two.scompare1=7;
    CHECK_EQ(execute(&two,&bus,0x00e322,&event,&f),ES_OK);CHECK_EQ(two.cpu.a[2],7);CHECK_EQ(b.value,8);
    CHECK_EQ(b.reads,0);CHECK_EQ(b.writes,0);
}
static void failures_and_window_priority(void)
{
    bus_state b={9,0,0,0,0,0,0,0};esk_xtensa_memory bus={&b,read_mem,write_mem,cas,NULL};
    esk_xtensa_windows w,before;unsigned event,calls;esk_xtensa_fault f;
    esk_xtensa_windows_init(&w,0x40000000,NULL);w.cpu.a[3]=257;w.cpu.a[2]=5;w.scompare1=9;before=w;
    CHECK_EQ(execute(&w,&bus,0x00e322,&event,&f),ES_EINVAL);CHECK_EQ(f.kind,ESK_XT_ALIGN);
    CHECK_EQ(b.calls,0);CHECK_EQ(memcmp(&w,&before,sizeof w),0);CHECK_EQ(b.value,9);
    w.cpu.a[3]=256;before=w;bus.compare_exchange=NULL;
    CHECK_EQ(execute(&w,&bus,0x00e322,&event,&f),ES_EUNSUPPORTED);CHECK_EQ(b.reads,0);CHECK_EQ(b.writes,0);
    CHECK_EQ(memcmp(&w,&before,sizeof w),0);bus.compare_exchange=cas;b.fail=1;
    CHECK_EQ(execute(&w,&bus,0x00e322,&event,&f),ES_EINVAL);CHECK_EQ(f.kind,ESK_XT_ATOMIC);
    CHECK_EQ(memcmp(&w,&before,sizeof w),0);CHECK_EQ(b.value,9);b.fail=0;
    w.cpu.a[3]=512;before=w;
    CHECK_EQ(execute(&w,&bus,0x00e322,&event,&f),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    w.cpu.a[3]=257;w.cpu.a[8]=4;w.start=7;calls=b.calls;
    CHECK_EQ(execute(&w,&bus,0x00e382,&event,&f),ES_OK);CHECK_EQ(event,1);CHECK_EQ(b.calls,calls);
    CHECK_EQ(w.ar[8],4); /* even misalignment waits until the register window is available */
}
static void special_register(void)
{
    esk_xtensa_windows w;unsigned event;esk_xtensa_fault f;
    esk_xtensa_windows_init(&w,0x40000000,NULL);w.ps|=32u|64u;w.cpu.a[3]=0x12345678;
    CHECK_EQ(execute(&w,NULL,0x130c30,&event,&f),ES_OK);CHECK_EQ(event,0);CHECK_EQ(w.scompare1,0x12345678u);
    CHECK_EQ(execute(&w,NULL,0x030c20,&event,&f),ES_OK);CHECK_EQ(w.cpu.a[2],0x12345678u);
    w.cpu.a[4]=9;CHECK_EQ(execute(&w,NULL,0x610c40,&event,&f),ES_OK);CHECK_EQ(w.scompare1,9);CHECK_EQ(w.cpu.a[4],0x12345678u);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(compare_and_alias);RUN(failures_and_window_priority);RUN(special_register);
TEST_MAIN_END
