#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Cross-check the experimental integer kernel with separately assembled bytes.

GNU binutils is an external encoding referee, never linked or copied. LX106
binutils can assemble this shared integer subset; it is not an LX6 system referee.
Requires a host C compiler supporting -shared/-fPIC and Python ctypes (Linux).
"""
import argparse
import ctypes
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
MASK = 2**32-1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--assembler', required=True)
    ap.add_argument('--objcopy', required=True)
    ap.add_argument('--cc', default='cc')
    args = ap.parse_args()
    rng = random.Random(0x32)
    cases = []
    operations = ['add', 'sub', 'and', 'or', 'xor', 'addx2', 'addx4', 'addx8',
                  'subx2', 'subx4', 'subx8', 'mull', 'neg', 'abs', 'movi', 'addi', 'addmi', 'slli', 'srli', 'srai', 'extui']
    for operation in operations:
        for k in range(256):
            regs = [rng.getrandbits(32) for _ in range(16)]
            d, s, t = [rng.randrange(16) for _ in range(3)]
            if k < 5:
                regs[s] = [0, 1, 0x7fffffff, 0x80000000, 0xffffffff][k]
                regs[t] = regs[s]
            x, y = regs[s], regs[t]
            instruction = f'{operation} a{d},a{s},a{t}'
            if operation == 'add': result = x+y
            elif operation == 'sub': result = x-y
            elif operation == 'and': result = x & y
            elif operation == 'or': result = x | y
            elif operation == 'xor': result = x ^ y
            elif operation.startswith('addx'): result = x*int(operation[-1])+y
            elif operation.startswith('subx'): result = x*int(operation[-1])-y
            elif operation == 'mull': result = x*y
            elif operation == 'neg':
                instruction = f'neg a{d},a{s}'; result = -x
            elif operation == 'abs':
                instruction = f'abs a{d},a{s}'; result = abs(x if x < 2**31 else x-2**32)
            elif operation in ['slli','srli','srai']:
                shift = rng.randrange(1,32) if operation == 'slli' else rng.randrange(16 if operation == 'srli' else 32)
                instruction = f'{operation} a{d},a{s},{shift}'
                result = x << shift if operation == 'slli' else x >> shift if operation == 'srli' else (x if x < 2**31 else x-2**32) >> shift
            elif operation == 'extui':
                shift = rng.randrange(32); bits = rng.randrange(1,min(16,32-shift)+1)
                instruction = f'extui a{d},a{s},{shift},{bits}'; result = (x >> shift) & ((1 << bits)-1)
            elif operation == 'movi':
                immediate = rng.randrange(-2048, 2048)
                instruction = f'movi a{d},{immediate}'; result = immediate
            else:
                immediate = rng.randrange(-128, 128) * (256 if operation == 'addmi' else 1)
                instruction = f'{operation} a{d},a{s},{immediate}'; result = x+immediate
            expected = list(regs); expected[d] = result & MASK
            cases.append((instruction, regs, expected))
    with tempfile.TemporaryDirectory(prefix='embsim-xtensa-integer-') as tmp:
        out = Path(tmp)
        source = out/'integer.s'; obj = out/'integer.o'; binary = out/'integer.bin'
        source.write_text('.text\n.begin no-transform\n'+ '\n'.join(x[0] for x in cases)+'\n.end no-transform\n', encoding='utf-8')
        subprocess.run([args.assembler, str(source), '-o', str(obj)], check=True)
        subprocess.run([args.objcopy, '-O', 'binary', '--only-section=.text', str(obj), str(binary)], check=True)
        data = binary.read_bytes()
        assert len(data) == len(cases)*3, (len(data), len(cases))
        lib = out/'integer.so'
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-shared', '-fPIC',
                        '-I'+str(ROOT/'core'), '-I'+str(ROOT/'include'),
                        str(ROOT/'core/mcu/xtensa/integer.c'), '-o', str(lib)], check=True)
        execute = ctypes.CDLL(str(lib)).esk_xtensa_integer_execute
        execute.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        execute.restype = ctypes.c_int
        for i, (instruction, registers, expected) in enumerate(cases):
            regs = (ctypes.c_uint32*16)(*registers)
            encoded = (ctypes.c_ubyte*3).from_buffer_copy(data[i*3:i*3+3]); consumed = ctypes.c_size_t()
            assert execute(regs, encoded, 3, ctypes.byref(consumed)) == 0, instruction
            assert consumed.value == 3 and list(regs) == expected, (instruction, list(regs), expected)
    print(f'{len(cases)} independently assembled integer instructions pass all-register comparisons')


if __name__ == '__main__':
    main()
