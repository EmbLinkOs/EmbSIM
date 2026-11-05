#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Independent assembled control-flow, density and memory execution acceptance.

External GNU Xtensa tools provide encodings, Python provides arithmetic and bus
expectations. This qualifies only the documented non-windowed execution subset.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
MASK = 2**32-1


class State(C.Structure):
    _fields_ = [('a', C.c_uint32*16), ('pc', C.c_uint32)]


class Fault(C.Structure):
    _fields_ = [('kind', C.c_uint), ('pc', C.c_uint32), ('address', C.c_uint32), ('opcode', C.c_uint32)]


Read = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint, C.POINTER(C.c_uint32))
Write = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint, C.c_uint32)


CAS = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.POINTER(C.c_uint32))


AtomicType = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint))


class Memory(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('read', Read), ('write', Write), ('compare_exchange', CAS), ('fetch', Read), ('atomic_type', AtomicType)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--assembler', required=True)
    ap.add_argument('--objcopy', required=True)
    ap.add_argument('--linker', required=True)
    ap.add_argument('--cc', default='cc')
    args = ap.parse_args()
    rng = random.Random(0x1a6)
    cases = []
    operations = ['j', 'call0', 'jx', 'callx0', 'ret', 'ret.n', 'nop', 'nop.n',
                  'beqz', 'bnez', 'bltz', 'bgez', 'beq', 'bne', 'blt', 'bge', 'bltu', 'bgeu',
                  'beqz.n', 'bnez.n', 'bany','bnone','ball','bnall','bbc','bbs','bbci','bbsi', 'beqi', 'bnei', 'blti', 'bgei', 'bltui', 'bgeui', 'l8ui', 'l16ui', 'l16si', 'l32i', 'l32ai', 's32ri', 's8i', 's16i', 's32i',
                  'l32i.n', 's32i.n', 'l32r', 'movi.n', 'mov.n', 'add.n', 'addi.n']
    signed = lambda v: v if v < 2**31 else v-2**32
    for operation in operations:
        for k in range(96):
            i = len(cases); pc = 0x1000+i*16
            regs = [rng.getrandbits(32) for _ in range(16)]
            d, s, t = [rng.randrange(16) for _ in range(3)]
            regs[s] = [0, 1, 0x7fffffff, 0x80000000, 0xffffffff, regs[s]][k % 6]
            if k % 3 == 0: regs[t] = regs[s]
            expected = list(regs); width = 2 if operation.endswith('.n') else 3
            next_pc = pc+width; target = pc+(8 if operation.endswith('.n') or k % 2 else -8)
            asm = operation; access = None
            if operation in ['j', 'call0']:
                asm += f' .L{i}{target-pc:+d}'; next_pc = target
                if operation == 'call0': expected[0] = pc+3
            elif operation in ['jx', 'callx0']:
                asm += f' a{s}'; next_pc = regs[s]
                if operation == 'callx0': expected[0] = pc+3
            elif operation in ['ret', 'ret.n']: next_pc = regs[0]
            elif operation in ['beqz', 'bnez', 'bltz', 'bgez', 'beqz.n', 'bnez.n']:
                asm += f' a{s},.L{i}{target-pc:+d}'
                taken = (regs[s] == 0 if operation.startswith('beqz') else regs[s] != 0 if operation.startswith('bnez')
                         else signed(regs[s]) < 0 if operation == 'bltz' else signed(regs[s]) >= 0)
                if taken: next_pc = target
            elif operation in ['beq', 'bne', 'blt', 'bge', 'bltu', 'bgeu']:
                asm += f' a{s},a{t},.L{i}{target-pc:+d}'
                x, y = regs[s], regs[t]
                taken = {'beq': x == y, 'bne': x != y, 'blt': signed(x) < signed(y),
                         'bge': signed(x) >= signed(y), 'bltu': x < y, 'bgeu': x >= y}[operation]
                if taken: next_pc = target
            elif operation in ['bany','bnone','ball','bnall','bbc','bbs','bbci','bbsi']:
                left,right=regs[s],regs[t]
                if operation in ['bbci','bbsi']:
                    bit=k%32;asm+=f' a{s},{bit},.L{i}{target-pc:+d}'
                    taken=((left>>bit)&1)==(operation=='bbsi')
                else:
                    asm+=f' a{s},a{t},.L{i}{target-pc:+d}'
                    if operation in ['bany','bnone']:taken=((left&right)!=0)==(operation=='bany')
                    elif operation in ['ball','bnall']:taken=((left&right)==right)==(operation=='ball')
                    else:taken=((left>>(right&31))&1)==(operation=='bbs')
                if taken:next_pc=target
            elif operation in ['beqi','bnei','blti','bgei','bltui','bgeui']:
                unsigned = operation in ['bltui','bgeui']
                values = ([32768,65536] if unsigned else [-1,1])+[2,3,4,5,6,7,8,10,12,16,32,64,128,256]
                immediate = values[k % 16]
                regs[s] = (immediate + [0,-1,1][k % 3]) & MASK; expected = list(regs)
                asm += f' a{s},{immediate},.L{i}{target-pc:+d}'
                x = regs[s] if unsigned else signed(regs[s])
                taken = x == immediate if operation == 'beqi' else x != immediate if operation == 'bnei' else x < immediate if operation in ['blti','bltui'] else x >= immediate
                if taken: next_pc = target
            elif operation == 'l32r':
                asm += f' a{d},.L{i}-4'; value = rng.getrandbits(32)
                expected[d] = value; access = ('r', (pc-4) & MASK, 4, value)
            elif operation.startswith(('l8', 'l16', 'l32', 's8', 's16', 's32')):
                size = 1 if '8' in operation else 2 if '16' in operation else 4
                offset = rng.randrange(16 if operation.endswith('.n') else 256)*size
                regs[s] = rng.getrandbits(32) & 0xfffffffc
                expected = list(regs)
                address = (regs[s]+offset) & MASK
                asm += f' a{d},a{s},{offset}'
                if operation.startswith('l'):
                    value = rng.getrandbits(32)
                    loaded = value & ((1 << (8*size))-1)
                    if operation == 'l16si' and loaded >= 32768: loaded -= 65536
                    expected[d] = loaded & MASK
                    access = ('r', address, size, value)
                else: access = ('w', address, size, regs[d])
            elif operation == 'movi.n':
                value = rng.randrange(-32,96); asm += f' a{d},{value}'; expected[d] = value & MASK
            elif operation == 'mov.n': asm += f' a{d},a{s}'; expected[d] = regs[s]
            elif operation == 'add.n': asm += f' a{d},a{s},a{t}'; expected[d] = (regs[s]+regs[t]) & MASK
            elif operation == 'addi.n':
                value = rng.choice([-1]+list(range(1,16))); asm += f' a{d},a{s},{value}'; expected[d] = (regs[s]+value) & MASK
            cases.append((asm, regs, expected, pc, next_pc & MASK, width, access))
    with tempfile.TemporaryDirectory(prefix='embsim-xtensa-execute-') as tmp:
        out = Path(tmp); source=out/'execute.s'; obj=out/'execute.o'; elf=out/'execute.elf'; binary=out/'execute.bin'
        source.write_text('.text\n.global _start\n_start:\n.begin no-transform\n'+'\n'.join(
            f'.org {i*16}\n.L{i}:\n{case[0]}' for i, case in enumerate(cases))+'\n.end no-transform\n',encoding='utf-8')
        subprocess.run([args.assembler,str(source),'-o',str(obj)],check=True)
        subprocess.run([args.linker,'--no-relax','-Ttext=0x1000',str(obj),'-o',str(elf)],check=True)
        subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
        data=binary.read_bytes();lib=out/'execute.so'
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),str(ROOT/'core/mcu/xtensa/integer.c'),
                        str(ROOT/'core/mcu/xtensa/execute.c'),'-o',str(lib)],check=True)
        execute=C.CDLL(str(lib)).esk_xtensa_execute
        execute.argtypes=[C.POINTER(State),C.POINTER(Memory),C.POINTER(C.c_ubyte),C.c_size_t,C.POINTER(C.c_size_t),C.POINTER(Fault)]
        execute.restype=C.c_int
        fault_checks=0
        for i,(asm,regs,expected,pc,next_pc,width,access) in enumerate(cases):
            for refuse in ([False,True] if access else [False]):
                state=State((C.c_uint32*16)(*regs),pc); calls=[]
                @Read
                def read(ctx,address,size,value):
                    calls.append(('r',address,size)); value[0]=access[3] if access else 0
                    return -1 if refuse else 0
                @Write
                def write(ctx,address,size,value):
                    calls.append(('w',address,size,value)); return -1 if refuse else 0
                memory=Memory(None,read,write);used=C.c_size_t();fault=Fault()
                code=(C.c_ubyte*width).from_buffer_copy(data[i*16:i*16+width])
                status=execute(C.byref(state),C.byref(memory),code,width,C.byref(used),C.byref(fault))
                if refuse:
                    assert status != 0 and used.value == 0 and state.pc == pc and list(state.a) == regs,(asm,status,fault.kind)
                    assert fault.pc == pc and fault.address == access[1] and fault.kind == (5 if access[0]=='r' else 6),asm
                    fault_checks+=1
                else:
                    assert status == 0 and used.value == width,(asm,status,fault.kind)
                    assert list(state.a) == expected and state.pc == next_pc,(asm,bytes(code).hex(),list(state.a),expected,hex(state.pc),hex(next_pc))
                if access:
                    wanted=access[:3] if access[0]=='r' else access
                    assert calls == [wanted],(asm,calls,wanted)
                else: assert not calls,asm
    print(f'{len(cases)} independently assembled execution cases and {fault_checks} rejected accesses pass')


if __name__ == '__main__': main()
