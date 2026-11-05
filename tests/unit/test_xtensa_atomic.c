/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"
/* Serialized fixture bus only: the provider's callback contract, not this
 * fixture, supplies indivisibility for real concurrent bus agents. */
typedef struct bus_state { uint32_t value,address,expected,desired;unsigned calls,reads,writes,fail,type,typecalls,typefail; } bus_state;
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
    bus_state b={0};esk_xtensa_memory bus={&b,read_mem,write_mem,cas,NULL,NULL};
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
    bus_state b={9,0,0,0,0,0,0,0,0,0,0};esk_xtensa_memory bus={&b,read_mem,write_mem,cas,NULL,NULL};
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
static es_status memory_type(void *ctx,uint32_t address,unsigned *type)
{
    bus_state *b=(bus_state *)ctx;(void)address;b->typecalls++;
    if(b->typefail) return ES_EUNSUPPORTED;
    *type=b->type;return ES_OK;
}
static void atomctl_register_and_reset(void)
{
    unsigned reg,form,value,event;esk_xtensa_fault fault;esk_xtensa_windows w,before;
    const uint32_t forms[3]={0x036300,0x136300,0x616300};
    CHECK_EQ(esk_xtensa_atomctl_configure(NULL),ES_EINVAL);
    esk_xtensa_windows_init(&w,0x40080000,NULL);before=w;
    CHECK_EQ(execute(&w,NULL,forms[0],&event,&fault),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    for(reg=0;reg<16;reg++) for(form=0;form<3;form++) for(value=0;value<64;value++) {
        esk_xtensa_windows_init(&w,0x40080000,NULL);CHECK_EQ(esk_xtensa_atomctl_configure(&w),ES_OK);
        CHECK_EQ(w.atomctl,0x28);w.atomctl=63-value;w.cpu.a[reg]=value;
        CHECK_EQ(execute(&w,NULL,forms[form]|(reg<<4),&event,&fault),ES_OK);
        CHECK_EQ(w.atomctl,form ? value : 63-value);CHECK_EQ(w.cpu.a[reg],form==1 ? value : 63-value);
    }
    for(value=6;value<32;value++) {
        esk_xtensa_windows_init(&w,0x40080000,NULL);CHECK_EQ(esk_xtensa_atomctl_configure(&w),ES_OK);
        w.cpu.a[3]=UINT32_C(1)<<value;before=w;
        CHECK_EQ(execute(&w,NULL,0x136330,&event,&fault),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
        CHECK_EQ(execute(&w,NULL,0x616330,&event,&fault),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    }
    esk_xtensa_windows_init(&w,0x40080000,NULL);CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);
    CHECK_EQ(w.atomctl_enabled,1);CHECK_EQ(w.atomctl,0x28);w.atomctl=0x15;
    CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);CHECK_EQ(w.atomctl,0x28);
    w.ps=64;w.cpu.a[3]=0x15;
    CHECK_EQ(execute(&w,NULL,0x136330,&event,&fault),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.exccause,8);CHECK_EQ(w.atomctl,0x28);
}
static void atomctl_memory_policy(void)
{
    unsigned type,control,matched,event;esk_xtensa_fault fault;esk_xtensa_windows w,before;
    bus_state b={0};esk_xtensa_memory bus={&b,read_mem,write_mem,cas,NULL,memory_type};
    for(type=0;type<4;type++) for(control=0;control<64;control++) for(matched=0;matched<2;matched++) {
        unsigned mode=type ? (control>>(2u*(type-1u)))&3u : 1u;
        memset(&b,0,sizeof b);b.type=type;b.value=matched ? 9u : 8u;
        esk_xtensa_windows_init(&w,0x40080000,NULL);CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);
        w.cpu.pc=0x40080000;w.ps=0;w.cpu.a[3]=256;w.cpu.a[2]=42;w.scompare1=9;w.atomctl=control;before=w;
        if(mode==1) {
            CHECK_EQ(execute(&w,&bus,0x00e322,&event,&fault),ES_OK);CHECK_EQ(event,0);
            CHECK_EQ(b.calls,1);CHECK_EQ(w.cpu.a[2],matched ? 9 : 8);CHECK_EQ(b.value,matched ? 42 : 8);
        } else if(!mode) {
            CHECK_EQ(execute(&w,&bus,0x00e322,&event,&fault),ES_OK);CHECK_EQ(event,7);CHECK_EQ(b.calls,0);
            CHECK_EQ(w.exccause,3);CHECK_EQ(w.excvaddr,256);CHECK_EQ(w.epc1,before.cpu.pc);CHECK_EQ(w.cpu.a[2],42);
        } else {
            CHECK_EQ(execute(&w,&bus,0x00e322,&event,&fault),ES_EUNSUPPORTED);CHECK_EQ(b.calls,0);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
        }
        CHECK_EQ(b.typecalls,1);CHECK_EQ(b.reads,0);CHECK_EQ(b.writes,0);
    }
    esk_xtensa_windows_init(&w,0x40080000,NULL);CHECK_EQ(esk_xtensa_atomctl_configure(&w),ES_OK);
    w.cpu.a[3]=256;w.cpu.a[2]=42;w.atomctl=0x15;before=w;b.calls=0;b.typefail=1;
    CHECK_EQ(execute(&w,&bus,0x00e322,&event,&fault),ES_EUNSUPPORTED);CHECK_EQ(b.calls,0);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    b.typefail=0;b.type=4;
    CHECK_EQ(execute(&w,&bus,0x00e322,&event,&fault),ES_EUNSUPPORTED);CHECK_EQ(b.calls,0);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    b.type=ESK_XT_MEM_BYPASS;w.atomctl=0;before=w;
    CHECK_EQ(execute(&w,&bus,0x00e322,&event,&fault),ES_EINVAL);CHECK_EQ(fault.kind,ESK_XT_ATOMIC);CHECK_EQ(b.calls,0);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    for(type=0;type<3;type++) {
        CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);w.ps=type==2 ? ESK_XT_PS_EXCM : type==1 ? 32u : 0u;
        w.cpu.pc=0x40080000;w.atomctl=0;w.cpu.a[3]=256;
        CHECK_EQ(execute(&w,&bus,0x00e322,&event,&fault),ES_OK);CHECK_EQ(event,7+type);
        CHECK_EQ(w.exccause,3);CHECK_EQ(w.excvaddr,256);CHECK_EQ(b.calls,0);
        CHECK_EQ(w.cpu.pc,0x40000300u+(type==2 ? 0xc0u : type*0x40u));
    }
    before=w;bus.atomic_type=NULL;
    CHECK_EQ(execute(&w,&bus,0x00e322,&event,&fault),ES_EUNSUPPORTED);CHECK_EQ(b.calls,0);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(compare_and_alias);RUN(failures_and_window_priority);RUN(special_register);RUN(atomctl_register_and_reset);RUN(atomctl_memory_policy);
TEST_MAIN_END
