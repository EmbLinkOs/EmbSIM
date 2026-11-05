/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "mcu/xtensa/windows.h"
#include "mcu/xtensa/integer.h"
#include <string.h>

enum execution_kind {
    XT_BASE, XT_CALL, XT_ENTRY, XT_RETW, XT_WINDOW_RETURN, XT_ROTW,
    XT_SPECIAL, XT_RSYNC, XT_LOOP, XT_EXCEPTION_MEMORY,
    XT_SAR_SETUP=11, XT_SAR_SHIFT, XT_FENCE, XT_RSIL, XT_SYSCALL,
    XT_EXCEPTION_RETURN, XT_ILLEGAL, XT_RFI, XT_WAITI, XT_ATOMIC, XT_DIVIDE, XT_THREADPTR, XT_MOVSP
};

static uint32_t bit(unsigned n) { return UINT32_C(1) << (n & 15u); }
static void save(esk_xtensa_windows *w)
{
    unsigned i; for (i=0;i<16;i++) w->ar[(w->base*4+i)&63u]=w->cpu.a[i];
}
static void load(esk_xtensa_windows *w)
{
    unsigned i; for (i=0;i<16;i++) w->cpu.a[i]=w->ar[(w->base*4+i)&63u];
}
static es_status reject(esk_xtensa_fault *f, unsigned kind, uint32_t pc, uint32_t addr, uint32_t op)
{
    if (f) { f->kind=kind; f->pc=pc; f->address=addr; f->opcode=op; }
    return kind==ESK_XT_UNSUPPORTED ? ES_EUNSUPPORTED : ES_EINVAL;
}
void esk_xtensa_windows_init(esk_xtensa_windows *w, uint32_t pc, const uint32_t vectors[6])
{
    if (!w) return;
    memset(w,0,sizeof *w); w->cpu.pc=pc; w->ps=ESK_XT_PS_WOE; w->start=1;
    if (vectors) memcpy(w->vectors,vectors,sizeof w->vectors);
}
es_status esk_xtensa_prid_configure(esk_xtensa_windows *w,uint32_t value)
{
    if(!w) return ES_EINVAL;
    w->prid=value;w->prid_enabled=1;return ES_OK;
}
es_status esk_xtensa_vecbase_configure(esk_xtensa_windows *w,uint32_t base)
{
    unsigned i;static const uint32_t general[3]={0x300,0x340,0x3c0};
    if(!w) return ES_EINVAL;
    if(base&1023u) return ES_EUNSUPPORTED;
    w->vecbase=base;w->vecbase_enabled=1;
    for(i=0;i<6;i++) { w->vectors[i]=base+i*64u;w->irq.vectors[i+2]=base+0x180u+i*64u; }
    for(i=0;i<3;i++) w->exception_vectors[i]=base+general[i];
    return ES_OK;
}
es_status esk_xtensa_lx6_reset(esk_xtensa_windows *w,unsigned vector_input)
{
    if(!w || vector_input>1) return ES_EINVAL;
    save(w);w->base&=15u;w->start&=65535u;
    w->ps=0x1f;w->lcount=0;w->cpu.pc=vector_input ? 0x40000400u : 0x50000000u;
    w->exceptions_enabled=1;w->irq.enabled=1;w->irq.waiting=0;
    /* INTERRUPT is undefined at reset. Choose no pending requests; the SoC
     * provider re-drives its live levels after releasing reset. */
    w->irq.lines=w->irq.latched=w->irq.last_level=w->irq.last_pending=0;
    return esk_xtensa_vecbase_configure(w,0x40000000);
}
void esk_xtensa_exception_configure(esk_xtensa_windows *w, const uint32_t vectors[3])
{
    if (!w) return;
    w->exceptions_enabled=vectors ? 1u : 0u;
    if (!vectors) { w->irq.enabled=0; w->irq.waiting=0; }
    if (vectors) memcpy(w->exception_vectors,vectors,sizeof w->exception_vectors);
    if(w->vecbase_enabled) (void)esk_xtensa_vecbase_configure(w,w->vecbase);
}
es_status esk_xtensa_raise_exception(esk_xtensa_windows *w, unsigned cause,
                                     uint32_t address, int has_address, unsigned *event)
{
    unsigned index;
    if (event) *event=0;
    if (!w || !event || cause>63) return ES_EINVAL;
    if (!w->exceptions_enabled) return ES_EUNSUPPORTED;
    if (w->ps&ESK_XT_PS_EXCM) { w->depc=w->cpu.pc; index=2; }
    else { w->epc1=w->cpu.pc; index=(w->ps&32u) ? 1u : 0u; }
    w->exccause=cause;
    if (has_address) w->excvaddr=address;
    w->ps|=ESK_XT_PS_EXCM;
    w->cpu.pc=w->vecbase_enabled ? w->vecbase+(index==2 ? 0x3c0u : 0x300u+index*64u) : w->exception_vectors[index];
    *event=7u+index;
    return ES_OK;
}
static int special_valid(const esk_xtensa_windows *w,unsigned reg)
{
    return (w->irq.enabled && esk_xtensa_irq_special(reg)) || reg<=3 || reg==12 || reg==72 || reg==73 || reg==177 || reg==192 || reg==230 || (reg==231 && w->vecbase_enabled) || reg==232 || reg==238 || (reg>=244 && reg<=247) || (reg==235 && w->prid_enabled);
}
static uint32_t special_read(const esk_xtensa_windows *w,unsigned reg)
{
    if(reg>=244 && reg<=247) return w->misc[reg-244];
    if (esk_xtensa_irq_special(reg)) { uint32_t value=0; (void)esk_xtensa_irq_read(w,reg,&value); return value; }
    switch (reg) {
    case 0:return w->lbeg; case 1:return w->lend; case 2:return w->lcount; case 3:return w->sar;
    case 12:return w->scompare1; case 72:return w->base; case 73:return w->start; case 177:return w->epc1;
    case 192:return w->depc; case 230:return w->ps&ESK_XT_PS_MASK; case 235:return w->prid; case 231:return w->vecbase; case 232:return w->exccause; default:return w->excvaddr;
    }
}
static void special_write(esk_xtensa_windows *w,unsigned reg,uint32_t value)
{
    if(reg>=244 && reg<=247) { w->misc[reg-244]=value;return; }
    if (esk_xtensa_irq_special(reg)) { (void)esk_xtensa_irq_write(w,reg,value); return; }
    switch (reg) {
    case 0:w->lbeg=value;break; case 1:w->lend=value;break; case 2:w->lcount=value;break;
    case 3:w->sar=value&63u;break;
    case 12:w->scompare1=value;break;
    case 72:w->base=value&15u;load(w);break;
    case 73:w->start=value&65535u;break;
    case 177:w->epc1=value;break; case 192:w->depc=value;break;
    case 230:w->ps=value&ESK_XT_PS_MASK;break; case 231:(void)esk_xtensa_vecbase_configure(w,value);break; case 232:w->exccause=value;break; default:w->excvaddr=value;break;
    }
}
/* Register references are checked before any access. Zero/compare branches do
 * not reference their instruction fields as destination register numbers. */
