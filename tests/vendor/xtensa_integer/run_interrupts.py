#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Compare CPU input behavior with hash-pinned Espressif core configuration facts.

The vendor header is an external test input; no vendor implementation is copied
or linked. This does not qualify SoC routing, peripheral timing or NMI sampling.
"""
import argparse
import ctypes as C
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile
from run_execute import ROOT
from run_windows_c import Windows


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--core-isa',type=Path,required=True)
    ap.add_argument('--cc',default='cc');args=ap.parse_args();raw=args.core_isa.read_bytes()
    assert hashlib.sha256(raw).hexdigest()=='60d20f76c7730b24041b9f55b16e0062f30bf85c7661c7ed215f050fedcdb344','Unexpected ESP32 core configuration'
    text=raw.decode('utf-8')
    levels={int(n):int(level) for n,level in re.findall(r'#define\s+XCHAL_INT(\d+)_LEVEL\s+(\d+)',text)}
    kinds={int(n):kind for n,kind in re.findall(r'#define\s+XCHAL_INT(\d+)_TYPE\s+XTHAL_INTTYPE_(\w+)',text)}
    floor=int(re.search(r'#define\s+XCHAL_EXCM_LEVEL\s+(\d+)',text)[1])
    assert set(levels)==set(kinds)==set(range(32))
    with tempfile.TemporaryDirectory(prefix='embsim-irq-profile-') as tmp:
        lib=Path(tmp)/'irq.so'
        subprocess.run([args.cc,'-std=c99','-Wall','-Wextra','-Wconversion','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'core'),'-I'+str(ROOT/'include'),
                        *[str(ROOT/'core/mcu/xtensa'/f) for f in ['integer.c','execute.c','windows.c','interrupts.c']],'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));wp=C.POINTER(Windows)
        api.esk_xtensa_windows_init.argtypes=[wp,C.c_uint32,C.POINTER(C.c_uint32)];api.esk_xtensa_windows_init.restype=None
        api.esk_xtensa_exception_configure.argtypes=[wp,C.POINTER(C.c_uint32)];api.esk_xtensa_exception_configure.restype=None
        api.esk_xtensa_irq_configure.argtypes=[wp,C.POINTER(C.c_uint32)];api.esk_xtensa_irq_configure.restype=C.c_int
        api.esk_xtensa_irq_write.argtypes=[wp,C.c_uint,C.c_uint32];api.esk_xtensa_irq_write.restype=C.c_int
        api.esk_xtensa_irq_line.argtypes=[wp,C.c_uint,C.c_int];api.esk_xtensa_irq_line.restype=C.c_int
        api.esk_xtensa_irq_poll.argtypes=[wp,C.POINTER(C.c_uint)];api.esk_xtensa_irq_poll.restype=C.c_int
        api.esk_xtensa_irq_pending.argtypes=[wp];api.esk_xtensa_irq_pending.restype=C.c_uint32
        general=(C.c_uint32*3)(0x5000,0x5040,0x5080);high=(C.c_uint32*6)(*[0x6000+i*64 for i in range(6)])
        checks=0
        for source in range(32):
            for threshold in range(16):
                for excm in [False,True]:
                    for enable in [False,True]:
                        w=Windows();p=C.byref(w)
                        api.esk_xtensa_windows_init(p,0x1000,None);api.esk_xtensa_exception_configure(p,general)
                        assert api.esk_xtensa_irq_configure(p,high)==0
                        w.ps|=threshold|(16 if excm else 0);old_ps=w.ps;mask=1<<source
                        assert api.esk_xtensa_irq_write(p,228,mask if enable else 0)==0
                        if kinds[source]=='SOFTWARE':assert api.esk_xtensa_irq_write(p,226,mask)==0
                        else:assert api.esk_xtensa_irq_line(p,source,1)==0
                        effective=max(threshold,floor if excm else 0)
                        accepted=kinds[source]=='NMI' or (enable and levels[source]>effective)
                        event=C.c_uint();assert api.esk_xtensa_irq_poll(p,C.byref(event))==0
                        if accepted:
                            level=levels[source]
                            assert event.value==10+level and w.irq.last_level==level and w.irq.last_pending==mask,(source,threshold,excm,enable,event.value)
                            assert w.cpu.pc==(general[0] if level==1 else high[level-2])
                            if level==1:assert w.epc1==0x1000 and w.exccause==4
                            else:assert w.irq.epc[level]==0x1000 and w.irq.eps[level]==old_ps
                        else:assert event.value==0 and w.cpu.pc==0x1000 and w.ps==old_ps
                        checks+=1
        print(f'{checks} interrupt mask/type/level comparisons pass against Espressif core-isa.h')


if __name__=='__main__':main()
