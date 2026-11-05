#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""UART bitstream/shared-RAM referee and unchanged ESP-IDF helpers on native core buses.

Source-clock ticks are explicit fixture input, not CPU instruction timing.
Decoded RX frames are external inputs; no waveform/pin/USB/board qualification.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import re
import subprocess
import tempfile
from run_execute import ROOT, Memory, Fault
from run_windows_c import Windows
from run_machine_c import Allocator
from run_matrix import read_header


class Frame(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ['data','bits','parity_enabled','parity','stop_half_bits','divider16','apb']]


def shared_ram_oracle(bus,m,attach,receive,inputs,advance,emit,frames,times,addr,rng):
    """Byte-address referee: shared bytes, independent cursors, no FIFO value cache."""
    ram=bytearray(1024);bases=[[0,128,256],[384,512,640]]
    capacities=[[384,256,128],[384,256,128]]
    heads=[[0]*3 for _ in range(2)];tails=[[0]*3 for _ in range(2)];counts=[[0]*3 for _ in range(2)]
    raw=[2]*3;ena=[0]*3;high=[[0]*3 for _ in range(2)]
    def write(p,name,data):assert bus.write(bus.ctx,addr(name,p),4,data)==0,(p,name,hex(data))
    def read(p,name):
        value=C.c_uint32(0xdeadbeef);assert bus.read(bus.ctx,addr(name,p),4,C.byref(value))==0
        return value.value
    for p in range(3):
        assert attach(m,p,1,1,1,0)==0;write(p,'CONF0',0x0800801c);write(p,'CLKDIV',2)
        write(p,'MEM_CONF',(capacities[0][p]//128<<7)|(capacities[1][p]//128<<3))
    for turn in range(4096):
        p=rng.randrange(3);op=rng.randrange(8);data=rng.randrange(256)
        if op==0:
            assert receive(m,p,data,0)==0
            if counts[1][p]==capacities[1][p]:raw[p]|=16
            else:
                ram[bases[1][p]+tails[1][p]]=data
                tails[1][p]=(tails[1][p]+1)%capacities[1][p];counts[1][p]+=1
        elif op==1:
            if counts[1][p]:
                assert read(p,'FIFO')==ram[bases[1][p]+heads[1][p]],('shared RX',turn,p)
                heads[1][p]=(heads[1][p]+1)%capacities[1][p];counts[1][p]-=1
            else:
                value=C.c_uint32(0xdeadbeef)
                assert bus.read(bus.ctx,addr('FIFO',p),4,C.byref(value))!=0 and value.value==0xdeadbeef
        elif op==2:
            result=bus.write(bus.ctx,addr('FIFO',p),4,data)
            if counts[0][p]==capacities[0][p]:assert result!=0
            else:
                assert result==0
                ram[bases[0][p]+tails[0][p]]=data
                tails[0][p]=(tails[0][p]+1)%capacities[0][p];counts[0][p]+=1
        elif op==3:
            wanted=[ram[bases[0][p]+(heads[0][p]+n)%capacities[0][p]] for n in range(counts[0][p])]
            frames.clear();times.clear();assert inputs(m,p,1,0,0)==0
            assert advance(m,p,1000000,emit,None)==0
            assert [f[0] for f in frames]==wanted,('shared TX',turn,p)
            assert times==[(n+1)*640 for n in range(len(wanted))]
            if wanted:raw[p]|=0x4000
            raw[p]|=64;heads[0][p]=tails[0][p];counts[0][p]=0
            assert inputs(m,p,1,1,0)==0
        elif op==4:
            for kind,maximum in [(0,8-p),(1,5-p)]:
                size=(rng.randrange(1,maximum+1)*128) if not counts[kind][p] else capacities[kind][p]
                if size!=capacities[kind][p]:heads[kind][p]=tails[kind][p]=0
                capacities[kind][p]=size;high[kind][p]=rng.randrange(8)
            write(p,'MEM_CONF',(capacities[0][p]//128<<7)|(capacities[1][p]//128<<3)|(high[0][p]<<28)|(high[1][p]<<25))
        elif op==5:
            result=bus.write(bus.ctx,addr('CONF0',p),4,0x0804801c)
            if p==2:assert result!=0
            else:
                assert result==0
                for q in ([0] if p==0 else [1,2]):heads[0][q]=tails[0][q]=counts[0][q]=0
                write(p,'CONF0',0x0800801c)
        elif op==6:
            mask=rng.getrandbits(19);write(p,'INT_CLR',mask);raw[p]&=~mask
        else:
            ena[p]=rng.getrandbits(19);write(p,'INT_ENA',ena[p])
        for q in range(3):
            if counts[1][q]>(96|(high[1][q]<<7)):raw[q]|=1
            if counts[0][q]<(96|(high[0][q]<<7)):raw[q]|=2
            status=read(q,'STATUS');mem=read(q,'MEM_CNT_STATUS')
            assert ((status&255)|((mem&7)<<8))==counts[1][q],('RX count',turn,q)
            assert (((status>>16)&255)|(((mem>>3)&7)<<8))==counts[0][q],('TX count',turn,q)
            assert read(q,'MEM_RX_STATUS')==(heads[1][q]<<2)|(tails[1][q]<<13),('RX cursors',turn,q)
            assert read(q,'INT_RAW')==raw[q],('raw',turn,q,hex(read(q,'INT_RAW')),hex(raw[q]))
            assert read(q,'INT_ST')==raw[q]&ena[q]
    return 4096


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ['uart-reg','uart-ll','uart-struct','misc']:ap.add_argument('--'+n,type=Path,required=True)
    for n in ['compiler','objcopy','linker','nm']:ap.add_argument('--'+n,required=True)
    ap.add_argument('--all-ports',action='store_true',help='qualify UART0/1/2, shared RAM/allocations/reset and RX drain helper')
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0x4000900b)
    reg=read_header(args.uart_reg,'29d5e65d1dd560a339eb6057a93a1c8e2bcc90d730c82a8506fd894bd4869922')
    ll=read_header(args.uart_ll,'b98b4846e92480a24b6710b4dcd025a9c6951e4be37a95ce1fcd0c89df20dda5')
    struct=read_header(args.uart_struct,'4438b0d2289fa7acc417a6df9a5e4a024bcfe7932396955ad8594a907988be91')
    misc=read_header(args.misc,'552e2d4ce5be7d93d228eb3b48bd959d0485290a7476ce5a7de438fe7ab9e152')
    def field(name):return int(re.search(r'#define UART_'+name+r'_S\s+(\d+)',reg)[1])
    bases=[0x3ff40000,0x3ff50000,0x3ff6e000]
    def addr(name,port=0):return bases[port]+int(re.search(r'#define UART_'+name+r'_REG\(i\)\s+\(REG_UART_BASE\(i\)\s*\+\s*(0x[0-9A-Fa-f]+)\)',reg)[1],16)
    names=['ena_intr_mask','disable_intr_mask','get_intraw_mask','get_intsts_mask','clr_intsts_mask','get_intr_ena_status','read_rxfifo','write_txfifo','get_rxfifo_len','get_txfifo_len','txfifo_rst']+(['rxfifo_rst'] if args.all_ports else [])
    helpers=[]
    for name in names:
        match=re.search(r'^(?:FORCE_INLINE_ATTR|static inline) [^\n]*\buart_ll_'+name+r'\([^\n]*\)\n\{',ll,re.M);assert match,name
        end=match.end();depth=1
        while depth:
            if ll[end]=='{':depth+=1
            elif ll[end]=='}':depth-=1
            end+=1
        helpers.append(ll[match.start():end])
    with tempfile.TemporaryDirectory(prefix='embsim-uart-c-') as tmp:
        out=Path(tmp);lib=out/'uart.so'
        sources=[ROOT/'core/mcu/xtensa'/n for n in ['integer.c','execute.c','windows.c','interrupts.c']]
        sources += [ROOT/'core/mcu/esp32'/n for n in ['fabric.c','efuse.c','machine.c','uart.c','gpio.c','matrix.c','flashmap.c','flashspi.c','w25q32.c','nor32.c','gt25q32.c']]
        sources += [ROOT/'core/util/alloc.c']
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC','-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),*map(str,sources),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));pointer=C.c_void_p
        def bind(name,result,*params):f=getattr(api,name);f.restype=result;f.argtypes=list(params);return f
        ai=bind('esk_alloc_init',None,C.POINTER(Allocator),pointer);create=bind('esk_esp32_machine_create',C.c_int,C.POINTER(Allocator),C.POINTER(pointer));destroy=bind('esk_esp32_machine_destroy',None,pointer)
        attach=bind('esk_esp32_machine_uart_port_configure',C.c_int,pointer,C.c_uint,C.c_int,C.c_int,C.c_int,C.c_int)
        receive=bind('esk_esp32_machine_uart_port_receive',C.c_int,pointer,C.c_uint,C.c_uint32,C.c_uint)
        callback=C.CFUNCTYPE(None,pointer,C.POINTER(Frame),C.c_uint64);frames=[];times=[]
        def emitted(ctx,frame,offset32):
            frames.append(tuple(getattr(frame.contents,n) for n,_ in Frame._fields_));times.append(offset32)
        emit=callback(emitted)
        advance=bind('esk_esp32_machine_uart_port_advance',C.c_int,pointer,C.c_uint,C.c_uint64,callback,pointer)
        inputs=bind('esk_esp32_machine_uart_port_inputs',C.c_int,pointer,C.c_uint,C.c_int,C.c_int,C.c_int)
        bus_get=bind('esk_esp32_machine_bus',Memory,pointer,C.c_uint);fabric=bind('esk_esp32_machine_fabric',pointer,pointer)
        load=bind('esk_esp32_fabric_load',C.c_int,pointer,C.c_uint,C.c_uint32,pointer,C.c_size_t)
        configure=bind('esk_esp32_machine_configure',C.c_int,pointer,C.c_uint,C.c_uint32);core=bind('esk_esp32_machine_core',C.POINTER(Windows),pointer,C.c_uint)
        run=bind('esk_esp32_machine_run_core',C.c_int,pointer,C.c_uint,C.c_int);step=bind('esk_esp32_machine_step',C.c_int,pointer,C.c_uint,C.POINTER(C.c_uint),C.POINTER(Fault))
        allocator=Allocator();ai(C.byref(allocator),None);m=pointer();assert create(C.byref(allocator),C.byref(m))==0
        comparisons=0;calls=0;instructions=0;value=C.c_uint32();event=C.c_uint();fault=Fault()
        try:
            bus=bus_get(m,0)
            for trial in range(512):
                port=trial%3 if args.all_ports else 0
                assert attach(m,port,1,1,0,0)==0;frames.clear();times.clear()
                bits=rng.randrange(5,9);parity=rng.randrange(2);odd=rng.randrange(2);stop=rng.randrange(1,3);data=rng.randrange(256);div=rng.randrange(2,130);frag=rng.randrange(16);apb=rng.randrange(2)
                conf=(apb<<field('TICK_REF_ALWAYS_ON'))|((bits-5)<<field('BIT_NUM'))|(parity<<field('PARITY_EN'))|(odd<<field('PARITY'))|(stop<<field('STOP_BIT_NUM'))
                assert bus.write(bus.ctx,addr('CONF0',port),4,conf)==0
                assert bus.write(bus.ctx,addr('CLKDIV',port),4,div|(frag<<field('CLKDIV_FRAG')))==0
                assert bus.write(bus.ctx,addr('FIFO',port),4,data)==0
                bitstream=[0]+[(data>>n)&1 for n in range(bits)]
                p=(sum(bitstream)%2)^odd
                if parity:bitstream.append(p)
                halves=[b for b in bitstream for _ in range(2)]+[1]*(2 if stop==1 else 3)
                divider=div*16+frag;duration=divider*len(halves);tick=0
                while tick*32<duration:
                    assert bus.read(bus.ctx,addr('STATUS',port),4,C.byref(value))==0
                    assert (value.value>>field('TXD'))&1==halves[tick*32//divider],(trial,tick,value.value,halves)
                    comparisons+=1;assert advance(m,port,1,emit,None)==0;tick+=1
                assert frames==[(data&((1<<bits)-1),bits,parity,p,2 if stop==1 else 3,divider,apb)]
                assert times==[duration-(tick-1)*32]
                comparisons+=2
            shared_turns=shared_ram_oracle(bus,m,attach,receive,inputs,advance,emit,frames,times,addr,rng) if args.all_ports else 0
            for name,data in [('uart_struct.h',struct),('misc.h',misc),('uart_reg.h',reg)]: (out/name).write_text(data,encoding='utf-8')
            (out/'soc.h').write_text('#include <stdint.h>\n#define DR_REG_UART_BASE 0x3ff40000\n#define BIT(n) (1U<<(n))\n',encoding='utf-8')
            source=ll[:ll.index('#pragma once')]+'''
#include <stdint.h>
#include <stdbool.h>
#include "uart_struct.h"
#include "uart_reg.h"
#include "misc.h"
extern uart_dev_t UART0,UART1,UART2;
#define FORCE_INLINE_ATTR static inline __attribute__((always_inline))
#define UART_LL_FIFO_DEF_LEN 128
#define READ_PERI_REG(r) (*(volatile uint32_t *)(r))
#define WRITE_PERI_REG(r,v) (*(volatile uint32_t *)(r)=(v))
'''+ '\n'.join(helpers)+'''
unsigned work(unsigned seed,unsigned port) {
    uart_dev_t *hw=port==0 ? &UART0 : port==1 ? &UART1 : &UART2;
    volatile unsigned *phase=(volatile unsigned *)0x3ffb9000;
    uint8_t tx[16],rx[16];unsigned sum=0;
    uart_ll_disable_intr_mask(hw,0x7ffff);uart_ll_clr_intsts_mask(hw,0x7ffff);
    uart_ll_txfifo_rst(hw);
    for(unsigned i=0;i<16;i++) tx[i]=(uint8_t)(seed+i);
    if(uart_ll_get_txfifo_len(hw)!=128) return 0xbad00001;
    uart_ll_ena_intr_mask(hw,1);
    if(uart_ll_get_intr_ena_status(hw)!=1) return 0xbad00002;
    uart_ll_write_txfifo(hw,tx,16);
    *phase=1;while(*phase!=2) { }
    if(uart_ll_get_rxfifo_len(hw)!=16) return 0xbad00003;
    if(!(uart_ll_get_intsts_mask(hw)&1) || !(uart_ll_get_intraw_mask(hw)&1)) return 0xbad00004;
    uart_ll_read_rxfifo(hw,rx,16);
    for(unsigned i=0;i<16;i++) sum+=rx[i];
    uart_ll_clr_intsts_mask(hw,1);uart_ll_disable_intr_mask(hw,1);
    return sum;
}
'''
            if not args.all_ports:source=source.replace('unsigned work(unsigned seed,unsigned port) {\n    uart_dev_t *hw=port==0 ? &UART0 : port==1 ? &UART1 : &UART2;', 'unsigned work(unsigned seed) {\n    uart_dev_t *hw=&UART0;')
            if args.all_ports:source=source.replace('    return sum;', '    *phase=3;while(*phase!=4) { }\n    uart_ll_rxfifo_rst(hw);\n    return sum;')
            src=out/'work.c';obj=out/'work.o';elf=out/'work.elf';binary=out/'work.bin';src.write_text(source,encoding='utf-8')
            subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/','-O1','-ffreestanding','-fno-builtin','-mtext-section-literals','-c',str(src),'-o',str(obj)],check=True)
            subprocess.run([args.linker,'--no-relax','-Ttext=0x40080000','--entry=work','--defsym=UART0=0x3ff40000','--defsym=UART1=0x3ff50000','--defsym=UART2=0x3ff6e000',str(obj),'-o',str(elf)],check=True)
            subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
            symbols={f[2]:int(f[0],16) for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines() if len(f:=line.split())==3}
            code=binary.read_bytes();assert load(fabric(m),0,0x40080000,C.c_char_p(code),len(code))==0
            assert bus.write(bus.ctx,0x3ff00030,4,1)==0
            for cpu in range(2):
                for trial in range(192 if args.all_ports else 64):
                    port=trial//64 if args.all_ports else 0
                    assert attach(m,port,1,1,0,0)==0;frames.clear();times.clear()
                    if args.all_ports and port==1:
                        assert attach(m,2,1,1,1,0)==0
                        assert bus.write(bus.ctx,addr('CONF0',2),4,0x0800801c)==0
                        assert bus.write(bus.ctx,addr('FIFO',2),4,0xab)==0
                    assert bus.write(bus.ctx,addr('CONF1',port),4,7|(96<<8))==0
                    assert bus.write(bus.ctx,0x3ffb9000,4,0)==0
                    assert configure(m,cpu,symbols['work'])==0;w=core(m,cpu).contents;w.ps|=1<<16
                    seed=rng.randrange(256);w.cpu.a[1]=0x3ffd0000+cpu*4096;w.cpu.a[4]=0x7ffffff0;w.cpu.a[6]=seed;w.cpu.a[7]=port
                    assert run(m,cpu,1)==0;injected=False;rx=[rng.randrange(256) for _ in range(16)]
                    for turn in range(6000):
                        if w.cpu.pc==0x7ffffff0:break
                        assert step(m,cpu,C.byref(event),C.byref(fault))==0 and event.value==0,(hex(fault.pc),hex(fault.opcode),hex(fault.address),event.value)
                        instructions+=1
                        assert bus.read(bus.ctx,0x3ffb9000,4,C.byref(value))==0
                        if value.value==1 and not injected:
                            assert advance(m,port,1000000,emit,None)==0
                            assert [f[0] for f in frames]==[(seed+i)&255 for i in range(16)]
                            assert times==[(i+1)*20*16*0x2b6 for i in range(16)]
                            for data in rx:assert receive(m,port,data,0)==0
                            assert bus.write(bus.ctx,0x3ffb9000,4,2)==0;injected=True
                        elif args.all_ports and value.value==3:
                            for data in [0xee,0xdd,0xcc]:assert receive(m,port,data,0)==0
                            assert bus.write(bus.ctx,0x3ffb9000,4,4)==0
                    else:raise AssertionError('UART firmware budget exceeded')
                    assert injected and w.cpu.a[6]==sum(rx),(cpu,trial,hex(w.cpu.a[6]),sum(rx))
                    if args.all_ports:
                        assert bus.read(bus.ctx,addr('STATUS',port),4,C.byref(value))==0 and value.value&255==0
                    if args.all_ports and port==1:
                        assert bus.read(bus.ctx,addr('STATUS',2),4,C.byref(value))==0 and (value.value>>16)&255==1
                        assert attach(m,2,0,0,0,0)==0
                    calls+=1
        finally:destroy(m)
        assert allocator.live_bytes==0
        print(f'{comparisons} independent UART half-bit/frame comparisons; {shared_turns} independent shared-RAM turns; {calls} actual ESP32 C interactive calls, {instructions} instructions, {len(names)} unchanged SDK helpers; FIFO, IRQ and native APB/AHB paths')


if __name__=='__main__':main()
