#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""ESP32 C + unchanged selected SDK helpers read SPI1 flash through native MMIO.

Both cores serialize the shared controller with a compiled atomic lock. Some
runs copy code from SPI into instruction RAM and call it. No vendor ROM/SDK boot,
DMA, physical timing or complete board qualification. --program-workers uses
an explicitly selected unprotected named-part subset, not a full board identity.
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

SOURCE = '''
unsigned operation(unsigned address, unsigned length, unsigned char *out,
                   unsigned entry, unsigned value, unsigned high) {
    volatile unsigned *lock=(volatile unsigned *)0x3ffc1000;
    spi_dev_t *dev=(spi_dev_t *)0x3ff42000;
    while(__atomic_exchange_n(lock,1,__ATOMIC_ACQUIRE)) { }
    dev->ctrl.fastrd_mode=0;
    dev->user.val=0xd0000040 | (high<<24);
    spi_flash_ll_set_command(dev,3,8);
    spi_flash_ll_set_addr_bitlen(dev,24);
    spi_flash_ll_set_usr_address(dev,address,24);
    spi_flash_ll_set_miso_bitlen(dev,length*8);
    spi_flash_ll_set_mosi_bitlen(dev,0);
    spi_flash_ll_user_start(dev);
    while(!spi_flash_ll_cmd_is_done(dev)) { }
    unsigned checksum=0;
    for(unsigned i=0;i<length;i++) {
        unsigned byte=(dev->data_buf[(high?8:0)+i/4]>>((i&3)*8))&255;
        out[i]=byte;
        checksum=checksum*33+byte;
    }
    dev->slave.trans_done=0;
    if(entry) {
        volatile unsigned *destination=(volatile unsigned *)(entry&~4095u);
        unsigned *words=(unsigned *)out;
        for(unsigned i=0;i<length/4;i++) destination[i]=words[i];
    }
    __atomic_store_n(lock,0,__ATOMIC_RELEASE);
    return entry ? ((unsigned (*)(unsigned))entry)(value) : checksum;
}
'''
PROGRAM_PREFIX = r"""
    if(entry) {
        unsigned sent=0;
        while(sent<length) {
            unsigned n=256-((address+sent)&255);
            if(n>length-sent) n=length-sent;
            if(n>64) n=64;
            dev->ctrl.fastrd_mode=0;
            dev->user.val=0x80000040;
            spi_flash_ll_set_command(dev,6,8);
            spi_flash_ll_user_start(dev);
            while(!spi_flash_ll_cmd_is_done(dev)) { }
            dev->user.val=0xc8000040;
            spi_flash_ll_set_command(dev,2,8);
            spi_flash_ll_set_addr_bitlen(dev,24);
            spi_flash_ll_set_usr_address(dev,address+sent,24);
            spi_flash_ll_set_mosi_bitlen(dev,n*8);
            spi_flash_ll_set_miso_bitlen(dev,0);
            for(unsigned i=0;i<n/4;i++) dev->data_buf[i]=((unsigned *)(out+sent))[i];
            spi_flash_ll_user_start(dev);
            while(!spi_flash_ll_cmd_is_done(dev)) { }
            dev->user.val=0x90000040;
            spi_flash_ll_set_command(dev,5,8);
            spi_flash_ll_set_miso_bitlen(dev,8);
            spi_flash_ll_set_mosi_bitlen(dev,0);
            do {
                spi_flash_ll_user_start(dev);
                while(!spi_flash_ll_cmd_is_done(dev)) { }
            } while(dev->data_buf[0]&1);
            sent+=n;
        }
    }
"""

