/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"
static const uint32_t general[3]={0x40081000,0x40081040,0x40081080};
static const uint32_t high[6]={0x40082000,0x40082040,0x40082080,0x400820c0,0x40082100,0x40082140};
static es_status execute(esk_xtensa_windows *w,unsigned reg,uint32_t form,unsigned *event)
{
    unsigned char code[3];size_t used=99;esk_xtensa_fault fault;es_status st;
    uint32_t word=form|0x00e600u|(reg<<4);
    code[0]=(unsigned char)(word&255u);code[1]=(unsigned char)((word>>8)&255u);code[2]=(unsigned char)((word>>16)&255u);
    st=esk_xtensa_window_execute(w,NULL,code,3,&used,event,&fault);
    CHECK_EQ(used,st==ES_OK && !*event ? 3u : 0u);return st;
}
static void fields_and_exchange(void)
{
    unsigned reg,k,event;const uint32_t samples[]={0,UINT32_MAX,0x18,0x40000,0xfff8f000,0x12345678};
    for(reg=0;reg<16;reg++) for(k=0;k<sizeof samples/sizeof samples[0];k++) {
        esk_xtensa_windows w;uint32_t old=ESK_XT_PS_WOE|ESK_XT_PS_EXCM|3u;
        esk_xtensa_windows_init(&w,0x40080000,NULL);w.ps=old;w.cpu.a[reg]=samples[k];
        CHECK_EQ(execute(&w,reg,0x130000,&event),ES_OK);CHECK_EQ(event,0);
        CHECK_EQ(w.ps,samples[k]&0x00070fffu);CHECK_EQ(w.cpu.a[reg],samples[k]);
        /* Enter EXCM explicitly for legal RING-field writes in this fixture. */
        w.ps=old;w.cpu.a[reg]=samples[k];
        CHECK_EQ(execute(&w,reg,0x610000,&event),ES_OK);CHECK_EQ(event,0);
        CHECK_EQ(w.cpu.a[reg],old);CHECK_EQ(w.ps,samples[k]&0x00070fffu);
        w.ps|=ESK_XT_PS_EXCM;
        CHECK_EQ(execute(&w,reg,0x030000,&event),ES_OK);CHECK_EQ(w.cpu.a[reg],(samples[k]&0x00070fffu)|ESK_XT_PS_EXCM);
    }
    /* RFI copies only implemented PS fields; EPS storage itself is retained. */
    for(k=2;k<=7;k++) {
        esk_xtensa_windows w;
        esk_xtensa_windows_init(&w,0x40080000,NULL);esk_xtensa_exception_configure(&w,general);
        CHECK_EQ(esk_xtensa_irq_configure(&w,high),ES_OK);w.irq.eps[k]=UINT32_MAX;w.irq.epc[k]=0x40080100;
        CHECK_EQ(esk_xtensa_irq_return(&w,k),ES_OK);CHECK_EQ(w.ps,0x00070fffu);
        CHECK_EQ(w.irq.eps[k],UINT32_MAX);CHECK_EQ(w.cpu.pc,0x40080100);
    }
}
static void priority_and_atomic_mask_change(void)
{
    esk_xtensa_windows w;unsigned event;uint32_t vectors[6]={0x40083000,0,0,0,0,0};
    esk_xtensa_windows_init(&w,0x40080000,vectors);w.start=7;w.cpu.a[8]=0;
    CHECK_EQ(execute(&w,8,0x610000,&event),ES_OK);CHECK_EQ(event,1);CHECK_EQ(w.cpu.pc,vectors[0]);
    CHECK_EQ(w.ps&ESK_XT_PS_WOE,ESK_XT_PS_WOE);CHECK_EQ(w.cpu.a[8],0);
    esk_xtensa_windows_init(&w,0x40080000,NULL);esk_xtensa_exception_configure(&w,general);
    CHECK_EQ(esk_xtensa_irq_configure(&w,high),ES_OK);
    w.ps=ESK_XT_PS_WOE|3u;w.irq.intenable=1u<<19;CHECK_EQ(esk_xtensa_irq_line(&w,19,1),ES_OK);
    /* Level 2 is masked before and after; no temporary acceptance gap. */
    w.cpu.a[2]=ESK_XT_PS_WOE|ESK_XT_PS_EXCM;
    CHECK_EQ(execute(&w,2,0x610000,&event),ES_OK);CHECK_EQ(w.cpu.a[2],ESK_XT_PS_WOE|3u);
    CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,0);CHECK_EQ(w.cpu.pc,0x40080003);
    w.cpu.a[2]=ESK_XT_PS_WOE;
    CHECK_EQ(execute(&w,2,0x610000,&event),ES_OK);CHECK_EQ(event,0);
    CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,12);CHECK_EQ(w.irq.epc[2],0x40080006);
    esk_xtensa_windows_init(&w,0x40080000,NULL);esk_xtensa_exception_configure(&w,general);
    w.ps=ESK_XT_PS_WOE|64u;w.cpu.a[2]=0x42;
    CHECK_EQ(execute(&w,2,0x610000,&event),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.exccause,8);
    CHECK_EQ(w.cpu.a[2],0x42);CHECK_EQ(w.epc1,0x40080000);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(fields_and_exchange);RUN(priority_and_atomic_mask_change);
TEST_MAIN_END
