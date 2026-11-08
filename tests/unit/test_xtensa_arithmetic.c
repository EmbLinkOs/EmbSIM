/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/integer.h"
#include "mcu/xtensa/windows.h"
static void integer(uint32_t a[16],uint32_t word)
{
    unsigned char code[3];size_t used=99;
    code[0]=(unsigned char)(word&255u);code[1]=(unsigned char)((word>>8)&255u);code[2]=(unsigned char)((word>>16)&255u);
    CHECK_EQ(esk_xtensa_integer_execute(a,code,3,&used),ES_OK);CHECK_EQ(used,3);
}
static es_status execute(esk_xtensa_windows *w,uint32_t word,unsigned *event)
{
    unsigned char code[3];size_t used;esk_xtensa_fault f;
    code[0]=(unsigned char)(word&255u);code[1]=(unsigned char)((word>>8)&255u);code[2]=(unsigned char)((word>>16)&255u);
    return esk_xtensa_window_execute(w,NULL,code,3,&used,event,&f);
}
static void integer_edges(void)
{
    uint32_t a[16]={0},before[16];unsigned char zero_div[]={0x40,0x23,0xd2};size_t used;
    a[2]=99;a[3]=7;a[4]=0;integer(a,0x832340);CHECK_EQ(a[2],7);
    a[2]=99;integer(a,0x932340);CHECK_EQ(a[2],99);
    a[4]=0x80000000;integer(a,0xa32340);CHECK_EQ(a[2],7);
    a[2]=99;integer(a,0xb32340);CHECK_EQ(a[2],99);
    a[3]=UINT32_MAX;a[4]=1;integer(a,0x432340);CHECK_EQ(a[2],UINT32_MAX);
    integer(a,0x532340);CHECK_EQ(a[2],1);integer(a,0x632340);CHECK_EQ(a[2],1);
    integer(a,0x732340);CHECK_EQ(a[2],UINT32_MAX);
    a[3]=UINT32_MAX;a[4]=UINT32_MAX;integer(a,0xa22340);CHECK_EQ(a[2],0xfffffffeu);
    integer(a,0xb22340);CHECK_EQ(a[2],0);
    a[3]=0x80000000;a[4]=2;integer(a,0xa22340);CHECK_EQ(a[2],1);
    integer(a,0xb22340);CHECK_EQ(a[2],UINT32_MAX);
    a[3]=0x80000000;a[4]=UINT32_MAX;integer(a,0xd22340);CHECK_EQ(a[2],0x80000000u);
    integer(a,0xf22340);CHECK_EQ(a[2],0);
    a[3]=0xfffffff9;a[4]=3;integer(a,0xd22340);CHECK_EQ(a[2],0xfffffffeu);
    integer(a,0xf22340);CHECK_EQ(a[2],UINT32_MAX);
    a[3]=UINT32_MAX;integer(a,0xc22340);CHECK_EQ(a[2],0x55555555u);
    integer(a,0xe22340);CHECK_EQ(a[2],0);
    a[3]=0x80;integer(a,0x232300);CHECK_EQ(a[2],0xffffff80u);
    a[3]=0x400000;integer(a,0x2323f0);CHECK_EQ(a[2],0xffc00000u);
    a[3]=0;integer(a,0x40f320);CHECK_EQ(a[2],32);integer(a,0x40e320);CHECK_EQ(a[2],31);
    a[3]=UINT32_MAX;integer(a,0x40f320);CHECK_EQ(a[2],0);integer(a,0x40e320);CHECK_EQ(a[2],31);
    a[3]=1;integer(a,0x40f320);CHECK_EQ(a[2],31);integer(a,0x40e320);CHECK_EQ(a[2],30);
    a[4]=0;memcpy(before,a,sizeof a);CHECK_EQ(esk_xtensa_integer_execute(a,zero_div,3,&used),ES_EINVAL);
    CHECK_EQ(used,0);CHECK_EQ(memcmp(a,before,sizeof a),0);
}
static void divide_priority(void)
{
    static const uint32_t vectors[3]={0x40010000,0x40010040,0x40010080};
    static const uint32_t win[6]={0x40020000,0x40020040,0x40020080,0x400200c0,0x40020100,0x40020140};
    esk_xtensa_windows w;unsigned event;
    esk_xtensa_windows_init(&w,0x40001234,win);esk_xtensa_exception_configure(&w,vectors);
    w.cpu.a[2]=42;w.cpu.a[3]=7;w.cpu.a[4]=0;
    CHECK_EQ(execute(&w,0xd22340,&event),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.exccause,6);
    CHECK_EQ(w.epc1,0x40001234u);CHECK_EQ(w.cpu.a[2],42);
    esk_xtensa_windows_init(&w,0x40001234,win);esk_xtensa_exception_configure(&w,vectors);
    w.start=7;w.cpu.a[12]=7;w.cpu.a[13]=0;w.cpu.a[14]=42;
    CHECK_EQ(execute(&w,0xc2ecd0,&event),ES_OK);CHECK_EQ(event,1);CHECK_EQ(w.exccause,0);
    CHECK_EQ(w.ar[14],42); /* window overflow precedes divide-by-zero */
}
static void bit_branches(void)
{
    static const struct { uint32_t code,left,right;unsigned yes; } cases[]={
        {0x048237,0,0,0},{0x040237,0,0,1},{0x044237,0,0,1},{0x04c237,0,0,0},
        {0x048237,8,8,1},{0x044237,1,3,0},{0x04c237,1,3,1},
        {0x045237,0x80000000u,UINT32_MAX,0},{0x04d237,0x80000000u,UINT32_MAX,1},
        {0x0472f7,0,0,1},{0x04f2f7,0x80000000u,0,1},{0x04e207,1,0,1}
    };
    unsigned i,event;
    for(i=0;i<sizeof cases/sizeof cases[0];i++) {
        esk_xtensa_windows w;esk_xtensa_windows_init(&w,0x40000000,NULL);
        w.cpu.a[2]=cases[i].left;w.cpu.a[3]=cases[i].right;
        CHECK_EQ(execute(&w,cases[i].code,&event),ES_OK);CHECK_EQ(event,0);
        CHECK_EQ(w.cpu.pc,cases[i].yes ? 0x40000008u : 0x40000003u);
    }
    { esk_xtensa_windows w;esk_xtensa_windows_init(&w,0x40000000,NULL);w.start=7;
        CHECK_EQ(execute(&w,0x0472f7,&event),ES_OK);CHECK_EQ(event,0); /* immediate 31 is not AR15 */
    }
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(integer_edges);RUN(divide_priority);RUN(bit_branches);
TEST_MAIN_END
