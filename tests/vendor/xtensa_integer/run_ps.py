#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""GNU-assembled PS read/write/exchange and RFI functional acceptance.

Expected implemented PS fields come from the Xtensa ISA register definition.
No pipeline/RSYNC latency, full privilege or reset-sequence qualification.
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
    specs = [(f'{form} a{reg},PS', form, reg) for form in ['rsr', 'wsr', 'xsr'] for reg in range(16)]
    specs += [(f'rfi {level}', 'rfi', level) for level in range(2, 8)]
    rng = random.Random(0x230); cases = 0
    # PS implements INTLEVEL/EXCM/UM/RING/OWB and CALLINC/WOE, not the gaps.
    fields = ((1 << 12) - 1) | (7 << 16)
    with tempfile.TemporaryDirectory(prefix='embsim-ps-') as tmp:
        out = Path(tmp); src, obj, binary = [out / ('ps.' + suffix) for suffix in ['S', 'o', 'bin']]
        src.write_text('.text\n.begin no-transform\n' + '\n'.join(asm for asm, _, _ in specs) + '\n.end no-transform\n', encoding='utf-8')
        subprocess.run([args.assembler, str(src), '-o', str(obj)], check=True)
        subprocess.run([args.objcopy, '-O', 'binary', '--only-section=.text', str(obj), str(binary)], check=True)
        code = binary.read_bytes(); assert len(code) == len(specs) * 3
        lib = out / 'ps.so'
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'core'), '-I' + str(ROOT / 'include'),
                        *[str(ROOT / 'core/mcu/xtensa' / n) for n in ['integer.c', 'execute.c', 'windows.c', 'interrupts.c']],
                        '-o', str(lib)], check=True)
        api = C.CDLL(str(lib)); wp = C.POINTER(Windows)
        api.esk_xtensa_windows_init.argtypes = [wp, C.c_uint32, C.POINTER(C.c_uint32)]
        api.esk_xtensa_windows_init.restype = None
        execute = api.esk_xtensa_window_execute
        execute.argtypes = [wp, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t), C.POINTER(C.c_uint), C.POINTER(Fault)]
        for index, (asm, form, reg) in enumerate(specs):
            for trial in range(64):
                w = Windows(); api.esk_xtensa_windows_init(C.byref(w), 0x40080000, None)
                old = (rng.getrandbits(32) & fields) | (1 << 18) | 16
                w.ps = old
                for n in range(16): w.cpu.a[n] = rng.getrandbits(32)
                expected = list(w.cpu.a); value = [0, 0xffffffff, 0xfff8f000][trial] if trial < 3 else rng.getrandbits(32)
                new_ps = old; pc = w.cpu.pc + 3
                if form == 'rfi':
                    w.irq.enabled = 1; w.exceptions_enabled = 1
                    w.irq.eps[reg] = value; w.irq.epc[reg] = rng.getrandbits(32)
                    new_ps = value & fields; pc = w.irq.epc[reg]
                else:
                    w.cpu.a[reg] = value; expected[reg] = value
                    if form in ['wsr', 'xsr']: new_ps = value & fields
                    if form in ['rsr', 'xsr']: expected[reg] = old
                used = C.c_size_t(); event = C.c_uint(); fault = Fault()
                instruction = C.create_string_buffer(code[index * 3:index * 3 + 3])
                status = execute(C.byref(w), None, instruction, 3, C.byref(used), C.byref(event), C.byref(fault))
                assert status == 0 and used.value == 3 and event.value == 0, (asm, status, fault.kind)
                assert w.ps == new_ps and w.cpu.pc == pc and list(w.cpu.a) == expected, asm
                cases += 1
    print(f'{cases} independently assembled PS/RFI field and all-register comparisons pass')


if __name__ == '__main__':
    main()
