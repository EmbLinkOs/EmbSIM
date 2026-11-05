#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""ESP32-compiled cross-core mailbox with native DPORT routing and ISR return.

A windowed-ABI sender signals a WAITI peer. An assembled context-saving entry
clears the source and publishes a release ACK from an assembled ISR. Neither
memory nor interrupt delivery uses Python callbacks. This is not ESP-IDF boot,
full ISR/RTOS context switching, reset hardware or cycle qualification.
"""
import argparse
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
from run_execute import ROOT, Memory, Fault
from run_windows_c import Windows
from run_machine_c import Allocator

SENDER = '''
unsigned sender(volatile unsigned *mailbox,unsigned count,unsigned seed) {
    volatile unsigned *ipi=(volatile unsigned *)0x3ff000dc;
    unsigned i;
    for(i=1;i<=count;i++) {
        mailbox[0]=seed^i;
        *ipi=1;
        while(__atomic_load_n(mailbox+1,__ATOMIC_ACQUIRE)!=(seed^i)) { }
    }
    return mailbox[1];
}
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['compiler', 'objcopy', 'linker', 'nm']:
        ap.add_argument('--' + name, required=True)
    ap.add_argument('--cc', default='cc'); args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-ipi-c-') as tmp:
        out = Path(tmp); objects = []
        for name, source in [('sender', SENDER)]:
            src, obj = out / (name + '.c'), out / (name + '.o'); objects.append(obj)
            src.write_text(source, encoding='utf-8')
            subprocess.run([args.compiler, '-B' + str(Path(args.compiler).parent) + '/', '-O1',
                            '-mtext-section-literals', '-ffreestanding', '-fno-builtin', '-c', str(src), '-o', str(obj)], check=True)
        # Fixture IRQ prologue/epilogue saves the interrupted integer context
        # on an EXCSAVE2 stack, using EXCSAVE3 to save the idle SP. Not an RTOS handler.
        entry = ['.literal .Lmailbox,0x3ffb0000', '.literal .Lipi,0x3ff000dc', '.text', '.align 4',
                 '.global irq_entry', 'irq_entry:', 'wsr a1,EXCSAVE3', 'rsr a1,EXCSAVE2', 'addi a1,a1,-64']
        regs = [0] + list(range(2, 16))
        entry += [f's32i a{r},a1,{i * 4}' for i, r in enumerate(regs)]
        entry += ['l32r a2,.Lmailbox', 'l32i a3,a2,0', 'l32r a4,.Lipi', 'movi a5,0', 's32i a5,a4,0',
                  'l32i a4,a2,8', 'addi a4,a4,1', 's32i a4,a2,8', 's32ri a3,a2,4']
        entry += [f'l32i a{r},a1,{i * 4}' for i, r in enumerate(regs)]
        entry += ['rsr a1,EXCSAVE3', 'rfi 2', '.align 4', '.global idle',
                  'idle:', 'waiti 0', 'j idle']
        src, obj = out / 'entry.S', out / 'entry.o'; objects.append(obj)
        src.write_text('\n'.join(entry) + '\n', encoding='utf-8')
        subprocess.run([args.compiler, '-B' + str(Path(args.compiler).parent) + '/', '-c', str(src), '-o', str(obj)], check=True)
        script = out / 'memory.ld'
        script.write_text('SECTIONS { .text 0x40080000 : { *(.literal .literal.*) *(.text .text.*) } }\n', encoding='utf-8')
        elf, binary = out / 'ipi.elf', out / 'ipi.bin'
        subprocess.run([args.linker, '--no-relax', '-T', str(script), '--entry=sender', *map(str, objects), '-o', str(elf)], check=True)
        subprocess.run([args.objcopy, '-O', 'binary', '--only-section=.text', str(elf), str(binary)], check=True)
        symbols = {f[2]: int(f[0], 16) for line in subprocess.check_output([args.nm, str(elf)], text=True).splitlines()
                   if len(f := line.split()) == 3}
        lib = out / 'ipi.so'
        sources = [ROOT / 'core/mcu/xtensa' / n for n in ['integer.c', 'execute.c', 'windows.c', 'interrupts.c']]
        sources += [ROOT / 'core/mcu/esp32' / n for n in ['fabric.c', 'machine.c', 'matrix.c']]
        sources += [ROOT / 'core/util/alloc.c']
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'core'), '-I' + str(ROOT / 'include'), *map(str, sources), '-o', str(lib)], check=True)
        api = C.CDLL(str(lib)); pointer = C.c_void_p; wp = C.POINTER(Windows)
        def bind(name, result, *params):
            fn = getattr(api, name); fn.restype = result; fn.argtypes = list(params); return fn
        alloc_init = bind('esk_alloc_init', None, C.POINTER(Allocator), pointer)
        create = bind('esk_esp32_machine_create', C.c_int, C.POINTER(Allocator), C.POINTER(pointer))
        destroy = bind('esk_esp32_machine_destroy', None, pointer)
        fabric = bind('esk_esp32_machine_fabric', pointer, pointer)
        bus_get = bind('esk_esp32_machine_bus', Memory, pointer, C.c_uint)
        load = bind('esk_esp32_fabric_load', C.c_int, pointer, C.c_uint, C.c_uint32, pointer, C.c_size_t)
        configure = bind('esk_esp32_machine_configure', C.c_int, pointer, C.c_uint, C.c_uint32)
        core = bind('esk_esp32_machine_core', wp, pointer, C.c_uint)
        run = bind('esk_esp32_machine_run_core', C.c_int, pointer, C.c_uint, C.c_int)
        step = bind('esk_esp32_machine_step', C.c_int, pointer, C.c_uint, C.POINTER(C.c_uint), C.POINTER(Fault))
        exceptions = bind('esk_xtensa_exception_configure', None, wp, C.POINTER(C.c_uint32))
        irq = bind('esk_xtensa_irq_configure', C.c_int, wp, C.POINTER(C.c_uint32))
        total = 0; deliveries = 0; runs = 0; code = binary.read_bytes()
        for trial in range(64):
            count = 1 + trial % 32; seed = 0x10203040 + trial * 256; replay_trace = None
            for replay in range(2):
                allocator = Allocator(); alloc_init(C.byref(allocator), None)
                machine = pointer(); assert create(C.byref(allocator), C.byref(machine)) == 0
                try:
                    ram = fabric(machine); bus = bus_get(machine, 0)
                    assert load(ram, 0, 0x40080000, C.c_char_p(code), len(code)) == 0
                    assert configure(machine, 0, symbols['sender']) == 0
                    assert configure(machine, 1, symbols['idle']) == 0
                    pro, app = [core(machine, cpu).contents for cpu in range(2)]
                    pro.ps |= 1 << 16; pro.cpu.a[4] = 0x7ffffff0
                    pro.cpu.a[0] = 0x12345678; pro.cpu.a[1] = 0x3ffd0000
                    pro.cpu.a[6] = 0x3ffb0000; pro.cpu.a[7] = count; pro.cpu.a[8] = seed
                    # Distinct idle registers must survive every actual ISR.
                    app.cpu.a[1] = 0x3ffd8000
                    for r in [0] + list(range(2, 16)): app.cpu.a[r] = 0xa5000000 + r
                    original = list(app.cpu.a)
                    general = (C.c_uint32 * 3)(*[symbols['irq_entry']] * 3)
                    high = (C.c_uint32 * 6)(*[symbols['irq_entry']] * 6)
                    exceptions(C.byref(app), general); assert irq(C.byref(app), high) == 0
                    app.irq.excsave[2] = 0x3ffd9000; app.irq.intenable = 1 << 19
                    assert bus.write(bus.ctx, 0x3ff00278, 4, 19) == 0
                    # Guard both ends of the IRQ frame/leaf-call stack region.
                    for addr in [0x3ffd8f00, 0x3ffd9000]: assert bus.write(bus.ctx, addr, 4, 0xc0decafe) == 0
                    for cpu in range(2): assert run(machine, cpu, 1) == 0
                    event = C.c_uint(); fault = Fault(); trace = []; accepted = 0; sender_done = False
                    for turn in range(50000):
                        cpu = (turn + trial) % 2
                        if cpu == 0 and pro.cpu.pc == 0x7ffffff0:
                            if not sender_done:
                                assert run(machine, 0, 0) == 0; sender_done = True
                        if cpu == 0 and sender_done:
                            continue
                        status = step(machine, cpu, C.byref(event), C.byref(fault))
                        assert status == 0, (status, cpu, hex(fault.pc), hex(fault.opcode), fault.kind)
                        assert event.value in [0, 12, 18], event.value
                        trace.append((cpu, event.value, pro.cpu.pc, app.cpu.pc)); total += event.value == 0
                        if event.value == 12: accepted += 1
                        # Finish only after last ISR returned and re-entered WAITI.
                        if sender_done and app.irq.waiting and not (app.ps & 0x10): break
                    assert sender_done and accepted == count and app.irq.waiting
                    value = C.c_uint32()
                    for addr, expected in [(0x3ffb0000, seed ^ count), (0x3ffb0004, seed ^ count),
                                           (0x3ffb0008, count), (0x3ff000dc, 0),
                                           (0x3ff000f8, 0), (0x3ffd8f00, 0xc0decafe), (0x3ffd9000, 0xc0decafe)]:
                        assert bus.read(bus.ctx, addr, 4, C.byref(value)) == 0 and value.value == expected, hex(addr)
                    assert pro.cpu.a[6] == seed ^ count and pro.cpu.a[0] == 0x12345678 and pro.cpu.a[1] == 0x3ffd0000
                    assert pro.base == 0 and pro.start == 1 and app.base == 0 and app.start == 1
                    assert list(app.cpu.a) == original and app.irq.excsave[2] == 0x3ffd9000
                    if replay: assert trace == replay_trace
                    else: replay_trace = trace
                    deliveries += accepted; runs += 1
                finally:
                    destroy(machine)
                assert allocator.live_bytes == 0
        print(f'{runs} compiled mailbox executions, {deliveries} routed interrupts, 64 exact replays, {total} retired instructions')


if __name__ == '__main__':
    main()
