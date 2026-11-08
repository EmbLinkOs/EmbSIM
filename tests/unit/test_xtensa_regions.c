/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"
typedef struct fixture { unsigned reads,writes,cas;uint32_t value; } fixture;
static es_status read_word(void *ctx,uint32_t address,unsigned bytes,uint32_t *value)
{
    fixture *f=(fixture *)ctx;(void)address;(void)bytes;f->reads++;*value=f->value;return ES_OK;
}
static es_status write_word(void *ctx,uint32_t address,unsigned bytes,uint32_t value)
{
    fixture *f=(fixture *)ctx;(void)address;(void)bytes;f->writes++;f->value=value;return ES_OK;
}
static es_status fetch_byte(void *ctx,uint32_t address,unsigned bytes,uint32_t *value)
{
    fixture *f=(fixture *)ctx;(void)bytes;f->reads++;*value=(address&1u) ? 0xf0u : 0x3du;return ES_OK;
}
static es_status exchange(void *ctx,uint32_t address,uint32_t expected,uint32_t desired,uint32_t *observed)
{
    fixture *f=(fixture *)ctx;(void)address;f->cas++;*observed=f->value;if(f->value==expected) f->value=desired;return ES_OK;
}
static es_status execute(esk_xtensa_windows *w,const esk_xtensa_memory *bus,uint32_t word,unsigned *event)
{
    unsigned char code[3];size_t used=99;es_status st;esk_xtensa_fault fault;
    code[0]=(unsigned char)(word&255u);code[1]=(unsigned char)((word>>8)&255u);code[2]=(unsigned char)((word>>16)&255u);
    st=esk_xtensa_window_execute(w,bus,code,3,&used,event,&fault);
    CHECK_EQ(used,st==ES_OK && !*event ? ((word&15u)<8 ? 3u : 2u) : 0u);return st;
}
static void initialise(esk_xtensa_windows *w)
{
    esk_xtensa_windows_init(w,0x40081000,NULL);CHECK_EQ(esk_xtensa_lx6_reset(w,1),ES_OK);
    CHECK_EQ(esk_xtensa_region_configure(w),ES_OK);w->cpu.pc=0x40081000;w->ps=0;
}
static void instructions_and_barriers(void)
{
    unsigned bank,entry,attr,event,i;esk_xtensa_windows w,before;
    CHECK_EQ(esk_xtensa_region_configure(NULL),ES_EINVAL);
    esk_xtensa_windows_init(&w,0x40081000,NULL);before=w;
    CHECK_EQ(execute(&w,NULL,0x506320,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    for(bank=0;bank<2;bank++) for(entry=0;entry<8;entry++) for(attr=0;attr<16;attr++) {
        initialise(&w);w.cpu.a[2]=UINT32_C(0xa5a50000)|attr;w.cpu.a[3]=(entry<<29)|0x1fffffffu;
        CHECK_EQ(execute(&w,NULL,0x506320u+(bank<<15),&event),ES_OK);
        CHECK_EQ(w.region[bank][entry],attr);CHECK_EQ(w.region_active[bank][entry],2);
        CHECK_EQ(execute(&w,NULL,0x503340u+(bank<<15),&event),ES_OK);CHECK_EQ(w.cpu.a[4],0);
        CHECK_EQ(execute(&w,NULL,0x507340u+(bank<<15),&event),ES_OK);CHECK_EQ(w.cpu.a[4],(entry<<29)|attr);
        CHECK_EQ(execute(&w,NULL,0x505340u+(bank<<15),&event),ES_OK);CHECK_EQ(w.cpu.a[4],(entry<<29)|1u);
        CHECK_EQ(execute(&w,NULL,0x504300u+(bank<<15),&event),ES_OK);CHECK_EQ(w.region[bank][entry],attr);
        CHECK_EQ(execute(&w,NULL,0x0020c0,&event),ES_OK);CHECK_EQ(w.region_active[bank][entry],2);
        CHECK_EQ(execute(&w,NULL,bank ? 0x002030u : 0x002000u,&event),ES_OK);
        CHECK_EQ(w.region_active[bank][entry],attr);
        for(i=0;i<8;i++) CHECK_EQ(w.region_active[1-bank][i],2);
        CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);
        for(i=0;i<8;i++) { CHECK_EQ(w.region[bank][i],2);CHECK_EQ(w.region_active[bank][i],2); }
    }
    initialise(&w);w.cpu.a[3]=0;
    for(i=4;i<12;i++) {
        w.cpu.a[2]=UINT32_C(1)<<i;before=w;
        CHECK_EQ(execute(&w,NULL,0x506320,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    }
    w.ps=0xc0;w.cpu.a[2]=2;CHECK_EQ(execute(&w,NULL,0x506320,&event),ES_OK);CHECK_EQ(event,0);
    CHECK_EQ(execute(&w,NULL,0x03e640,&event),ES_OK);CHECK_EQ(w.cpu.a[4]&0xc0u,0);
    w.cpu.a[4]=0xc0;CHECK_EQ(execute(&w,NULL,0x13e640,&event),ES_OK);CHECK_EQ(w.ps&0xc0u,0);
    w.cpu.a[4]=0xc5;CHECK_EQ(execute(&w,NULL,0x13c240,&event),ES_OK);CHECK_EQ(w.irq.eps[2],5);
    w.irq.eps[2]=0xc5;CHECK_EQ(execute(&w,NULL,0x03c240,&event),ES_OK);CHECK_EQ(w.cpu.a[4],5);
    w.ps=ESK_XT_PS_EXCM|2u;w.irq.epc[2]=0x40081000;
    CHECK_EQ(esk_xtensa_irq_return(&w,2),ES_OK);CHECK_EQ(w.ps,5);
}
static void permission_matrix(void)
{
    const unsigned rights[16]={3,7,7,4,7,7,7,0,0,0,0,0,0,0,3,0};
    unsigned entry,attr,access,event;esk_xtensa_windows w;esk_xtensa_fault fault;fixture f={0};
    esk_xtensa_memory bus={&f,read_word,write_word,exchange,fetch_byte,NULL};
    for(entry=0;entry<8;entry++) for(attr=0;attr<16;attr++) for(access=0;access<3;access++) {
        uint32_t address=entry==1 ? 0x3ffb1000u : entry==2 ? 0x40081000u : (entry<<29)|0x1000u;
        int allowed=(rights[attr]&(1u<<access))!=0;
        initialise(&w);memset(&f,0,sizeof f);f.value=77;
        w.cpu.a[2]=123;w.cpu.a[3]=address;w.region[access==2 ? 0 : 1][entry]=attr;
        CHECK_EQ(execute(&w,&bus,access==2 ? 0x002000u : 0x002030u,&event),ES_OK);
        if(access==2) w.cpu.pc=address;
        if(allowed && attr==14 && entry!=1 && entry!=2) {
            CHECK_EQ(execute(&w,&bus,access ? 0x006322u : 0x002322u,&event),ES_EUNSUPPORTED);
            CHECK_EQ(f.reads,0);CHECK_EQ(f.writes,0);continue;
        }
        if(access==2) CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);
        else CHECK_EQ(execute(&w,&bus,access ? 0x006322u : 0x002322u,&event),ES_OK);
        if(allowed) {
            CHECK_EQ(event,0);CHECK_EQ(f.reads,access==2 ? 2u : access==0 ? 1u : 0u);CHECK_EQ(f.writes,access==1 ? 1u : 0u);
        } else {
            CHECK_EQ(event,7);CHECK_EQ(w.exccause,access==2 ? 20u : access==1 ? 29u : 28u);
            CHECK_EQ(w.excvaddr,address);CHECK_EQ(f.reads,0);CHECK_EQ(f.writes,0);CHECK_EQ(f.cas,0);
        }
    }
}
static void atomic_priority_and_fetch_commit(void)
{
    unsigned event;esk_xtensa_windows w;esk_xtensa_fault fault;fixture f={0};
    esk_xtensa_memory bus={&f,read_word,write_word,exchange,fetch_byte,NULL};
    initialise(&w);w.cpu.a[3]=0x3ffb1000;w.region[1][1]=3;
    CHECK_EQ(execute(&w,&bus,0x002030,&event),ES_OK);
    CHECK_EQ(execute(&w,&bus,0x00e322,&event),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.exccause,29);CHECK_EQ(f.cas,0);
    initialise(&w);w.region[0][2]=15;
    /* Until ISYNC the previous table is the declared fixture policy. */
    CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);CHECK_EQ(event,0);
    CHECK_EQ(execute(&w,&bus,0x002000,&event),ES_OK);
    f.reads=0;CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.exccause,20);CHECK_EQ(f.reads,0);
}
static void memory_forms_and_exception_modes(void)
{
    const uint32_t forms[]={0x000322,0x001322,0x002322,0x004322,0x005322,0x006322,0x009322,0x00b322,0x00f322,0x000328,0x000329,0xffff21,0x09f320,0x49f320};
    const unsigned stores[]={0,0,0,1,1,1,0,0,1,0,1,0,0,1};
    unsigned i,event,mode;esk_xtensa_windows w,before;fixture f={0};
    esk_xtensa_memory bus={&f,read_word,write_word,exchange,fetch_byte,NULL};
    for(i=0;i<sizeof forms/sizeof forms[0];i++) for(mode=0;mode<3;mode++) {
        uint32_t address=i==11 ? 0x40081000u : i>=12 ? 0x3ffb0ffcu : 0x3ffb1000u;
        initialise(&w);w.cpu.a[3]=0x3ffb1000;w.ps=(mode==2 || i>=12) ? ESK_XT_PS_EXCM : mode==1 ? 32u : 0u;
        w.region[1][address>>29]=15;CHECK_EQ(execute(&w,&bus,0x002030,&event),ES_OK);
        CHECK_EQ(execute(&w,&bus,forms[i],&event),ES_OK);
        CHECK_EQ(event,(mode==2 || i>=12) ? 9u : mode==1 ? 8u : 7u);
        CHECK_EQ(w.exccause,stores[i] ? 29u : 28u);CHECK_EQ(w.excvaddr,address);
        CHECK_EQ(f.reads,0);CHECK_EQ(f.writes,0);
    }
    initialise(&w);w.cpu.a[3]=0x3ffb1000;w.region[1][1]=15;
    CHECK_EQ(execute(&w,&bus,0x002030,&event),ES_OK);w.exceptions_enabled=0;before=w;
    CHECK_EQ(execute(&w,&bus,0x002322,&event),ES_EINVAL);CHECK_EQ(memcmp(&w,&before,sizeof w),0);CHECK_EQ(f.reads,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(instructions_and_barriers);RUN(permission_matrix);RUN(atomic_priority_and_fetch_commit);RUN(memory_forms_and_exception_modes);
TEST_MAIN_END
