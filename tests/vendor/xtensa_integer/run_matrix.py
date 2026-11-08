#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Compare native ESP32 matrix MMIO/routing with pinned vendor header facts.

Headers are external reference inputs; no vendor controller implementation is
linked. This does not qualify peripheral generators, pulse timing or silicon.
"""
import argparse
import ctypes as C
import hashlib
from pathlib import Path
import random
import re
import subprocess
import tempfile
from run_execute import ROOT


class Matrix(C.Structure):
    _fields_ = [('map', (C.c_uint32 * 69) * 2), ('sources', C.c_uint32 * 3),
                ('gpio', C.c_uint32 * 2), ('ipi', C.c_uint32 * 4), ('nmi_mask', C.c_uint32 * 2)]


def read_header(path, digest):
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == digest, f'unexpected reference: {path.name}'
    return raw.decode('utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['dport-reg', 'soc-header', 'core-isa']:
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--cc', default='cc'); args = ap.parse_args()
    dport = read_header(args.dport_reg, '38deaa4ef02016ee9cdf490a9bfe4b587f11d1d1e6a2329dc858a50b9b248817')
    soc = read_header(args.soc_header, '7a7148c288d7c289c961810e5617fa7b14d105b28349a45f772150b1e0845dd3')
    core = read_header(args.core_isa, '60d20f76c7730b24041b9f55b16e0062f30bf85c7661c7ed215f050fedcdb344')
    base = int(re.search(r'#define\s+DR_REG_DPORT_BASE\s+(0x[0-9a-f]+)', soc)[1], 16)
    registers = {name: base + int(offset, 16) for name, offset in re.findall(
        r'#define\s+(DPORT_\w+_REG)\s+\(DR_REG_DPORT_BASE\s*\+\s*(0x[0-9A-Fa-f]+)\)', dport)}
    sources = {int(n) for name, n in re.findall(r'#define\s+(ETS_\w+_SOURCE)\s+(\d+)\b', soc)
               if name != 'ETS_MAX_INTR_SOURCE'}
    assert len(sources) == 67 and min(sources) == 0 and max(sources) == 68
    internal = {int(n) for n, kind in re.findall(r'#define\s+XCHAL_INT(\d+)_TYPE\s+XTHAL_INTTYPE_(\w+)', core)
                if kind in ['TIMER', 'SOFTWARE', 'PROFILING']}
    assert len(internal) == 6
    rng = random.Random(0x1a7); routes = 0; combinations = 0
    with tempfile.TemporaryDirectory(prefix='embsim-matrix-') as tmp:
        lib = Path(tmp) / 'matrix.so'
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'core'), '-I' + str(ROOT / 'include'),
                        str(ROOT / 'core/mcu/esp32/matrix.c'), '-o', str(lib)], check=True)
        api = C.CDLL(str(lib)); mp = C.POINTER(Matrix)
        def bind(name, result, *params):
            fn = getattr(api, 'esk_esp32_matrix_' + name); fn.restype = result; fn.argtypes = list(params); return fn
        init = bind('init', None, mp)
        read = bind('read', C.c_int, mp, C.c_uint32, C.c_uint, C.POINTER(C.c_uint32))
        write = bind('write', C.c_int, mp, C.c_uint32, C.c_uint, C.c_uint32)
        level = bind('levels', C.c_uint32, mp, C.c_uint)
        source_input = bind('source', C.c_int, mp, C.c_uint, C.c_int)
        gpio = bind('gpio', C.c_int, mp, C.c_uint, C.c_int, C.c_int)
        mask_nmi = bind('nmi_mask', C.c_int, mp, C.c_uint, C.c_int)
        maps = []
        for cpu, prefix in enumerate(['PRO', 'APP']):
            first = registers[f'DPORT_{prefix}_MAC_INTR_MAP_REG']
            maps.append({(addr - first) // 4: addr for name, addr in registers.items()
                         if name.startswith(f'DPORT_{prefix}_') and name.endswith('_MAP_REG')
                         and first <= addr <= first + 68 * 4})
            assert sources <= maps[cpu].keys()
        def drive(m, cpu, source, state):
            if source in [22, 23]:
                assert gpio(C.byref(m), cpu, source == 23, state) == 0
            elif 24 <= source <= 27:
                assert write(C.byref(m), registers[f'DPORT_CPU_INTR_FROM_CPU_{source - 24}_REG'], 4, state) == 0
            else:
                assert source_input(C.byref(m), source, state) == 0
        value = C.c_uint32()
        for cpu in range(2):
            for source in sorted(sources):
                address = maps[cpu][source]
                name = next(name for name, addr in registers.items() if addr == address and name.endswith('_MAP_REG'))
                default = int(re.search(r'#define\s+' + name + r"[^\n]*\n/\* [^\n]* ;bitpos:\[4:0\] ;default: 5'd(\d+)", dport)[1])
                for dest in range(32):
                    m = Matrix(); init(C.byref(m))
                    assert read(C.byref(m), address, 4, C.byref(value)) == 0 and value.value == default
                    assert write(C.byref(m), address, 4, dest | 0xffffffe0) == 0
                    assert read(C.byref(m), address, 4, C.byref(value)) == 0 and value.value == dest
                    drive(m, cpu, source, 1)
                    assert level(C.byref(m), cpu) == (0 if dest in internal else 1 << dest)
                    assert level(C.byref(m), 1 - cpu) == 0
                    for other, prefix in enumerate(['PRO', 'APP']):
                        status = registers[f'DPORT_{prefix}_INTR_STATUS_{source // 32}_REG']
                        assert read(C.byref(m), status, 4, C.byref(value)) == 0
                        expected = 0 if source in [22, 23] and other != cpu else 1 << (source % 32)
                        assert value.value == expected
                    routes += 1
        # Independent Boolean OR oracle for active sources, arbitrary maps,
        # core-local GPIO, all four IPC registers and PID NMI mask inputs.
        for trial in range(256):
            m = Matrix(); init(C.byref(m)); mapping = [{}, {}]; active = [set(), set()]
            for cpu in range(2):
                for source in sources:
                    dest = rng.randrange(32); mapping[cpu][source] = dest
                    assert write(C.byref(m), maps[cpu][source], 4, dest) == 0
            for source in sorted(sources):
                if source in [22, 23]:
                    for cpu in range(2):
                        on = rng.randrange(2); drive(m, cpu, source, on)
                        if on: active[cpu].add(source)
                else:
                    on = rng.randrange(2); drive(m, 0, source, on)
                    if on:
                        for cpu in range(2): active[cpu].add(source)
            for cpu, prefix in enumerate(['PRO', 'APP']):
                masked = rng.randrange(2); assert mask_nmi(C.byref(m), cpu, masked) == 0
                expected = 0
                for source in active[cpu]:
                    dest = mapping[cpu][source]
                    if dest not in internal and not (dest == 14 and masked): expected |= 1 << dest
                assert level(C.byref(m), cpu) == expected
                for group in range(3):
                    expected = sum(1 << (s % 32) for s in active[cpu] if s // 32 == group)
                    assert read(C.byref(m), registers[f'DPORT_{prefix}_INTR_STATUS_{group}_REG'], 4, C.byref(value)) == 0
                    assert value.value == expected
                combinations += 1
    print(f'{routes} vendor-addressed routes and {combinations} combined-source/mask comparisons pass')


if __name__ == '__main__':
    main()
