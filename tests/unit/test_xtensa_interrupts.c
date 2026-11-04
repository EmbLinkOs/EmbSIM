/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"

static const uint32_t general[3]={0x40010000,0x40010040,0x40010080};
static const uint32_t high[6]={0x40020000,0x40020040,0x40020080,0x400200c0,0x40020100,0x40020140};
static const unsigned input_level[32]={1,1,1,1,1,1,1,1,1,1,1,3,1,1,7,3,5,1,1,2,2,2,3,3,4,4,5,3,4,3,4,5};
static void init(esk_xtensa_windows *w)
{
    esk_xtensa_windows_init(w,0x40001234,NULL);esk_xtensa_exception_configure(w,general);
    CHECK_EQ(esk_xtensa_irq_configure(w,high),ES_OK);
}
static es_status execute(esk_xtensa_windows *w,uint32_t word,unsigned *event)
{
    unsigned char code[3];size_t used=99;esk_xtensa_fault f;es_status st;
    code[0]=(unsigned char)(word&255u);code[1]=(unsigned char)((word>>8)&255u);code[2]=(unsigned char)((word>>16)&255u);
    st=esk_xtensa_window_execute(w,NULL,code,3,&used,event,&f);
    CHECK_EQ(used,st==ES_OK && !*event ? 3u : 0u);return st;
}
static void drive(esk_xtensa_windows *w,unsigned input)
{
    if(input==7 || input==29) CHECK_EQ(esk_xtensa_irq_write(w,226,UINT32_C(1)<<input),ES_OK);
    else CHECK_EQ(esk_xtensa_irq_line(w,input,1),ES_OK);
}
static void exhaustive_masks(void)
{
    unsigned input,threshold,exception_mode,enable;
    for(input=0;input<32;input++) for(threshold=0;threshold<16;threshold++)
    for(exception_mode=0;exception_mode<2;exception_mode++) for(enable=0;enable<2;enable++) {
        esk_xtensa_windows w,before;unsigned event,level=input_level[input],effective=threshold;
        uint32_t mask=UINT32_C(1)<<input,old_ps;
        init(&w);w.ps=(w.ps&~15u)|threshold|(exception_mode ? ESK_XT_PS_EXCM : 0u);
        old_ps=w.ps;w.irq.intenable=enable ? mask : 0;
        drive(&w,input);before=w;
        if(exception_mode && effective<3) effective=3;
        CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);
        if(input==14 || (enable && level>effective)) {
            CHECK_EQ(event,10u+level);CHECK_EQ(w.irq.last_level,level);CHECK_EQ(w.irq.last_pending,mask);
            CHECK_EQ(w.cpu.pc,level==1 ? general[0] : high[level-2]);
            CHECK_EQ(w.ps&ESK_XT_PS_EXCM,ESK_XT_PS_EXCM);
            if(level==1) { CHECK_EQ(w.epc1,0x40001234u);CHECK_EQ(w.exccause,4); }
            else {
                CHECK_EQ(w.irq.epc[level],0x40001234u);CHECK_EQ(w.irq.eps[level],old_ps);
                CHECK_EQ(w.ps&15u,level);
            }
            CHECK_EQ(esk_xtensa_irq_pending(&w),input==14 ? 0u : mask);
        } else { CHECK_EQ(event,0);CHECK_EQ(memcmp(&w,&before,sizeof w),0); }
    }
}
static void pending_and_edges(void)
{
    esk_xtensa_windows w,before;unsigned event;uint32_t value;
    init(&w);
    CHECK_EQ(esk_xtensa_irq_write(&w,226,UINT32_MAX),ES_OK);
    CHECK_EQ(esk_xtensa_irq_pending(&w),0x20000080u);
    CHECK_EQ(esk_xtensa_irq_write(&w,227,UINT32_MAX),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(&w),0);
    CHECK_EQ(esk_xtensa_irq_line(&w,0,1),ES_OK);CHECK_EQ(esk_xtensa_irq_line(&w,6,1),ES_OK);
    CHECK_EQ(esk_xtensa_irq_write(&w,227,UINT32_MAX),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(&w),65);
    CHECK_EQ(esk_xtensa_irq_line(&w,0,0),ES_OK);CHECK_EQ(esk_xtensa_irq_line(&w,6,0),ES_OK);
    CHECK_EQ(esk_xtensa_irq_line(&w,10,1),ES_OK);CHECK_EQ(esk_xtensa_irq_line(&w,10,0),ES_OK);
    CHECK_EQ(esk_xtensa_irq_pending(&w),1024);CHECK_EQ(esk_xtensa_irq_write(&w,227,1024),ES_OK);
    CHECK_EQ(esk_xtensa_irq_pending(&w),0);
    CHECK_EQ(esk_xtensa_irq_line(&w,10,1),ES_OK);CHECK_EQ(esk_xtensa_irq_write(&w,227,1024),ES_OK);
    CHECK_EQ(esk_xtensa_irq_line(&w,10,1),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(&w),0);
    CHECK_EQ(esk_xtensa_irq_line(&w,10,0),ES_OK);CHECK_EQ(esk_xtensa_irq_line(&w,10,1),ES_OK);
    CHECK_EQ(esk_xtensa_irq_pending(&w),1024);
    before=w;CHECK_EQ(esk_xtensa_irq_line(&w,7,1),ES_EINVAL);CHECK_EQ(esk_xtensa_irq_line(&w,32,1),ES_EINVAL);
    CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    CHECK_EQ(esk_xtensa_irq_read(&w,227,&value),ES_EUNSUPPORTED);
    before=w;CHECK_EQ(execute(&w,0x03e320,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    /* Same-level vector does not invent a winning input number. */
    init(&w);w.irq.intenable=(1u<<19)|(1u<<20);drive(&w,19);drive(&w,20);
    CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,12);CHECK_EQ(w.irq.last_pending,(1u<<19)|(1u<<20));
    CHECK_EQ(esk_xtensa_irq_line(&w,19,0),ES_OK);CHECK_EQ(execute(&w,0x003210,&event),ES_OK);
    CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,12);CHECK_EQ(w.irq.last_pending,1u<<20);
}
static void nesting_and_nmi(void)
{
    esk_xtensa_windows w;unsigned event;uint32_t root,ps;
    init(&w);root=w.cpu.pc;ps=w.ps;w.irq.intenable=(1u<<0)|(1u<<24)|(1u<<31);
    drive(&w,0);CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,11);CHECK_EQ(w.epc1,root);
    drive(&w,24);CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,14);CHECK_EQ(w.irq.epc[4],general[0]);
    drive(&w,31);CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,15);CHECK_EQ(w.irq.epc[5],high[2]);
    drive(&w,14);CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,17);CHECK_EQ(w.irq.epc[7],high[3]);
    CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,0); /* held NMI edge not repeated */
    CHECK_EQ(execute(&w,0x003710,&event),ES_OK);CHECK_EQ(w.cpu.pc,high[3]);CHECK_EQ(w.ps&15u,5);
    CHECK_EQ(esk_xtensa_irq_line(&w,31,0),ES_OK);CHECK_EQ(execute(&w,0x003510,&event),ES_OK);
    CHECK_EQ(w.cpu.pc,high[2]);CHECK_EQ(w.ps&15u,4);
    CHECK_EQ(esk_xtensa_irq_line(&w,24,0),ES_OK);CHECK_EQ(execute(&w,0x003410,&event),ES_OK);
    CHECK_EQ(w.cpu.pc,general[0]);CHECK_EQ(w.ps&ESK_XT_PS_EXCM,ESK_XT_PS_EXCM);
    CHECK_EQ(esk_xtensa_irq_line(&w,0,0),ES_OK);CHECK_EQ(execute(&w,0x003000,&event),ES_OK);
    CHECK_EQ(w.cpu.pc,root);CHECK_EQ(w.ps,ps);
    /* User level-1 entry follows the configured user exception vector. */
    init(&w);w.ps|=32u;w.irq.intenable=1;drive(&w,0);
    CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,11);CHECK_EQ(w.cpu.pc,general[1]);
}
static void wait_and_registers(void)
{
    esk_xtensa_windows w,before;unsigned event;esk_xtensa_fault f;
    init(&w);CHECK_EQ(execute(&w,0x007100,&event),ES_OK); /* WAITI 1 */
    CHECK_EQ(w.cpu.pc,0x40001237u);CHECK_EQ(w.irq.waiting,1);
    CHECK_EQ(esk_xtensa_window_step(&w,NULL,&event,&f),ES_OK);CHECK_EQ(event,18);CHECK_EQ(w.cpu.pc,0x40001237u);
    w.irq.intenable=1u|(1u<<19);drive(&w,0);
    CHECK_EQ(esk_xtensa_window_step(&w,NULL,&event,&f),ES_OK);CHECK_EQ(event,18);
    drive(&w,19);CHECK_EQ(esk_xtensa_window_step(&w,NULL,&event,&f),ES_OK);CHECK_EQ(event,12);
    CHECK_EQ(w.irq.epc[2],0x40001237u);CHECK_EQ(w.irq.waiting,0);
    CHECK_EQ(esk_xtensa_irq_line(&w,19,0),ES_OK);CHECK_EQ(execute(&w,0x003210,&event),ES_OK);
    CHECK_EQ(w.cpu.pc,0x40001237u);CHECK_EQ(w.ps&15u,1);
    /* Pending-before-WAITI input is not lost when masking is lowered. */
    init(&w);w.ps|=15u;w.irq.intenable=1;drive(&w,0);
    CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,0);
    CHECK_EQ(execute(&w,0x007000,&event),ES_OK);
    CHECK_EQ(esk_xtensa_window_step(&w,NULL,&event,&f),ES_OK);CHECK_EQ(event,11);CHECK_EQ(w.epc1,0x40001237u);
    init(&w);w.cpu.a[3]=0x20000080;
    CHECK_EQ(execute(&w,0x13e230,&event),ES_OK); /* INTSET */
    CHECK_EQ(execute(&w,0x03e220,&event),ES_OK);CHECK_EQ(w.cpu.a[2],0x20000080u);
    w.cpu.a[6]=0x20000080;CHECK_EQ(execute(&w,0x13e460,&event),ES_OK);CHECK_EQ(w.irq.intenable,0x20000080u);
    CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,13); /* SW 29 is level 3 */
    w.cpu.a[4]=1u<<29;CHECK_EQ(execute(&w,0x13e340,&event),ES_OK);
    CHECK_EQ(execute(&w,0x003310,&event),ES_OK);CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,11);
    /* Scratch/high context registers and undefined RFI levels. */
    init(&w);w.cpu.a[3]=0x12345678;CHECK_EQ(execute(&w,0x13c230,&event),ES_OK);
    CHECK_EQ(w.irq.eps[2],0x12345678u);CHECK_EQ(execute(&w,0x03c220,&event),ES_OK);CHECK_EQ(w.cpu.a[2],0x12345678u);
    before=w;CHECK_EQ(execute(&w,0x003110,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
}
static es_status code_read(void *ctx,uint32_t address,unsigned bytes,uint32_t *value)
{
    const unsigned char *data=(const unsigned char *)ctx;unsigned i;uint32_t offset;
    if(address<0x1000 || bytes>4) return ES_EINVAL;
    offset=address-0x1000;
    if(offset>=512 || bytes>512-offset) return ES_EINVAL;
    *value=0;for(i=0;i<bytes;i++) *value|=(uint32_t)data[offset+i]<<(8*i);return ES_OK;
}
static void fetched_handler(void)
{
    static const unsigned char program[]={0x0c,0x72,0x1b,0x22,0x3d,0xf0};
    /* Save A2 in EXCSAVE3, acknowledge software input 29, restore A2, RFI 3. */
    static const unsigned char handler[]={0x20,0xd3,0x13,0x22,0xa0,1,0x30,0x22,1,0x20,0xe3,0x13,0x20,0xd3,3,0x10,0x33,0};
    static const uint32_t h[6]={0x1080,0x10c0,0x1100,0x1140,0x1180,0x11c0};
    unsigned char data[512]={0};esk_xtensa_memory bus={data,code_read,NULL};esk_xtensa_windows w;
    esk_xtensa_fault f;unsigned event,steps=0,entries=0;
    memcpy(data,program,sizeof program);memcpy(data+192,handler,sizeof handler);
    init(&w);w.cpu.pc=0x1000;CHECK_EQ(esk_xtensa_irq_configure(&w,h),ES_OK);
    CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&f),ES_OK);CHECK_EQ(w.cpu.a[2],7);
    w.irq.intenable=1u<<29;CHECK_EQ(esk_xtensa_irq_write(&w,226,1u<<29),ES_OK);
    while(w.cpu.pc!=0x1004 && steps<20) {
        CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&f),ES_OK);
        if(event) { CHECK_EQ(event,13);entries++; }steps++;
    }
    CHECK_EQ(steps,8);CHECK_EQ(entries,1);CHECK_EQ(w.cpu.a[2],8);CHECK_EQ(w.cpu.pc,0x1004u);
    CHECK_EQ(w.irq.epc[3],0x1002u);CHECK_EQ(w.irq.excsave[3],7);CHECK_EQ(esk_xtensa_irq_pending(&w),0);
    CHECK_EQ(w.ps&ESK_XT_PS_EXCM,0);
}
static void configuration_and_privilege(void)
{
    esk_xtensa_windows w,before;unsigned event;
    esk_xtensa_windows_init(&w,0x40001234,NULL);before=w;
    CHECK_EQ(esk_xtensa_irq_configure(&w,high),ES_EINVAL);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    init(&w);w.ps|=15u;CHECK_EQ(esk_xtensa_irq_line(&w,14,1),ES_OK);CHECK_EQ(esk_xtensa_irq_line(&w,14,0),ES_OK);
    CHECK_EQ(esk_xtensa_irq_write(&w,227,UINT32_MAX),ES_OK);CHECK_EQ(esk_xtensa_irq_pending(&w),1u<<14);
    CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,17);CHECK_EQ(esk_xtensa_irq_pending(&w),0);
    CHECK_EQ(execute(&w,0x003710,&event),ES_OK);CHECK_EQ(w.ps&15u,15);
    init(&w);w.ps|=32u|64u;
    CHECK_EQ(execute(&w,0x007000,&event),ES_OK);CHECK_EQ(event,8);CHECK_EQ(w.exccause,8);CHECK_EQ(w.irq.waiting,0);
    CHECK_EQ(execute(&w,0x003000,&event),ES_OK); /* return to unprivileged context */
    CHECK_EQ(execute(&w,0x03e220,&event),ES_OK);CHECK_EQ(event,8);CHECK_EQ(w.exccause,8);
    w.irq.waiting=1;esk_xtensa_exception_configure(&w,NULL);
    CHECK_EQ(w.irq.enabled,0);CHECK_EQ(w.irq.waiting,0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;
    RUN(exhaustive_masks);RUN(pending_and_edges);RUN(nesting_and_nmi);RUN(wait_and_registers);RUN(fetched_handler);RUN(configuration_and_privilege);
TEST_MAIN_END
