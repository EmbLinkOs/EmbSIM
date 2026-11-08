#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Pinned ESP32 SPI1 register defaults/fields and independent READ-byte oracle.

No timing, SPI pins, flash programming or complete SDK/board qualification.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import re
import subprocess
import tempfile
from run_execute import ROOT
from run_matrix import read_header


class SPI(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ['addr','ctrl','user','user1','user2','mosi','miso','slave']]+[('buffer',C.c_uint32*16)]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--spi-reg',type=Path,required=True);ap.add_argument('--soc-header',type=Path,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0x5b11)
    header=read_header(args.spi_reg,'8fdea138d074362521d9e052894ce3c775326e95006c51ff8a84c2821a715a70')
    soc=read_header(args.soc_header,'7a7148c288d7c289c961810e5617fa7b14d105b28349a45f772150b1e0845dd3')
    base=int(re.search(r'#define\s+DR_REG_SPI1_BASE\s+(\S+)',soc)[1],0)
    assert int(re.search(r'#define\s+ETS_SPI1_INTR_SOURCE\s+(\d+)',soc)[1])==29
    def reg(name):
        return base+int(re.search(r'#define\s+SPI_'+name+r'_REG\(i\)\s+\(REG_SPI_BASE\(i\)\s*\+\s*(0x[0-9a-fA-F]+)\)',header)[1],16)
    def initial(name):
        start=re.search(r'^#define\s+SPI_'+name+r'_REG\(i\)[^\n]*',header,re.M).end()
        following=re.search(r'^#define\s+SPI_\w+_REG\(i\)',header[start:],re.M)
        block=header[start:start+following.start()] if following else header[start:]
        value=0
        for high,low,radix,digits in re.findall(r";bitpos:\[(\d+)(?::(\d+))?\]\s*;default:\s*\d+'([bhd])([0-9a-fA-F_]+)",block):
            value|=int(digits.replace('_',''),{'b':2,'h':16,'d':10}[radix])<<int(low or high)
        return value
    def field(name):
        shift=int(re.search(r'#define\s+SPI_'+name+r'_S\s+(\d+)',header)[1])
        mask=int(re.search(r'#define\s+SPI_'+name+r'_V\s+(0x[0-9a-fA-F]+|\d+)\b',header)[1],0)
        return lambda value:(value&mask)<<shift
    with tempfile.TemporaryDirectory(prefix='embsim-flashspi-ref-') as tmp:
        lib=Path(tmp)/'spi.so'
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),str(ROOT/'core/mcu/esp32/flashspi.c'),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));p=C.POINTER(SPI);init=api.esk_esp32_flashspi_init;init.argtypes=[p];init.restype=None
        read=api.esk_esp32_flashspi_read;read.argtypes=[p,C.c_uint32,C.c_uint,C.POINTER(C.c_uint32)];read.restype=C.c_int
        write=api.esk_esp32_flashspi_write
        write.argtypes=[p,C.c_void_p,C.c_size_t,C.c_int,C.c_uint32,C.c_uint,C.c_uint32];write.restype=C.c_int
        irq=api.esk_esp32_flashspi_irq;irq.argtypes=[p];irq.restype=C.c_int
        s=SPI();init(C.byref(s));value=C.c_uint32();defaults=0
        names=['CMD','ADDR','CTRL','CLOCK','USER','USER1','USER2','MOSI_DLEN','MISO_DLEN','PIN','SLAVE']+[f'W{i}' for i in range(16)]
        for name in names:
            assert read(C.byref(s),reg(name),4,C.byref(value))==0
            assert value.value==initial(name),(name,hex(value.value),hex(initial(name)))
            defaults+=1
        image=bytes(rng.getrandbits(8) for _ in range(131072));checks=0
        def put(name,value):
            assert write(C.byref(s),C.c_char_p(image),len(image),1,reg(name),4,value)==0,name
        for high in range(2):
            for length in range(1,65-high*32):
                for address in [0,65535,65536-length,len(image)-length]:
                    init(C.byref(s));sentinels=[rng.getrandbits(32) for _ in range(16)]
                    for i,word in enumerate(sentinels):put(f'W{i}',word)
                    put('CTRL',initial('CTRL')&~field('FASTRD_MODE')(1))
                    user=initial('USER')|field('USR_COMMAND')(1)|field('USR_ADDR')(1)|field('USR_MISO')(1)|field('USR_MISO_HIGHPART')(high)
                    put('USER',user);put('USER2',field('USR_COMMAND_BITLEN')(7)|field('USR_COMMAND_VALUE')(3))
                    put('ADDR',(address<<8)|rng.getrandbits(8));put('MISO_DLEN',field('USR_MISO_DBITLEN')(length*8-1))
                    put('CMD',field('USR')(1))
                    expected=bytearray(b''.join(word.to_bytes(4,'little') for word in sentinels))
                    expected[high*32:high*32+length]=image[address:address+length]
                    assert b''.join(word.to_bytes(4,'little') for word in s.buffer)==expected
                    assert irq(C.byref(s))==1
                    put('SLAVE',field('INT_EN')(16));assert irq(C.byref(s))==0
                    checks+=1
        print(f'{defaults} parsed vendor register defaults and {checks} SPI1 field/address/receive/IRQ comparisons pass')


if __name__=='__main__':main()
