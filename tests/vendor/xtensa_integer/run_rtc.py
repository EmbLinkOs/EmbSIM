#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Pinned RTC reset/retention referee and unchanged external ROM helper.

Explicit raw snapshots, not owner RTC/power state or complete reset-domain boot.
No ROM bytes or vendor implementation are distributed by this runner.
"""
import argparse
import ctypes as C
import hashlib
from pathlib import Path
import random
import re
import subprocess
import tempfile
from run_execute import ROOT,Memory,Fault
from run_windows_c import Windows
from run_machine_c import Allocator
from run_matrix import read_header


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['rtc-reg','rtc-header','rom-symbols','rom']:ap.add_argument('--'+name,type=Path,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0x3041)
    reg=read_header(args.rtc_reg,'c2d17880e08b28288fba95c3daa48f9a8672722fd56a5616728ee025dc61e0fb')
    header=read_header(args.rtc_header,'90bb6c9a0d497a264041c0ac7abdd0475375edcae812868a9deaed0044994f44')
    symbols=read_header(args.rom_symbols,'0160cb9e14e83de87ad7363bd4977a9dd353b22e868a9bd4474e167ee86e58ec')
    rom=args.rom.read_bytes();assert hashlib.sha256(rom).hexdigest()=='9115bbcd03da9f54d3baca5ad94d588a43da54ed332347137ba3dc9010df258b'
    entry=int(re.search(r'PROVIDE\s*\(\s*rtc_get_reset_reason\s*=\s*(0x[0-9a-f]+)',symbols)[1],16)
    def address(name):return 0x3ff48000+int(re.search(r'#define\s+RTC_CNTL_'+name+r'_REG\s+\(DR_REG_RTCCNTL_BASE\s*\+\s*(0x[0-9a-f]+)\)',reg)[1],16)
    def field(name):
        shift=int(re.search(r'#define\s+RTC_CNTL_'+name+r'_S\s+(\d+)',reg)[1])
        mask=int(re.search(r'#define\s+RTC_CNTL_'+name+r'_V\s+(\S+)',reg)[1],0);return mask,shift
    pro_mask,pro_shift=field('RESET_CAUSE_PROCPU');app_mask,app_shift=field('RESET_CAUSE_APPCPU')
    vector_mask=sum(mask<<shift for mask,shift in [field('PROCPU_STAT_VECTOR_SEL'),field('APPCPU_STAT_VECTOR_SEL')])
    reason=int(re.search(r'\bEXT_CPU_RESET\s*=\s*(\d+)',header)[1]);reset_reg=address('RESET_STATE')
    with tempfile.TemporaryDirectory(prefix='embsim-rtc-') as tmp:
        lib=Path(tmp)/'rtc.so';sources=[ROOT/'core/mcu/xtensa'/n for n in ['integer.c','execute.c','windows.c','interrupts.c']]
        sources += [ROOT/'core/mcu/esp32'/n for n in ['fabric.c','efuse.c','machine.c','matrix.c','flashmap.c','flashspi.c','w25q32.c','nor32.c','gt25q32.c']]
        sources += [ROOT/'core/util/alloc.c']
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC','-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),*map(str,sources),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));pointer=C.c_void_p
        def bind(name,result,*params):f=getattr(api,name);f.restype=result;f.argtypes=list(params);return f
        ai=bind('esk_alloc_init',None,C.POINTER(Allocator),pointer);create=bind('esk_esp32_machine_create',C.c_int,C.POINTER(Allocator),C.POINTER(pointer));destroy=bind('esk_esp32_machine_destroy',None,pointer)
        rtc=bind('esk_esp32_machine_rtc_configure',C.c_int,pointer,C.c_uint32,C.POINTER(C.c_uint32),C.c_size_t)
        fabric=bind('esk_esp32_machine_fabric',pointer,pointer);install=bind('esk_esp32_fabric_rom',C.c_int,pointer,C.c_uint,pointer,C.c_size_t)
        bus_get=bind('esk_esp32_machine_bus',Memory,pointer,C.c_uint);configure=bind('esk_esp32_machine_configure',C.c_int,pointer,C.c_uint,C.c_uint32)
        core=bind('esk_esp32_machine_core',C.POINTER(Windows),pointer,C.c_uint);run=bind('esk_esp32_machine_run_core',C.c_int,pointer,C.c_uint,C.c_int)
        app_reset=bind('esk_esp32_machine_app_reset_configure',C.c_int,pointer,C.c_uint)
        step=bind('esk_esp32_machine_step',C.c_int,pointer,C.c_uint,C.POINTER(C.c_uint),C.POINTER(Fault))
        allocator=Allocator();ai(C.byref(allocator),None);m=pointer();assert create(C.byref(allocator),C.byref(m))==0
        comparisons=0;calls=0;instructions=0;value=C.c_uint32();event=C.c_uint();fault=Fault()
        try:
            ram=fabric(m);assert install(ram,0,C.c_char_p(rom[:0x60000]),0x60000)==0
            buses=[bus_get(m,cpu) for cpu in range(2)];bus=buses[0]
            assert bus.write(bus.ctx,0x3ff00030,4,1)==0
            for trial in range(64):
                state=rng.getrandbits(14);words=[rng.getrandbits(32) for _ in range(8)];raw=(C.c_uint32*8)(*words)
                assert rtc(m,state,raw,8)==0
                for cpu in range(2):
                    current=buses[cpu]
                    assert current.read(current.ctx,reset_reg,4,C.byref(value))==0 and value.value==state;comparisons+=1
                    changed=rng.getrandbits(32);assert current.write(current.ctx,reset_reg,4,changed)==0
                    state=(state&~vector_mask)|(changed&vector_mask)
                    assert current.read(current.ctx,reset_reg,4,C.byref(value))==0 and value.value==state;comparisons+=1
                    for i in range(8):
                        words[i]^=0xffffffff;assert current.write(current.ctx,address(f'STORE{i}'),4,words[i])==0
                        peer=buses[1-cpu];assert peer.read(peer.ctx,address(f'STORE{i}'),4,C.byref(value))==0 and value.value==words[i];comparisons+=1
                    for selected in range(2):
                        assert configure(m,cpu,entry)==0;w=core(m,cpu).contents;w.ps|=1<<16
                        w.cpu.a[1]=0x3ffd0000+cpu*4096;w.cpu.a[4]=0x7ffffff0;w.cpu.a[6]=selected
                        assert run(m,cpu,1)==0
                        for turn in range(30):
                            if w.cpu.pc==0x7ffffff0:break
                            assert step(m,cpu,C.byref(event),C.byref(fault))==0 and event.value==0,(hex(fault.pc),hex(fault.opcode),event.value)
                            instructions+=1
                        else:raise AssertionError('ROM helper exceeded budget')
                        mask,shift=(app_mask,app_shift) if selected else (pro_mask,pro_shift)
                        assert w.cpu.a[6]==(state>>shift)&mask;calls+=1
            assert app_reset(m,1)==0
            assert bus.read(bus.ctx,reset_reg,4,C.byref(value))==0 and ((value.value>>app_shift)&app_mask)==reason
            comparisons+=1
        finally:destroy(m)
        assert allocator.live_bytes==0
        print(f'{comparisons} pinned RTC field/retention/reset comparisons; {calls} unchanged external ROM rtc_get_reset_reason calls, {instructions} retired instructions on native buses')


if __name__=='__main__':main()
