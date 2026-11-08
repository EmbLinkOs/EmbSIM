/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"

static void init(esk_xtensa_windows *w)
{
    static const uint32_t general[3]={0x40010000,0x40010040,0x40010080};
    static const uint32_t high[6]={0x40020000,0x40020040,0x40020080,0x400200c0,0x40020100,0x40020140};
    esk_xtensa_windows_init(w,0x40001234,NULL);esk_xtensa_exception_configure(w,general);
    CHECK_EQ(esk_xtensa_irq_configure(w,high),ES_OK);
}
static es_status execute(esk_xtensa_windows *w,uint32_t word,unsigned *event)
{
    unsigned char code[3];size_t used;esk_xtensa_fault fault;
    code[0]=(unsigned char)(word&255u);code[1]=(unsigned char)((word>>8)&255u);code[2]=(unsigned char)((word>>16)&255u);
    return esk_xtensa_window_execute(w,NULL,code,3,&used,event,&fault);
}
static void deadlines_and_clear(void)
{
    esk_xtensa_windows w;uint64_t ticks;unsigned event;uint32_t value;
    init(&w);CHECK_EQ(esk_xtensa_irq_write(&w,240,5),ES_OK);CHECK_EQ(esk_xtensa_irq_write(&w,241,8),ES_OK);
    CHECK_EQ(esk_xtensa_irq_write(&w,242,12),ES_OK);CHECK_EQ(esk_xtensa_clock_deadline(&w,&ticks),1);CHECK_EQ(ticks,5);
    CHECK_EQ(esk_xtensa_clock_advance(&w,0),ES_OK);CHECK_EQ(w.irq.ccount,0);
    CHECK_EQ(esk_xtensa_clock_advance(&w,4),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(&w),0);
    CHECK_EQ(esk_xtensa_clock_deadline(&w,&ticks),1);CHECK_EQ(ticks,1);
    CHECK_EQ(esk_xtensa_clock_advance(&w,1),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(&w),1u<<6);
    CHECK_EQ(esk_xtensa_irq_write(&w,227,UINT32_MAX),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(&w),1u<<6);
    CHECK_EQ(esk_xtensa_clock_deadline(&w,&ticks),1);CHECK_EQ(ticks,3);
    CHECK_EQ(esk_xtensa_clock_advance(&w,7),ES_OK);CHECK_EQ(w.irq.ccount,12);
    CHECK_EQ(esk_xtensa_irq_pending(&w),(1u<<6)|(1u<<15)|(1u<<16));
    CHECK_EQ(esk_xtensa_clock_deadline(&w,&ticks),0);CHECK_EQ(ticks,0);
    w.irq.intenable=UINT32_MAX;CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,15);
    CHECK_EQ(w.irq.last_pending,1u<<16);CHECK_EQ(esk_xtensa_irq_write(&w,242,20),ES_OK);
    CHECK_EQ(esk_xtensa_irq_pending(&w),(1u<<6)|(1u<<15));
    CHECK_EQ(esk_xtensa_irq_read(&w,234,&value),ES_OK);CHECK_EQ(value,12);
    CHECK_EQ(esk_xtensa_irq_read(&w,242,&value),ES_OK);CHECK_EQ(value,20);
    /* The register path performs the same acknowledgement. */
    w.cpu.a[3]=30;CHECK_EQ(execute(&w,0x13f130,&event),ES_OK);CHECK_EQ(w.irq.compare[1],30);
    CHECK_EQ(esk_xtensa_irq_pending(&w),1u<<6);
}
static void wraps(void)
{
    esk_xtensa_windows w;uint64_t ticks;
    init(&w);CHECK_EQ(esk_xtensa_irq_write(&w,234,0xfffffffcu),ES_OK);
    CHECK_EQ(esk_xtensa_irq_write(&w,240,0),ES_OK);CHECK_EQ(esk_xtensa_irq_write(&w,241,1),ES_OK);
    CHECK_EQ(esk_xtensa_irq_write(&w,242,0xfffffffcu),ES_OK);
    CHECK_EQ(esk_xtensa_clock_deadline(&w,&ticks),1);CHECK_EQ(ticks,4);
    CHECK_EQ(esk_xtensa_clock_advance(&w,4),ES_OK);CHECK_EQ(w.irq.ccount,0);CHECK_EQ(esk_xtensa_irq_pending(&w),1u<<6);
    CHECK_EQ(esk_xtensa_clock_advance(&w,1),ES_OK);CHECK_EQ(w.irq.ccount,1);CHECK_EQ(esk_xtensa_irq_pending(&w),(1u<<6)|(1u<<15));
    CHECK_EQ(esk_xtensa_irq_write(&w,234,7),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(&w),(1u<<6)|(1u<<15));
    CHECK_EQ(esk_xtensa_irq_write(&w,240,7),ES_OK);CHECK_EQ(esk_xtensa_irq_write(&w,241,7),ES_OK);
    CHECK_EQ(esk_xtensa_irq_write(&w,242,7),ES_OK);CHECK_EQ(esk_xtensa_clock_deadline(&w,&ticks),1);
    CHECK_EQ(ticks,UINT64_C(0x100000000));CHECK_EQ(esk_xtensa_clock_advance(&w,ticks-1),ES_OK);
    CHECK_EQ(esk_xtensa_irq_pending(&w),0);CHECK_EQ(esk_xtensa_clock_advance(&w,1),ES_OK);
    CHECK_EQ(w.irq.ccount,7);CHECK_EQ(esk_xtensa_irq_pending(&w),(1u<<6)|(1u<<15)|(1u<<16));
    CHECK_EQ(esk_xtensa_clock_advance(&w,UINT64_MAX),ES_OK);CHECK_EQ(w.irq.ccount,6);
}
static void wait_clock_and_registers(void)
{
    esk_xtensa_windows w;unsigned event;esk_xtensa_fault fault;
    init(&w);w.irq.intenable=1u<<6;CHECK_EQ(esk_xtensa_irq_write(&w,240,10),ES_OK);
    CHECK_EQ(execute(&w,0x007000,&event),ES_OK);CHECK_EQ(w.irq.waiting,1);CHECK_EQ(w.irq.ccount,0);
    CHECK_EQ(esk_xtensa_clock_advance(&w,9),ES_OK);CHECK_EQ(esk_xtensa_window_step(&w,NULL,&event,&fault),ES_OK);
    CHECK_EQ(event,18);CHECK_EQ(w.irq.ccount,9);
    CHECK_EQ(esk_xtensa_clock_advance(&w,1),ES_OK);CHECK_EQ(esk_xtensa_window_step(&w,NULL,&event,&fault),ES_OK);
    CHECK_EQ(event,11);CHECK_EQ(w.epc1,0x40001237u);CHECK_EQ(w.irq.waiting,0);
    w.cpu.a[3]=0x12345678;CHECK_EQ(execute(&w,0x13ea30,&event),ES_OK); /* WSR CCOUNT */
    CHECK_EQ(w.irq.ccount,0x12345678u);CHECK_EQ(execute(&w,0x03ea20,&event),ES_OK);CHECK_EQ(w.cpu.a[2],0x12345678u);
    CHECK_EQ(esk_xtensa_irq_pending(&w),1u<<6); /* CCOUNT write does not acknowledge timer */
}
TEST_MAIN_BEGIN
    (void)t_str_eq;
    RUN(deadlines_and_clear);RUN(wraps);RUN(wait_clock_and_registers);
TEST_MAIN_END
