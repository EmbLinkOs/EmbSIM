#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Check native APP controls using pinned vendor addresses, widths and defaults.

External headers provide facts only; no vendor implementation is linked. Reset
and ROM boot are outside this qualified execution-control subset.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import re
import subprocess
import tempfile
from run_execute import ROOT, Memory
from run_machine_c import Allocator
from run_matrix import read_header


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dport-reg', type=Path, required=True)
    ap.add_argument('--soc-header', type=Path, required=True)
    ap.add_argument('--cc', default='cc'); ap.add_argument('--include-reset', action='store_true'); args = ap.parse_args()
    dport = read_header(args.dport_reg, '38deaa4ef02016ee9cdf490a9bfe4b587f11d1d1e6a2329dc858a50b9b248817')
    soc = read_header(args.soc_header, '7a7148c288d7c289c961810e5617fa7b14d105b28349a45f772150b1e0845dd3')
    base = int(re.search(r'#define\s+DR_REG_DPORT_BASE\s+(0x[0-9a-f]+)', soc)[1], 16)
    fields = []
    for letter in ('ABCD' if args.include_reset else 'BCD'):
        match = re.search(r'#define\s+DPORT_APPCPU_CTRL_' + letter + r"_REG\s+\(DR_REG_DPORT_BASE\s*\+\s*(0x[0-9A-F]+)\)\n"
                          r"/\* \w+ : R/W ;bitpos:\[(\d+)(?::(\d+))?\] ;default: \d+'([bh])([0-9a-fA-F]+)", dport)
        assert match, letter
        offset, high, low, radix, default = match.groups(); high = int(high); low = int(low or high)
        fields.append((base + int(offset, 16), ((1 << (high - low + 1)) - 1) << low,
                       int(default, 2 if radix == 'b' else 16)))
    rng = random.Random(0xc10c); checks = 0
    with tempfile.TemporaryDirectory(prefix='embsim-controls-') as tmp:
        lib = Path(tmp) / 'controls.so'
        sources = [ROOT / 'core/mcu/xtensa' / n for n in ['integer.c', 'execute.c', 'windows.c', 'interrupts.c']]
        sources += [ROOT / 'core/mcu/esp32' / n for n in ['fabric.c', 'machine.c', 'matrix.c', 'flashmap.c', 'flashspi.c']]
        sources += [ROOT / 'core/util/alloc.c']
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'core'), '-I' + str(ROOT / 'include'), *map(str, sources), '-o', str(lib)], check=True)
        api = C.CDLL(str(lib))
        api.esk_alloc_init.argtypes = [C.POINTER(Allocator), C.c_void_p]; api.esk_alloc_init.restype = None
        api.esk_esp32_machine_create.argtypes = [C.POINTER(Allocator), C.POINTER(C.c_void_p)]
        api.esk_esp32_machine_destroy.argtypes = [C.c_void_p]; api.esk_esp32_machine_destroy.restype = None
        api.esk_esp32_machine_app_reset_configure.argtypes = [C.c_void_p, C.c_uint]
        api.esk_esp32_machine_bus.argtypes = [C.c_void_p, C.c_uint]; api.esk_esp32_machine_bus.restype = Memory
        for cpu in range(2):
            allocator = Allocator(); api.esk_alloc_init(C.byref(allocator), None)
            machine = C.c_void_p(); assert api.esk_esp32_machine_create(C.byref(allocator), C.byref(machine)) == 0
            try:
                if args.include_reset: assert api.esk_esp32_machine_app_reset_configure(machine, 1) == 0
                bus = api.esk_esp32_machine_bus(machine, cpu); value = C.c_uint32()
                for address, mask, default in fields:
                    assert bus.read(bus.ctx, address, 4, C.byref(value)) == 0 and value.value == default
                    for trial in range(64):
                        written = [0, 1, 2, 0xffffffff, 0x80000000][trial] if trial < 5 else rng.getrandbits(32)
                        assert bus.write(bus.ctx, address, 4, written) == 0
                        assert bus.read(bus.ctx, address, 4, C.byref(value)) == 0 and value.value == written & mask
                        checks += 1
            finally:
                api.esk_esp32_machine_destroy(machine)
            assert allocator.live_bytes == 0
    print(f'{checks} vendor-addressed APP control field comparisons pass across both CPU bus views')


if __name__ == '__main__':
    main()
