/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"
typedef struct fixture { uint32_t address,value;unsigned reads,writes,fail; } fixture;
static es_status read_word(void *ctx,uint32_t address,unsigned bytes,uint32_t *value)
{
    fixture *f=(fixture *)ctx;f->reads++;f->address=address;*value=f->value;
    return f->fail || bytes!=4 ? ES_EINVAL : ES_OK;
}
static es_status write_word(void *ctx,uint32_t address,unsigned bytes,uint32_t value)
{
    fixture *f=(fixture *)ctx;f->writes++;f->address=address;
    if(f->fail || bytes!=4) return ES_EINVAL;
    f->value=value;return ES_OK;
}
static es_status execute(esk_xtensa_windows *w,esk_xtensa_memory *bus,unsigned t,unsigned s,unsigned offset,int store,unsigned *event,esk_xtensa_fault *fault)
{
    unsigned char code[3];size_t used=99;es_status st;
    code[0]=(unsigned char)((t<<4)|2u);code[1]=(unsigned char)((store ? 0xf0u : 0xb0u)|s);code[2]=(unsigned char)offset;
    st=esk_xtensa_window_execute(w,bus,code,3,&used,event,fault);
    CHECK_EQ(used,st==ES_OK && !*event ? 3u : 0u);return st;
}
static void registers_offsets_and_faults(void)
{
    unsigned t,s,k,store,event;esk_xtensa_fault fault;
    for(t=0;t<16;t++) for(s=0;s<16;s++) for(k=0;k<3;k++) for(store=0;store<2;store++) {
        esk_xtensa_windows w,before;fixture f={0,0x89abcdef,0,0,0};uint32_t expected,address;
        esk_xtensa_memory bus={&f,read_word,write_word,NULL,NULL,NULL};
        unsigned offset=k==2 ? 255u : k;
        esk_xtensa_windows_init(&w,0x40080000,NULL);
        w.cpu.a[t]=0x12345678;w.cpu.a[s]=0x3ffb0000;
        expected=store ? w.cpu.a[t] : f.value;address=w.cpu.a[s]+offset*4u;
        CHECK_EQ(execute(&w,&bus,t,s,offset,(int)store,&event,&fault),ES_OK);CHECK_EQ(event,0);
        CHECK_EQ(f.address,address);CHECK_EQ(f.reads,store ? 0u : 1u);CHECK_EQ(f.writes,store);
        CHECK_EQ(w.cpu.pc,0x40080003);
        if(store) CHECK_EQ(f.value,expected);else CHECK_EQ(w.cpu.a[t],expected);
        /* Failed callbacks and misalignment do not commit architectural state. */
        f.fail=1;w.cpu.a[s]=0x3ffb0000;f.reads=f.writes=0;before=w;
        CHECK_EQ(execute(&w,&bus,t,s,offset,(int)store,&event,&fault),ES_EINVAL);
        CHECK_EQ(memcmp(&w,&before,sizeof w),0);
        CHECK_EQ(fault.kind,(unsigned)(store ? ESK_XT_STORE : ESK_XT_LOAD));
        CHECK_EQ(f.reads,store ? 0u : 1u);CHECK_EQ(f.writes,store);
        f.reads=f.writes=0;w.cpu.a[s]=0x3ffb0001;before=w;
        CHECK_EQ(execute(&w,&bus,t,s,offset,(int)store,&event,&fault),ES_EINVAL);
        CHECK_EQ(fault.kind,ESK_XT_ALIGN);CHECK_EQ(f.reads,0);CHECK_EQ(f.writes,0);
        CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    }
}
static void window_priority(void)
{
    unsigned store,event;esk_xtensa_fault fault;
    uint32_t vectors[6]={0x40081000,0x40081100,0x40081200,0x40081300,0x40081400,0x40081500};
    for(store=0;store<2;store++) {
        esk_xtensa_windows w;fixture f={0,0,0,0,0};esk_xtensa_memory bus={&f,read_word,write_word,NULL,NULL,NULL};
        esk_xtensa_windows_init(&w,0x40080000,vectors);w.start=7;
        CHECK_EQ(execute(&w,&bus,8,3,0,(int)store,&event,&fault),ES_OK);
        CHECK_EQ(event,1);CHECK_EQ(w.cpu.pc,vectors[0]);CHECK_EQ(f.reads,0);CHECK_EQ(f.writes,0);
    }
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(registers_offsets_and_faults);RUN(window_priority);
TEST_MAIN_END