WORKER = 'unsigned worker(unsigned x) { return (x ^ TAG) + (x >> 7); }\n'


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['compiler','objcopy','linker','nm']: ap.add_argument('--'+name,required=True)
    ap.add_argument('--ll-header',type=Path,required=True);ap.add_argument('--struct-header',type=Path,required=True)
    ap.add_argument('--program-workers',action='store_true')
    ap.add_argument('--flash-tick-period',type=int,default=37)
    ap.add_argument('--flash-part',choices=['w25q32','gt25q32'],default='w25q32')
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0x5b11)
    if args.flash_part!='w25q32' and not args.program_workers:ap.error('--flash-part requires --program-workers')
    if not 1<=args.flash_tick_period<=100000:ap.error('invalid flash tick period')
    ll=read_header(args.ll_header,'d1331e0d99ff2377e80e7ec4b8f314453345e68d894f5d2b3139c0e8069ae724')
    struct=read_header(args.struct_header,'97fffd958e992ea25af94e08bb6e5b6c7487671b3e070efa68e80d216db383b2')
    helpers=[]
    for name in ['cmd_is_done','set_miso_bitlen','set_mosi_bitlen','set_command','set_addr_bitlen','set_usr_address','user_start']:
        match=re.search(r'^static inline [^\n]*\bspi_flash_ll_'+name+r'\([^\n]*\)\n\{',ll,re.M);assert match,name
        end=match.end();depth=1
        while depth:
            if ll[end]=='{':depth+=1
            elif ll[end]=='}':depth-=1
            end+=1
        helpers.append(ll[match.start():end])
    notice=ll[:ll.index('#pragma once')]
    total=0;calls=0;loaded=0;busy_turns=0;busy_polls=0
    with tempfile.TemporaryDirectory(prefix='embsim-spi-c-') as tmp:
        out=Path(tmp);(out/'vendor-spi-struct.h').write_text(struct,encoding='utf-8')
        def compile_code(name,source,base,entry,flags=()):
            src,obj,elf,binary=[out/(name+'.'+ext) for ext in ['c','o','elf','bin']]
            src.write_text(source,encoding='utf-8')
            subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/', '-O1','-ffreestanding','-fno-builtin',
                            '-mtext-section-literals',*flags,'-c',str(src),'-o',str(obj)],check=True)
            subprocess.run([args.linker,'--no-relax','-Ttext='+hex(base),'--entry='+entry,str(obj),'-o',str(elf)],check=True)
            subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
            symbols={f[2]:int(f[0],16) for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines()
                     if len(f:=line.split())==3}
            return binary.read_bytes(),symbols[entry]
        body=SOURCE.replace('    dev->ctrl.fastrd_mode=0;', PROGRAM_PREFIX+'    dev->ctrl.fastrd_mode=0;',1) if args.program_workers else SOURCE
        if args.program_workers and args.flash_tick_period!=37:
            body=body.replace('} while(dev->data_buf[0]&1);', 'if(dev->data_buf[0]&1) (*(volatile unsigned *)0x3ffc1004)++;\n            } while(dev->data_buf[0]&1);')
        source=notice+'\n#include <stdint.h>\n#include <stdbool.h>\n#include "vendor-spi-struct.h"\n'+'\n'.join(helpers)+body
        code,start=compile_code('operation',source,0x40080000,'operation')
        image=bytearray(rng.getrandbits(8) for _ in range(0x400000 if args.program_workers else 4*65536));entries=[];lengths=[];payloads=[];tags=[0x12345678,0x98765432]
        for cpu in range(2):
            worker,entry=compile_code('worker'+str(cpu),WORKER,0x40088000+cpu*4096,'worker',['-DTAG='+hex(tags[cpu])+'u'])
            worker+=bytes((-len(worker))%4);assert 16<len(worker)<=64
            offset=0xfff0+cpu*65536;image[offset:offset+len(worker)]=b'\xff'*len(worker) if args.program_workers else worker
            payloads.append(worker)
            entries.append(entry);lengths.append(len(worker))
        image=bytes(image);lib=out/'spi.so'
        sources=[ROOT/'core/mcu/xtensa'/n for n in ['integer.c','execute.c','windows.c','interrupts.c']]
        sources += [ROOT/'core/mcu/esp32'/n for n in ['fabric.c', 'efuse.c','machine.c','gpio.c','matrix.c','flashmap.c','flashspi.c', 'w25q32.c', 'nor32.c', 'gt25q32.c']]
        sources += [ROOT/'core/util/alloc.c']
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),*map(str,sources),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));pointer=C.c_void_p
        def bind(name,result,*params):
            fn=getattr(api,name);fn.restype=result;fn.argtypes=list(params);return fn
        init=bind('esk_alloc_init',None,C.POINTER(Allocator),pointer)
        create=bind('esk_esp32_machine_create',C.c_int,C.POINTER(Allocator),C.POINTER(pointer))
        destroy=bind('esk_esp32_machine_destroy',None,pointer)
        fabric=bind('esk_esp32_machine_fabric',pointer,pointer)
        bus_get=bind('esk_esp32_machine_bus',Memory,pointer,C.c_uint)
        load=bind('esk_esp32_fabric_load',C.c_int,pointer,C.c_uint,C.c_uint32,pointer,C.c_size_t)
        select=bind('esk_esp32_fabric_'+args.flash_part+'_configure',C.c_int,pointer,C.c_int)
        advance=bind('esk_esp32_fabric_flash_advance',C.c_int,pointer,C.c_uint64)
        deadline=bind('esk_esp32_fabric_flash_deadline',C.c_int,pointer,C.POINTER(C.c_uint64))
        flash=bind('esk_esp32_fabric_flash',C.c_int,pointer,pointer,C.c_size_t)
        spi=bind('esk_esp32_fabric_spi1_configure',C.c_int,pointer,C.c_int)
        configure=bind('esk_esp32_machine_configure',C.c_int,pointer,C.c_uint,C.c_uint32)
        core=bind('esk_esp32_machine_core',C.POINTER(Windows),pointer,C.c_uint)
        run=bind('esk_esp32_machine_run_core',C.c_int,pointer,C.c_uint,C.c_int)
        step=bind('esk_esp32_machine_step',C.c_int,pointer,C.c_uint,C.POINTER(C.c_uint),C.POINTER(Fault))
        for trial in range(32):
            params=[];expected=[]
            for cpu in range(2):
                value=rng.getrandbits(32);high=trial%2;entry=entries[cpu] if trial%4==0 else 0
                size=lengths[cpu] if entry else rng.randint(1,32 if high else 64)
                address=0xfff0+cpu*65536 if entry else rng.randint(0,len(image)-size)
                data=payloads[cpu] if entry and args.program_workers else image[address:address+size];wanted=0
                for byte in data:wanted=(wanted*33+byte)&0xffffffff
                if entry:wanted=((value^tags[cpu])+(value>>7))&0xffffffff
                params.append([address,size,0x3ffb8000+cpu*256,entry,value,high]);expected.append((wanted,data))
            previous=None
            for replay in range(2):
                a=Allocator();init(C.byref(a),None);m=pointer();assert create(C.byref(a),C.byref(m))==0
                try:
                    ram=fabric(m);bus=bus_get(m,0)
                    assert flash(ram,C.c_char_p(image),len(image))==0 and spi(ram,1)==0
                    if args.program_workers: assert select(ram,1)==0
                    assert load(ram,0,0x40080000,C.c_char_p(code),len(code))==0
                    assert bus.write(bus.ctx,0x3ff00030,4,1)==0
                    states=[]
                    for cpu in range(2):
                        assert configure(m,cpu,start)==0
                        w=core(m,cpu).contents;w.ps|=1<<16;w.cpu.a[1]=0x3ffd0000+cpu*4096;w.cpu.a[4]=0x7ffffff0
                        for n,arg in enumerate(params[cpu]):w.cpu.a[6+n]=arg
                        if args.program_workers and params[cpu][3]:
                            payload=payloads[cpu];assert load(ram,cpu,params[cpu][2],C.c_char_p(payload),len(payload))==0
                        states.append(w);assert run(m,cpu,1)==0
                    done=[False,False];event=C.c_uint();fault=Fault();trace=[];worker_steps=[0,0]
                    for tick in range(30000):
                        # Explicit external flash-time fixture: 1 ms events at the selected
                        # scheduled turns. This is not a CPU instruction/cycle timing model.
                        if args.program_workers:
                            remaining=C.c_uint64();assert deadline(ram,C.byref(remaining))==0
                            busy_turns+=remaining.value!=0
                            if tick%args.flash_tick_period==0: assert advance(ram,1000)==0
                        cpu=(tick+trial)%2;w=states[cpu]
                        if done[cpu]:continue
                        if w.cpu.pc==0x7ffffff0:
                            done[cpu]=True;assert run(m,cpu,0)==0
                            if all(done):break
                            continue
                        pc=w.cpu.pc
                        status=step(m,cpu,C.byref(event),C.byref(fault))
                        assert status==0 and event.value==0,(status,cpu,hex(fault.pc),hex(fault.opcode),fault.kind,event.value)
                        if 0x40088000<=pc<0x4008a000:worker_steps[cpu]+=1
                        trace.append((cpu,pc,w.cpu.pc));total+=1
                    assert all(done)
                    read=C.c_uint32()
                    for cpu,w in enumerate(states):
                        assert w.cpu.a[6]==expected[cpu][0] and w.base==0 and w.start==1
                        assert w.cpu.a[1]==0x3ffd0000+cpu*4096
                        actual=bytearray()
                        for n in range(params[cpu][1]):
                            assert bus.read(bus.ctx,params[cpu][2]+n,1,C.byref(read))==0;actual.append(read.value)
                        assert actual==expected[cpu][1]
                        assert bool(worker_steps[cpu])==bool(params[cpu][3]);loaded+=bool(worker_steps[cpu]);calls+=1
                    assert bus.read(bus.ctx,0x3ffc1000,4,C.byref(read))==0 and read.value==0
                    if args.program_workers and args.flash_tick_period!=37:
                        assert bus.read(bus.ctx,0x3ffc1004,4,C.byref(read))==0
                        if params[0][3] and args.flash_tick_period>=257:assert read.value>0
                        busy_polls+=read.value
                    assert bus.read(bus.ctx,0x3ff000ec,4,C.byref(read))==0 and not read.value&(1<<29)
                    result=trace,[w.cpu.a[6] for w in states]
                    if replay:assert result==previous
                    else:previous=result
                finally:destroy(m)
                assert a.live_bytes==0
        if args.program_workers: assert busy_turns>0
        action=(args.flash_part+'-programmed') if args.program_workers else 'SPI-loaded'
        print(f'{calls} actual ESP32 SPI1 C reads, {loaded} {action} RAM workers, 32 exact replays, {total} retired instructions; {busy_turns} busy turns, {busy_polls} instrumented firmware BUSY reads; seven unchanged SDK helper bodies')


if __name__=='__main__':main()
