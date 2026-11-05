#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Run real ESP32 windowed-ABI compiler output; no ESP-IDF/SoC boot claim."""
import argparse
import ctypes as C
from pathlib import Path
import random
import hashlib
import re
import subprocess
import tempfile
from run_execute import State, Fault, Read, Write, Memory, ROOT, MASK
from run_c import SOURCE


class Interrupts(C.Structure):
    _fields_=[(name,C.c_uint32) for name in ['enabled','intenable','lines','latched','waiting','last_level','last_pending']]+[(name,C.c_uint32*8) for name in ['epc','eps','excsave','vectors']]+[('ccount',C.c_uint32),('compare',C.c_uint32*3)]


class Windows(C.Structure):
    _fields_=[('cpu',State),('ar',C.c_uint32*64),('ps',C.c_uint32),('base',C.c_uint32),
              ('start',C.c_uint32),('epc1',C.c_uint32),('lbeg',C.c_uint32),('lend',C.c_uint32),
              ('lcount',C.c_uint32),('sar',C.c_uint32),('exccause',C.c_uint32),('excvaddr',C.c_uint32),
              ('depc',C.c_uint32),('scompare1',C.c_uint32),('exceptions_enabled',C.c_uint32),('exception_vectors',C.c_uint32*3),('irq',Interrupts),('vectors',C.c_uint32*6),('vecbase',C.c_uint32),('vecbase_enabled',C.c_uint32),('threadptr',C.c_uint32),('misc',C.c_uint32*4),('prid',C.c_uint32),('prid_enabled',C.c_uint32),('atomctl',C.c_uint32),('atomctl_enabled',C.c_uint32)]


