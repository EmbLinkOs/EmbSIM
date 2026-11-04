#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real ESP32 compiler acceptance for SAR-controlled bit manipulation.

Tests target C semantics and instruction execution, not interrupt timing or SoC.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
from run_execute import Fault, Read, Write, Memory, ROOT, MASK
from run_windows_c import Windows

SOURCE='''
unsigned left(unsigned v,unsigned n) { return v<<(n&31); }
unsigned right(unsigned v,unsigned n) { return v>>(n&31); }
int arithmetic(int v,unsigned n) { return v>>(n&31); }
unsigned rotate(unsigned v,unsigned n) { n&=31; return (v<<n)|(v>>((0u-n)&31)); }
unsigned funnel(unsigned hi,unsigned lo,unsigned n) {
    if (!n) return lo;
    if (n>=32) return hi;
    return (hi<<(32-n))|(lo>>n);
}
'''


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['compiler','objcopy','linker','nm']:ap.add_argument('--'+name,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0x5a4)
    values=[0,1,0x7fffffff,0x80000000,MASK,0x89abcdef]+[rng.getrandbits(32) for _ in range(16)]
    with tempfile.TemporaryDirectory(prefix='embsim-system-c-') as tmp:
        out=Path(tmp);src=out/'bits.c';obj=out/'bits.o';elf=out/'bits.elf';binary=out/'bits.bin'
        src.write_text(SOURCE,encoding='utf-8')
        subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/','-O1','-ffreestanding','-fno-builtin','-c',str(src),'-o',str(obj)],check=True)
        subprocess.run([args.linker,'--no-relax','-Ttext=0x1000','--entry=left',str(obj),'-o',str(elf)],check=True)
        subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
        symbols={}
        for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines():
            f=line.split()
            if len(f)==3:symbols[f[2]]=int(f[0],16)
        lib=out/'system.so'
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),
                        *[str(ROOT/'core/mcu/xtensa'/f) for f in ['integer.c','execute.c','windows.c','interrupts.c']],'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));init=api.esk_xtensa_windows_init
        init.argtypes=[C.POINTER(Windows),C.c_uint32,C.POINTER(C.c_uint32)];init.restype=None
        step=api.esk_xtensa_window_step;step.argtypes=[C.POINTER(Windows),C.POINTER(Memory),C.POINTER(C.c_uint),C.POINTER(Fault)];step.restype=C.c_int
        code=binary.read_bytes()
        @Read
        def read(ctx,address,size,value):
            if not (0x1000<=address and address+size<=0x1000+len(code)):return -1
            value[0]=int.from_bytes(code[address-0x1000:address-0x1000+size],'little');return 0
        @Write
        def write(ctx,address,size,value):return -1
        bus=Memory(None,read,write);checks=0;instructions=0
        for name in ['left','right','arithmetic','rotate','funnel']:
            for v in values:
                for n in range(64):
                    lo=rng.getrandbits(32);amount=n&31
                    if name=='left':expected=v<<amount
                    elif name=='right':expected=v>>amount
                    elif name=='arithmetic':expected=(v if v<2**31 else v-2**32)>>amount
                    elif name=='rotate':expected=(v<<amount)|(v>>((-amount)&31))
                    else:expected=lo if n==0 else v if n>=32 else (v<<(32-n))|(lo>>n)
                    state=Windows();init(C.byref(state),symbols[name],None)
                    stop=0x3ffffff0;state.ps|=1<<16;state.cpu.a[4]=(1<<30)|stop;state.cpu.a[1]=0x201000
                    state.cpu.a[6]=v;state.cpu.a[7]=n
                    if name=='funnel':state.cpu.a[7]=lo;state.cpu.a[8]=n
                    event=C.c_uint();fault=Fault();count=0
                    while state.cpu.pc!=stop and count<100:
                        status=step(C.byref(state),C.byref(bus),C.byref(event),C.byref(fault))
                        assert status==0 and event.value==0,(name,v,n,hex(fault.pc),hex(fault.opcode),fault.kind,event.value)
                        count+=1
                    assert state.cpu.pc==stop and state.base==0 and state.start==1,(name,'return/window')
                    assert state.cpu.a[6]==expected&MASK,(name,hex(v),n,hex(state.cpu.a[6]),hex(expected&MASK))
                    assert state.cpu.a[1]==0x201000,(name,'stack')
                    checks+=1;instructions+=count
        print(f'{checks} real ESP32 compiled shift/rotate/funnel executions pass; {instructions} fetched instructions')


if __name__=='__main__':main()
