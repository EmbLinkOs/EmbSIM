#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""W25Q32JV protocol oracle and hash-pinned Rev.G maximum-duration facts.

PDF remains external, requires pdftotext. Does not identify the requested board's
chip or qualify power loss, protection, electrical timing or full command set.
"""
import argparse
import ctypes as C
import hashlib
from pathlib import Path
import random
import re
import subprocess
import tempfile
from run_execute import ROOT


class NOR(C.Structure):
    _fields_=[('remaining_us',C.c_uint64)]+[(name,C.c_uint32) for name in ['wel','operation','address','length']]+[('data',C.c_ubyte*256)]


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--datasheet',type=Path,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args();rng=random.Random(0x2532)
    assert hashlib.sha256(args.datasheet.read_bytes()).hexdigest()=='b2585095ac503e7cee7916ab3f025a51699b49e2cad7f6782e92b828c1cc09d8'
    text=subprocess.check_output(['pdftotext','-layout',str(args.datasheet),'-'],text=True)
    names={2:'tPP',0x20:'tSE',0x52:'tBE1',0xd8:'tBE2',0xc7:'tCE',0x60:'tCE'};times={}
    for cmd,symbol in names.items():
        match=re.search(r'\b'+symbol+r'\s+([\d.]+)\s+([\d,]+)\s+(ms|s)\b',text);assert match,symbol
        times[cmd]=int(match[2].replace(',',''))*(1000 if match[3]=='ms' else 1000000)
    identity=int(re.search(r'W25Q32JV-IQ/JQ\s+15h\s+([0-9A-F]+)h',text)[1],16)
    manufacturer=int(re.search(r'Winbond Serial Flash\s+([0-9A-F]+)h',text)[1],16)
    jedec=bytes([manufacturer,identity>>8,identity&255])
    for name,cmd in [('Page Program',2),(r'Sector Erase \(4KB\)',0x20),(r'Block Erase \(32KB\)',0x52),(r'Block Erase \(64KB\)',0xd8)]:
        assert int(re.search(name+r'\s+([0-9A-F]+)h\s+A23-A16',text)[1],16)==cmd
    with tempfile.TemporaryDirectory(prefix='embsim-w25q32-ref-') as tmp:
        lib=Path(tmp)/'nor.so'
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),str(ROOT/'core/mcu/esp32/w25q32.c'),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));p=C.POINTER(NOR)
        init=api.esk_esp32_w25q32_init;init.argtypes=[p];init.restype=None
        command=api.esk_esp32_w25q32_command
        command.argtypes=[p,C.c_void_p,C.c_size_t,C.c_uint,C.c_int,C.c_uint32,C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t];command.restype=C.c_int
        advance=api.esk_esp32_w25q32_advance;advance.argtypes=[p,C.c_void_p,C.c_size_t,C.c_uint64];advance.restype=C.c_int
        size=4*1024*1024;expected=bytearray(b'\xff'*size);actual=C.create_string_buffer(bytes(expected),size)
        nor=NOR();init(C.byref(nor));wel=False;pending=None;checks=0;completed=0;ignored=0
        def call(cmd,address=None,data=b'',read=0):
            nonlocal checks
            rx=C.create_string_buffer(b'\xa5'*read,read) if read else None
            status=command(C.byref(nor),actual,size,cmd,address is not None,address or 0,C.c_char_p(data) if data else None,len(data),rx,read)
            checks+=1;assert status==0,(hex(cmd),status)
            return rx.raw if rx else b''
        def tick(amount):
            nonlocal pending,wel,completed,checks
            assert advance(C.byref(nor),actual,size,amount)==0;checks+=1
            if pending:
                cmd,address,data,remaining=pending
                if amount>=remaining:
                    if cmd==2:
                        page=address-address%256
                        for i,byte in enumerate(data):expected[page+(address+i)%256]&=byte
                    else:
                        count={0x20:4096,0x52:32768,0xd8:65536,0xc7:size,0x60:size}[cmd]
                        base=address-address%count;expected[base:base+count]=b'\xff'*count
                    pending=None;wel=False;completed+=1
                else:pending=cmd,address,data,remaining-amount
            assert nor.remaining_us==(pending[3] if pending else 0) and bool(nor.wel)==wel
        for trial in range(256):
            if trial%23==0:cmd=[0x20,0x52,0xd8,0xc7,0x60][(trial//23)%5]
            else:cmd=2
            address=rng.randrange(size);data=rng.randbytes(rng.randint(1,256)) if cmd==2 else b''
            if trial%7:
                call(6);wel=True
            call(cmd,address if cmd not in [0xc7,0x60] else None,data)
            if wel:pending=cmd,address if cmd not in [0xc7,0x60] else 0,data,times[cmd]
            else:ignored+=1
            assert nor.remaining_us==(times[cmd] if wel else 0)
            assert call(5,read=7)==bytes([3 if wel else 0])*7
            if pending:
                snapshot=bytes(nor);call(4);call(6);assert bytes(nor)==snapshot
                tick(0);tick(times[cmd]-1)
                # All cells still match the previous completed state.
                assert actual.raw==expected
                tick(1)
            else:tick((1<<64)-1)
            assert actual.raw==expected
            assert call(5,read=1)==b'\x00'
            assert call(0x9f,read=3)==jedec
            assert call(3,address,data=b'',read=min(64,size-address))==expected[address:address+min(64,size-address)]
            assert not nor.remaining_us and not nor.wel
        print(f'{checks} independent NOR command/tick comparisons, {completed} completed operations, {ignored} writes ignored without WEL; maximum durations and IQ ID parsed from pinned datasheet')


if __name__=='__main__':main()
