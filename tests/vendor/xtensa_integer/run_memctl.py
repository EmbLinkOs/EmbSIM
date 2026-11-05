#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""GNU-assembled MEMCTL register oracle and real ESP32 C local-loop execution.

ISA RI-2021.8 loop option / table 191 supplies bit 0 and reset 1.
No private-cache internal operation, complete MMU or SDK boot qualification.
"""
import argparse
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
from run_execute import ROOT, Memory, Fault
from run_windows_c import Windows
from run_machine_c import Allocator


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['assembler','compiler','objcopy','linker','nm']:ap.add_argument('--'+name,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args()
    specs=[(form,reg) for form in ['rsr','wsr','xsr'] for reg in range(16)]
    with tempfile.TemporaryDirectory(prefix='embsim-memctl-') as tmp:
        out=Path(tmp);src=out/'ops.S';obj=out/'ops.o';binary=out/'ops.bin'
        src.write_text('.text\n.begin no-transform\n'+'\n'.join(f'{form} a{reg},MEMCTL' for form,reg in specs)+'\n.end no-transform\n',encoding='utf-8')
        subprocess.run([args.assembler,str(src),'-o',str(obj)],check=True)
        subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(obj),str(binary)],check=True)
        ops=binary.read_bytes();assert len(ops)==len(specs)*3
        sources=[ROOT/'core/mcu/xtensa'/n for n in ['integer.c','execute.c','windows.c','interrupts.c']]
        sources += [ROOT/'core/mcu/esp32'/n for n in ['fabric.c','efuse.c','machine.c','matrix.c','flashmap.c','flashspi.c','w25q32.c','nor32.c','gt25q32.c']]
        sources += [ROOT/'core/util/alloc.c'];lib=out/'memctl.so'
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC','-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),*map(str,sources),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));pointer=C.c_void_p;wp=C.POINTER(Windows)
        def bind(name,result,*params):f=getattr(api,name);f.restype=result;f.argtypes=list(params);return f
        wi=bind('esk_xtensa_windows_init',None,wp,C.c_uint32,C.POINTER(C.c_uint32))
        enable=bind('esk_xtensa_memctl_configure',C.c_int,wp)
        execute=bind('esk_xtensa_window_execute',C.c_int,wp,pointer,pointer,C.c_size_t,C.POINTER(C.c_size_t),C.POINTER(C.c_uint),C.POINTER(Fault))
        comparisons=0
        for index,(form,reg) in enumerate(specs):
            for value in range(2):
                w=Windows();wi(C.byref(w),0x40080000,None);assert enable(C.byref(w))==0 and w.memctl==1
                old=1-value;w.memctl=old;w.cpu.a[reg]=value;used=C.c_size_t();event=C.c_uint();fault=Fault()
                code=C.create_string_buffer(ops[index*3:index*3+3])
                assert execute(C.byref(w),None,code,3,C.byref(used),C.byref(event),C.byref(fault))==0
                assert used.value==3 and event.value==0 and w.cpu.pc==0x40080003
                assert w.memctl==(value if form!='rsr' else old)
                assert w.cpu.a[reg]==(value if form=='wsr' else old)
                comparisons+=1
        source=out/'inspect.c';obj=out/'inspect.o';elf=out/'inspect.elf';binary=out/'inspect.bin'
        source.write_text('''unsigned inspect(unsigned count,unsigned enabled) {
    unsigned old,result=0;
    __asm__ volatile("rsr.memctl %0" : "=a"(old));
    __asm__ volatile("wsr.memctl %0; isync" :: "a"(enabled) : "memory");
    for(unsigned i=0;i<count;i++) result+=i^0x55u;
    __asm__ volatile("wsr.memctl %0; isync" :: "a"(old) : "memory");
    return result;
}
''',encoding='utf-8')
        subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/','-O2','-ffreestanding','-fno-builtin','-mtext-section-literals','-c',str(source),'-o',str(obj)],check=True)
        subprocess.run([args.linker,'--no-relax','-Ttext=0x40080000','--entry=inspect',str(obj),'-o',str(elf)],check=True)
        subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
        symbols={f[2]:int(f[0],16) for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines() if len(f:=line.split())==3}
        ai=bind('esk_alloc_init',None,C.POINTER(Allocator),pointer);create=bind('esk_esp32_machine_create',C.c_int,C.POINTER(Allocator),C.POINTER(pointer));destroy=bind('esk_esp32_machine_destroy',None,pointer)
        fabric=bind('esk_esp32_machine_fabric',pointer,pointer);load=bind('esk_esp32_fabric_load',C.c_int,pointer,C.c_uint,C.c_uint32,pointer,C.c_size_t)
        bus_get=bind('esk_esp32_machine_bus',Memory,pointer,C.c_uint);configure=bind('esk_esp32_machine_configure',C.c_int,pointer,C.c_uint,C.c_uint32)
        core=bind('esk_esp32_machine_core',wp,pointer,C.c_uint);run=bind('esk_esp32_machine_run_core',C.c_int,pointer,C.c_uint,C.c_int)
        step=bind('esk_esp32_machine_step',C.c_int,pointer,C.c_uint,C.POINTER(C.c_uint),C.POINTER(Fault))
        allocator=Allocator();ai(C.byref(allocator),None);m=pointer();assert create(C.byref(allocator),C.byref(m))==0
        calls=0;instructions=0;loops=0;value=C.c_uint32();event=C.c_uint();fault=Fault()
        try:
            ram=fabric(m);code=binary.read_bytes();assert load(ram,0,0x40080000,C.c_char_p(code),len(code))==0
            bus=bus_get(m,0);assert bus.write(bus.ctx,0x3ff00030,4,1)==0
            for cpu in range(2):
                for control in range(2):
                    for count in [1,2,8,32,128]:
                        assert configure(m,cpu,symbols['inspect'])==0
                        w=core(m,cpu);assert enable(w)==0;w.contents.ps|=1<<16
                        w.contents.cpu.a[1]=0x3ffd0000+cpu*4096;w.contents.cpu.a[4]=0x7ffffff0
                        w.contents.cpu.a[6]=count;w.contents.cpu.a[7]=control
                        assert run(m,cpu,1)==0
                        for turn in range(2000):
                            if w.contents.cpu.pc==0x7ffffff0:break
                            offset=w.contents.cpu.pc-0x40080000
                            word=int.from_bytes(code[offset:offset+3],'little')
                            loops+=(word&0xf0ff) in (0x8076,0x9076,0xa076)
                            assert step(m,cpu,C.byref(event),C.byref(fault))==0 and event.value==0,(hex(fault.pc),hex(fault.opcode),event.value)
                            instructions+=1
                        else:raise AssertionError('C acceptance exceeded step budget')
                        assert w.contents.cpu.a[6]==sum(i^0x55 for i in range(count)) and w.contents.memctl==1
                        calls+=1
        finally:destroy(m)
        assert allocator.live_bytes==0
        assert loops==20,loops
        print(f'{comparisons} independent GNU MEMCTL register comparisons; {calls} actual ESP32 C calls, {instructions} retired instructions, {loops} compiled hardware loops, native instruction bus and MEMCTL restore')


if __name__=='__main__':main()
