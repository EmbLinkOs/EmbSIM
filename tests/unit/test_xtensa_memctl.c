/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/windows.h"
#define BASE UINT32_C(0x40080000)
typedef struct fixture { unsigned char code[1024];unsigned reads;uint32_t fail; } fixture;
static es_status fetch(void *ctx,uint32_t address,unsigned bytes,uint32_t *value)
{
    fixture *f=(fixture *)ctx;f->reads++;
    if(bytes!=1 || address<BASE || address-BASE>=sizeof f->code || address==f->fail) return ES_EINVAL;
    *value=f->code[address-BASE];return ES_OK;
}
static es_status execute(esk_xtensa_windows *w,uint32_t word,unsigned *event)
{
    unsigned char code[3];size_t used;esk_xtensa_fault fault;
    code[0]=(unsigned char)(word&255u);code[1]=(unsigned char)((word>>8)&255u);code[2]=(unsigned char)((word>>16)&255u);
    return esk_xtensa_window_execute(w,NULL,code,3,&used,event,&fault);
}
static void setup(esk_xtensa_windows *w,fixture *f)
{
    unsigned i;esk_xtensa_windows_init(w,BASE,NULL);CHECK_EQ(esk_xtensa_memctl_configure(w),ES_OK);
    memset(f,0,sizeof *f);for(i=0;i<sizeof f->code;i+=2) { f->code[i]=0x3d;f->code[i+1]=0xf0; }
    w->ps=0;w->lbeg=BASE;w->lend=BASE+256;w->lcount=1;
}
static void register_contract(void)
{
    unsigned reg,form,value,event,bit;esk_xtensa_windows w,before;fixture f;
    const uint32_t forms[3]={0x036100,0x136100,0x616100};
    CHECK_EQ(esk_xtensa_memctl_configure(NULL),ES_EINVAL);
    esk_xtensa_windows_init(&w,BASE,NULL);before=w;
    CHECK_EQ(execute(&w,0x036120,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    for(reg=0;reg<16;reg++) for(form=0;form<3;form++) for(value=0;value<2;value++) {
        setup(&w,&f);w.memctl=1-value;w.cpu.a[reg]=value;
        CHECK_EQ(execute(&w,forms[form]|(reg<<4),&event),ES_OK);CHECK_EQ(event,0);
        CHECK_EQ(w.memctl,form ? value : 1-value);CHECK_EQ(w.cpu.a[reg],form==1 ? value : 1-value);
    }
    for(bit=1;bit<32;bit++) {
        setup(&w,&f);w.cpu.a[2]=UINT32_C(1)<<bit;before=w;
        CHECK_EQ(execute(&w,0x136120,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
        CHECK_EQ(execute(&w,0x616120,&event),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    }
    setup(&w,&f);CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);w.ps=64;w.cpu.a[2]=0;
    CHECK_EQ(execute(&w,0x136120,&event),ES_OK);CHECK_EQ(event,7);CHECK_EQ(w.exccause,8);CHECK_EQ(w.memctl,1);
}
static void prefix_capacity_and_disable(void)
{
    unsigned length,step,event,i;esk_xtensa_windows w;esk_xtensa_fault fault;fixture f;
    esk_xtensa_memory bus={&f,NULL,NULL,NULL,fetch,NULL};
    for(length=2;length<=512;length+=2) {
        setup(&w,&f);w.lend=BASE+length;w.lcount=2;
        for(step=0;step<length*3/2;step++) { CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);CHECK_EQ(event,0); }
        CHECK_EQ(w.cpu.pc,BASE+length);CHECK_EQ(w.lcount,0);
        CHECK_EQ(f.reads,length+(length>256 ? (length-256)*2 : 0));
        w.cpu.a[2]=0;CHECK_EQ(execute(&w,0x136120,&event),ES_OK);
        for(i=0;i<8;i++) CHECK_EQ(w.loopbuf_valid[i],0);
        w.cpu.pc=BASE;w.lcount=1;f.reads=0;
        for(step=0;step<length;step++) CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);
        CHECK_EQ(f.reads,length*2);
    }
}
static void barriers_bounds_reset_and_isolation(void)
{
    unsigned event,reads,i;esk_xtensa_windows w,other;esk_xtensa_fault fault;fixture f;
    esk_xtensa_memory bus={&f,NULL,NULL,NULL,fetch,NULL};
    setup(&w,&f);CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);reads=f.reads;
    w.cpu.pc=BASE;f.fail=BASE;CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);CHECK_EQ(f.reads,reads);
    CHECK_EQ(execute(&w,0x002000,&event),ES_OK);w.cpu.pc=BASE;
    CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_EINVAL);CHECK_EQ(fault.kind,ESK_XT_FETCH);
    f.fail=0;CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);
    w.cpu.a[2]=BASE;CHECK_EQ(execute(&w,0x130020,&event),ES_OK);
    for(i=0;i<8;i++) CHECK_EQ(w.loopbuf_valid[i],0);
    w.cpu.pc=BASE;CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);
    other=w;CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);CHECK_EQ(w.memctl,1);
    for(i=0;i<8;i++) CHECK_EQ(w.loopbuf_valid[i],0);
    CHECK(other.loopbuf_valid[0]!=0); /* core state is independent */
    { const uint32_t vectors[3]={0x40080300,0x40080340,0x400803c0};esk_xtensa_exception_configure(&other,vectors); }
    CHECK_EQ(esk_xtensa_region_configure(&other),ES_OK);other.region[0][2]=15;other.cpu.pc=BASE;
    CHECK_EQ(execute(&other,0x002000,&event),ES_OK);other.cpu.pc=BASE;
    reads=f.reads;CHECK_EQ(esk_xtensa_window_step(&other,&bus,&event,&fault),ES_OK);CHECK_EQ(event,7);CHECK_EQ(other.exccause,20);CHECK_EQ(f.reads,reads);
}
static void failed_fetch_and_decode_rollback(void)
{
    unsigned event;esk_xtensa_windows w,before;esk_xtensa_fault fault;fixture f;
    esk_xtensa_memory bus={&f,NULL,NULL,NULL,fetch,NULL};
    setup(&w,&f);f.fail=BASE+1;before=w;
    CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_EINVAL);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    setup(&w,&f);f.code[0]=0xff;f.code[1]=0xff;before=w;
    CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_EUNSUPPORTED);CHECK_EQ(memcmp(&w,&before,sizeof w),0);
    setup(&w,&f);w.ps=ESK_XT_PS_EXCM;
    CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);CHECK_EQ(w.loopbuf_valid[0],0);
}
static void mixed_width_and_self_modifying_flush(void)
{
    unsigned event,step;esk_xtensa_windows w;esk_xtensa_fault fault;fixture f;
    esk_xtensa_memory bus={&f,NULL,NULL,NULL,fetch,NULL};
    setup(&w,&f);w.lend=BASE+257;w.lcount=2;
    f.code[254]=0xf0;f.code[255]=0x20;f.code[256]=0;
    for(step=0;step<384;step++) { CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);CHECK_EQ(event,0); }
    CHECK_EQ(w.cpu.pc,BASE+257);CHECK_EQ(f.reads,259);
    setup(&w,&f);CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);
    f.code[0]=0x0c;f.code[1]=0x72;w.cpu.pc=BASE;
    CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);CHECK_EQ(w.cpu.a[2],0);
    CHECK_EQ(execute(&w,0x002000,&event),ES_OK);w.cpu.pc=BASE;
    CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);CHECK_EQ(w.cpu.a[2],7);
    CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);w.cpu.pc=BASE;w.ps=0;
    CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);CHECK(w.loopbuf_valid[0]!=0);
    w.irq.intenable=UINT32_C(1)<<19;CHECK_EQ(esk_xtensa_irq_line(&w,19,1),ES_OK);
    CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);CHECK_EQ(event,12);CHECK_EQ(w.loopbuf_valid[0],0);
    CHECK_EQ(esk_xtensa_lx6_reset(&w,1),ES_OK);w.cpu.pc=BASE;w.ps=0;
    CHECK_EQ(esk_xtensa_window_step(&w,&bus,&event,&fault),ES_OK);CHECK(w.loopbuf_valid[0]!=0);
    w.irq.intenable=UINT32_C(1)<<19;CHECK_EQ(esk_xtensa_irq_line(&w,19,1),ES_OK);
    CHECK_EQ(esk_xtensa_irq_poll(&w,&event),ES_OK);CHECK_EQ(event,12);CHECK_EQ(w.loopbuf_valid[0],0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(register_contract);RUN(prefix_capacity_and_disable);RUN(barriers_bounds_reset_and_isolation);RUN(failed_fetch_and_decode_rollback);RUN(mixed_width_and_self_modifying_flush);
TEST_MAIN_END