NESTED='''
unsigned leaf(unsigned x) { return x+3; }
unsigned middle(unsigned x) { return leaf(x)*2; }
unsigned nested(unsigned x) { return middle(x)+leaf(x+1); }
'''


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['compiler','objcopy','linker','nm']:ap.add_argument('--'+name,required=True)
    ap.add_argument('--vectors-source',type=Path)
    ap.add_argument('--assembler')
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0x64a)
    with tempfile.TemporaryDirectory(prefix='embsim-window-c-') as tmp:
        out=Path(tmp);src=out/'routines.c';obj=out/'routines.o';elf=out/'routines.elf';binary=out/'routines.bin'
        extra_objects=[]
        if args.vectors_source:
            vendor=args.vectors_source.read_bytes()
            assert hashlib.sha256(vendor).hexdigest()=='29b544a01b708ca59af85ec6caa80101fba33345d8ec765e6a8fd832b1728366', 'Unexpected vendor vector source'
            text=vendor.decode('utf-8')
            # Keep the vendor's complete permissive copyright/permission notice.
            notice=text[:text.index('--------------------------------------------------------------------------------')]+'*/\n'
            assembly=notice+'.section .vectors,"ax"\n.begin no-transform\n'
            for i,name in enumerate(['_WindowOverflow4','_WindowUnderflow4','_WindowOverflow8','_WindowUnderflow8','_WindowOverflow12','_WindowUnderflow12']):
                match=re.search(r'^'+name+r':\n.*?^\s+rfw[ou][^\n]*',text,re.M|re.S)
                assert match,name
                assembly+=f'.org {i*64}\n.global {name}\n'+match.group(0)+'\n'
            assembly+='.end no-transform\n.text\n.begin no-transform\n'
            for increment in [4,12]:
                arg=increment+2;name=f'recurse{increment}'
                assembly+=f'.align 4\n.global {name}\n{name}:\nentry a1,64\nbeqz a2,.Lbase{increment}\naddi a{arg},a2,-1\ncall{increment} {name}\nadd a2,a{arg},a2\nretw.n\n.Lbase{increment}:\nmovi.n a2,0\nretw.n\n'
            assembly+='.end no-transform\n';asm=out/'vendor-vectors.S';asm.write_text(assembly,encoding='utf-8')
            assembler=args.assembler or str(Path(args.compiler).with_name(Path(args.compiler).name.replace('gcc','as')))
            asmobj=out/'vendor-vectors.o';subprocess.run([assembler,str(asm),'-o',str(asmobj)],check=True);extra_objects=[str(asmobj)]
        recursive='unsigned recurse(unsigned n,unsigned seed) { if (!n) return seed; return recurse(n-1,seed+3)+n; }' if args.vectors_source else ''
        src.write_text(SOURCE+NESTED+recursive,encoding='utf-8')
        subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/', '-O1','-ffreestanding','-fno-builtin','-fno-inline','-c',str(src),'-o',str(obj)],check=True)
        subprocess.run([args.linker,'--no-relax','-Ttext=0x1000','--entry=sum','--section-start=.vectors=0x8000',str(obj),*extra_objects,'-o',str(elf)],check=True)
        subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
        symbols={}
        for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines():
            fields=line.split()
            if len(fields)==3:symbols[fields[2]]=int(fields[0],16)
        lib=out/'windows.so'
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),
                        *[str(ROOT/'core/mcu/xtensa'/file) for file in ['integer.c','execute.c','windows.c','interrupts.c']],'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));init=api.esk_xtensa_windows_init
        init.argtypes=[C.POINTER(Windows),C.c_uint32,C.POINTER(C.c_uint32)];init.restype=None
        step=api.esk_xtensa_window_step;step.argtypes=[C.POINTER(Windows),C.POINTER(Memory),C.POINTER(C.c_uint),C.POINTER(Fault)];step.restype=C.c_int
        code=binary.read_bytes();checks=0;instructions=0;events=[0]*6;vcode=b''
        if args.vectors_source:
            vectorbin=out/'vectors.bin';subprocess.run([args.objcopy,'-O','binary','--only-section=.vectors',str(elf),str(vectorbin)],check=True);vcode=vectorbin.read_bytes()
        for name in ['sum','insertion_sort','copy_bytes','nested']+(['recurse','recurse4','recurse12'] if args.vectors_source else []):
            for length in range(32):
                values=[rng.getrandbits(32) for _ in range(length)]
                if length%4==0:values=sorted(values,reverse=True)
                if length%4==1:values=[MASK]*length
                data=bytearray([0xa5]*8192);base=0x200000
                original=b''.join(v.to_bytes(4,'little') for v in values);data[16:16+len(original)]=original
                initial=bytes(data)
                @Read
                def read(ctx,address,size,value):
                    if 0x1000<=address and address+size<=0x1000+len(code):b=code[address-0x1000:address-0x1000+size]
                    elif 0x8000<=address and address+size<=0x8000+len(vcode):b=vcode[address-0x8000:address-0x8000+size]
                    elif base<=address and address+size<=base+len(data):b=data[address-base:address-base+size]
                    else:return -1
                    value[0]=int.from_bytes(b,'little');return 0
                @Write
                def write(ctx,address,size,value):
                    if not (base<=address and address+size<=base+len(data)):return -1
                    data[address-base:address-base+size]=(value&((1<<(8*size))-1)).to_bytes(size,'little');return 0
                state=Windows();vectors=(C.c_uint32*6)(*[(0x8000 if args.vectors_source else 0x40000000)+i*64 for i in range(6)])
                init(C.byref(state),symbols[name],vectors);state.ps|=1<<16
                stop=0x3ffffff0;state.cpu.a[4]=(1<<30)|stop;state.cpu.a[0]=0x12345678;state.cpu.a[1]=base+8000
                state.cpu.a[6]=base+16;state.cpu.a[7]=length
                argument=rng.getrandbits(32)
                if name=='nested':state.cpu.a[6]=argument
                if name.startswith('recurse'):state.cpu.a[6]=length;state.cpu.a[7]=argument
                if name=='copy_bytes':state.cpu.a[6]=base+2048;state.cpu.a[7]=base+16;state.cpu.a[8]=length*4
                bus=Memory(None,read,write);fault=Fault();event=C.c_uint();count=0;retired=0
                while state.cpu.pc!=stop and count<30000:
                    status=step(C.byref(state),C.byref(bus),C.byref(event),C.byref(fault))
                    assert status==0 and (args.vectors_source or event.value==0),(name,length,count,hex(fault.pc),hex(fault.opcode),fault.kind,event.value,hex(state.cpu.pc))
                    count+=1
                    if event.value:events[event.value-1]+=1
                    else:retired+=1
                assert state.cpu.pc==stop and state.base==0 and state.start==1,(name,length,'return/window state')
                assert state.cpu.a[0]==0x12345678 and state.cpu.a[1]==base+8000,(name,length,'caller state')
                if name=='sum':assert state.cpu.a[6]==sum(values)&MASK and bytes(data)==initial
                elif name=='nested':
                    assert state.cpu.a[6]==(3*argument+10)&MASK,(argument,state.cpu.a[6])
                    assert data[:4000]==initial[:4000], 'nested output/guard corruption'
                elif name.startswith('recurse'):
                    expected_result=(argument+length*3+length*(length+1)//2) if name=='recurse' else length*(length+1)//2
                    assert state.cpu.a[6]==expected_result&MASK,(name,length,state.cpu.a[6],expected_result&MASK)
                    assert data[:4000]==initial[:4000] and data[8016:]==initial[8016:],'stack guards'
                elif name=='insertion_sort':
                    expected=bytearray(initial);expected[16:16+len(original)]=b''.join(v.to_bytes(4,'little') for v in sorted(values))
                    assert data==expected,(name,length)
                else:
                    expected=bytearray(initial);expected[2048:2048+len(original)]=original
                    assert data==expected,(name,length)
                checks+=1;instructions+=retired
        if args.vectors_source:
            assert all(events) and events[::2]==events[1::2],events
        print('Window events (overflow4/underflow4/overflow8/underflow8/overflow12/underflow12):',events)
        print(f'{checks} ESP32 windowed-ABI executions pass ({160 if args.vectors_source else 128} C, {64 if args.vectors_source else 0} assembly); {instructions} instructions; windows/loops/data/returns verified')


if __name__=='__main__':main()
