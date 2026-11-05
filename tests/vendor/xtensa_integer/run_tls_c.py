#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Actual ESP32 local-exec TLS code through two cores and the native RAM bus.

Firmware selects task TLS banks and MISC cookies. PRID values are explicit test
fixtures, not identified silicon values. No scheduler, SDK/ROM or full SMP proof.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import tempfile
from run_execute import ROOT, Memory, Fault
from run_windows_c import Windows
from run_machine_c import Allocator
from run_matrix import read_header

SOURCE = '''
__thread unsigned counter = 7;
__thread unsigned guard = 0x12345678;
__thread unsigned zero;
__attribute__((noinline)) unsigned update(unsigned delta) {
    counter += delta;
    zero += 1;
    return counter ^ guard ^ zero;
}
unsigned task(unsigned tp, unsigned cookie, unsigned delta, unsigned *out) {
    unsigned got, id;
    __asm__ volatile("wur.threadptr %0" :: "a"(tp) : "memory");
    __asm__ volatile("wsr.misc0 %0" :: "a"(cookie));
    unsigned value = update(delta);
    __asm__ volatile("rsr.misc0 %0" : "=a"(got));
    __asm__ volatile("rsr.prid %0" : "=a"(id));
    out[0] = (unsigned)&counter;
    out[1] = (id >> 13) & 1;
    out[2] = got;
    return value;
}
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['compiler', 'objcopy', 'linker', 'nm']:
        ap.add_argument('--' + name, required=True)
    ap.add_argument('--dynamic-stack', action='store_true')
    ap.add_argument('--port-source', type=Path, required=True)
    ap.add_argument('--cpu-ll', type=Path, required=True)
    ap.add_argument('--cc', default='cc'); args = ap.parse_args(); rng = random.Random(0x715)
    port = read_header(args.port_source, 'b81102899f12b67b1228ad3ad9deb8ce5a71fb5155691e225672ccdd146a1052')
    cpu_ll = read_header(args.cpu_ll, '0b75eb5517f4ae4a0daf13574ee27aaa33394aa5149e89a4dab89471abaa18e1')
    assert 'const uint32_t tcb_size = 8;' in port and 'tls_section_alignment' in port
    assert 'rsr.prid' in cpu_ll and 'extui %0,%0,13,1' in cpu_ll
    with tempfile.TemporaryDirectory(prefix='embsim-tls-c-') as tmp:
        out = Path(tmp); src, obj, elf, binary = [out / ('tls.' + ext) for ext in ['c', 'o', 'elf', 'bin']]
        source = SOURCE
        if args.dynamic_stack:
            source = source.replace('    counter += delta;', '    unsigned count = (delta & 31) + 1;\n    volatile unsigned *scratch = __builtin_alloca(count * sizeof(unsigned));\n    for (unsigned i = 0; i < count; i++) scratch[i] = delta ^ i;\n    delta = scratch[count - 1] ^ (count - 1);\n    counter += delta;')
        src.write_text(source, encoding='utf-8')
        subprocess.run([args.compiler, '-B' + str(Path(args.compiler).parent) + '/', '-O1', '-ffreestanding',
                        '-fno-builtin', '-ftls-model=local-exec', '-mtext-section-literals', '-c', str(src), '-o', str(obj)], check=True)
        subprocess.run([args.linker, '--no-relax', '-Ttext=0x40080000', '--entry=task', str(obj), '-o', str(elf)], check=True)
        subprocess.run([args.objcopy, '-O', 'binary', '--only-section=.text', str(elf), str(binary)], check=True)
        symbols = {f[2]: int(f[0], 16) for line in subprocess.check_output([args.nm, str(elf)], text=True).splitlines()
                   if len(f := line.split()) == 3}
        # ELF32 PT_TLS describes the initial per-task template, including .tbss.
        raw = elf.read_bytes(); assert raw[:7] == b'\x7fELF\x01\x01\x01'
        phoff = struct.unpack_from('<I', raw, 28)[0]; stride, count = struct.unpack_from('<HH', raw, 42)
        tls = [struct.unpack_from('<8I', raw, phoff + n * stride) for n in range(count)
               if struct.unpack_from('<I', raw, phoff + n * stride)[0] == 7]
        assert len(tls) == 1
        _, offset, _, _, filesz, memsz, _, align = tls[0]
        assert align and not align & (align - 1) and filesz <= memsz < 128
        # GNU Xtensa TLS ABI: align_up(TCB_SIZE=8, PT_TLS alignment), as documented
        # by ESP-IDF v4.4.7 port/xtensa/port.c pxPortInitialiseStack.
        bias = (8 + align - 1) & -align
        template = raw[offset:offset + filesz] + bytes(memsz - filesz)
        assert int.from_bytes(template[symbols['counter']:symbols['counter'] + 4], 'little') == 7
        assert int.from_bytes(template[symbols['guard']:symbols['guard'] + 4], 'little') == 0x12345678
        assert int.from_bytes(template[symbols['zero']:symbols['zero'] + 4], 'little') == 0
        lib = out / 'tls.so'
        sources = [ROOT / 'core/mcu/xtensa' / n for n in ['integer.c', 'execute.c', 'windows.c', 'interrupts.c']]
        sources += [ROOT / 'core/mcu/esp32' / n for n in ['fabric.c', 'machine.c', 'matrix.c', 'flashmap.c']]
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
        configure = bind('esk_esp32_machine_configure', C.c_int, pointer, C.c_uint, C.c_uint32)
        core = bind('esk_esp32_machine_core', C.POINTER(Windows), pointer, C.c_uint)
        prid = bind('esk_xtensa_prid_configure', C.c_int, C.POINTER(Windows), C.c_uint32)
        run = bind('esk_esp32_machine_run_core', C.c_int, pointer, C.c_uint, C.c_int)
        step = bind('esk_esp32_machine_step', C.c_int, pointer, C.c_uint, C.POINTER(C.c_uint), C.POINTER(Fault))
        total = 0; calls = 0; stack_moves = 0
        for trial in range(16):
            expected_replay = None
            deltas = [[rng.getrandbits(32) for _ in range(2)] for _ in range(24)]
            for replay in range(2):
                allocator = Allocator(); alloc_init(C.byref(allocator), None)
                machine = pointer(); assert create(C.byref(allocator), C.byref(machine)) == 0
                try:
                    ram = fabric(machine); bus = bus_get(machine, 0)
                    code = binary.read_bytes()
                    assert load(ram, 0, 0x40080000, C.c_char_p(code), len(code)) == 0
                    assert bus.write(bus.ctx, 0x3ff00030, 4, 1) == 0
                    bases = [[0x3ffb0000 + cpu * 4096 + task * 256 for task in range(3)] for cpu in range(2)]
                    counters = [[7] * 3 for _ in range(2)]; zeros = [[0] * 3 for _ in range(2)]
                    for banks in bases:
                        for base in banks:
                            initial = b'\xa5' * bias + template + b'\x5a' * 16
                            assert load(ram, 0, base, C.c_char_p(initial), len(initial)) == 0
                    trace = []; results = []
                    for turn, increments in enumerate(deltas):
                        states = []; selected = [(turn + cpu + trial) % 3 for cpu in range(2)]
                        for cpu in range(2):
                            assert configure(machine, cpu, symbols['task']) == 0
                            w = core(machine, cpu).contents; w.ps |= 1 << 16
                            assert prid(core(machine, cpu), 0xa5a50000 | (cpu << 13)) == 0
                            w.cpu.a[1] = 0x3ffd0000 + cpu * 4096; w.cpu.a[4] = 0x7ffffff0
                            args_c = [bases[cpu][selected[cpu]], 0xcafe0000 | (cpu << 8) | turn,
                                      increments[cpu], 0x3ffc0000 + cpu * 256]
                            for n, arg in enumerate(args_c): w.cpu.a[6 + n] = arg
                            states.append(w); assert run(machine, cpu, 1) == 0
                        done = [False, False]; event = C.c_uint(); fault = Fault()
                        for tick in range(5000):
                            cpu = (tick + trial) % 2; w = states[cpu]
                            if done[cpu]: continue
                            if w.cpu.pc == 0x7ffffff0:
                                done[cpu] = True; assert run(machine, cpu, 0) == 0
                                if all(done): break
                                continue
                            pc = w.cpu.pc
                            word = int.from_bytes(code[pc - 0x40080000:pc - 0x40080000 + 3], 'little')
                            if word & 0xfff00f == 0x001000: stack_moves += 1
                            status = step(machine, cpu, C.byref(event), C.byref(fault))
                            assert status == 0 and event.value == 0, (status, cpu, hex(fault.pc), hex(fault.opcode), fault.kind, event.value)
                            trace.append((cpu, pc, w.cpu.pc)); total += 1
                        assert all(done)
                        value = C.c_uint32()
                        def read(address):
                            assert bus.read(bus.ctx, address, 4, C.byref(value)) == 0
                            return value.value
                        for cpu, w in enumerate(states):
                            task = selected[cpu]; base = bases[cpu][task]
                            counters[cpu][task] = (counters[cpu][task] + increments[cpu]) & 0xffffffff
                            zeros[cpu][task] += 1
                            wanted = counters[cpu][task] ^ 0x12345678 ^ zeros[cpu][task]
                            assert w.cpu.a[6] == wanted and w.base == 0 and w.start == 1
                            assert w.cpu.a[1] == 0x3ffd0000 + cpu * 4096
                            assert w.threadptr == base and w.misc[0] == (0xcafe0000 | (cpu << 8) | turn)
                            assert read(0x3ffc0000 + cpu * 256) == base + bias + symbols['counter']
                            assert read(0x3ffc0004 + cpu * 256) == cpu
                            assert read(0x3ffc0008 + cpu * 256) == w.misc[0]
                            for bank, location in enumerate(bases[cpu]):
                                assert read(location + bias + symbols['counter']) == counters[cpu][bank]
                                assert read(location + bias + symbols['zero']) == zeros[cpu][bank]
                                assert read(location + bias + symbols['guard']) == 0x12345678
                                assert read(location) == 0xa5a5a5a5 and read(location + bias + memsz) == 0x5a5a5a5a
                            results.append(w.cpu.a[6]); calls += 1
                    result = trace, results
                    if replay: assert result == expected_replay
                    else: expected_replay = result
                finally:
                    destroy(machine)
                assert allocator.live_bytes == 0
        assert stack_moves == (calls if args.dynamic_stack else 0)
        print(f'{calls} actual ESP32 TLS C calls, six persistent task banks, 16 exact replays, {total} retired instructions; {stack_moves} MOVSP executions')


if __name__ == '__main__':
    main()
