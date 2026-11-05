#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Independent GNU MOVSP operands and ISA caller-window/AllocaCause oracle.

Tests instruction/exception entry, not ABI base-save relocation by a real ISR.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
from run_execute import ROOT, Fault
from run_windows_c import Windows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--assembler', required=True); ap.add_argument('--objcopy', required=True)
    ap.add_argument('--cc', default='cc'); args = ap.parse_args()
    specs = [(dest, src) for dest in range(16) for src in range(16)]
    rng = random.Random(0xa110ca); checks = 0
    with tempfile.TemporaryDirectory(prefix='embsim-alloca-') as tmp:
        out = Path(tmp); src, obj, binary = [out / ('alloca.' + ext) for ext in ['S', 'o', 'bin']]
        src.write_text('.text\n.begin no-transform\n' + '\n'.join(f'movsp a{dest},a{source}' for dest, source in specs)
                       + '\n.end no-transform\n', encoding='utf-8')
        subprocess.run([args.assembler, str(src), '-o', str(obj)], check=True)
        subprocess.run([args.objcopy, '-O', 'binary', '--only-section=.text', str(obj), str(binary)], check=True)
        code = binary.read_bytes(); assert len(code) == len(specs) * 3
        lib = out / 'alloca.so'
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'core'), '-I' + str(ROOT / 'include'),
                        *[str(ROOT / 'core/mcu/xtensa' / n) for n in ['integer.c', 'execute.c', 'windows.c', 'interrupts.c']],
                        '-o', str(lib)], check=True)
        api = C.CDLL(str(lib)); wp = C.POINTER(Windows)
        init = api.esk_xtensa_windows_init; init.argtypes = [wp, C.c_uint32, C.c_void_p]; init.restype = None
        configure = api.esk_xtensa_exception_configure; configure.argtypes = [wp, C.POINTER(C.c_uint32)]; configure.restype = None
        execute = api.esk_xtensa_window_execute
        execute.argtypes = [wp, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t), C.POINTER(C.c_uint), C.POINTER(Fault)]
        execute.restype = C.c_int
        vectors = (C.c_uint32 * 3)(0x40081000, 0x40081400, 0x40081800)
        for index, (dest, source) in enumerate(specs):
            for base in range(16):
                for pattern in range(8):
                    w = Windows(); init(C.byref(w), 0x40080000, None); configure(C.byref(w), vectors)
                    w.base = base; w.start = 1 << base
                    for distance in range(1, 4):
                        if pattern & (1 << (distance - 1)): w.start |= 1 << ((base - distance) % 16)
                    # Decoy windows outside the caller and visible operand range.
                    for distance in range(4, 13):
                        if rng.getrandbits(1): w.start |= 1 << ((base + distance) % 16)
                    for reg in range(16): w.cpu.a[reg] = rng.getrandbits(32)
                    ps = (rng.getrandbits(1) << 18) | (rng.getrandbits(1) << 4) | (rng.getrandbits(1) << 5)
                    w.ps = ps; w.epc1 = 0x11223344; w.depc = 0x55667788; w.excvaddr = 0xa5a5a5a5
                    registers = list(w.cpu.a); original_start = w.start
                    used = C.c_size_t(); event = C.c_uint(); fault = Fault()
                    instruction = C.create_string_buffer(code[index * 3:index * 3 + 3])
                    status = execute(C.byref(w), None, instruction, 3, C.byref(used), C.byref(event), C.byref(fault))
                    assert status == 0, (dest, source, base, pattern, status, fault.kind)
                    if pattern:
                        registers[dest] = registers[source]
                        assert used.value == 3 and event.value == 0 and w.cpu.pc == 0x40080003 and w.ps == ps
                    else:
                        vector = 2 if ps & 16 else 1 if ps & 32 else 0
                        assert used.value == 0 and event.value == 7 + vector and w.cpu.pc == vectors[vector]
                        assert w.ps == ps | 16 and w.exccause == 5
                        assert w.epc1 == (0x11223344 if ps & 16 else 0x40080000)
                        assert w.depc == (0x40080000 if ps & 16 else 0x55667788)
                    assert list(w.cpu.a) == registers and w.base == base and w.start == original_start
                    assert w.excvaddr == 0xa5a5a5a5
                    checks += 1
    print(f'{checks} GNU-assembled MOVSP all-operand, wrapped-window and AllocaCause comparisons pass')


if __name__ == '__main__':
    main()
