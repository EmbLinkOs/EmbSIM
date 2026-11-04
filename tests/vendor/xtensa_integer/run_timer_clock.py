#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Check compare-clock arithmetic against a tick-by-tick oracle and vendor mapping.

Clock ticks are supplied explicitly. No instruction timing or silicon read/write
phase accuracy is inferred from this test.
"""
import argparse
import ctypes as C
import hashlib
from pathlib import Path
import random
import re
import subprocess
import tempfile
from run_execute import ROOT, MASK
from run_windows_c import Windows


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--core-isa',type=Path,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args();raw=args.core_isa.read_bytes()
    assert hashlib.sha256(raw).hexdigest()=='60d20f76c7730b24041b9f55b16e0062f30bf85c7661c7ed215f050fedcdb344'
    mapping={int(n):int(source) for n,source in re.findall(r'#define\s+XCHAL_TIMER([0-2])_INTERRUPT\s+(\d+)',raw.decode('utf-8'))}
    assert set(mapping)=={0,1,2};rng=random.Random(0xc0c0)
    with tempfile.TemporaryDirectory(prefix='embsim-compare-clock-') as tmp:
        lib=Path(tmp)/'clock.so'
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),
                        *[str(ROOT/'core/mcu/xtensa'/f) for f in ['integer.c','execute.c','windows.c','interrupts.c']],'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));wp=C.POINTER(Windows)
        api.esk_xtensa_windows_init.argtypes=[wp,C.c_uint32,C.POINTER(C.c_uint32)];api.esk_xtensa_windows_init.restype=None
        api.esk_xtensa_exception_configure.argtypes=[wp,C.POINTER(C.c_uint32)];api.esk_xtensa_exception_configure.restype=None
        api.esk_xtensa_irq_configure.argtypes=[wp,C.POINTER(C.c_uint32)];api.esk_xtensa_irq_configure.restype=C.c_int
        api.esk_xtensa_irq_write.argtypes=[wp,C.c_uint,C.c_uint32];api.esk_xtensa_irq_write.restype=C.c_int
        api.esk_xtensa_irq_pending.argtypes=[wp];api.esk_xtensa_irq_pending.restype=C.c_uint32
        api.esk_xtensa_clock_advance.argtypes=[wp,C.c_uint64];api.esk_xtensa_clock_advance.restype=C.c_int
        api.esk_xtensa_clock_deadline.argtypes=[wp,C.POINTER(C.c_uint64)];api.esk_xtensa_clock_deadline.restype=C.c_int
        general=(C.c_uint32*3)(0x5000,0x5040,0x5080);high=(C.c_uint32*6)(*[0x6000+i*64 for i in range(6)])
        checks=0;ticks_checked=0
        for trial in range(256):
            w=Windows();p=C.byref(w);api.esk_xtensa_windows_init(p,0x1000,None);api.esk_xtensa_exception_configure(p,general)
            assert api.esk_xtensa_irq_configure(p,high)==0
            now=(MASK-rng.randrange(64)) if trial%2 else rng.getrandbits(32)
            assert api.esk_xtensa_irq_write(p,234,now)==0
            compares=[(now+rng.randrange(33))&MASK for _ in range(3)];pending=[False]*3
            for i,value in enumerate(compares):assert api.esk_xtensa_irq_write(p,240+i,value)==0
            for step in range(32):
                if rng.randrange(4)==0:
                    i=rng.randrange(3);compares[i]=(now+rng.randrange(33))&MASK;pending[i]=False
                    assert api.esk_xtensa_irq_write(p,240+i,compares[i])==0
                delta=rng.randrange(65)
                for tick in range(delta):
                    now=(now+1)&MASK
                    for i in range(3):
                        if now==compares[i]:pending[i]=True
                ticks_checked+=delta
                assert api.esk_xtensa_clock_advance(p,delta)==0
                assert w.irq.ccount==now,(trial,step,'count')
                expected=sum(1<<mapping[i] for i in range(3) if pending[i])
                assert api.esk_xtensa_irq_pending(p)==expected,(trial,step,'pending')
                deadline=C.c_uint64();available=api.esk_xtensa_clock_deadline(p,C.byref(deadline))
                future=[((compares[i]-now)&MASK) or 2**32 for i in range(3) if not pending[i]]
                assert available==bool(future) and deadline.value==(min(future) if future else 0),(trial,step,'deadline')
                checks+=1
        print(f'{checks} clock/deadline comparisons pass against {ticks_checked} individual oracle ticks and vendor timer-input mapping')


if __name__=='__main__':main()
