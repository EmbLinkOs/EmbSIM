#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Two ESP32-compiled tasks over the native shared internal-RAM fabric.

No Python memory or atomic callbacks. Instruction-interleaved functional proof;
not ROM boot, a complete SMP system, cache coherence or cycle qualification.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
from run_execute import Fault, Memory, ROOT, MASK
from run_windows_c import Windows

SOURCE = '''
unsigned task(volatile unsigned *counter, unsigned count) {
    unsigned sum=0;
    while(count--) sum+=__sync_add_and_fetch(counter,1);
    return sum;
}
unsigned task_locked(volatile unsigned *counter, unsigned count, volatile unsigned *lock) {
    unsigned sum=0;
    while(count--) {
        while(__atomic_load_n(lock,__ATOMIC_ACQUIRE) || __sync_lock_test_and_set(lock,1)) { }
        *counter=*counter+1;
        sum+=*counter;
        __sync_lock_release(lock);
    }
    return sum;
}
'''


class Allocator(C.Structure):
    _fields_ = [('hooks', C.c_void_p * 4), ('live_bytes', C.c_size_t),
                ('allocations', C.c_size_t)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['compiler', 'objcopy', 'linker', 'nm']:
        ap.add_argument('--' + name, required=True)
    ap.add_argument('--cc', default='cc')
    args = ap.parse_args()
    rng = random.Random(0x32c0)
    with tempfile.TemporaryDirectory(prefix='embsim-machine-') as tmp:
        out = Path(tmp)
        src, obj, elf, binary = [out / ('task.' + x) for x in ['c', 'o', 'elf', 'bin']]
        src.write_text(SOURCE, encoding='utf-8')
        subprocess.run([args.compiler, '-B' + str(Path(args.compiler).parent) + '/',
                        '-O1', '-ffreestanding', '-fno-builtin', '-c', str(src), '-o', str(obj)], check=True)
        subprocess.run([args.linker, '--no-relax', '-Ttext=0x40080000', '--entry=task',
                        str(obj), '-o', str(elf)], check=True)
        subprocess.run([args.objcopy, '-O', 'binary', '--only-section=.text', str(elf), str(binary)], check=True)
        symbols = {f[2]: int(f[0], 16) for line in subprocess.check_output([args.nm, str(elf)], text=True).splitlines()
                   if len(f := line.split()) == 3}
        lib = out / 'machine.so'
        sources = [ROOT / 'core/mcu/xtensa' / n for n in ['integer.c', 'execute.c', 'windows.c', 'interrupts.c']]
        sources += [ROOT / 'core/mcu/esp32' / n for n in ['fabric.c', 'efuse.c', 'machine.c','rtc_cal.c','uart.c','gpio.c', 'matrix.c', 'flashmap.c', 'flashspi.c', 'w25q32.c', 'nor32.c', 'gt25q32.c']]
        sources += [ROOT / 'core/util/alloc.c']
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'core'), '-I' + str(ROOT / 'include'),
                        *map(str, sources), '-o', str(lib)], check=True)
        api = C.CDLL(str(lib))
        def bind(name, result, *params):
            fn = getattr(api, name); fn.restype = result; fn.argtypes = list(params); return fn
        pointer = C.c_void_p
        alloc_init = bind('esk_alloc_init', None, C.POINTER(Allocator), pointer)
        create = bind('esk_esp32_machine_create', C.c_int, C.POINTER(Allocator), C.POINTER(pointer))
        destroy = bind('esk_esp32_machine_destroy', None, pointer)
        fabric = bind('esk_esp32_machine_fabric', pointer, pointer)
        bus_get = bind('esk_esp32_machine_bus', Memory, pointer, C.c_uint)
        load = bind('esk_esp32_fabric_load', C.c_int, pointer, C.c_uint, C.c_uint32, pointer, C.c_size_t)
        configure = bind('esk_esp32_machine_configure', C.c_int, pointer, C.c_uint, C.c_uint32)
        core = bind('esk_esp32_machine_core', C.POINTER(Windows), pointer, C.c_uint)
        run = bind('esk_esp32_machine_run_core', C.c_int, pointer, C.c_uint, C.c_int)
        step = bind('esk_esp32_machine_step', C.c_int, pointer, C.c_uint, C.POINTER(C.c_uint), C.POINTER(Fault))
        retired = bind('esk_esp32_machine_retired', C.c_uint64, pointer, C.c_uint)
        code = binary.read_bytes(); total = 0; runs = 0
        # Repeat each initial condition/schedule to prove deterministic replay.
        for trial in range(128):
            initial = [0, MASK - 30, 0x80000000][trial] if trial < 3 else rng.getrandbits(32)
            counts = [rng.randrange(1, 33), rng.randrange(1, 33)]
            trace = None
            for replay in range(2):
                allocator = Allocator(); alloc_init(C.byref(allocator), None)
                machine = pointer(); assert create(C.byref(allocator), C.byref(machine)) == 0
                try:
                    ram = fabric(machine); bus = bus_get(machine, 0)
                    assert load(ram, 0, 0x40080000, C.c_char_p(code), len(code)) == 0
                    assert bus.write(bus.ctx, 0x3ff00030, 4, 1) == 0
                    assert bus.write(bus.ctx, 0x3ffb0000, 4, initial) == 0
                    stop = 0x7ffffff0; states = []
                    for cpu in range(2):
                        assert configure(machine, cpu, symbols['task' if trial < 64 else 'task_locked']) == 0
                        w = core(machine, cpu).contents
                        w.ps |= 1 << 16; w.cpu.a[4] = stop
                        w.cpu.a[0] = 0x12340000 + cpu; w.cpu.a[1] = 0x3ffd0000 + cpu * 4096
                        w.cpu.a[6] = 0x3ffb0000; w.cpu.a[7] = counts[cpu]; w.cpu.a[8] = 0x3ffb0004
                        states.append(w); assert run(machine, cpu, 1) == 0
                    done = [False, False]; event = C.c_uint(); fault = Fault(); schedule = []
                    for turn in range(30000):
                        cpu = (turn + trial) % 2
                        if done[cpu]:
                            continue
                        w = states[cpu]
                        if w.cpu.pc == stop:
                            done[cpu] = True; assert run(machine, cpu, 0) == 0
                            if all(done):
                                break
                            continue
                        status = step(machine, cpu, C.byref(event), C.byref(fault))
                        assert status == 0 and event.value == 0, (status, cpu, hex(fault.pc), hex(fault.opcode), fault.kind, event.value)
                        schedule.append((cpu, w.cpu.pc)); total += 1
                    assert all(done), 'tasks did not finish'
                    value = C.c_uint32(); assert bus.read(bus.ctx, 0x3ffb0000, 4, C.byref(value)) == 0
                    n = sum(counts); assert value.value == (initial + n) & MASK
                    final_value = value.value
                    assert bus.read(bus.ctx, 0x3ffb0004, 4, C.byref(value)) == 0 and value.value == 0
                    # Every successful increment returns a distinct consecutive value.
                    assert sum(w.cpu.a[6] for w in states) & MASK == (n * initial + n * (n + 1) // 2) & MASK
                    for cpu, w in enumerate(states):
                        assert w.base == 0 and w.start == 1
                        assert w.cpu.a[0] == 0x12340000 + cpu and w.cpu.a[1] == 0x3ffd0000 + cpu * 4096
                        assert retired(machine, cpu) == sum(1 for owner, _ in schedule if owner == cpu)
                    result = (schedule, final_value, [w.cpu.a[6] for w in states])
                    if replay:
                        assert result == trace
                    else:
                        trace = result
                    runs += 2
                finally:
                    destroy(machine)
                assert allocator.live_bytes == 0
        print(f'{runs} compiled task executions, 128 schedule replays (atomic counters and spinlock-protected ordinary writes), {total} instructions; native shared RAM/CAS')


if __name__ == '__main__':
    main()
