/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "mcu/xtensa/windows.h"
#include "mcu/xtensa/integer.h"
#include <string.h>

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
    else if (op==7 && (r==1 || r==9 || r==2 || r==10 || r==3 || r==11)) *mask=bit(s)|bit(t);
    else if (op==12 && t>=8) *mask=bit(s);
    else if (op==8 || op==9 || (op==2 && (r<=2 || r==4 || r==5 || r==6 || r==9))) *mask=bit(s)|bit(t);
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
    save(w); w->base=victim; load(w); w->cpu.pc=w->vectors[event-1];
    return event;
}
es_status esk_xtensa_window_execute(esk_xtensa_windows *w, const esk_xtensa_memory *memory,
                                    const unsigned char *code, size_t available,
                                    size_t *consumed, unsigned *event, esk_xtensa_fault *fault)
{
    esk_xtensa_windows next;
    uint32_t word,pc,mask=0,target=0; unsigned width,op,t,s,n=0,kind=0,e,flow=1;
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
    op=word&15u; t=(word>>4)&15u; s=(word>>8)&15u;
    next=*w; save(&next);
    if (op==5 && ((word>>4)&3u)) { /* CALL4/8/12 */
        n=(word>>4)&3u; kind=1; mask=bit(n*4);
        target=(pc&UINT32_C(0xfffffffc))+4+((((word>>6)^UINT32_C(0x20000))-UINT32_C(0x20000))<<2);
    } else if ((word&UINT32_C(0xfff00f))==0 && t>=13) { /* CALLX4/8/12 */
        n=t-12; kind=1; mask=bit(n*4)|bit(s); target=next.cpu.a[s];
    } else if ((word&255u)==0x36) {
        kind=2; n=(next.ps>>16)&3u;
        if (s>3 || !(next.ps&ESK_XT_PS_WOE) || (next.ps&ESK_XT_PS_EXCM))
            return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        mask=bit(n*4+s)|bit(s);
    } else if (word==0x000090 || word==0xf01d) kind=3;
    else if (word==0x003400 || word==0x003500) kind=4;
    else if ((word&UINT32_C(0xffff0f))==0x408000) kind=5;
    else if ((word&UINT32_C(0xff000f))==0x030000 || (word&UINT32_C(0xff000f))==0x130000) {
        unsigned reg=(word>>8)&255u;
        if (reg>2 && reg!=72 && reg!=73 && reg!=177 && reg!=230) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        kind=6; mask=bit(t);
    }
    else if (op==6 && t==7 && ((word>>12)&15u)>=8 && ((word>>12)&15u)<=10) { kind=8; mask=bit(s); }
    else if ((word&UINT32_C(0xff000f))==0x090000 || (word&UINT32_C(0xff000f))==0x490000) {
        if (!(next.ps&ESK_XT_PS_EXCM)) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        kind=9; /* L32E/S32E in exception mode only */
    }
    else if (word==0x002010) kind=7; /* RSYNC; no asynchronous pipeline here */
    else if (references(code,width,word,&mask)!=ES_OK) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
    if (kind>=4 && kind<=6 && !(kind==6 && ((word>>8)&255u)<=2) && !(next.ps&ESK_XT_PS_EXCM) && ((next.ps>>6)&3u))
        return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word); /* privilege dispatch not yet modelled */
    e=overflow(&next,mask);
    if (e) { *w=next; *event=e; return ES_OK; }
    if (kind==1) {
        next.ps=(next.ps&~UINT32_C(0x30000)) | (n<<16);
        next.cpu.a[n*4]=(n<<30)|((pc+3)&UINT32_C(0x3fffffff)); next.cpu.pc=target; flow=0;
    } else if (kind==2) {
        uint32_t sp=next.cpu.a[s]-((word>>12)<<3);
        next.base=(next.base+n)&15u; next.start|=bit(next.base);
        next.ar[(next.base*4+s)&63u]=sp; load(&next); next.cpu.pc=pc+3;
    } else if (kind==3) {
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
            load(&next); e=n*2; next.cpu.pc=next.vectors[e-1]; *w=next; *event=e; return ES_OK;
        }
        flow=0;
        /* CALLINC is preserved; the ISA permits implementation-dependent update. */
    } else if (kind==4) {
        if (!(next.ps&ESK_XT_PS_EXCM)) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        if (word==0x003400) next.start&=~bit(next.base); else next.start|=bit(next.base);
        next.base=(next.ps>>8)&15u; next.ps&=~ESK_XT_PS_EXCM; load(&next); next.cpu.pc=next.epc1; flow=0;
    } else if (kind==5) {
        next.base=(next.base+t)&15u; /* modulo 16 is identical for signed imm4 */
        load(&next); next.cpu.pc=pc+3;
    } else if (kind==6) {
        unsigned reg=(word>>8)&255u; uint32_t value=next.cpu.a[t];
        int write=(word&UINT32_C(0x100000))!=0;
        if (reg>2 && reg!=72 && reg!=73 && reg!=177 && reg!=230) return reject(fault,ESK_XT_UNSUPPORTED,pc,pc,word);
        if (write) {
            if (reg==0) next.lbeg=value;
            else if (reg==1) next.lend=value;
            else if (reg==2) next.lcount=value;
            else if (reg==72) { next.base=value&15u; load(&next); }
            else if (reg==73) next.start=value&65535u;
            else if (reg==177) next.epc1=value;
            else next.ps=value;
        } else next.cpu.a[t]=reg==0 ? next.lbeg : reg==1 ? next.lend : reg==2 ? next.lcount :
                              reg==72 ? next.base : reg==73 ? next.start : reg==177 ? next.epc1 : next.ps;
        next.cpu.pc=pc+3;
    } else if (kind==8) {
        unsigned condition=(word>>12)&15u; uint32_t count=next.cpu.a[s];
        next.lcount=count-1u; next.lbeg=pc+3; next.lend=pc+4+(word>>16);
        next.cpu.pc=pc+3;
        if ((condition==9 && !count) || (condition==10 && (!count || (count>>31)))) {
            next.cpu.pc=next.lend; flow=0;
        }
    } else if (kind==9) {
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
    } else if (kind==7) next.cpu.pc=pc+3;
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
    for (i=0;i<width;i++) {
        uint32_t address=w->cpu.pc+i;
        if (!memory || !memory->read || memory->read(memory->ctx,address,1,&value)!=ES_OK)
            return reject(fault,ESK_XT_FETCH,w->cpu.pc,address,0);
        code[i]=(unsigned char)(value&255u);
        if (i==0 && (code[0]&15u)<8) width=3;
    }
    return esk_xtensa_window_execute(w,memory,code,width,&used,event,fault);
}
