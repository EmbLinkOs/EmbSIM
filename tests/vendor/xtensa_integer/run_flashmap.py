#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Compare the read-only PID0/1 flash profile against pinned ESP32 header facts.

Reference addresses, fields and bus windows are parsed, not vendor code linked.
No physical cache/SPI, live-remap or complete SDK boot qualification.
"""
import argparse
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
from run_execute import ROOT
from run_machine_c import Allocator
from run_matrix import read_header


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['dport-reg', 'soc-header', 'core-isa', 'mmu-header']:
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--cc', default='cc'); args = ap.parse_args()
    dport = read_header(args.dport_reg, '38deaa4ef02016ee9cdf490a9bfe4b587f11d1d1e6a2329dc858a50b9b248817')
    soc = read_header(args.soc_header, '7a7148c288d7c289c961810e5617fa7b14d105b28349a45f772150b1e0845dd3')
    core = read_header(args.core_isa, '60d20f76c7730b24041b9f55b16e0062f30bf85c7661c7ed215f050fedcdb344')
    mmu = read_header(args.mmu_header, '59842853da8931e4a5fd61ec0cad565a38b691d53a96110439d9d0aadaf920b9')
    def number(source, name):
        return int(re.search(r'#define\s+' + name + r'\s+(0x[0-9A-Fa-f]+|\d+)\b', source)[1], 0)
    base = number(soc, 'DR_REG_DPORT_BASE')
    def register(name):
        return base + int(re.search(r'#define\s+' + name + r'\s+\(DR_REG_DPORT_BASE\s*\+\s*(0x[0-9A-Fa-f]+)\)', dport)[1], 16)
    def initial(name):
        start = re.search(r'^#define\s+' + name + r'\s+\(DR_REG_DPORT_BASE[^\n]*', dport, re.M).end()
        following = re.search(r'^#define\s+DPORT_\w+_REG\s+\(DR_REG_DPORT_BASE', dport[start:], re.M)
        text = dport[start:start + following.start()] if following else dport[start:]
        value = 0
        for high, low, radix, digits in re.findall(r";bitpos:\[(\d+)(?::(\d+))?\] ;default: \d+'([bhd])([0-9a-fA-F]+)", text):
            low = int(low or high); value |= int(digits, {'b': 2, 'h': 16, 'd': 10}[radix]) << low
        return value
    invalid = number(dport, 'DPORT_FLASH_MMU_TABLE_INVALID_VAL')
    ppn_mask = number(dport, 'DPORT_MMU_ADDRESS_MASK')
    pages = number(mmu, 'SOC_MMU_PAGES_PER_REGION')
    first = [number(mmu, 'SOC_MMU_DROM0_PAGES_START')] + [number(mmu, 'SOC_MMU_IROM0_PAGES_START') + pages * n for n in range(3)]
    windows = [number(core, 'XCHAL_' + region + '_VADDR') for region in ['DATAROM0', 'INSTRAM0', 'INSTRAM1', 'INSTROM0']]
    internal_end = number(soc, 'SOC_RTC_IRAM_HIGH')
    image = b''.join(bytes(byte ^ page for byte in range(256)) * 256 for page in range(ppn_mask + 1))
    assert len(image) == 16 * 1024 * 1024 and pages == 64
    compared = 0; fields = 0
    with tempfile.TemporaryDirectory(prefix='embsim-flashmap-') as tmp:
        lib = Path(tmp) / 'flashmap.so'
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'core'), '-I' + str(ROOT / 'include'), str(ROOT / 'core/mcu/esp32/flashmap.c'),
                        str(ROOT / 'core/util/alloc.c'), '-o', str(lib)], check=True)
        api = C.CDLL(str(lib)); pointer = C.c_void_p
        def bind(name, result, *params):
            fn = getattr(api, name); fn.restype = result; fn.argtypes = list(params); return fn
        alloc_init = bind('esk_alloc_init', None, C.POINTER(Allocator), pointer)
        create = bind('esk_esp32_flashmap_create', C.c_int, C.POINTER(Allocator), C.POINTER(pointer))
        install = bind('esk_esp32_flashmap_install', C.c_int, pointer, pointer, C.c_size_t)
        destroy = bind('esk_esp32_flashmap_destroy', None, pointer)
        read = bind('esk_esp32_flashmap_reg_read', C.c_int, pointer, C.c_uint32, C.c_uint, C.POINTER(C.c_uint32))
        write = bind('esk_esp32_flashmap_reg_write', C.c_int, pointer, C.c_uint32, C.c_uint, C.c_uint32)
        access = bind('esk_esp32_flashmap_read', C.c_int, pointer, C.c_uint, C.c_uint32, C.c_uint, C.c_int, C.POINTER(C.c_uint32))
        allocator = Allocator(); alloc_init(C.byref(allocator), None)
        flash = pointer(); assert create(C.byref(allocator), C.byref(flash)) == 0
        try:
            assert install(flash, C.c_char_p(image), len(image)) == 0
            value = C.c_uint32()
            for cpu, prefix in enumerate(['PRO', 'APP']):
                control_name, masks_name = [f'DPORT_{prefix}_CACHE_{suffix}_REG' for suffix in ['CTRL', 'CTRL1']]
                control, masks = register(control_name), register(masks_name)
                table = number(soc, 'DR_REG_FLASH_MMU_TABLE_' + prefix)
                enable = 1 << number(dport, f'DPORT_{prefix}_CACHE_ENABLE_S')
                flush = 1 << number(dport, f'DPORT_{prefix}_CACHE_FLUSH_ENA_S')
                done = 1 << number(dport, f'DPORT_{prefix}_CACHE_FLUSH_DONE_S')
                mask_bits = [1 << number(dport, f'DPORT_{prefix}_CACHE_MASK_{region}_S') for region in ['DROM0', 'IRAM0', 'IRAM1', 'IROM0']]
                for address, expected in [(control, initial(control_name)), (masks, initial(masks_name))]:
                    assert read(flash, address, 4, C.byref(value)) == 0 and value.value == expected, (control_name, hex(address), hex(value.value), hex(expected)); fields += 1
                assert write(flash, control, 4, 0) == 0
                for index in range(4 * pages):
                    assert read(flash, table + index * 4, 4, C.byref(value)) == 0 and value.value == invalid
                    assert write(flash, table + index * 4, 4, (index * 7 + cpu * 11) & ppn_mask) == 0
                unmasked = initial(masks_name) & ~sum(mask_bits)
                assert write(flash, masks, 4, unmasked) == 0
                assert write(flash, control, 4, flush) == 0
                assert read(flash, control, 4, C.byref(value)) == 0 and value.value == flush | done; fields += 1
                assert write(flash, control, 4, flush | enable) == 0
                for region, virtual in enumerate(windows):
                    for page in range(pages):
                        address = virtual + page * 65536 + 65535
                        if region == 1 and address < internal_end: continue
                        physical_page = ((first[region] + page) * 7 + cpu * 11) & ppn_mask
                        assert access(flash, cpu, address, 1, region != 0, C.byref(value)) == 0
                        assert value.value == 255 ^ physical_page
                        compared += 1
                    probe = virtual + (13 * 65536 if region == 1 else 0)
                    assert write(flash, masks, 4, unmasked | mask_bits[region]) == 0
                    assert access(flash, cpu, probe, 1, region != 0, C.byref(value)) != 0
                    assert write(flash, masks, 4, unmasked) == 0
        finally:
            destroy(flash)
        assert allocator.live_bytes == 0
    print(f'{compared} vendor-window/page translations and {fields} parsed control/default/flush comparisons pass')


if __name__ == '__main__':
    main()
