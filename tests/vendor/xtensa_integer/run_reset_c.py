#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Compiled PRO reset/release and APP worker through an original test-ROM program.

Not Espressif ROM, ESP-IDF boot, flash/cache or a qualified complete reset model.
The ROM fixture initializes supported CPU state and reads BOOT_ADDR in firmware;
no Python memory callbacks or host PC shortcut perform the handoff.
"""
import argparse
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
from run_execute import ROOT, Memory, Fault
from run_windows_c import Windows
from run_machine_c import Allocator

SOURCE = '''
unsigned supervisor(unsigned boot,unsigned token) {
    volatile unsigned *reset=(volatile unsigned *)0x3ff0002c;
    volatile unsigned *clock=(volatile unsigned *)0x3ff00030;
    volatile unsigned *stall=(volatile unsigned *)0x3ff00034;
    volatile unsigned *address=(volatile unsigned *)0x3ff00038;
    volatile unsigned *mailbox=(volatile unsigned *)0x3ffb0000;
    *reset=1;
    *clock=1;
    *stall=0;
#if MAPPED_FLASH
    volatile unsigned *cache=(volatile unsigned *)0x3ff00058;
    volatile unsigned *mmu=(volatile unsigned *)0x3ff12000;
    *cache=0;
    mmu[77]=0;
    cache[1]=cache[1]&~1u;
    *cache=16;
    while(!(*cache&32)) { }
    *cache=24;
#endif
    mailbox[0]=token;
    mailbox[1]=0;
    *address=boot;
    *reset=0;
    while(__atomic_load_n(mailbox+1,__ATOMIC_ACQUIRE)!=(token^0x6a09e667)) { }
    return mailbox[1];
}
unsigned worker(void) {
    volatile unsigned *mailbox=(volatile unsigned *)0x3ffb0000;
    unsigned value=mailbox[0]^0x6a09e667;
    mailbox[2]=mailbox[2]+1;
    return value;
}
'''
ROM = '''
.text
.begin no-transform
.Lstack: .long 0x3ffd2000
.Lps: .long 0x40000
.Lboot: .long 0x3ff00038
.Lmailbox: .long 0x3ffb0000
.Lvecbase: .long 0x40088000
.org 0x400
.global reset_entry
reset_entry:
    movi a2,0
    wsr a2,WINDOWBASE
    movi a2,1
    wsr a2,WINDOWSTART
    movi a2,0
    wsr a2,INTENABLE
    l32r a1,.Lstack
    l32r a2,.Lvecbase
    wsr a2,VECBASE
    l32r a2,.Lps
    wsr a2,PS
    rsync
    l32r a3,.Lboot
    l32i a3,a3,0
    callx4 a3
    l32r a2,.Lmailbox
    s32ri a6,a2,4
.global reset_done
reset_done:
    waiti 0
    j reset_done
.end no-transform
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['compiler', 'objcopy', 'linker', 'nm']:
        ap.add_argument('--' + name, required=True)
    ap.add_argument('--cc', default='cc'); ap.add_argument('--flash-worker', action='store_true'); args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-reset-c-') as tmp:
        out = Path(tmp)
        def compile_image(name, source, base, entry, assembly=False):
            src = out / (name + ('.S' if assembly else '.c')); obj = out / (name + '.o')
            elf, binary = out / (name + '.elf'), out / (name + '.bin')
            src.write_text(source, encoding='utf-8')
            subprocess.run([args.compiler, '-B' + str(Path(args.compiler).parent) + '/', '-O1',
                            '-DMAPPED_FLASH=' + str(int(args.flash_worker)), '-mtext-section-literals', '-ffreestanding', '-fno-builtin', '-c', str(src), '-o', str(obj)], check=True)
            subprocess.run([args.linker, '--no-relax', '-Ttext=' + hex(base), '--entry=' + entry,
                            str(obj), '-o', str(elf)], check=True)
            subprocess.run([args.objcopy, '-O', 'binary', '--only-section=.text', str(elf), str(binary)], check=True)
            symbols = {f[2]: int(f[0], 16) for line in subprocess.check_output([args.nm, str(elf)], text=True).splitlines()
                       if len(f := line.split()) == 3}
            return binary.read_bytes(), symbols
        code, symbols = compile_image('tasks', SOURCE, 0x40080000, 'supervisor')
        flash_image = None
        if args.flash_worker:
            worker_source = 'unsigned worker(void)' + SOURCE.split('unsigned worker(void)', 1)[1]
            worker, flash_symbols = compile_image('flashworker', worker_source, 0x400d0000, 'worker')
            assert len(worker) < 65536
            flash_image = worker + bytes(65536 - len(worker)); symbols['worker'] = flash_symbols['worker']
        rom, rom_symbols = compile_image('rom', ROM, 0x40000000, 'reset_entry', True)
        assert rom_symbols['reset_entry'] == 0x40000400 and len(rom) < 0x60000
        rom = rom + bytes(0x60000 - len(rom))  # original test fixture, not a product ROM
        lib = out / 'reset.so'
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
        install_flash = bind('esk_esp32_fabric_flash', C.c_int, pointer, pointer, C.c_size_t)
        install_rom = bind('esk_esp32_fabric_rom', C.c_int, pointer, C.c_uint, pointer, C.c_size_t)
        configure = bind('esk_esp32_machine_configure', C.c_int, pointer, C.c_uint, C.c_uint32)
        reset_profile = bind('esk_esp32_machine_app_reset_configure', C.c_int, pointer, C.c_uint)
        source_input = bind('esk_esp32_machine_source', C.c_int, pointer, C.c_uint, C.c_int)
        core = bind('esk_esp32_machine_core', C.POINTER(Windows), pointer, C.c_uint)
        run = bind('esk_esp32_machine_run_core', C.c_int, pointer, C.c_uint, C.c_int)
        step = bind('esk_esp32_machine_step', C.c_int, pointer, C.c_uint, C.POINTER(C.c_uint), C.POINTER(Fault))
        pending = bind('esk_xtensa_irq_pending', C.c_uint32, C.POINTER(Windows))
        total = 0; cycles = 0; rom_entries = 0; held = 0; flash_fetches = 0
        for trial in range(16):
            replay_trace = None
            for replay in range(2):
                allocator = Allocator(); alloc_init(C.byref(allocator), None)
                machine = pointer(); assert create(C.byref(allocator), C.byref(machine)) == 0
                try:
                    ram = fabric(machine); bus = bus_get(machine, 0); value = C.c_uint32()
                    assert load(ram, 0, 0x40080000, C.c_char_p(code), len(code)) == 0
                    assert install_rom(ram, 0, C.c_char_p(rom), len(rom)) == 0
                    if flash_image is not None: assert install_flash(ram, C.c_char_p(flash_image), len(flash_image)) == 0
                    assert reset_profile(machine, 1) == 0 and run(machine, 1, 1) == 0
                    app = core(machine, 1).contents; app.irq.intenable = 0xffffffff
                    # A live peripheral source must survive CPU-only resets. ROM
                    # explicitly initializes INTENABLE before lowering PS mask.
                    assert bus.write(bus.ctx, 0x3ff002a0, 4, 19) == 0
                    assert source_input(machine, 34, 1) == 0
                    for address in [0x3ffd1f00, 0x3ffd2000]: assert bus.write(bus.ctx, address, 4, 0xc0decafe) == 0
                    trace = []
                    for cycle in range(4):
                        token = 0x12340000 + trial * 16 + cycle
                        assert configure(machine, 0, symbols['supervisor']) == 0
                        pro = core(machine, 0).contents; pro.ps |= 1 << 16
                        pro.cpu.a[0] = 0x11223344; pro.cpu.a[1] = 0x3ffc0000; pro.cpu.a[4] = 0x7ffffff0
                        pro.cpu.a[6] = symbols['worker']; pro.cpu.a[7] = token
                        if cycle: app.lcount = 99  # next reset discards the prior hardware loop
                        assert run(machine, 0, 1) == 0
                        event = C.c_uint(); fault = Fault(); entered = False; pro_done = False; observed_hold = cycle == 0
                        for turn in range(10000):
                            cpu = (turn + trial) % 2
                            if cpu == 0 and pro.cpu.pc == 0x7ffffff0:
                                if not pro_done:
                                    assert run(machine, 0, 0) == 0; pro_done = True
                            if cpu == 0 and pro_done: continue
                            if cpu == 1:
                                assert bus.read(bus.ctx, 0x3ff0002c, 4, C.byref(value)) == 0
                                if value.value: observed_hold = True
                                if not value.value and observed_hold and not entered:
                                    assert app.cpu.pc == 0x40000400 and app.vecbase == 0x40000000
                                    assert app.ps == 0x1f and app.lcount == 0
                                    entered = True; rom_entries += 1
                            if cpu == 1 and 0x400d0000 <= app.cpu.pc < 0x400e0000: flash_fetches += 1
                            status = step(machine, cpu, C.byref(event), C.byref(fault))
                            assert status == 0 and event.value in [0, 18, 20, 22], (status, cpu, hex(fault.pc), hex(fault.opcode), event.value)
                            total += event.value == 0; held += event.value == 22
                            trace.append((cycle, cpu, event.value, pro.cpu.pc, app.cpu.pc, app.vecbase))
                            if pro_done and app.irq.waiting: break
                        assert pro_done and entered and app.irq.waiting
                        assert pro.cpu.a[6] == token ^ 0x6a09e667
                        assert pro.cpu.a[0] == 0x11223344 and pro.cpu.a[1] == 0x3ffc0000
                        assert pro.base == 0 and pro.start == 1 and app.base == 0 and app.start == 1
                        assert app.vecbase == 0x40088000 and app.lcount == 0 and app.irq.intenable == 0
                        assert pending(C.byref(app)) == 1 << 19
                        assert app.cpu.pc == rom_symbols['reset_done'] + 3
                        for address, expected in [(0x3ffb0000, token), (0x3ffb0004, token ^ 0x6a09e667),
                                                  (0x3ffb0008, cycle + 1), (0x3ff00038, symbols['worker']),
                                                  (0x3ffd1f00, 0xc0decafe), (0x3ffd2000, 0xc0decafe)]:
                            assert bus.read(bus.ctx, address, 4, C.byref(value)) == 0 and value.value == expected, hex(address)
                        cycles += 1
                    if replay: assert trace == replay_trace
                    else: replay_trace = trace
                finally:
                    destroy(machine)
                assert allocator.live_bytes == 0
        assert held > 0
        if args.flash_worker: assert flash_fetches > 0
        print(f'{cycles} compiled reset/worker cycles, {rom_entries} static test-ROM entries, 16 exact replays, {total} retired instructions; {held} reset-held turns; {flash_fetches} mapped-flash instructions')


if __name__ == '__main__':
    main()
