#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Diagnose the first native blocker using external Espressif QEMU PRO ROM.

This is a boot investigation, not a passing conformance suite. Returns 1 when
blocked or inconclusive. No vendor bytes embedded, actual board identity claimed,
unknown MMIO supplied, or host PC shortcut past reset. PRO only; no UART success
validator or APP-specific ROM provider, so a step budget cannot establish boot.
"""
from collections import deque
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from run_execute import ROOT, Fault
from run_windows_c import Windows
from run_machine_c import Allocator

ROM_SHA256='9115bbcd03da9f54d3baca5ad94d588a43da54ed332347137ba3dc9010df258b'


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--rom',type=Path,required=True);ap.add_argument('--flash',type=Path,required=True)
    ap.add_argument('--report',type=Path);ap.add_argument('--steps',type=int,default=100000)
    ap.add_argument('--cc',default='cc');args=ap.parse_args()
    if not 1<=args.steps<=1000000:ap.error('--steps must be 1..1000000')
    rom=args.rom.read_bytes();image=args.flash.read_bytes()
    if hashlib.sha256(rom).hexdigest()!=ROM_SHA256:ap.error('expected pinned Espressif QEMU 9.2.2 PRO ROM')
    if not image or len(image)%65536 or len(image)>0x1000000:ap.error('flash must be 1..256 complete 64KiB pages')
    with tempfile.TemporaryDirectory(prefix='embsim-native-rom-probe-') as tmp:
        lib=Path(tmp)/'probe.so'
        sources=[ROOT/'core/mcu/xtensa'/n for n in ['integer.c','execute.c','windows.c','interrupts.c']]
        sources += [ROOT/'core/mcu/esp32'/n for n in ['fabric.c','machine.c','matrix.c','flashmap.c','flashspi.c','w25q32.c']]
        sources += [ROOT/'core/util/alloc.c']
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),*map(str,sources),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));pointer=C.c_void_p
        def bind(name,result,*params):
            f=getattr(api,name);f.restype=result;f.argtypes=list(params);return f
        init=bind('esk_alloc_init',None,C.POINTER(Allocator),pointer)
        create=bind('esk_esp32_machine_create',C.c_int,C.POINTER(Allocator),C.POINTER(pointer))
        destroy=bind('esk_esp32_machine_destroy',None,pointer)
        fabric=bind('esk_esp32_machine_fabric',pointer,pointer)
        install=bind('esk_esp32_fabric_rom',C.c_int,pointer,C.c_uint,pointer,C.c_size_t)
        flash=bind('esk_esp32_fabric_flash',C.c_int,pointer,pointer,C.c_size_t)
        spi=bind('esk_esp32_fabric_spi1_configure',C.c_int,pointer,C.c_int)
        core=bind('esk_esp32_machine_core',C.POINTER(Windows),pointer,C.c_uint)
        reset=bind('esk_xtensa_lx6_reset',C.c_int,C.POINTER(Windows),C.c_uint)
        prid=bind('esk_xtensa_prid_configure',C.c_int,C.POINTER(Windows),C.c_uint32)
        run=bind('esk_esp32_machine_run_core',C.c_int,pointer,C.c_uint,C.c_int)
        step=bind('esk_esp32_machine_step',C.c_int,pointer,C.c_uint,C.POINTER(C.c_uint),C.POINTER(Fault))
        retired=bind('esk_esp32_machine_retired',C.c_uint64,pointer,C.c_uint)
        allocator=Allocator();init(C.byref(allocator),None);machine=pointer()
        assert create(C.byref(allocator),C.byref(machine))==0
        report={'boot_verified':False,'outcome':'step_budget_inconclusive','core':'PRO only',
                'rom_sha256':ROM_SHA256,'flash_sha256':hashlib.sha256(image).hexdigest(),
                'reset_vector':'0x40000400','prid_fixture':'0x00000000 (not silicon-qualified)',
                'rom_tail_policy':'zero-fill absent DROM tail to match QEMU region setup, not silicon contents',
                'flash_profile':'immutable SPI1 fixture, not a physical flash identity'}
        try:
            ram=fabric(machine);data=rom[:0x60000]
            assert install(ram,0,C.c_char_p(data),len(data))==0
            data=rom[0x60000:]+bytes(0x70000-len(rom));assert len(data)==65536
            assert install(ram,1,C.c_char_p(data),len(data))==0
            assert flash(ram,C.c_char_p(image),len(image))==0 and spi(ram,1)==0
            w=core(machine,0);assert reset(w,1)==0 and prid(w,0)==0 and run(machine,0,1)==0
            event=C.c_uint();fault=Fault();trace=deque(maxlen=12)
            for attempt in range(args.steps):
                pc=w.contents.cpu.pc;status=step(machine,0,C.byref(event),C.byref(fault));trace.append(hex(pc))
                if status or event.value>=18:
                    report.update(outcome='blocked' if status else 'suspended',status=status,event=event.value,
                                  pc=hex(fault.pc if status else pc),opcode=hex(fault.opcode),address=hex(fault.address),
                                  fault_kind=fault.kind,attempts=attempt+1)
                    break
            report.update(retired_before_stop=retired(machine,0),trace_tail=list(trace))
        finally:destroy(machine)
        assert allocator.live_bytes==0
    output=json.dumps(report,indent=2)+'\n'
    if args.report:args.report.write_text(output,encoding='utf-8')
    print(output,end='')
    return 1


if __name__=='__main__':raise SystemExit(main())
