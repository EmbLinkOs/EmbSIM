#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Pinned ESP32 reset/vector facts plus separately assembled VECBASE accesses.

Only aligned vector-base writes and the documented CPU subset are qualified.
No silicon low-bit/lock masking, complete reset, MMU or actual ROM boot claim.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import re
import subprocess
import tempfile
from run_execute import ROOT, Fault
from run_windows_c import Windows
from run_matrix import read_header


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--core-isa', type=Path, required=True)
    ap.add_argument('--assembler', required=True); ap.add_argument('--objcopy', required=True)
    ap.add_argument('--cc', default='cc'); args = ap.parse_args()
    source = read_header(args.core_isa, '60d20f76c7730b24041b9f55b16e0062f30bf85c7661c7ed215f050fedcdb344')
    definitions = dict(re.findall(r'^#define\s+(XCHAL_\w+)\s+(\S+)', source, re.M))
    def fact(name):
        value = definitions[name]
        return fact(value) if value.startswith('XCHAL_') else int(value, 0)
    assert fact('XCHAL_HAVE_VECBASE') == 1 and fact('XCHAL_NUM_AREGS') == 64
    window = [fact('XCHAL_WINDOW_' + name + '_VECOFS') for name in ['OF4', 'UF4', 'OF8', 'UF8', 'OF12', 'UF12']]
    general = [fact('XCHAL_' + name + '_VECOFS') for name in ['KERNEL', 'USER', 'DOUBLEEXC']]
    high = [fact(f'XCHAL_INTLEVEL{level}_VECOFS') for level in range(2, 8)]
    reset_pc = [fact(f'XCHAL_RESET_VECTOR{n}_VADDR') for n in range(2)]
    reset_base = fact('XCHAL_VECBASE_RESET_VADDR')
    levels = {int(n): int(level) for n, level in re.findall(r'#define\s+XCHAL_INT(\d+)_LEVEL\s+(\d+)', source)}
    kinds = dict((int(n), kind) for n, kind in re.findall(r'#define\s+XCHAL_INT(\d+)_TYPE\s+XTHAL_INTTYPE_(\w+)', source))
    inputs = {level: next(n for n in levels if levels[n] == level and kinds[n] in ['EXTERN_LEVEL', 'EXTERN_EDGE', 'NMI'])
              for level in [1, 2, 3, 4, 5, 7]}
    specs = [(f'{op} a{reg},VECBASE', op, reg) for op in ['rsr', 'wsr', 'xsr'] for reg in range(16)]
    assembly = [asm for asm, _, _ in specs] + ['movi a4,0', 'retw']
    rng = random.Random(0xe7); accesses = 0; dispatches = 0; resets = 0
    with tempfile.TemporaryDirectory(prefix='embsim-vectors-') as tmp:
        out = Path(tmp); src, obj, binary = [out / ('vectors.' + suffix) for suffix in ['S', 'o', 'bin']]
        src.write_text('.text\n.begin no-transform\n' + '\n'.join(assembly) + '\n.end no-transform\n', encoding='utf-8')
        subprocess.run([args.assembler, str(src), '-o', str(obj)], check=True)
        subprocess.run([args.objcopy, '-O', 'binary', '--only-section=.text', str(obj), str(binary)], check=True)
        code = binary.read_bytes(); assert len(code) == len(assembly) * 3
        lib = out / 'vectors.so'
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'core'), '-I' + str(ROOT / 'include'),
                        *[str(ROOT / 'core/mcu/xtensa' / n) for n in ['integer.c', 'execute.c', 'windows.c', 'interrupts.c']],
                        '-o', str(lib)], check=True)
        api = C.CDLL(str(lib)); wp = C.POINTER(Windows)
        def bind(name, result, *params):
            fn = getattr(api, 'esk_xtensa_' + name); fn.restype = result; fn.argtypes = list(params); return fn
        init = bind('windows_init', None, wp, C.c_uint32, C.c_void_p)
        reset = bind('lx6_reset', C.c_int, wp, C.c_uint)
        configure = bind('vecbase_configure', C.c_int, wp, C.c_uint32)
        execute = bind('window_execute', C.c_int, wp, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t), C.POINTER(C.c_uint), C.POINTER(Fault))
        exception = bind('raise_exception', C.c_int, wp, C.c_uint, C.c_uint32, C.c_int, C.POINTER(C.c_uint))
        line = bind('irq_line', C.c_int, wp, C.c_uint, C.c_int)
        poll = bind('irq_poll', C.c_int, wp, C.POINTER(C.c_uint))
        def state(base):
            w = Windows(); init(C.byref(w), 0x40080000, None)
            assert reset(C.byref(w), 1) == 0 and configure(C.byref(w), base) == 0
            return w
        def run(w, index):
            instruction = C.create_string_buffer(code[index * 3:index * 3 + 3])
            used = C.c_size_t(); event = C.c_uint(); fault = Fault()
            assert execute(C.byref(w), None, instruction, 3, C.byref(used), C.byref(event), C.byref(fault)) == 0
            return used.value, event.value
        for index, (_, op, reg) in enumerate(specs):
            for _ in range(64):
                old, new = [rng.getrandbits(32) & ~1023 for _ in range(2)]
                w = state(old)
                for n in range(16): w.cpu.a[n] = rng.getrandbits(32)
                w.cpu.a[reg] = new; expected = list(w.cpu.a)
                base = new if op != 'rsr' else old
                if op != 'wsr': expected[reg] = old
                pc = w.cpu.pc; assert run(w, index) == (3, 0)
                assert w.cpu.pc == pc + 3 and w.vecbase == base and list(w.cpu.a) == expected
                assert list(w.vectors) == [base + offset for offset in window]
                assert list(w.exception_vectors) == [base + offset for offset in general]
                assert list(w.irq.vectors)[2:] == [base + offset for offset in high]
                accesses += 1
        for _ in range(64):
            base = rng.getrandbits(32) & ~1023
            for selector in range(2):
                w = state(base); w.lcount = 77; w.ps = 0x40000
                assert reset(C.byref(w), selector) == 0
                assert w.cpu.pc == reset_pc[selector] and w.vecbase == reset_base and w.lcount == 0
                assert w.ps == (0x1f if fact('XCHAL_HAVE_INTERRUPTS') else 0x10)
                resets += 1
            for index, ps in enumerate([0, 32, 16]):
                w = state(base); w.ps = ps; event = C.c_uint()
                assert exception(C.byref(w), 1, 0, 0, C.byref(event)) == 0
                assert event.value == 7 + index and w.cpu.pc == base + general[index]; dispatches += 1
            for n in range(1, 4):
                w = state(base); w.ps = 1 << 18; w.start = 3 | (4 if n == 1 else 8 if n == 2 else 0)
                assert run(w, len(specs)) == (0, n * 2 - 1) and w.cpu.pc == base + window[(n - 1) * 2]
                w = state(base); w.ps = 1 << 18; w.base = 3; w.start = 8; w.cpu.a[0] = (n << 30) | 0x5678
                assert run(w, len(specs) + 1) == (0, n * 2) and w.cpu.pc == base + window[n * 2 - 1]
                dispatches += 2
            for level, input_number in inputs.items():
                w = state(base); w.ps = 0; w.irq.intenable = 1 << input_number; event = C.c_uint()
                assert line(C.byref(w), input_number, 1) == 0 and poll(C.byref(w), C.byref(event)) == 0
                assert event.value == 10 + level and w.cpu.pc == base + (general[0] if level == 1 else high[level - 2])
                dispatches += 1
    print(f'{accesses} assembled VECBASE accesses, {dispatches} vendor-offset dispatches and {resets} reset-vector comparisons pass')


if __name__ == '__main__':
    main()
