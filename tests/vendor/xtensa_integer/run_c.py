#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Compile unchanged freestanding C routines and run them through fetch/bus execution.

External LX106 CALL0 GCC tests the shared instruction subset, not ESP32 boot,
LX6 register windows, SMP, cycle accuracy or vendor SDK compatibility.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
from run_execute import State, Fault, Read, Write, Memory, ROOT, MASK

SOURCE = '''
unsigned sum(const unsigned *p, unsigned n) {
    unsigned result=0;
    while(n--) result+=*p++;
    return result;
}
void insertion_sort(unsigned *p, unsigned n) {
    for (unsigned i=1;i<n;i++) {
        unsigned v=p[i],j=i;
        while(j && p[j-1]>v) { p[j]=p[j-1]; --j; }
        p[j]=v;
    }
}
void copy_bytes(unsigned char *out, const unsigned char *in, unsigned n) {
    for (unsigned i=0;i<n;i++) out[i]=in[i];
}
'''


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',required=True)
    ap.add_argument('--objcopy',required=True)
    ap.add_argument('--linker',required=True)
    ap.add_argument('--nm',required=True)
    ap.add_argument('--cc',default='cc')
    args=ap.parse_args();rng=random.Random(0xc32)
    with tempfile.TemporaryDirectory(prefix='embsim-xtensa-c-') as tmp:
        out=Path(tmp);src=out/'routines.c';obj=out/'routines.o';elf=out/'routines.elf';binary=out/'routines.bin'
        src.write_text(SOURCE,encoding='utf-8')
        subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/', '-O1','-ffreestanding','-fno-builtin','-c',str(src),'-o',str(obj)],check=True)
        subprocess.run([args.linker,'--no-relax','-Ttext=0x1000','--entry=sum',str(obj),'-o',str(elf)],check=True)
        subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
        symbols={}
        for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines():
            fields=line.split()
            if len(fields)==3: symbols[fields[2]]=int(fields[0],16)
        lib=out/'execute.so'
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),str(ROOT/'core/mcu/xtensa/integer.c'),
                        str(ROOT/'core/mcu/xtensa/execute.c'),'-o',str(lib)],check=True)
        step=C.CDLL(str(lib)).esk_xtensa_step
        step.argtypes=[C.POINTER(State),C.POINTER(Memory),C.POINTER(Fault)];step.restype=C.c_int
        code=binary.read_bytes();checks=0;instructions=0
        for name in ['sum','insertion_sort','copy_bytes']:
            for length in range(32):
                values=[rng.getrandbits(32) for _ in range(length)]
                if length%4==0: values=sorted(values,reverse=True)
                if length%4==1: values=[MASK]*length
                data=bytearray([0xa5]*4096);base=0x200000
                original=b''.join(v.to_bytes(4,'little') for v in values);data[16:16+len(original)]=original
                initial=bytes(data)
                @Read
                def read(ctx,address,size,value):
                    if 0x1000<=address and address+size<=0x1000+len(code): b=code[address-0x1000:address-0x1000+size]
                    elif base<=address and address+size<=base+len(data): b=data[address-base:address-base+size]
                    else: return -1
                    value[0]=int.from_bytes(b,'little');return 0
                @Write
                def write(ctx,address,size,value):
                    if not (base<=address and address+size<=base+len(data)): return -1
                    data[address-base:address-base+size]=(value&((1<<(size*8))-1)).to_bytes(size,'little');return 0
                regs=[rng.getrandbits(32) for _ in range(16)];regs[0]=0xfffffff0;regs[1]=base+4000
                regs[2]=base+16;regs[3]=length
                if name=='copy_bytes':regs[2]=base+2048;regs[3]=base+16;regs[4]=length*4
                state=State((C.c_uint32*16)(*regs),symbols[name]);bus=Memory(None,read,write);fault=Fault();count=0
                while state.pc!=regs[0] and count<20000:
                    status=step(C.byref(state),C.byref(bus),C.byref(fault))
                    assert status==0,(name,length,count,hex(fault.pc),hex(fault.opcode),fault.kind)
                    count+=1
                assert state.pc==regs[0],(name,length,'did not return')
                assert list(state.a)[12:]==regs[12:],(name,length,'CALL0 callee-save corruption')
                if name=='sum': assert state.a[2]==sum(values)&MASK and bytes(data)==initial
                elif name=='insertion_sort':
                    expected=bytearray(initial);expected[16:16+len(original)]=b''.join(v.to_bytes(4,'little') for v in sorted(values))
                    assert data==expected,(name,length)
                else:
                    expected=bytearray(initial);expected[2048:2048+len(original)]=original
                    assert data==expected,(name,length)
                checks+=1;instructions+=count
        print(f'{checks} compiler-generated C executions pass; {instructions} fetched instructions; arrays/guards/callee-save/returns verified')


if __name__=='__main__':main()
