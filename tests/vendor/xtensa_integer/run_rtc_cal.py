#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""RTC calibration period referee and firmware using unchanged SDK access macros.

Clock periods are explicit inputs. No oscillator frequency, SDK clock-tree
implementation, delay/timeout duration or physical timing is substituted.
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


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ['timer-reg','soc-header','rtc-time']:ap.add_argument('--'+n,type=Path,required=True)
    for n in ['compiler','objcopy','linker','nm']:ap.add_argument('--'+n,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0x3ff5f06c)
    reg=read_header(args.timer_reg,'9aac43c44e42188bc7381e0936166383575c554157d2b087bf46d89b03d35e6c')
    soc=read_header(args.soc_header,'7a7148c288d7c289c961810e5617fa7b14d105b28349a45f772150b1e0845dd3')
    rtc_time=read_header(args.rtc_time,'7542476747cd6175a7f846d3ac1af21914717f1278e3d9b0cdc5ab04c4f21883')
    assert 'In cycling mode' in rtc_time and 'In one-off mode' in rtc_time
    def field(name):return int(re.search(r'#define TIMG_RTC_CALI_'+name+r'_S\s+(\d+)',reg)[1])
    def address(name):return 0x3ff5f000+int(re.search(r'#define TIMG_'+name+r'_REG\(i\)\s+\(REG_TIMG_BASE\(i\)\s*\+\s*(0x[0-9a-fA-F]+)',reg)[1],16)
    cfg=address('RTCCALICFG');result=address('RTCCALICFG1');ready=1<<field('RDY');start=1<<field('START');cycle=1<<field('START_CYCLING')
    macro_names=['REG_WRITE','REG_READ','REG_GET_FIELD','REG_SET_FIELD','ETS_UNCACHED_ADDR','READ_PERI_REG','WRITE_PERI_REG','CLEAR_PERI_REG_MASK','SET_PERI_REG_MASK','GET_PERI_REG_MASK']
    def macro(name):
        lines=soc.splitlines();n=next(i for i,line in enumerate(lines) if re.match(r'#define '+name+r'\(',line));end=n+1
        while lines[end-1].rstrip().endswith('\\'):end+=1
        return '\n'.join(lines[n:end])
    macros='\n'.join(macro(n) for n in macro_names)
    assertion=re.search(r'^#define ASSERT_IF_DPORT_REG\(_r, OP\)\s*$',soc,re.M)[0]
    base_line=re.search(r'^#define\s+DR_REG_TIMERGROUP0_BASE\s+\S+',soc,re.M)[0]
    with tempfile.TemporaryDirectory(prefix='embsim-rtc-cal-') as tmp:
        out=Path(tmp);lib=out/'cal.so';sources=[ROOT/'core/mcu/xtensa'/n for n in ['integer.c','execute.c','windows.c','interrupts.c']]
        sources += [ROOT/'core/mcu/esp32'/n for n in ['fabric.c','efuse.c','machine.c','rtc_cal.c','uart.c','gpio.c','matrix.c','flashmap.c','flashspi.c','w25q32.c','nor32.c','gt25q32.c']]
        sources += [ROOT/'core/util/alloc.c']
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC','-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),*map(str,sources),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));pointer=C.c_void_p
        def bind(name,result_type,*params):f=getattr(api,name);f.restype=result_type;f.argtypes=list(params);return f
        ai=bind('esk_alloc_init',None,C.POINTER(Allocator),pointer);create=bind('esk_esp32_machine_create',C.c_int,C.POINTER(Allocator),C.POINTER(pointer));destroy=bind('esk_esp32_machine_destroy',None,pointer)
        attach=bind('esk_esp32_machine_rtc_cal_configure',C.c_int,pointer,C.c_int,C.c_uint32,C.c_uint32)
        period=bind('esk_esp32_machine_rtc_cal_period',C.c_int,pointer,C.c_uint,C.c_uint32)
        bus_get=bind('esk_esp32_machine_bus',Memory,pointer,C.c_uint);fabric=bind('esk_esp32_machine_fabric',pointer,pointer)
        load=bind('esk_esp32_fabric_load',C.c_int,pointer,C.c_uint,C.c_uint32,pointer,C.c_size_t)
        configure=bind('esk_esp32_machine_configure',C.c_int,pointer,C.c_uint,C.c_uint32);core=bind('esk_esp32_machine_core',C.POINTER(Windows),pointer,C.c_uint)
        run=bind('esk_esp32_machine_run_core',C.c_int,pointer,C.c_uint,C.c_int);step=bind('esk_esp32_machine_step',C.c_int,pointer,C.c_uint,C.POINTER(C.c_uint),C.POINTER(Fault))
        allocator=Allocator();ai(C.byref(allocator),None);m=pointer();assert create(C.byref(allocator),C.byref(m))==0
        comparisons=0;calls=0;instructions=0;value=C.c_uint32();event=C.c_uint();fault=Fault()
        try:
            bus=bus_get(m,0)
            def read(addr):assert bus.read(bus.ctx,addr,4,C.byref(value))==0;return value.value
            for trial in range(1024):
                source=rng.randrange(3);cycling=bool(trial&1);n=1 if cycling else rng.randrange(1,33);previous=rng.getrandbits(25)
                control=(n<<field('MAX'))|(source<<field('CLK_SEL'))|(cycle if cycling else start)
                assert attach(m,1,control,previous)==0
                for window in range(2 if cycling else 1):
                    ticks=[rng.randrange(1,4096) for _ in range(n)]
                    for i,tick in enumerate(ticks):
                        assert period(m,(source+1)%3,50000)==0
                        assert period(m,source,tick)==0
                        assert read(result)==((sum(ticks) if i==n-1 else previous)<<field('VALUE'));comparisons+=1
                        assert bool(read(cfg)&ready)==(not cycling and i==n-1);comparisons+=1
                    previous=sum(ticks)
            (out/'timer_group_reg.h').write_text(reg,encoding='utf-8')
            notices=soc[:soc.index('#ifndef')] if '#ifndef' in soc else ''
            (out/'soc.h').write_text(notices+'\n#include <stdint.h>\n#define BIT(n) (1U<<(n))\n'+base_line+'\n'+assertion+'\n'+macros+'\n',encoding='utf-8')
            source='''/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "timer_group_reg.h"
static void prepare(unsigned n,unsigned source) {
    CLEAR_PERI_REG_MASK(TIMG_RTCCALICFG_REG(0),TIMG_RTC_CALI_START);
    REG_SET_FIELD(TIMG_RTCCALICFG_REG(0),TIMG_RTC_CALI_CLK_SEL,source);
    CLEAR_PERI_REG_MASK(TIMG_RTCCALICFG_REG(0),TIMG_RTC_CALI_START_CYCLING);
    REG_SET_FIELD(TIMG_RTCCALICFG_REG(0),TIMG_RTC_CALI_MAX,n);
}
unsigned work(unsigned n,unsigned source,unsigned mode) {
    volatile unsigned *phase=(volatile unsigned *)0x3ffb9000;
    prepare(n,source);
    SET_PERI_REG_MASK(TIMG_RTCCALICFG_REG(0),TIMG_RTC_CALI_START);
    if(mode) {
        for(unsigned poll=0;poll<32;poll++) if(GET_PERI_REG_MASK(TIMG_RTCCALICFG_REG(0),TIMG_RTC_CALI_RDY)) return 0xbad00001;
        return 0xffff0000;
    }
    *phase=1;while(*phase!=2) { }
    if(GET_PERI_REG_MASK(TIMG_RTCCALICFG_REG(0),TIMG_RTC_CALI_RDY)) return 0xbad00002;
    *phase=3;while(*phase!=4) { }
    if(!GET_PERI_REG_MASK(TIMG_RTCCALICFG_REG(0),TIMG_RTC_CALI_RDY)) return 0xbad00003;
    return REG_GET_FIELD(TIMG_RTCCALICFG1_REG(0),TIMG_RTC_CALI_VALUE);
}
unsigned cycling(unsigned n,unsigned source) {
    volatile unsigned *phase=(volatile unsigned *)0x3ffb9000;
    prepare(n,source);
    SET_PERI_REG_MASK(TIMG_RTCCALICFG_REG(0),TIMG_RTC_CALI_START_CYCLING);
    *phase=1;while(*phase!=2) { }
    if(GET_PERI_REG_MASK(TIMG_RTCCALICFG_REG(0),TIMG_RTC_CALI_RDY)) return 0xbad00004;
    unsigned first=REG_GET_FIELD(TIMG_RTCCALICFG1_REG(0),TIMG_RTC_CALI_VALUE);
    *phase=3;while(*phase!=4) { }
    if(GET_PERI_REG_MASK(TIMG_RTCCALICFG_REG(0),TIMG_RTC_CALI_RDY)) return 0xbad00005;
    return first^REG_GET_FIELD(TIMG_RTCCALICFG1_REG(0),TIMG_RTC_CALI_VALUE);
}
'''
            src=out/'work.c';obj=out/'work.o';elf=out/'work.elf';binary=out/'work.bin';src.write_text(source,encoding='utf-8')
            subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/','-O1','-ffreestanding','-fno-builtin','-mtext-section-literals','-c',str(src),'-o',str(obj)],check=True)
            subprocess.run([args.linker,'--no-relax','-Ttext=0x40080000','--entry=work',str(obj),'-o',str(elf)],check=True)
            subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
            symbols={f[2]:int(f[0],16) for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines() if len(f:=line.split())==3}
            code=binary.read_bytes();assert load(fabric(m),0,0x40080000,C.c_char_p(code),len(code))==0
            assert bus.write(bus.ctx,0x3ff00030,4,1)==0
            for cpu in range(2):
                for trial in range(243):
                    clock=trial%3;mode=1 if trial>=240 else 0;continuous=192<=trial<240;n=1 if continuous else 2+trial%31
                    assert attach(m,1,0x13000,0x1234)==0 and bus.write(bus.ctx,0x3ffb9000,4,0)==0
                    assert configure(m,cpu,symbols['cycling' if continuous else 'work'])==0;w=core(m,cpu).contents;w.ps|=1<<16
                    w.cpu.a[1]=0x3ffd0000+cpu*4096;w.cpu.a[4]=0x7ffffff0;w.cpu.a[6]=n;w.cpu.a[7]=clock;w.cpu.a[8]=mode
                    assert run(m,cpu,1)==0;stage=0;ticks=[rng.randrange(1,8192) for _ in range(n)];other=[rng.randrange(1,8192) for _ in range(n)]
                    for turn in range(3000):
                        if w.cpu.pc==0x7ffffff0:break
                        assert step(m,cpu,C.byref(event),C.byref(fault))==0 and event.value==0,(hex(fault.pc),hex(fault.opcode),hex(fault.address),event.value)
                        instructions+=1
                        phase=read(0x3ffb9000)
                        if phase==1 and stage==0:
                            for tick in ticks if continuous else ticks[:-1]:assert period(m,clock,tick)==0
                            assert read(result)==((sum(ticks) if continuous else 0x1234)<<field('VALUE'))
                            assert bus.write(bus.ctx,0x3ffb9000,4,2)==0;stage=1
                        elif phase==3 and stage==1:
                            for tick in other if continuous else ticks[-1:]:assert period(m,clock,tick)==0
                            assert bus.write(bus.ctx,0x3ffb9000,4,4)==0;stage=2
                    else:raise AssertionError('calibration firmware budget exceeded')
                    expected=0xffff0000 if mode else (sum(ticks)^sum(other)) if continuous else sum(ticks)
                    assert w.cpu.a[6]==expected,(cpu,trial,hex(w.cpu.a[6]),expected)
                    if mode:assert not read(cfg)&ready and read(result)==0x1234<<field('VALUE')
                    else:assert stage==2
                    calls+=1
        finally:destroy(m)
        assert allocator.live_bytes==0
        print(f'{comparisons} independent calibration period/result/ready comparisons; {calls} actual ESP32 C interactive calls, {instructions} instructions; ten unchanged SDK access macros, three sources, one-shot/cycling/stalled-clock paths on both native cores')


if __name__=='__main__':main()
