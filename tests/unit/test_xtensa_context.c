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
static void threadptr_registers_and_modes(void)
{
    unsigned reg,mode,k,event;const uint32_t values[]={0,UINT32_MAX,0x12345679,0x3ffb0000};
    for(reg=0;reg<16;reg++) for(mode=0;mode<8;mode++) for(k=0;k<sizeof values/sizeof values[0];k++) {
        esk_xtensa_windows w;uint32_t ps=ESK_XT_PS_WOE|((mode&3u)<<6)|((mode&4u) ? ESK_XT_PS_EXCM : 0u);
        esk_xtensa_windows_init(&w,0x40080000,NULL);w.ps=ps;w.threadptr=0x89abcdef;
        CHECK_EQ(execute(&w,0xe30e70u|(reg<<12),&event),ES_OK);CHECK_EQ(event,0);
        CHECK_EQ(w.cpu.a[reg],0x89abcdefu);CHECK_EQ(w.ps,ps);CHECK_EQ(w.cpu.pc,0x40080003);
        w.cpu.a[reg]=values[k];
        CHECK_EQ(execute(&w,0xf3e700u|(reg<<4),&event),ES_OK);CHECK_EQ(event,0);
        CHECK_EQ(w.threadptr,values[k]);CHECK_EQ(w.cpu.a[reg],values[k]);CHECK_EQ(w.ps,ps);
    }
}
static void scratch_and_identity(void)
{
    unsigned reg,index,k,event;const uint32_t values[]={0,UINT32_MAX,0x80000000,0x12345678};
    for(reg=0;reg<16;reg++) for(index=0;index<4;index++) for(k=0;k<sizeof values/sizeof values[0];k++) {
        esk_xtensa_windows w;uint32_t sr=(244u+index)<<8;
        esk_xtensa_windows_init(&w,0x40080000,NULL);w.cpu.a[reg]=values[k];
        CHECK_EQ(execute(&w,0x130000u|sr|(reg<<4),&event),ES_OK);CHECK_EQ(event,0);CHECK_EQ(w.misc[index],values[k]);
        CHECK_EQ(w.misc[(index+1u)&3u],0);w.cpu.a[reg]=~values[k];
        CHECK_EQ(execute(&w,0x610000u|sr|(reg<<4),&event),ES_OK);CHECK_EQ(event,0);
        CHECK_EQ(w.cpu.a[reg],values[k]);CHECK_EQ(w.misc[index],~values[k]);
        CHECK_EQ(execute(&w,0x030000u|sr|(reg<<4),&event),ES_OK);CHECK_EQ(w.cpu.a[reg],~values[k]);
    }
    for(reg=0;reg<16;reg++) {
        esk_xtensa_windows w,before;
        esk_xtensa_windows_init(&w,0x40080000,NULL);before=w;
        CHECK_EQ(execute(&w,0x03eb00u|(reg<<4),&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
        CHECK_EQ(esk_xtensa_prid_configure(&w,0xa5a52000),ES_OK);
        CHECK_EQ(execute(&w,0x03eb00u|(reg<<4),&event),ES_OK);CHECK_EQ(w.cpu.a[reg],0xa5a52000u);
        before=w;
        CHECK_EQ(execute(&w,0x13eb00u|(reg<<4),&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
        CHECK_EQ(execute(&w,0x61eb00u|(reg<<4),&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    }
}
static void priority_reset_and_unsupported(void)
{
    esk_xtensa_windows w,peer,before;unsigned event,i;
    const uint32_t general[3]={0x40082000,0x40082040,0x40082080},vectors[6]={0x40083000,0,0,0,0,0};
    esk_xtensa_windows_init(&w,0x40080000,vectors);w.start=7;w.cpu.a[2]=0x12345678;w.threadptr=9;
    /* Encoded UR fragments are not extra register operands. */
    CHECK_EQ(execute(&w,0xf3e720,&event),ES_OK);CHECK_EQ(event,0);CHECK_EQ(w.threadptr,0x12345678);
    CHECK_EQ(execute(&w,0xe32e70,&event),ES_OK);CHECK_EQ(event,0);CHECK_EQ(w.cpu.a[2],0x12345678);
    CHECK_EQ(execute(&w,0xf3e780,&event),ES_OK);CHECK_EQ(event,1);CHECK_EQ(w.threadptr,0x12345678);
    esk_xtensa_windows_init(&w,0x40080000,vectors);w.start=7;w.misc[0]=99;
    CHECK_EQ(execute(&w,0x61f480,&event),ES_OK);CHECK_EQ(event,1);CHECK_EQ(w.misc[0],99);
    esk_xtensa_windows_init(&w,0x40080000,NULL);esk_xtensa_exception_configure(&w,general);
    w.ps=ESK_XT_PS_WOE|64u;w.cpu.a[2]=77;w.misc[0]=88;
    CHECK_EQ(execute(&w,0x13f420,&event),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.exccause,8);CHECK_EQ(w.misc[0],88);
    CHECK_EQ(esk_xtensa_prid_configure(&w,0xa5a52000),ES_OK);w.cpu.pc=0x40080000;w.ps=ESK_XT_PS_WOE|64u;
    CHECK_EQ(execute(&w,0x03eb20,&event),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.exccause,8);CHECK_EQ(w.cpu.a[2],77);
    esk_xtensa_windows_init(&w,0x40080000,NULL);before=w;
    CHECK_EQ(execute(&w,0xe32e80,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0); /* FCR still unsupported */
    CHECK_EQ(execute(&w,0x03f820,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0); /* no MISC4 */
    w.threadptr=0x3ffb0100;for(i=0;i<4;i++) w.misc[i]=0x12340000u+i;
    CHECK_EQ(esk_xtensa_prid_configure(&w,0xa5a50000),ES_OK);
    esk_xtensa_windows_init(&peer,0x50000000,NULL);peer.threadptr=0x3ffc0000;before=peer;
    CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);CHECK_EQ(w.threadptr,0x3ffb0100);CHECK_EQ(w.prid,0xa5a50000u);
    for(i=0;i<4;i++) CHECK_EQ(w.misc[i],0x12340000u+i);
    CHECK_EQ(memcmp(&peer,&before,sizeof peer),0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;CHECK_EQ(esk_xtensa_prid_configure(NULL,0),ES_EINVAL);RUN(threadptr_registers_and_modes);RUN(scratch_and_identity);RUN(priority_reset_and_unsupported);
TEST_MAIN_END
