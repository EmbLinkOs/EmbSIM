#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Pinned LX6 features and GNU-assembled THREADPTR/MISC/fixture PRID accesses.

PRID constants are test inputs, not assertions about ESP32 silicon identity.
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
    facts = dict(re.findall(r'^#define\s+(XCHAL_\w+)\s+(\S+)', source, re.M))
    assert int(facts['XCHAL_HAVE_THREADPTR'], 0) == 1
    assert int(facts['XCHAL_HAVE_PRID'], 0) == 1 and int(facts['XCHAL_NUM_MISC_REGS'], 0) == 4
    specs = [(f'{form}.threadptr a{reg}', form, 'threadptr', reg) for form in ['rur', 'wur'] for reg in range(16)]
    specs += [(f'{form} a{reg},MISC{index}', form, index, reg)
              for index in range(4) for form in ['rsr', 'wsr', 'xsr'] for reg in range(16)]
    specs += [(f'rsr a{reg},PRID', 'rsr', 'prid', reg) for reg in range(16)]
    rng = random.Random(0x231); checks = 0
    with tempfile.TemporaryDirectory(prefix='embsim-context-') as tmp:
        out = Path(tmp); src, obj, binary = [out / ('context.' + ext) for ext in ['S', 'o', 'bin']]
        src.write_text('.text\n.begin no-transform\n' + '\n'.join(s[0] for s in specs) + '\n.end no-transform\n', encoding='utf-8')
        subprocess.run([args.assembler, str(src), '-o', str(obj)], check=True)
        subprocess.run([args.objcopy, '-O', 'binary', '--only-section=.text', str(obj), str(binary)], check=True)
        code = binary.read_bytes(); assert len(code) == len(specs) * 3
        lib = out / 'context.so'
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'core'), '-I' + str(ROOT / 'include'),
                        *[str(ROOT / 'core/mcu/xtensa' / n) for n in ['integer.c', 'execute.c', 'windows.c', 'interrupts.c']],
                        '-o', str(lib)], check=True)
        api = C.CDLL(str(lib)); wp = C.POINTER(Windows)
        init = api.esk_xtensa_windows_init; init.argtypes = [wp, C.c_uint32, C.c_void_p]; init.restype = None
        configure = api.esk_xtensa_prid_configure; configure.argtypes = [wp, C.c_uint32]; configure.restype = C.c_int
        execute = api.esk_xtensa_window_execute
        execute.argtypes = [wp, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t), C.POINTER(C.c_uint), C.POINTER(Fault)]
        execute.restype = C.c_int
        for index, (asm, form, target, reg) in enumerate(specs):
            for trial in range(32):
                w = Windows(); init(C.byref(w), 0x40080000, None)
                for n in range(16): w.cpu.a[n] = rng.getrandbits(32)
                for n in range(4): w.misc[n] = rng.getrandbits(32)
                w.threadptr = rng.getrandbits(32); identity = rng.getrandbits(32)
                assert configure(C.byref(w), identity) == 0
                # THREADPTR is a user register, including nonzero PS.RING without EXCM.
                if target == 'threadptr': w.ps |= (trial % 4) << 6
                registers = list(w.cpu.a); scratch = list(w.misc); tp = w.threadptr
                old = tp if target == 'threadptr' else identity if target == 'prid' else scratch[target]
                if form in ['wur', 'wsr', 'xsr']:
                    if target == 'threadptr': tp = registers[reg]
                    else: scratch[target] = registers[reg]
                if form in ['rur', 'rsr', 'xsr']: registers[reg] = old
                ps = w.ps; used = C.c_size_t(); event = C.c_uint(); fault = Fault()
                instruction = C.create_string_buffer(code[index * 3:index * 3 + 3])
                status = execute(C.byref(w), None, instruction, 3, C.byref(used), C.byref(event), C.byref(fault))
                assert status == 0 and event.value == 0 and used.value == 3, (asm, status, fault.kind)
                assert list(w.cpu.a) == registers and list(w.misc) == scratch and w.threadptr == tp, asm
                assert w.prid == identity and w.ps == ps and w.cpu.pc == 0x40080003, asm
                checks += 1
    print(f'{checks} independent GNU-assembled THREADPTR/MISC/fixture PRID comparisons pass')


if __name__ == '__main__':
    main()
