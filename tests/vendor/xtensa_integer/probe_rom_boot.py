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
    ap.add_argument('--efuse',type=Path,help='explicit JSON list of 31 raw None-coded fuse words; not inferred board defaults')
    ap.add_argument('--efuse-completion-period',type=int,default=64,help='external fixture event every N instruction attempts; not physical timing')
    ap.add_argument('--rtc-power-on-fixture',action='store_true',help='explicit synthetic power-on reasons/vector selects and eight zero retention words; not owner RTC state')
    ap.add_argument('--prid',type=lambda value:int(value,0),default=0xcdcd,help='PRO PRID; SDK documents 0xcdcd; 0 reproduces historical invalid-ID fixture')
    ap.add_argument('--report',type=Path);ap.add_argument('--steps',type=int,default=100000)
    ap.add_argument('--cc',default='cc');args=ap.parse_args()
    if not 1<=args.efuse_completion_period<=1000000:ap.error('--efuse-completion-period must be 1..1000000')
    if not 0<=args.prid<=0xffffffff:ap.error('--prid must be uint32')
    if not 1<=args.steps<=1000000:ap.error('--steps must be 1..1000000')
    rom=args.rom.read_bytes();image=args.flash.read_bytes()
    if hashlib.sha256(rom).hexdigest()!=ROM_SHA256:ap.error('expected pinned Espressif QEMU 9.2.2 PRO ROM')
    if not image or len(image)%65536 or len(image)>0x1000000:ap.error('flash must be 1..256 complete 64KiB pages')
    with tempfile.TemporaryDirectory(prefix='embsim-native-rom-probe-') as tmp:
        lib=Path(tmp)/'probe.so'
        sources=[ROOT/'core/mcu/xtensa'/n for n in ['integer.c','execute.c','windows.c','interrupts.c']]
        sources += [ROOT/'core/mcu/esp32'/n for n in ['fabric.c', 'efuse.c','machine.c','matrix.c','flashmap.c','flashspi.c','w25q32.c', 'nor32.c', 'gt25q32.c']]
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
        efuse=bind('esk_esp32_fabric_efuse_configure',C.c_int,pointer,C.POINTER(C.c_uint32),C.c_size_t)
        complete=bind('esk_esp32_fabric_efuse_read_complete',C.c_int,pointer)
        spi=bind('esk_esp32_fabric_spi1_configure',C.c_int,pointer,C.c_int)
        core=bind('esk_esp32_machine_core',C.POINTER(Windows),pointer,C.c_uint)
        rtc=bind('esk_esp32_machine_rtc_configure',C.c_int,pointer,C.c_uint32,C.POINTER(C.c_uint32),C.c_size_t)
        memctl=bind('esk_xtensa_memctl_configure',C.c_int,C.POINTER(Windows))
        region=bind('esk_xtensa_region_configure',C.c_int,C.POINTER(Windows))
        reset=bind('esk_xtensa_lx6_reset',C.c_int,C.POINTER(Windows),C.c_uint)
        prid=bind('esk_xtensa_prid_configure',C.c_int,C.POINTER(Windows),C.c_uint32)
        run=bind('esk_esp32_machine_run_core',C.c_int,pointer,C.c_uint,C.c_int)
        step=bind('esk_esp32_machine_step',C.c_int,pointer,C.c_uint,C.POINTER(C.c_uint),C.POINTER(Fault))
        retired=bind('esk_esp32_machine_retired',C.c_uint64,pointer,C.c_uint)
        allocator=Allocator();init(C.byref(allocator),None);machine=pointer()
        assert create(C.byref(allocator),C.byref(machine))==0
        report={'boot_verified':False,'outcome':'step_budget_inconclusive','core':'PRO only',
                'rom_sha256':ROM_SHA256,'flash_sha256':hashlib.sha256(image).hexdigest(),
                'reset_vector':'0x40000400','memctl_profile':'bit 0 plus deterministic 256-byte loop-buffer fixture; not physical fetch/timing qualification','region_profile':'original ESP32 eight ITLB/DTLB identity regions, reset attribute 2, one ring, no Xtensa cache','prid_fixture':f'0x{args.prid:08x}',
                'prid_reference':'ESP-IDF v4.4.7 xtensa_context.h documents PRO=0xcdcd, APP=0xabab; not a hardware readout',
                'rom_tail_policy':'zero-fill absent DROM tail to match QEMU region setup, not silicon contents',
                'flash_profile':'immutable SPI1 fixture, not a physical flash identity'}
        try:
            ram=fabric(machine)
            if args.rtc_power_on_fixture:
                words=(C.c_uint32*8)();assert rtc(machine,0x3041,words,8)==0
                report['rtc_fixture']='power-on cause 1 per core, vector selects 1, eight explicit zero retention words; not physical-owner RTC state'
            if args.efuse:
                words=json.loads(args.efuse.read_text(encoding='utf-8'))
                if not isinstance(words,list) or len(words)!=31 or any(type(n) is not int or not 0<=n<=0xffffffff for n in words):ap.error('eFuse image must contain 31 uint32 words')
                supplied=(C.c_uint32*31)(*words)
                if efuse(ram,supplied,31)!=0:ap.error('unsupported eFuse coding/profile; expected None coding')
                report['efuse_fixture_sha256']=hashlib.sha256(args.efuse.read_bytes()).hexdigest()
            data=rom[:0x60000]
            assert install(ram,0,C.c_char_p(data),len(data))==0
            data=rom[0x60000:]+bytes(0x70000-len(rom));assert len(data)==65536
            assert install(ram,1,C.c_char_p(data),len(data))==0
            assert flash(ram,C.c_char_p(image),len(image))==0 and spi(ram,1)==0
            w=core(machine,0);assert reset(w,1)==0 and region(w)==0 and memctl(w)==0 and prid(w,args.prid)==0 and run(machine,0,1)==0
            event=C.c_uint();fault=Fault();trace=deque(maxlen=12)
            completions=0
            report['efuse_completion_fixture']=f'one external completion opportunity every {args.efuse_completion_period} instruction attempts; not physical timing'
            for attempt in range(args.steps):
                if args.efuse and attempt%args.efuse_completion_period==0:
                    completion=complete(ram);assert completion in (0,-8)
                    completions+=completion==0
                pc=w.contents.cpu.pc;status=step(machine,0,C.byref(event),C.byref(fault));trace.append(hex(pc))
                if status or event.value>=18:
                    report.update(outcome='blocked' if status else 'suspended',status=status,event=event.value,
                                  pc=hex(fault.pc if status else pc),opcode=hex(fault.opcode),address=hex(fault.address),
                                  fault_kind=fault.kind,attempts=attempt+1)
                    break
            report['region_readback']=[list(bank) for bank in w.contents.region]
            report['region_access_attributes']=[list(bank) for bank in w.contents.region_active]
            report.update(efuse_completions=completions,retired_before_stop=retired(machine,0),trace_tail=list(trace))
        finally:destroy(machine)
        assert allocator.live_bytes==0
    output=json.dumps(report,indent=2)+'\n'
    if args.report:args.report.write_text(output,encoding='utf-8')
    print(output,end='')
    return 1


if __name__=='__main__':raise SystemExit(main())
