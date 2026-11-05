#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Pinned eFuse register/protection referee and actual ESP32 SDK helper reads.

Synthetic, explicit None-coded fuse images; not owner keys/MAC/calibration.
Selected unchanged helper bodies, not complete eFuse burn or SDK boot proof.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import re
import subprocess
import tempfile
from run_execute import ROOT, Memory, Fault
from run_windows_c import Windows
from run_machine_c import Allocator
from run_matrix import read_header


class EFuse(C.Structure):
    _fields_=[(name,C.c_uint32*31) for name in ['image','read','write']]+[(name,C.c_uint32) for name in ['enabled','clock','conf','raw','enable','dac','date','pending']]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['efuse-reg','efuse-ll','soc-header']:ap.add_argument('--'+name,type=Path,required=True)
    for name in ['compiler','objcopy','linker','nm']:ap.add_argument('--'+name,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0xef05e)
    reg=read_header(args.efuse_reg,'cfe37e8e692a22e41885f606bee7c4c12e0308abbdeb5f13f6cc92471453ff03')
    ll=read_header(args.efuse_ll,'ce3dfed567383582c6ced10a9edc08de296c5b5d541ee171efdd07b0014e3f30')
    soc=read_header(args.soc_header,'7a7148c288d7c289c961810e5617fa7b14d105b28349a45f772150b1e0845dd3')
    base=int(re.search(r'#define\s+DR_REG_EFUSE_BASE\s+(\S+)',soc)[1],0)
    def address(name):return base+int(re.search(r'#define\s+'+name+r'\s+\(DR_REG_EFUSE_BASE\s*\+\s*(0x[0-9a-f]+)\)',reg)[1],16)
    def field(name):
        shift=int(re.search(r'#define\s+EFUSE_'+name+r'_S\s+(\d+)',reg)[1])
        mask=int(re.search(r'#define\s+EFUSE_'+name+r'_V\s+(\S+)',reg)[1],0)
        return mask,shift
    def get(value,name):mask,shift=field(name);return (value>>shift)&mask
    reads=[address(f'EFUSE_BLK{block}_RDATA{word}_REG') for block in range(4) for word in range(7 if block==0 else 8)]
    writes=[address(f'EFUSE_BLK{block}_WDATA{word}_REG') for block in range(4) for word in range(7 if block==0 else 8)]
    helpers=[]
    for name in ['get_flash_crypt_cnt','get_mac0','get_mac1','get_coding_scheme','get_cmd','set_read_cmd','set_conf_read_op_code']:
        match=re.search(r'^__attribute__[^\n]*\befuse_ll_'+name+r'\([^\n]*\)\n\{',ll,re.M);assert match,name
        end=match.end();depth=1
        while depth:
            if ll[end]=='{':depth+=1
            elif ll[end]=='}':depth-=1
            end+=1
        helpers.append(ll[match.start():end])
    images=[];visible=[]
    for trial in range(64):
        words=[rng.getrandbits(32) for _ in range(31)];words[6]=(words[6]&~3)|(3 if trial&16 else 0)
        words[0]=(words[0]&~0xf0000)|((trial%16)<<16);expected=list(words)
        disabled=get(words[0],'RD_EFUSE_RD_DIS')
        for block in range(1,4):
            if disabled&(1<<(block-1)):expected[7+(block-1)*8:7+block*8]=[0]*8
        if disabled&8:
            for index,name in [(5,'RD_FLASH_CRYPT_CONFIG'),(6,'RD_KEY_STATUS'),(6,'RD_CODING_SCHEME')]:
                mask,shift=field(name);expected[index]&=~(mask<<shift)
        images.append(words);visible.append(expected)
    with tempfile.TemporaryDirectory(prefix='embsim-efuse-ref-') as tmp:
        out=Path(tmp);lib=out/'efuse.so'
        sources=[ROOT/'core/mcu/xtensa'/n for n in ['integer.c','execute.c','windows.c','interrupts.c']]
        sources += [ROOT/'core/mcu/esp32'/n for n in ['fabric.c','efuse.c','machine.c','rtc_cal.c','uart.c','gpio.c','matrix.c','flashmap.c','flashspi.c','w25q32.c','nor32.c','gt25q32.c']]
        sources += [ROOT/'core/util/alloc.c']
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC','-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),*map(str,sources),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));pointer=C.c_void_p
        def bind(name,result,*params):f=getattr(api,name);f.restype=result;f.argtypes=list(params);return f
        fi=bind('esk_esp32_efuse_init',None,C.POINTER(EFuse));fc=bind('esk_esp32_efuse_configure',C.c_int,C.POINTER(EFuse),C.POINTER(C.c_uint32),C.c_size_t)
        fr=bind('esk_esp32_efuse_read',C.c_int,C.POINTER(EFuse),C.c_uint32,C.c_uint,C.POINTER(C.c_uint32))
        fw=bind('esk_esp32_efuse_write',C.c_int,C.POINTER(EFuse),C.c_uint32,C.c_uint,C.c_uint32)
        complete=bind('esk_esp32_efuse_read_complete',C.c_int,C.POINTER(EFuse))
        finish=bind('esk_esp32_fabric_efuse_read_complete',C.c_int,pointer)
        comparisons=0;value=C.c_uint32()
        for words,expected in zip(images,visible):
            state=EFuse();fi(C.byref(state));raw=(C.c_uint32*31)(*words);assert fc(C.byref(state),raw,31)==0
            for i,(r,w) in enumerate(zip(reads,writes)):
                assert fr(C.byref(state),r,4,C.byref(value))==0 and value.value==expected[i]
                assert fw(C.byref(state),w,4,~words[i]&0xffffffff)==0
                assert fr(C.byref(state),w,4,C.byref(value))==0 and value.value==~words[i]&0xffffffff
                comparisons+=2
            assert fw(C.byref(state),address('EFUSE_CONF_REG'),4,0x5aa5)==0
            assert fw(C.byref(state),address('EFUSE_CMD_REG'),4,1)==0
            assert fr(C.byref(state),address('EFUSE_CMD_REG'),4,C.byref(value))==0 and value.value==1
            assert complete(C.byref(state))==0
            assert fr(C.byref(state),address('EFUSE_INT_RAW_REG'),4,C.byref(value))==0 and value.value==1
        # Unchanged register header and helper bodies; only minimal MMIO glue is
        # supplied here, rather than linking a vendor controller implementation.
        (out/'efuse_reg.h').write_text(reg,encoding='utf-8')
        (out/'soc.h').write_text('#include <stdint.h>\n#define DR_REG_EFUSE_BASE '+hex(base)+'''\n#define BIT(n) (1U<<(n))
#define REG_READ(r) (*(volatile uint32_t *)(r))
#define REG_WRITE(r,v) (*(volatile uint32_t *)(r)=(v))
#define REG_GET_FIELD(r,f) ((REG_READ(r)>>f##_S)&f##_V)
''',encoding='utf-8')
        source=ll[:ll.index('#pragma once')]+'\n#include <stdint.h>\n#include <stdbool.h>\n#include "efuse_reg.h"\n'+'\n'.join(helpers)+'''
unsigned inspect(unsigned *out) {
    efuse_ll_set_conf_read_op_code(); efuse_ll_set_read_cmd();
    while(efuse_ll_get_cmd()) { }
    out[0]=efuse_ll_get_flash_crypt_cnt();out[1]=efuse_ll_get_mac0();
    out[2]=efuse_ll_get_mac1();out[3]=efuse_ll_get_coding_scheme();
    out[4]=REG_READ(EFUSE_INT_RAW_REG);
    return out[0]^out[1]^out[2]^out[3]^out[4];
}
'''
        src=out/'inspect.c';obj=out/'inspect.o';elf=out/'inspect.elf';binary=out/'inspect.bin';src.write_text(source,encoding='utf-8')
        subprocess.run([args.compiler,'-B'+str(Path(args.compiler).parent)+'/', '-O1','-ffreestanding','-fno-builtin','-mtext-section-literals','-c',str(src),'-o',str(obj)],check=True)
        subprocess.run([args.linker,'--no-relax','-Ttext=0x40080000','--entry=inspect',str(obj),'-o',str(elf)],check=True)
        subprocess.run([args.objcopy,'-O','binary','--only-section=.text',str(elf),str(binary)],check=True)
        symbols={f[2]:int(f[0],16) for line in subprocess.check_output([args.nm,str(elf)],text=True).splitlines() if len(f:=line.split())==3}
        ai=bind('esk_alloc_init',None,C.POINTER(Allocator),pointer);create=bind('esk_esp32_machine_create',C.c_int,C.POINTER(Allocator),C.POINTER(pointer));destroy=bind('esk_esp32_machine_destroy',None,pointer)
        fabric=bind('esk_esp32_machine_fabric',pointer,pointer);config=bind('esk_esp32_fabric_efuse_configure',C.c_int,pointer,C.POINTER(C.c_uint32),C.c_size_t)
        load=bind('esk_esp32_fabric_load',C.c_int,pointer,C.c_uint,C.c_uint32,pointer,C.c_size_t);bus_get=bind('esk_esp32_machine_bus',Memory,pointer,C.c_uint)
        configure=bind('esk_esp32_machine_configure',C.c_int,pointer,C.c_uint,C.c_uint32);core=bind('esk_esp32_machine_core',C.POINTER(Windows),pointer,C.c_uint)
        run=bind('esk_esp32_machine_run_core',C.c_int,pointer,C.c_uint,C.c_int);step=bind('esk_esp32_machine_step',C.c_int,pointer,C.c_uint,C.POINTER(C.c_uint),C.POINTER(Fault))
        total=0;calls=0;code=binary.read_bytes()
        for words,expected in zip(images,visible):
            allocator=Allocator();ai(C.byref(allocator),None);m=pointer();assert create(C.byref(allocator),C.byref(m))==0
            try:
                ram=fabric(m);raw=(C.c_uint32*31)(*words);assert config(ram,raw,31)==0
                assert load(ram,0,0x40080000,C.c_char_p(code),len(code))==0
                bus=bus_get(m,0);assert bus.write(bus.ctx,0x3ff00030,4,1)==0
                states=[]
                for cpu in range(2):
                    assert configure(m,cpu,symbols['inspect'])==0
                    w=core(m,cpu).contents;w.ps|=1<<16;w.cpu.a[1]=0x3ffd0000+cpu*4096;w.cpu.a[4]=0x7ffffff0;w.cpu.a[6]=0x3ffb9000+cpu*64
                    states.append(w);assert run(m,cpu,1)==0
                done=[False,False];event=C.c_uint();fault=Fault()
                for turn in range(2000):
                    cpu=0 if not done[0] else 1;w=states[cpu]
                    if turn%17==0:assert finish(ram) in (0,-8)
                    if done[cpu]:continue
                    if w.cpu.pc==0x7ffffff0:
                        done[cpu]=True
                        if all(done):break
                        continue
                    status=step(m,cpu,C.byref(event),C.byref(fault));assert status==0 and event.value==0,(status,hex(fault.pc),hex(fault.opcode),event.value)
                    total+=1
                assert all(done)
                wanted=[get(expected[0],'RD_FLASH_CRYPT_CNT'),expected[1],get(expected[2],'RD_WIFI_MAC_CRC_HIGH')&65535,get(expected[6],'RD_CODING_SCHEME'),1]
                result=0
                for item in wanted:result^=item
                for cpu,w in enumerate(states):
                    for i,item in enumerate(wanted):assert bus.read(bus.ctx,0x3ffb9000+cpu*64+i*4,4,C.byref(value))==0 and value.value==item
                    assert w.cpu.a[6]==result and w.base==0 and w.start==1 and w.cpu.a[1]==0x3ffd0000+cpu*4096;calls+=1
            finally:destroy(m)
            assert allocator.live_bytes==0
        print(f'{comparisons} pinned eFuse register/protection comparisons; {calls} actual ESP32 C calls, {total} retired instructions, seven unchanged SDK helper bodies')


if __name__=='__main__':main()
