#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""GPIO/hold oracle and nine unchanged ESP-IDF helpers on the native bus.

Explicit software routes, not IO_MUX reset/package/electrical/SoC sleep proof.
High-bank W1 aliases are WO in the TRM: SDK routines reading them are unqualified.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import re
import subprocess
import tempfile
from run_execute import ROOT,Memory,Fault
from run_windows_c import Windows
from run_machine_c import Allocator
from run_matrix import read_header


class Pad(C.Structure):
    _fields_=[('drive',C.c_int)]+[(n,C.c_uint) for n in ['ie','pu','pd']]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ['gpio-reg','gpio-ll','gpio-struct','rtc-reg','misc','caps','sig-map']:ap.add_argument('--'+n,type=Path,required=True)
    for n in ['compiler','objcopy','linker','nm']:ap.add_argument('--'+n,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0xaaaa5000)
    reg=read_header(args.gpio_reg,'87609e6eb2349cfaa569344e3ee0fb6c38b9a1dbb23b81af32e701c53af1cf84')
    ll=read_header(args.gpio_ll,'e1c839b9fe34ce525af1c51935a8b0409aade38452516daadf2f3f62062a2907')
    struct=read_header(args.gpio_struct,'d7a6a542b8a0aa6e9530ca3a9201aefd4f12b195169cc8613a191bda2ed6cfc8')
    rtc=read_header(args.rtc_reg,'c2d17880e08b28288fba95c3daa48f9a8672722fd56a5616728ee025dc61e0fb')
    misc=read_header(args.misc,'552e2d4ce5be7d93d228eb3b48bd959d0485290a7476ce5a7de438fe7ab9e152')
    caps=read_header(args.caps,'8f353538e6b94a0dac7097b41247f65c5429c3f4c7a0be3481a5e9c9fa71668d')
    sig=read_header(args.sig_map,'40d374f73e03922ac13f4a9d7b6fa780f54e5afde7178ac5369bef4a41cc7c49')
    mask_line=re.search(r'^#define SOC_GPIO_VALID_GPIO_MASK\s+(.+)',caps,re.M)[1]
    pins=[n for n in range(40) if n not in list(map(int,re.findall(r'BIT(\d+)',mask_line)))]
    out_line=re.search(r'^#define SOC_GPIO_VALID_OUTPUT_GPIO_MASK\s+(.+)',caps,re.M)[1]
    outputs=[n for n in pins if n not in list(map(int,re.findall(r'BIT(\d+)',out_line)))]
    masks=[sum(1<<(n%32) for n in pins if n//32==b) for b in range(2)]
    def address(name):return 0x3ff44000+int(re.search(r'#define\s+GPIO_'+name+r'_REG\s+\(DR_REG_GPIO_BASE\s*\+\s*(0x[0-9a-f]+)\)',reg)[1],16)
    helpers=[]
    for name in ['output_enable','output_disable','od_enable','od_disable','set_level','get_level','deep_sleep_hold_en','deep_sleep_hold_dis','deep_sleep_hold_is_en']:
        match=re.search(r'^static inline [^\n]*\bgpio_ll_'+name+r'\([^\n]*\)\n\{',ll,re.M);assert match,name
        end=match.end();depth=1
        while depth:
            if ll[end]=='{':depth+=1
            elif ll[end]=='}':depth-=1
            end+=1
        helpers.append(ll[match.start():end])
    with tempfile.TemporaryDirectory(prefix='embsim-gpio-c-') as tmp:
        out=Path(tmp);lib=out/'gpio.so'
        sources=[ROOT/'core/mcu/xtensa'/n for n in ['integer.c','execute.c','windows.c','interrupts.c']]
        sources += [ROOT/'core/mcu/esp32'/n for n in ['fabric.c','efuse.c','machine.c','uart.c','gpio.c','matrix.c','flashmap.c','flashspi.c','w25q32.c','nor32.c','gt25q32.c']]
        sources += [ROOT/'core/util/alloc.c']
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC','-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),*map(str,sources),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));pointer=C.c_void_p
        def bind(name,result,*params):f=getattr(api,name);f.restype=result;f.argtypes=list(params);return f
        ai=bind('esk_alloc_init',None,C.POINTER(Allocator),pointer);create=bind('esk_esp32_machine_create',C.c_int,C.POINTER(Allocator),C.POINTER(pointer));destroy=bind('esk_esp32_machine_destroy',None,pointer)
        config=bind('esk_esp32_machine_gpio_configure',C.c_int,pointer,C.POINTER(C.c_uint32),C.c_size_t)
        input_level=bind('esk_esp32_machine_gpio_input',C.c_int,pointer,C.c_uint,C.c_int,C.c_int)
        output=bind('esk_esp32_machine_gpio_output',C.c_int,pointer,C.c_uint,C.POINTER(Pad));hold=bind('esk_esp32_machine_gpio_auto_hold_event',C.c_int,pointer)
        bus_get=bind('esk_esp32_machine_bus',Memory,pointer,C.c_uint);fabric=bind('esk_esp32_machine_fabric',pointer,pointer)
        load=bind('esk_esp32_fabric_load',C.c_int,pointer,C.c_uint,C.c_uint32,pointer,C.c_size_t)
        configure=bind('esk_esp32_machine_configure',C.c_int,pointer,C.c_uint,C.c_uint32);core=bind('esk_esp32_machine_core',C.POINTER(Windows),pointer,C.c_uint)
        run=bind('esk_esp32_machine_run_core',C.c_int,pointer,C.c_uint,C.c_int);step=bind('esk_esp32_machine_step',C.c_int,pointer,C.c_uint,C.POINTER(C.c_uint),C.POINTER(Fault))
        allocator=Allocator();ai(C.byref(allocator),None);m=pointer();assert create(C.byref(allocator),C.byref(m))==0
        comparisons=0;calls=0;instructions=0;value=C.c_uint32();pad=Pad();event=C.c_uint();fault=Fault()
        try:
            bus=bus_get(m,0);settings=(C.c_uint32*14)(masks[0],masks[1]);assert config(m,settings,14)==0
            expected=[[0,0],[0,0]]
            for trial in range(1024):
                bank=rng.randrange(2);kind=rng.randrange(2);operation=rng.randrange(3);data=rng.getrandbits(32)
                name=('OUT' if kind==0 else 'ENABLE')+('1' if bank else '')
                suffix=['','_W1TS','_W1TC'][operation]
                assert bus.write(bus.ctx,address(name+suffix),4,data)==0
                data&=255 if bank else 0xffffffff
                if operation==0:expected[kind][bank]=data
                elif operation==1:expected[kind][bank]|=data
                else:expected[kind][bank]&=~data
                assert bus.read(bus.ctx,address(name),4,C.byref(value))==0 and value.value==expected[kind][bank];comparisons+=1
                pin=rng.choice(pins);b=pin//32;bit=1<<(pin%32)
                assert output(m,pin,C.byref(pad))==0
                wanted=(1 if expected[0][b]&bit else 0) if pin in outputs and expected[1][b]&bit else -1
                assert pad.drive==wanted;comparisons+=1
            (out/'gpio_struct.h').write_text(struct,encoding='utf-8');(out/'misc.h').write_text(misc,encoding='utf-8')
            (out/'rtc_cntl_reg.h').write_text(rtc,encoding='utf-8');(out/'gpio_reg.h').write_text(reg,encoding='utf-8');(out/'soc.h').write_text('#include <stdint.h>\n#define DR_REG_GPIO_BASE 0x3ff44000\n#define DR_REG_RTCCNTL_BASE 0x3ff48000\n#define BIT(n) (1U<<(n))\n',encoding='utf-8')
            source=ll[:ll.index('#pragma once')]+'''\n#include <stdint.h>
#include <stdbool.h>
#include "gpio_struct.h"
#include "misc.h"
#include "rtc_cntl_reg.h"
#include "gpio_reg.h"
typedef int gpio_num_t;
#define SIG_GPIO_OUT_IDX '''+str(int(re.search(r'#define\s+SIG_GPIO_OUT_IDX\s+(\d+)',sig)[1]))+'''
#define REG_READ(r) (*(volatile uint32_t *)(r))
#define REG_WRITE(r,v) (*(volatile uint32_t *)(r)=(v))
#define CLEAR_PERI_REG_MASK(r,m) REG_WRITE(r,REG_READ(r)&~(m))
#define SET_PERI_REG_MASK(r,m) REG_WRITE(r,REG_READ(r)|(m))
#define GET_PERI_REG_MASK(r,m) (REG_READ(r)&(m))
'''+ '\n'.join(helpers)+'''
unsigned work(unsigned pin,unsigned flags) {
    gpio_dev_t *hw=(gpio_dev_t *)0x3ff44000;
    volatile unsigned *phase=(volatile unsigned *)0x3ffb9000;
    gpio_ll_output_enable(hw,pin);
    if(flags&1) gpio_ll_od_enable(hw,pin);else gpio_ll_od_disable(hw,pin);
    gpio_ll_set_level(hw,pin,(flags>>1)&1);
    gpio_ll_deep_sleep_hold_en(hw);
    *phase=1;while(*phase!=2) { }
    unsigned input=gpio_ll_get_level(hw,34);
    gpio_ll_set_level(hw,pin,!((flags>>1)&1));
    gpio_ll_output_disable(hw,pin);
    *phase=3;while(*phase!=4) { }
    unsigned held=gpio_ll_deep_sleep_hold_is_en(hw);
    gpio_ll_deep_sleep_hold_dis(hw);
    SET_PERI_REG_MASK(RTC_CNTL_DIG_ISO_REG,RTC_CNTL_CLR_DG_PAD_AUTOHOLD);
    return input|(held<<1);
}
'''
            src=out/'work.c';obj=out/'work.o';elf=out/'work.elf';binary=out/'work.bin';src.write_text(source,encoding='utf-8')
            subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/','-O1','-ffreestanding','-fno-builtin','-mtext-section-literals','-c',str(src),'-o',str(obj)],check=True)
            subprocess.run([args.linker,'--no-relax','-Ttext=0x40080000','--entry=work',str(obj),'-o',str(elf)],check=True)
            subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
            symbols={f[2]:int(f[0],16) for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines() if len(f:=line.split())==3}
            code=binary.read_bytes();assert load(fabric(m),0,0x40080000,C.c_char_p(code),len(code))==0
            assert bus.write(bus.ctx,0x3ff00030,4,1)==0
            # Low-bank outputs avoid SDK high-bank writes which first read WO aliases.
            for cpu in range(2):
                for pin in [n for n in outputs if n<32]:
                    for flags in range(4):
                        bit=1<<pin;settings=(C.c_uint32*14)();settings[0]=bit;settings[1]=1<<2;settings[3]=1<<2
                        assert config(m,settings,14)==0;initial=(cpu+pin+flags)&1
                        assert input_level(m,34,1,initial)==0 and bus.write(bus.ctx,0x3ffb9000,4,0)==0
                        assert configure(m,cpu,symbols['work'])==0;w=core(m,cpu).contents;w.ps|=1<<16
                        w.cpu.a[1]=0x3ffd0000+cpu*4096;w.cpu.a[4]=0x7ffffff0;w.cpu.a[6]=pin;w.cpu.a[7]=flags
                        assert run(m,cpu,1)==0;phase=0
                        wanted=-1 if (flags&1) and (flags&2) else int(bool(flags&2))
                        for turn in range(1000):
                            if w.cpu.pc==0x7ffffff0:break
                            assert step(m,cpu,C.byref(event),C.byref(fault))==0 and event.value==0,(pin,flags,hex(fault.pc),hex(fault.opcode),event.value)
                            instructions+=1
                            assert bus.read(bus.ctx,0x3ffb9000,4,C.byref(value))==0
                            if value.value==1 and phase==0:
                                assert output(m,pin,C.byref(pad))==0 and pad.drive==wanted
                                assert hold(m)==0 and input_level(m,34,1,1-initial)==0
                                assert bus.write(bus.ctx,0x3ffb9000,4,2)==0;phase=1
                            elif value.value==3 and phase==1:
                                assert output(m,pin,C.byref(pad))==0 and pad.drive==wanted
                                assert bus.read(bus.ctx,0x3ff48088,4,C.byref(value))==0 and value.value&(1<<9)
                                assert bus.write(bus.ctx,0x3ffb9000,4,4)==0;phase=2
                        else:raise AssertionError('GPIO firmware budget exceeded')
                        assert phase==2 and w.cpu.a[6]==(1-initial)|2
                        assert output(m,pin,C.byref(pad))==0 and pad.drive==-1
                        calls+=1
        finally:destroy(m)
        assert allocator.live_bytes==0
        print(f'{comparisons} independent GPIO latch/pad comparisons; {calls} actual ESP32 C interactive calls, {instructions} instructions, nine unchanged SDK helpers; live input and held-output checks')


if __name__=='__main__':main()
