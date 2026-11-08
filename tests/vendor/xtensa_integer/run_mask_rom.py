#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Independent per-core mask-ROM mapping checks and actual ROM helper execution.

External PRO/APP images and SDK headers are inputs, never bundled runtime code.
This does not establish APP reset/SDK boot or physical ROM/timing authenticity.
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
    for n in ['pro-rom','app-rom','soc-header','rom-symbols','miniz-header','qemu-source']:ap.add_argument('--'+n,type=Path,required=True)
    for n in ['compiler','objcopy','linker','nm']:ap.add_argument('--'+n,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0x40065c18)
    soc=read_header(args.soc_header,'7a7148c288d7c289c961810e5617fa7b14d105b28349a45f772150b1e0845dd3')
    symbols=read_header(args.rom_symbols,'0160cb9e14e83de87ad7363bd4977a9dd353b22e868a9bd4474e167ee86e58ec')
    miniz=read_header(args.miniz_header,'3d43ce51f596fbb8226a55c0c7f63693606c953a45f3c92ac7f33d4147ccf74d')
    qemu=read_header(args.qemu_source,'dcbdb0c67e2398e73e0d17b2f51d0f300161829b7bffd1c34b3114b168846d3f')
    low=int(re.search(r'#define\s+SOC_IROM_MASK_LOW\s+(0x[0-9a-fA-F]+)',soc)[1],16)
    high=int(re.search(r'#define\s+SOC_IROM_MASK_HIGH\s+(0x[0-9a-fA-F]+)',soc)[1],16)
    assert low==0x40000000 and high-low==0x70000
    assert re.search(r'\[ESP32_MEMREGION_IROM\]\s*=\s*\{\s*0x40000000,\s*0x70000\s*\}',qemu)
    assert re.search(r'memory_region_init_alias\(drom,\s*NULL,\s*name,\s*irom,\s*0x60000,',qemu)
    blobs=[]
    for path,digest in [(args.pro_rom,'9115bbcd03da9f54d3baca5ad94d588a43da54ed332347137ba3dc9010df258b'),(args.app_rom,'6d3240e7cdb1ec9c1d683515e0a8578e9fb8ece3e7736cb9a996512c656e8442')]:
        data=path.read_bytes();assert hashlib.sha256(data).hexdigest()==digest
        blobs.append(data+bytes(high-low-len(data)))
    helpers=['tdefl_get_adler32','tdefl_get_prev_return_status']
    entries={name:int(re.search(r'PROVIDE\s*\(\s*'+name+r'\s*=\s*(0x[0-9a-fA-F]+)',symbols)[1],16) for name in helpers}
    with tempfile.TemporaryDirectory(prefix='embsim-mask-rom-') as tmp:
        out=Path(tmp);lib=out/'rom.so';sources=[ROOT/'core/mcu/xtensa'/n for n in ['integer.c','execute.c','windows.c','interrupts.c']]
        sources += [ROOT/'core/mcu/esp32'/n for n in ['fabric.c','efuse.c','machine.c','rtc_cal.c','uart.c','gpio.c','matrix.c','flashmap.c','flashspi.c','w25q32.c','nor32.c','gt25q32.c']]
        sources += [ROOT/'core/util/alloc.c']
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC','-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),*map(str,sources),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));pointer=C.c_void_p
        def bind(name,result,*params):f=getattr(api,name);f.restype=result;f.argtypes=list(params);return f
        ai=bind('esk_alloc_init',None,C.POINTER(Allocator),pointer);create=bind('esk_esp32_machine_create',C.c_int,C.POINTER(Allocator),C.POINTER(pointer));destroy=bind('esk_esp32_machine_destroy',None,pointer)
        fabric=bind('esk_esp32_machine_fabric',pointer,pointer);install=bind('esk_esp32_fabric_mask_rom',C.c_int,pointer,C.c_uint,pointer,C.c_size_t)
        load=bind('esk_esp32_fabric_load',C.c_int,pointer,C.c_uint,C.c_uint32,pointer,C.c_size_t)
        bus_get=bind('esk_esp32_machine_bus',Memory,pointer,C.c_uint);configure=bind('esk_esp32_machine_configure',C.c_int,pointer,C.c_uint,C.c_uint32)
        core=bind('esk_esp32_machine_core',C.POINTER(Windows),pointer,C.c_uint);run=bind('esk_esp32_machine_run_core',C.c_int,pointer,C.c_uint,C.c_int)
        step=bind('esk_esp32_machine_step',C.c_int,pointer,C.c_uint,C.POINTER(C.c_uint),C.POINTER(Fault))
        allocator=Allocator();ai(C.byref(allocator),None);m=pointer();assert create(C.byref(allocator),C.byref(m))==0
        comparisons=0;calls=0;instructions=0;value=C.c_uint32();event=C.c_uint();fault=Fault()
        try:
            ram=fabric(m)
            for cpu,blob in enumerate(blobs):assert install(ram,cpu,C.c_char_p(blob),len(blob))==0
            for cpu,blob in enumerate(blobs):
                bus=bus_get(m,cpu)
                offsets=[rng.randrange((high-low)//4)*4 for _ in range(4096)]
                offsets+=[n&~3 for n,(a,b) in enumerate(zip(blobs[0],blobs[1])) if a!=b]
                for off in offsets:
                    wanted=int.from_bytes(blob[off:off+4],'little')
                    assert bus.read(bus.ctx,low+off,4,C.byref(value))==0 and value.value==wanted
                    assert bus.fetch(bus.ctx,low+off,4,C.byref(value))==0 and value.value==wanted;comparisons+=2
                for trial in range(1024):
                    off=rng.randrange(65536);assert bus.read(bus.ctx,0x3ff90000+off,1,C.byref(value))==0 and value.value==blob[0x60000+off]
                    comparisons+=1
            (out/'miniz.h').write_text(miniz,encoding='utf-8')
            src=out/'work.c';obj=out/'work.o';elf=out/'work.elf';binary=out/'work.bin'
            src.write_text('''/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "miniz.h"
unsigned work(unsigned value,unsigned which) {
    tdefl_compressor *d=(tdefl_compressor *)0x3ffb8000;
    d->m_adler32=value^0xa531d497U;
    d->m_prev_return_status=(tdefl_status)((int)(value&3U)-2);
    return which ? tdefl_get_adler32(d) : (unsigned)tdefl_get_prev_return_status(d);
}
''',encoding='utf-8')
            subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/','-O1','-ffreestanding','-fno-builtin','-mtext-section-literals','-c',str(src),'-o',str(obj)],check=True)
            subprocess.run([args.linker,'--no-relax','-Ttext=0x40080000','--entry=work',*[f'--defsym={name}={entry:#x}' for name,entry in entries.items()],str(obj),'-o',str(elf)],check=True)
            subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
            labels={f[2]:int(f[0],16) for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines() if len(f:=line.split())==3}
            code=binary.read_bytes();assert load(ram,0,0x40080000,C.c_char_p(code),len(code))==0
            bus=bus_get(m,0);assert bus.write(bus.ctx,0x3ff00030,4,1)==0
            for cpu in range(2):
                for trial in range(256):
                    data=rng.getrandbits(32)
                    for which in range(2):
                        assert configure(m,cpu,labels['work'])==0;w=core(m,cpu).contents;w.ps|=1<<16
                        w.cpu.a[1]=0x3ffd0000+cpu*4096;w.cpu.a[4]=0x7ffffff0;w.cpu.a[6]=data;w.cpu.a[7]=which
                        assert run(m,cpu,1)==0;seen=False
                        for turn in range(200):
                            if w.cpu.pc==0x7ffffff0:break
                            seen|=w.cpu.pc in entries.values()
                            assert step(m,cpu,C.byref(event),C.byref(fault))==0 and event.value==0,(hex(fault.pc),hex(fault.opcode),hex(fault.address),event.value)
                            instructions+=1
                        else:raise AssertionError('ROM helper firmware budget exceeded')
                        assert seen and w.cpu.a[6]==((data^0xa531d497) if which else (((data&3)-2)&0xffffffff))
                        calls+=1
        finally:destroy(m)
        assert allocator.live_bytes==0
        print(f'{comparisons} independent per-core ROM fetch/data/alias comparisons; {calls} actual ESP32 C calls, {instructions} retired instructions; two unchanged external ROM helpers, distinct PRO/APP images, SDK structure layout')


if __name__=='__main__':main()
