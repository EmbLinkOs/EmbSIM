#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""ESP32-compiled IRAM setup calls code through per-core flash MMU mappings.

Identical virtual addresses map to different physical code/data on the two
cores. Native control/table/flush access only; no Python memory callbacks.
Not SPI programming, cache tag/timing, high-PID, ROM/SDK or full board proof.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
from run_execute import ROOT, Memory, Fault
from run_windows_c import Windows
from run_machine_c import Allocator

BOOTSTRAP = '''
unsigned bootstrap(unsigned bank,unsigned codepage,unsigned datapage,unsigned x,unsigned entry) {
    volatile unsigned *mmu=(volatile unsigned *)(0x3ff10000+bank*0x2000);
    volatile unsigned *ctrl=(volatile unsigned *)(0x3ff00040+bank*24);
    volatile unsigned *masks=ctrl+1;
    *ctrl=0;
    mmu[0]=datapage;
    mmu[77]=codepage;
    *masks=*masks&~17u;
    *ctrl=16;
    while(!(*ctrl&32)) { }
    *ctrl=24;
    return ((unsigned (*)(unsigned))entry)(x);
}
'''
WORKER = '''
unsigned flash_worker(unsigned x) {
    volatile const unsigned *data=(volatile const unsigned *)0x3f400100;
    return (x^data[0])+CODETAG;
}
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['compiler', 'objcopy', 'linker', 'nm']:
        ap.add_argument('--' + name, required=True)
    ap.add_argument('--cc', default='cc'); args = ap.parse_args(); rng = random.Random(0xf1a5)
    with tempfile.TemporaryDirectory(prefix='embsim-flash-c-') as tmp:
        out = Path(tmp)
        def compile_code(name, source, base, entry, flags=()):
            src, obj, elf, binary = [out / (name + '.' + ext) for ext in ['c', 'o', 'elf', 'bin']]
            src.write_text(source, encoding='utf-8')
            subprocess.run([args.compiler, '-B' + str(Path(args.compiler).parent) + '/', '-O1', '-ffreestanding',
                            '-fno-builtin', '-mtext-section-literals', *flags, '-c', str(src), '-o', str(obj)], check=True)
            subprocess.run([args.linker, '--no-relax', '-Ttext=' + hex(base), '--entry=' + entry, str(obj), '-o', str(elf)], check=True)
            subprocess.run([args.objcopy, '-O', 'binary', '--only-section=.text', str(elf), str(binary)], check=True)
            symbols = {f[2]: int(f[0], 16) for line in subprocess.check_output([args.nm, str(elf)], text=True).splitlines()
                       if len(f := line.split()) == 3}
            return binary.read_bytes(), symbols[entry]
        bootstrap, start = compile_code('bootstrap', BOOTSTRAP, 0x40080000, 'bootstrap')
        tags = {1: 0x12345678, 3: 0x87654321}; data = {2: 0xa0123456, 4: 0xb0987654}
        image = bytearray(b'\xa5' * (6 * 65536)); entry = None
        for page, tag in tags.items():
            code, address = compile_code('worker' + str(page), WORKER, 0x400d0000, 'flash_worker', ['-DCODETAG=' + hex(tag) + 'u'])
            assert len(code) < 65536 and (entry is None or entry == address)
            entry = address; image[page * 65536:page * 65536 + len(code)] = code
        for page, value in data.items(): image[page * 65536 + 256:page * 65536 + 260] = value.to_bytes(4, 'little')
        flash_image = bytes(image)
        lib = out / 'flash.so'
        sources = [ROOT / 'core/mcu/xtensa' / n for n in ['integer.c', 'execute.c', 'windows.c', 'interrupts.c']]
        sources += [ROOT / 'core/mcu/esp32' / n for n in ['fabric.c', 'machine.c', 'matrix.c', 'flashmap.c', 'flashspi.c', 'w25q32.c']]
        sources += [ROOT / 'core/util/alloc.c']
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'core'), '-I' + str(ROOT / 'include'), *map(str, sources), '-o', str(lib)], check=True)
        api = C.CDLL(str(lib)); pointer = C.c_void_p
        def bind(name, result, *params):
            fn = getattr(api, name); fn.restype = result; fn.argtypes = list(params); return fn
        alloc_init = bind('esk_alloc_init', None, C.POINTER(Allocator), pointer)
        create = bind('esk_esp32_machine_create', C.c_int, C.POINTER(Allocator), C.POINTER(pointer))
        destroy = bind('esk_esp32_machine_destroy', None, pointer)
        fabric = bind('esk_esp32_machine_fabric', pointer, pointer)
        bus_get = bind('esk_esp32_machine_bus', Memory, pointer, C.c_uint)
        load = bind('esk_esp32_fabric_load', C.c_int, pointer, C.c_uint, C.c_uint32, pointer, C.c_size_t)
        install = bind('esk_esp32_fabric_flash', C.c_int, pointer, pointer, C.c_size_t)
        configure = bind('esk_esp32_machine_configure', C.c_int, pointer, C.c_uint, C.c_uint32)
        core = bind('esk_esp32_machine_core', C.POINTER(Windows), pointer, C.c_uint)
        run = bind('esk_esp32_machine_run_core', C.c_int, pointer, C.c_uint, C.c_int)
        step = bind('esk_esp32_machine_step', C.c_int, pointer, C.c_uint, C.POINTER(C.c_uint), C.POINTER(Fault))
        retired = bind('esk_esp32_machine_retired', C.c_uint64, pointer, C.c_uint)
        total = 0; workers = 0; flash_instructions = 0
        for trial in range(64):
            inputs = [rng.getrandbits(32) for _ in range(2)]
            codepages = [1, 3] if trial % 2 else [3, 1]
            datapages = [2, 4] if trial % 4 < 2 else [4, 2]
            expected = [((x ^ data[dp]) + tags[cp]) & 0xffffffff for x, cp, dp in zip(inputs, codepages, datapages)]
            replay_trace = None
            for replay in range(2):
                allocator = Allocator(); alloc_init(C.byref(allocator), None)
                machine = pointer(); assert create(C.byref(allocator), C.byref(machine)) == 0
                try:
                    ram = fabric(machine); bus = bus_get(machine, 0)
                    assert install(ram, C.c_char_p(flash_image), len(flash_image)) == 0
                    assert load(ram, 0, 0x40080000, C.c_char_p(bootstrap), len(bootstrap)) == 0
                    assert bus.write(bus.ctx, 0x3ff00030, 4, 1) == 0
                    states = []
                    for cpu in range(2):
                        assert configure(machine, cpu, start) == 0
                        w = core(machine, cpu).contents; w.ps |= 1 << 16
                        w.cpu.a[0] = 0x12340000 + cpu; w.cpu.a[1] = 0x3ffd0000 + cpu * 4096; w.cpu.a[4] = 0x7ffffff0
                        for n, arg in enumerate([cpu, codepages[cpu], datapages[cpu], inputs[cpu], entry]): w.cpu.a[6 + n] = arg
                        states.append(w); assert run(machine, cpu, 1) == 0
                    done = [False, False]; event = C.c_uint(); fault = Fault(); trace = []; fetched = [0, 0]
                    for turn in range(10000):
                        cpu = (turn + trial) % 2; w = states[cpu]
                        if done[cpu]: continue
                        if w.cpu.pc == 0x7ffffff0:
                            done[cpu] = True; assert run(machine, cpu, 0) == 0
                            if all(done): break
                            continue
                        address = w.cpu.pc
                        status = step(machine, cpu, C.byref(event), C.byref(fault))
                        assert status == 0 and event.value == 0, (status, cpu, hex(fault.pc), hex(fault.opcode), fault.kind, event.value)
                        if 0x400d0000 <= address < 0x400e0000: fetched[cpu] += 1; flash_instructions += 1
                        trace.append((cpu, address, w.cpu.pc)); total += 1
                    assert all(done) and all(fetched)
                    value = C.c_uint32()
                    for cpu, w in enumerate(states):
                        assert w.cpu.a[6] == expected[cpu] and w.base == 0 and w.start == 1
                        assert w.cpu.a[0] == 0x12340000 + cpu and w.cpu.a[1] == 0x3ffd0000 + cpu * 4096
                        assert retired(machine, cpu) == sum(owner == cpu for owner, _, _ in trace)
                        for address, wanted in [(0x3ff10000 + cpu * 0x2000, datapages[cpu]),
                                                 (0x3ff10000 + cpu * 0x2000 + 77 * 4, codepages[cpu]),
                                                 (0x3ff00040 + cpu * 24, 56)]:
                            assert bus.read(bus.ctx, address, 4, C.byref(value)) == 0 and value.value == wanted
                    result = (trace, [w.cpu.a[6] for w in states])
                    if replay: assert result == replay_trace
                    else: replay_trace = result
                    workers += 2
                finally:
                    destroy(machine)
                assert allocator.live_bytes == 0
        print(f'{workers} mapped-flash C workers, 64 exact replays, {total} retired instructions ({flash_instructions} fetched from mapped flash)')


if __name__ == '__main__':
    main()