static es_status references(const unsigned char *code, unsigned width, uint32_t word, uint32_t *mask)
{
    unsigned op=word&15u,t=(word>>4)&15u,s=(word>>8)&15u,r=(word>>12)&15u;
    if (esk_xtensa_integer_references(code,width,mask)==ES_OK) return ES_OK;
    *mask=0;
    if (word==0x0020f0 || word==0xf03d) return ES_OK;
    if (word==0x000080 || word==0xf00d) *mask=1;
    else if ((word&UINT32_C(0xfff0ff))==0x0000a0) *mask=bit(s);
    else if ((word&UINT32_C(0xfff0ff))==0x0000c0) *mask=bit(s)|1u;
    else if ((word&63u)==5) *mask=1;
    else if ((word&63u)==6) return ES_OK;
    else if (op==6 && ((t&3u)==1 || t==2 || t==6 || t==10 || t==14 || t==11 || t==15)) *mask=bit(s);
    else if (op==7) *mask=(r==6 || r==7 || r==14 || r==15) ? bit(s) : bit(s)|bit(t);
    else if (op==12 && t>=8) *mask=bit(s);
    else if (op==8 || op==9 || (op==2 && (r<=2 || r==4 || r==5 || r==6 || r==9 || r==11 || r==15))) *mask=bit(s)|bit(t);
    else if (op==1) *mask=bit(t);
    else return ES_EUNSUPPORTED;
    return ES_OK;
}
/* Lazy overflow: only the actually referenced register groups matter. */
static unsigned overflow(esk_xtensa_windows *w, uint32_t mask)
{
    unsigned max=0,i,n=0,victim,event;
    if (!(w->ps&ESK_XT_PS_WOE) || (w->ps&ESK_XT_PS_EXCM)) return 0;
    for (i=4;i<16;i++) if (mask&(1u<<i)) max=i/4;
    for (i=1;i<=max;i++) if (w->start&bit(w->base+i)) { n=i; break; }
    if (!n) return 0;
    victim=(w->base+n)&15u;
    event=(w->start&bit(victim+1)) ? 1u : (w->start&bit(victim+2)) ? 3u : 5u;
    w->epc1=w->cpu.pc;
    w->ps=(w->ps&~UINT32_C(0xf00)) | (w->base<<8) | ESK_XT_PS_EXCM;
    save(w); w->base=victim; load(w); w->cpu.pc=w->vecbase_enabled ? w->vecbase+(event-1u)*64u : w->vectors[event-1];
    return event;
}
es_status esk_xtensa_window_execute(esk_xtensa_windows *w, const esk_xtensa_memory *memory,
                                    const unsigned char *code, size_t available,
                                    size_t *consumed, unsigned *event, esk_xtensa_fault *fault)
{
    esk_xtensa_windows next;
    uint32_t word,pc,mask=0,target=0; unsigned width,op,t,s,r,n=0,e,flow=1;
    enum execution_kind kind=XT_BASE;
    es_status st;
    if (consumed) *consumed=0;
    if (event) *event=0;
    if (fault) memset(fault,0,sizeof *fault);
    if (!w || !code || !consumed || !event) return ES_EINVAL;
    pc=w->cpu.pc;
    if (available<2) return reject(fault,ESK_XT_TRUNCATED,pc,pc,0);
    word=(uint32_t)code[0]|(uint32_t)code[1]<<8; width=(word&15u)<8 ? 3u : 2u;
    if (available<width) return reject(fault,ESK_XT_TRUNCATED,pc,pc,word);
    if (width==3) word|=(uint32_t)code[2]<<16;
    op=word&15u; t=(word>>4)&15u; s=(word>>8)&15u; r=(word>>12)&15u;
    next=*w; save(&next);
    if (op==5 && ((word>>4)&3u)) { /* CALL4/8/12 */
        n=(word>>4)&3u; kind=XT_CALL; mask=bit(n*4);
        target=(pc&UINT32_C(0xfffffffc))+4+((((word>>6)^UINT32_C(0x20000))-UINT32_C(0x20000))<<2);
    } else if ((word&UINT32_C(0xfff00f))==0 && t>=13) { /* CALLX4/8/12 */
        n=t-12; kind=XT_CALL; mask=bit(n*4)|bit(s); target=next.cpu.a[s];
    } else if ((word&255u)==0x36) {
        kind=XT_ENTRY; n=(next.ps>>16)&3u;
        if (s>3 || !(next.ps&ESK_XT_PS_WOE) || (next.ps&ESK_XT_PS_EXCM))
            return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        mask=bit(n*4+s)|bit(s);
    } else if ((word&UINT32_C(0xfff00f))==0x001000) { kind=XT_MOVSP;mask=bit(s)|bit(t); }
    else if (word==0x000090 || word==0xf01d) kind=XT_RETW;
    else if (word==0x003400 || word==0x003500) kind=XT_WINDOW_RETURN;
    else if ((word&UINT32_C(0xffff0f))==0x408000) kind=XT_ROTW;
    else if ((word&UINT32_C(0xff000f))==0x030000 || (word&UINT32_C(0xff000f))==0x130000 || (word&UINT32_C(0xff000f))==0x610000) {
        unsigned reg=(word>>8)&255u;
        if (!special_valid(&next,reg) || (reg==227 && (word&UINT32_C(0xff000f))==0x030000) || ((word&UINT32_C(0xff000f))==0x610000 && reg!=3 && reg!=12 && reg!=230 && reg!=231 && !(reg>=244 && reg<=247)) || (reg==235 && (word&UINT32_C(0xff000f))!=0x030000))
            return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        kind=XT_SPECIAL; mask=bit(t);
    }
    else if ((word&UINT32_C(0xff000f))==0xe30000 && ((word>>4)&255u)==231) {
        kind=XT_THREADPTR;n=0;mask=bit(r);
    } else if ((word&UINT32_C(0xff000f))==0xf30000 && ((word>>8)&255u)==231) {
        kind=XT_THREADPTR;n=1;mask=bit(t);
    }
    else if (op==6 && t==7 && ((word>>12)&15u)>=8 && ((word>>12)&15u)<=10) { kind=XT_LOOP; mask=bit(s); }
    else if ((word&UINT32_C(0xff000f))==0x090000 || (word&UINT32_C(0xff000f))==0x490000) {
        if (!(next.ps&ESK_XT_PS_EXCM)) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        kind=XT_EXCEPTION_MEMORY; /* L32E/S32E in exception mode only */
    }
    else if ((word&UINT32_C(0xffc0ff))==0x400000) {
        kind=XT_SAR_SETUP; n=r; mask=bit(s); /* SSR/SSL/SSA8L/SSA8B */
    } else if ((word&UINT32_C(0xfff0ef))==0x404000) { kind=XT_SAR_SETUP; n=4; }
    else if ((word&UINT32_C(0xff000f))==0xa10000 && !t) { kind=XT_SAR_SHIFT; n=0; mask=bit(r)|bit(s); }
    else if ((word&UINT32_C(0xff000f))==0x910000 && !s) { kind=XT_SAR_SHIFT; n=1; mask=bit(r)|bit(t); }
    else if ((word&UINT32_C(0xff000f))==0xb10000 && !s) { kind=XT_SAR_SHIFT; n=2; mask=bit(r)|bit(t); }
    else if ((word&UINT32_C(0xff000f))==0x810000) { kind=XT_SAR_SHIFT; n=3; mask=bit(r)|bit(s)|bit(t); }
    else if (word==0x0020c0 || word==0x002000 || word==0x002020) kind=XT_FENCE;
    else if ((word&UINT32_C(0xfff00f))==0x006000) { kind=XT_RSIL; mask=bit(t); }
    else if (word==0x005000) kind=XT_SYSCALL;
    else if (word==0 || word==0xf06d) kind=XT_ILLEGAL;
    else if (word==0x003000 || word==0x003200) kind=XT_EXCEPTION_RETURN;
    else if ((word&UINT32_C(0xfff0ff))==0x003010 && s>=2 && s<=7 && next.irq.enabled) kind=XT_RFI;
    else if ((word&UINT32_C(0xfff0ff))==0x007000 && next.irq.enabled) kind=XT_WAITI;
    else if (op==2 && r==14) { kind=XT_ATOMIC;mask=bit(s)|bit(t); }
    else if ((word&UINT32_C(0x0f000f))==0x020000 && (word>>20)>=12) { kind=XT_DIVIDE;mask=bit(r)|bit(s)|bit(t); }
    else if (word==0x002010) kind=XT_RSYNC; /* RSYNC; no asynchronous pipeline here */
    else if (references(code,width,word,&mask)!=ES_OK) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
    if ((kind==XT_WINDOW_RETURN || kind==XT_ROTW ||
         (kind==XT_SPECIAL && ((word>>8)&255u)>=64) || kind==XT_RSIL || kind==XT_EXCEPTION_RETURN || kind==XT_RFI || kind==XT_WAITI) &&
        !(next.ps&ESK_XT_PS_EXCM) && ((next.ps>>6)&3u)) {
        if (!next.exceptions_enabled) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        st=esk_xtensa_raise_exception(&next,8,0,0,event); if (st==ES_OK) *w=next; return st;
    }
    e=overflow(&next,mask);
    if (e) { *w=next; *event=e; return ES_OK; }
    if (kind==XT_MOVSP) {
        if (!(next.start&(bit(next.base-1u)|bit(next.base-2u)|bit(next.base-3u)))) {
            if(!next.exceptions_enabled) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
            st=esk_xtensa_raise_exception(&next,5,0,0,event);if(st==ES_OK) *w=next;return st;
        }
        next.cpu.a[t]=next.cpu.a[s];next.cpu.pc=pc+3;
    } else if (kind==XT_CALL) {
        next.ps=(next.ps&~UINT32_C(0x30000)) | (n<<16);
        next.cpu.a[n*4]=(n<<30)|((pc+3)&UINT32_C(0x3fffffff)); next.cpu.pc=target; flow=0;
    } else if (kind==XT_ENTRY) {
        uint32_t sp=next.cpu.a[s]-((word>>12)<<3);
        next.base=(next.base+n)&15u; next.start|=bit(next.base);
        next.ar[(next.base*4+s)&63u]=sp; load(&next); next.cpu.pc=pc+3;
    } else if (kind==XT_RETW) {
        unsigned back,nearest=0,old=next.base;
        uint32_t link=next.cpu.a[0],return_pc=(pc&UINT32_C(0xc0000000))|(link&UINT32_C(0x3fffffff));
        n=link>>30;
        for (back=1;back<=3;back++) if (next.start&bit(old-back)) { nearest=back; break; }
        if (!n || (nearest && nearest!=n) || !(next.ps&ESK_XT_PS_WOE) || (next.ps&ESK_XT_PS_EXCM))
            return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        next.base=(old-n)&15u;
        if (next.start&bit(next.base)) { next.start&=~bit(old); load(&next); next.cpu.pc=return_pc; }
        else {
            next.epc1=pc; next.ps=(next.ps&~UINT32_C(0xf00))|(old<<8)|ESK_XT_PS_EXCM;
            load(&next); e=n*2; next.cpu.pc=next.vecbase_enabled ? next.vecbase+(e-1u)*64u : next.vectors[e-1]; *w=next; *event=e; return ES_OK;
        }
        flow=0;
        /* CALLINC is preserved; the ISA permits implementation-dependent update. */
    } else if (kind==XT_WINDOW_RETURN) {
        if (!(next.ps&ESK_XT_PS_EXCM)) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        if (word==0x003400) next.start&=~bit(next.base); else next.start|=bit(next.base);
        next.base=(next.ps>>8)&15u; next.ps&=~ESK_XT_PS_EXCM; load(&next); next.cpu.pc=next.epc1; flow=0;
    } else if (kind==XT_ROTW) {
        next.base=(next.base+t)&15u; /* modulo 16 is identical for signed imm4 */
        load(&next); next.cpu.pc=pc+3;
    } else if (kind==XT_SPECIAL) {
        unsigned reg=(word>>8)&255u; uint32_t value=next.cpu.a[t],old=special_read(&next,reg);
        uint32_t form=word&UINT32_C(0xff000f);
        if (form==0x030000) next.cpu.a[t]=old;
        else {
            if(reg==231 && (value&1023u)) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
            special_write(&next,reg,value);if(form==0x610000) next.cpu.a[t]=old;
        }
        next.cpu.pc=pc+3;
    } else if (kind==XT_THREADPTR) {
        if(n) next.threadptr=next.cpu.a[t];else next.cpu.a[r]=next.threadptr;
        next.cpu.pc=pc+3;
    } else if (kind==XT_LOOP) {
        unsigned condition=(word>>12)&15u; uint32_t count=next.cpu.a[s];
        next.lcount=count-1u; next.lbeg=pc+3; next.lend=pc+4+(word>>16);
        next.cpu.pc=pc+3;
        if ((condition==9 && !count) || (condition==10 && (!count || (count>>31)))) {
            next.cpu.pc=next.lend; flow=0;
        }
    } else if (kind==XT_EXCEPTION_MEMORY) {
        uint32_t addr=next.cpu.a[s]+((((word>>12)&15u)-16u)<<2),value=0;
        int write=(word&UINT32_C(0x400000))!=0;
        if (addr&3u) return reject(fault,ESK_XT_ALIGN,pc,addr,word);
        if (write) {
            if (!memory || !memory->write || memory->write(memory->ctx,addr,4,next.cpu.a[t])!=ES_OK)
                return reject(fault,ESK_XT_STORE,pc,addr,word);
        } else {
            if (!memory || !memory->read || memory->read(memory->ctx,addr,4,&value)!=ES_OK)
                return reject(fault,ESK_XT_LOAD,pc,addr,word);
            next.cpu.a[t]=value;
        }
        next.cpu.pc=pc+3;
    } else if (kind==XT_SAR_SETUP) {
        uint32_t value=next.cpu.a[s];
        next.sar=n==0 ? value&31u : n==1 ? 32u-(value&31u) : n==2 ? (value&3u)*8u :
                 n==3 ? 32u-(value&3u)*8u : s|((t&1u)<<4);
        next.cpu.pc=pc+3;
    } else if (kind==XT_SAR_SHIFT) {
        uint64_t joined;
        if (next.sar>32) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        if (n==0) joined=(uint64_t)next.cpu.a[s]<<32;
        else if (n==1) joined=next.cpu.a[t];
        else if (n==2) joined=(uint64_t)next.cpu.a[t] | ((next.cpu.a[t]>>31) ? UINT64_C(0xffffffff00000000) : 0);
        else joined=(uint64_t)next.cpu.a[s]<<32 | next.cpu.a[t];
        next.cpu.a[r]=(uint32_t)(joined>>next.sar); next.cpu.pc=pc+3;
    } else if (kind==XT_FENCE) next.cpu.pc=pc+3; /* fully serialized functional bus, no pipeline/cache claim */
    else if (kind==XT_RSIL) { next.cpu.a[t]=next.ps; next.ps=(next.ps&~15u)|s; next.cpu.pc=pc+3; }
    else if (kind==XT_SYSCALL || kind==XT_ILLEGAL) {
        if (!next.exceptions_enabled) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        st=esk_xtensa_raise_exception(&next,kind==XT_SYSCALL ? 1u : 0u,0,0,event); if (st==ES_OK) *w=next; return st;
    } else if (kind==XT_EXCEPTION_RETURN) {
        if (!next.exceptions_enabled) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        if (word==0x003000) { next.ps&=~ESK_XT_PS_EXCM; next.cpu.pc=next.epc1; }
        else next.cpu.pc=next.depc;
        flow=0;
    } else if (kind==XT_ATOMIC) {
        uint32_t address=next.cpu.a[s]+((word>>16)<<2),observed=0;
        if(address&3u) return reject(fault,ESK_XT_ALIGN,pc,address,word);
        if(!memory || !memory->compare_exchange) return reject(fault,ESK_XT_UNSUPPORTED,pc,address,word);
        st=memory->compare_exchange(memory->ctx,address,next.scompare1,next.cpu.a[t],&observed);
        if(st!=ES_OK) return reject(fault,st==ES_EUNSUPPORTED ? ESK_XT_UNSUPPORTED : ESK_XT_ATOMIC,pc,address,word);
        next.cpu.a[t]=observed;next.cpu.pc=pc+3;
    } else if (kind==XT_DIVIDE && !next.cpu.a[t]) {
        if(!next.exceptions_enabled) return reject(fault,ESK_XT_DIV_ZERO,pc,pc,word);
        st=esk_xtensa_raise_exception(&next,6,0,0,event);if(st==ES_OK) *w=next;return st;
    } else if (kind==XT_DIVIDE) {
        st=esk_xtensa_execute_flow(&next.cpu,memory,code,available,consumed,fault,&flow);
        if(st!=ES_OK) return st;
    } else if (kind==XT_RFI) {
        st=esk_xtensa_irq_return(&next,s); if (st!=ES_OK) return st; flow=0;
    } else if (kind==XT_WAITI) {
        next.ps=(next.ps&~15u)|s; next.cpu.pc=pc+3; next.irq.waiting=1; flow=0;
    } else if (kind==XT_RSYNC) next.cpu.pc=pc+3;
    else {
        st=esk_xtensa_execute_flow(&next.cpu,memory,code,available,consumed,fault,&flow);
        if (st!=ES_OK) return st;
    }
    if (flow && !(next.ps&ESK_XT_PS_EXCM) && next.lcount && next.cpu.pc==next.lend) {
        next.lcount--; next.cpu.pc=next.lbeg;
    }
    save(&next); *w=next; *consumed=width; return ES_OK;
}
es_status esk_xtensa_window_step(esk_xtensa_windows *w, const esk_xtensa_memory *memory,
                                 unsigned *event, esk_xtensa_fault *fault)
{
    unsigned char code[3]; unsigned i,width=2; size_t used; uint32_t value;
    if (event) *event=0;
    if (fault) memset(fault,0,sizeof *fault);
    if (!w || !event) return ES_EINVAL;
    if (w->irq.enabled) {
        es_status st=esk_xtensa_irq_poll(w,event);
        if (st!=ES_OK || *event) return st;
        if (w->irq.waiting) { *event=18; return ES_OK; }
    }
    for (i=0;i<width;i++) {
        uint32_t address=w->cpu.pc+i;
        if (!memory || (!memory->fetch && !memory->read) ||
            (memory->fetch ? memory->fetch : memory->read)(memory->ctx,address,1,&value)!=ES_OK)
            return reject(fault,ESK_XT_FETCH,w->cpu.pc,address,0);
        code[i]=(unsigned char)(value&255u);
        if (i==0 && (code[0]&15u)<8) width=3;
    }
    return esk_xtensa_window_execute(w,memory,code,width,&used,event,fault);
}
