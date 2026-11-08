#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real ESP32 compiler atomics/division with one shared indivisible callback.

Two execution states are interleaved at instruction boundaries to force CAS
retry. This is not a dual-core SoC, host-thread or memory-coherence qualification.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
from run_execute import Fault, Read, Write, CAS, Memory, ROOT, MASK
from run_windows_c import Windows

SOURCE='''
unsigned exchange(volatile unsigned *p,unsigned value) { return __atomic_exchange_n(p,value,__ATOMIC_SEQ_CST); }
unsigned increment(volatile unsigned *p) { return __sync_add_and_fetch(p,1); }
unsigned cas_bool(volatile unsigned *p,unsigned expected,unsigned desired) { return __sync_bool_compare_and_swap(p,expected,desired); }
unsigned divide_u(unsigned x,unsigned y) { return x/y; }
int divide_s(int x,int y) { return x/y; }
unsigned remainder_u(unsigned x,unsigned y) { return x%y; }
int remainder_s(int x,int y) { return x%y; }
'''


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['compiler','objcopy','linker','nm']:ap.add_argument('--'+name,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0xa70)
    with tempfile.TemporaryDirectory(prefix='embsim-atomic-c-') as tmp:
        out=Path(tmp);src=out/'atomic.c';obj=out/'atomic.o';elf=out/'atomic.elf';binary=out/'atomic.bin'
        src.write_text(SOURCE,encoding='utf-8')
        subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/','-O1','-ffreestanding','-fno-builtin','-c',str(src),'-o',str(obj)],check=True)
        subprocess.run([args.linker,'--no-relax','-Ttext=0x1000','--entry=exchange',str(obj),'-o',str(elf)],check=True)
        subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
        symbols={}
        for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines():
            f=line.split()
            if len(f)==3:symbols[f[2]]=int(f[0],16)
        lib=out/'atomic.so'
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),
                        *[str(ROOT/'core/mcu/xtensa'/f) for f in ['integer.c','execute.c','windows.c','interrupts.c']],'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));init=api.esk_xtensa_windows_init
        init.argtypes=[C.POINTER(Windows),C.c_uint32,C.POINTER(C.c_uint32)];init.restype=None
        step=api.esk_xtensa_window_step;step.argtypes=[C.POINTER(Windows),C.POINTER(Memory),C.POINTER(C.c_uint),C.POINTER(Fault)];step.restype=C.c_int
        code=binary.read_bytes();shared={'value':0,'calls':0,'failed':0,'writes':0}
        @Read
        def read(ctx,address,size,value):
            if 0x1000<=address and address+size<=0x1000+len(code):value[0]=int.from_bytes(code[address-0x1000:address-0x1000+size],'little');return 0
            if address==0x200000 and size==4:value[0]=shared['value'];return 0
            return -1
        @Write
        def write(ctx,address,size,value):shared['writes']+=1;return -1
        @CAS
        def cas(ctx,address,expected,desired,observed):
            if address!=0x200000:return -1
            shared['calls']+=1;observed[0]=shared['value']
            if shared['value']==expected:shared['value']=desired
            else:shared['failed']+=1
            return 0
        bus=Memory(None,read,write,cas);stop=0x3ffffff0;instructions=0;executions=0;pairs=0
        def state(name,args,index=0):
            w=Windows();init(C.byref(w),symbols[name],None);w.ps|=1<<16;w.cpu.a[4]=(1<<30)|stop
            w.cpu.a[0]=0x12340000+index;w.cpu.a[1]=0x201000+index*1024
            for i,value in enumerate(args):w.cpu.a[6+i]=value&MASK
            return w
        def tick(w):
            nonlocal instructions
            event=C.c_uint();fault=Fault();status=step(C.byref(w),C.byref(bus),C.byref(event),C.byref(fault))
            assert status==0 and event.value==0,(hex(fault.pc),hex(fault.opcode),fault.kind,event.value)
            instructions+=1
        def finish(w,index=0):
            assert w.cpu.pc==stop and w.base==0 and w.start==1
            assert w.cpu.a[0]==0x12340000+index and w.cpu.a[1]==0x201000+index*1024
        def run(name,args):
            nonlocal executions
            w=state(name,args)
            for _ in range(200):
                if w.cpu.pc==stop:break
                tick(w)
            finish(w);executions+=1;return w.cpu.a[6]
        for trial in range(64):
            initial=[0,MASK,MASK-1,0x80000000][trial] if trial<4 else rng.getrandbits(32);desired=initial if trial==0 else rng.getrandbits(32)
            shared['value']=initial;assert run('exchange',[0x200000,desired])==initial and shared['value']==desired
            for match in [False,True]:
                shared['value']=initial;expected=initial if match else initial^1
                assert run('cas_bool',[0x200000,expected,desired])==match
                assert shared['value']==(desired if match else initial)
            # Both CPUs load the same value before the first conditional store.
            shared['value']=initial;failed=shared['failed'];states=[state('increment',[0x200000],i) for i in range(2)]
            for _ in range(200):
                for w in states:
                    if w.cpu.pc!=stop:tick(w)
                if all(w.cpu.pc==stop for w in states):break
            for i,w in enumerate(states):finish(w,i)
            assert shared['value']==(initial+2)&MASK
            assert sorted(w.cpu.a[6] for w in states)==sorted([(initial+1)&MASK,(initial+2)&MASK])
            assert shared['failed']>failed,'CAS retry was not exercised'
            executions+=2;pairs+=1
        for name in ['divide_u','divide_s','remainder_u','remainder_s']:
            for trial in range(128):
                edges=[(0,1),(MASK,1),(0x80000000,1),(0x7fffffff,MASK),(MASK-6,3),(7,MASK-2),(0x80000000,2),(0x80000000,MASK)]
                x,y=edges[trial] if trial<len(edges) else (rng.getrandbits(32),rng.getrandbits(32) or 1)
                if name.endswith('_s'):
                    left=x if x<2**31 else x-2**32;right=y if y<2**31 else y-2**32
                    # Signed overflow is undefined C; the instruction has a separate native fixture.
                    if left==-2**31 and right==-1:y=right=1
                    quotient=abs(left)//abs(right)
                    if (left<0)!=(right<0):quotient=-quotient
                    expected=quotient if name.startswith('divide') else left-quotient*right
                else:expected=x//y if name.startswith('divide') else x%y
                assert run(name,[x,y])==expected&MASK,(name,x,y)
        assert shared['writes']==0
        print(f'{executions} real ESP32 compiled atomic/division executions pass; {pairs} interleaved increment pairs force retry; {instructions} instructions; {shared["calls"]} CAS callbacks/{shared["failed"]} mismatches; no normal stores')


if __name__=='__main__':main()
