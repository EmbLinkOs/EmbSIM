#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""GNU-assembled ATOMCTL register oracle and real ESP32 C local-SRAM RCW.

ISA RI-2021.8 table 190 / section 4.3.13 supplies six bits and reset 0x28.
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
    with tempfile.TemporaryDirectory(prefix='embsim-atomctl-') as tmp:
        out=Path(tmp);src=out/'ops.S';obj=out/'ops.o';binary=out/'ops.bin'
        src.write_text('.text\n.begin no-transform\n'+'\n'.join(f'{form} a{reg},ATOMCTL' for form,reg in specs)+'\n.end no-transform\n',encoding='utf-8')
        subprocess.run([args.assembler,str(src),'-o',str(obj)],check=True)
        subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(obj),str(binary)],check=True)
        ops=binary.read_bytes();assert len(ops)==len(specs)*3
        sources=[ROOT/'core/mcu/xtensa'/n for n in ['integer.c','execute.c','windows.c','interrupts.c']]
        sources += [ROOT/'core/mcu/esp32'/n for n in ['fabric.c','efuse.c','machine.c','gpio.c','matrix.c','flashmap.c','flashspi.c','w25q32.c','nor32.c','gt25q32.c']]
        sources += [ROOT/'core/util/alloc.c'];lib=out/'atomctl.so'
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC','-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),*map(str,sources),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));pointer=C.c_void_p;wp=C.POINTER(Windows)
        def bind(name,result,*params):f=getattr(api,name);f.restype=result;f.argtypes=list(params);return f
        wi=bind('esk_xtensa_windows_init',None,wp,C.c_uint32,C.POINTER(C.c_uint32))
        enable=bind('esk_xtensa_atomctl_configure',C.c_int,wp)
        execute=bind('esk_xtensa_window_execute',C.c_int,wp,pointer,pointer,C.c_size_t,C.POINTER(C.c_size_t),C.POINTER(C.c_uint),C.POINTER(Fault))
        comparisons=0
        for index,(form,reg) in enumerate(specs):
            for value in range(64):
                w=Windows();wi(C.byref(w),0x40080000,None);assert enable(C.byref(w))==0 and w.atomctl==0x28
                old=63-value;w.atomctl=old;w.cpu.a[reg]=value;used=C.c_size_t();event=C.c_uint();fault=Fault()
                code=C.create_string_buffer(ops[index*3:index*3+3])
                assert execute(C.byref(w),None,code,3,C.byref(used),C.byref(event),C.byref(fault))==0
                assert used.value==3 and event.value==0 and w.cpu.pc==0x40080003
                assert w.atomctl==(value if form!='rsr' else old)
                assert w.cpu.a[reg]==(value if form=='wsr' else old)
                comparisons+=1
        source=out/'inspect.c';obj=out/'inspect.o';elf=out/'inspect.elf';binary=out/'inspect.bin'
        source.write_text('''unsigned inspect(volatile unsigned *out,unsigned control) {
    unsigned old,readback,observed=out[1]+1,expected=out[1];
    __asm__ volatile("rsr.atomctl %0" : "=a"(old));
    __asm__ volatile("wsr.atomctl %0; rsync" :: "a"(control) : "memory");
    __asm__ volatile("rsr.atomctl %0" : "=a"(readback));
    __asm__ volatile("wsr.scompare1 %1; s32c1i %0,%2,0" : "+a"(observed) : "a"(expected),"a"(out+1) : "memory");
    out[0]=readback;out[2]=observed;
    __asm__ volatile("wsr.atomctl %0; rsync" :: "a"(old) : "memory");
    return out[1];
}
''',encoding='utf-8')
        subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/','-O1','-ffreestanding','-fno-builtin','-mtext-section-literals','-c',str(source),'-o',str(obj)],check=True)
        subprocess.run([args.linker,'--no-relax','-Ttext=0x40080000','--entry=inspect',str(obj),'-o',str(elf)],check=True)
        subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
        symbols={f[2]:int(f[0],16) for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines() if len(f:=line.split())==3}
        ai=bind('esk_alloc_init',None,C.POINTER(Allocator),pointer);create=bind('esk_esp32_machine_create',C.c_int,C.POINTER(Allocator),C.POINTER(pointer));destroy=bind('esk_esp32_machine_destroy',None,pointer)
        fabric=bind('esk_esp32_machine_fabric',pointer,pointer);load=bind('esk_esp32_fabric_load',C.c_int,pointer,C.c_uint,C.c_uint32,pointer,C.c_size_t)
        bus_get=bind('esk_esp32_machine_bus',Memory,pointer,C.c_uint);configure=bind('esk_esp32_machine_configure',C.c_int,pointer,C.c_uint,C.c_uint32)
        core=bind('esk_esp32_machine_core',wp,pointer,C.c_uint);run=bind('esk_esp32_machine_run_core',C.c_int,pointer,C.c_uint,C.c_int)
        step=bind('esk_esp32_machine_step',C.c_int,pointer,C.c_uint,C.POINTER(C.c_uint),C.POINTER(Fault))
        allocator=Allocator();ai(C.byref(allocator),None);m=pointer();assert create(C.byref(allocator),C.byref(m))==0
        calls=0;instructions=0;value=C.c_uint32();event=C.c_uint();fault=Fault()
        try:
            ram=fabric(m);code=binary.read_bytes();assert load(ram,0,0x40080000,C.c_char_p(code),len(code))==0
            bus=bus_get(m,0);assert bus.write(bus.ctx,0x3ff00030,4,1)==0
            for cpu in range(2):
                for control in range(64):
                    assert configure(m,cpu,symbols['inspect'])==0
                    w=core(m,cpu);assert enable(w)==0
                    w.contents.ps|=1<<16
                    w.contents.cpu.a[1]=0x3ffd0000+cpu*4096;w.contents.cpu.a[4]=0x7ffffff0
                    w.contents.cpu.a[6]=0x3ffb9000;w.contents.cpu.a[7]=control
                    initial=0xffffffff if control==0 else control*73+cpu
                    assert bus.write(bus.ctx,0x3ffb9004,4,initial)==0 and run(m,cpu,1)==0
                    for turn in range(200):
                        if w.contents.cpu.pc==0x7ffffff0:break
                        assert step(m,cpu,C.byref(event),C.byref(fault))==0 and event.value==0,(hex(fault.pc),hex(fault.opcode),event.value)
                        instructions+=1
                    else:raise AssertionError('C acceptance exceeded step budget')
                    for offset,wanted in [(0,control),(4,(initial+1)&0xffffffff),(8,initial)]:
                        assert bus.read(bus.ctx,0x3ffb9000+offset,4,C.byref(value))==0 and value.value==wanted
                    assert w.contents.atomctl==0x28 and w.contents.cpu.a[6]==(initial+1)&0xffffffff
                    calls+=1
        finally:destroy(m)
        assert allocator.live_bytes==0
        print(f'{comparisons} independent GNU ATOMCTL register comparisons; {calls} actual ESP32 C calls, {instructions} retired instructions, native local-SRAM atomic bus')


if __name__=='__main__':main()
