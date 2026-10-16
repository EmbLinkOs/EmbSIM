#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Arduino sketches, built with the unmodified ArduinoCore-avr, on EmbSIM's
Uno (01 §8.2: firmware written for the vendor's own framework runs as it
does on the part).

The core is not vendored (it is LGPL-2.1): --core names a checkout of
https://github.com/arduino/ArduinoCore-avr, CI's at a pinned commit. Every
file of cores/arduino is compiled into core.a, and each sketch here
(sketches/<name>/<name>.ino) is turned into C++ as the IDE's builder turns
it (Arduino.h included, a #line back to the .ino) and linked against it,
all with the flags of the core's platform.txt and the Uno's boards.txt
entry. The image then runs this directory's project.json under the
sketch's scenario.json, whose assertions must all pass.

    run.py --core DIR [--embsim PATH] [--keep DIR] [SKETCH...]

Without avr-gcc, or without --core (or ARDUINO_CORE_AVR), it skips."""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
# platform.txt: compiler.{c,cpp,S}.flags and compiler.c.elf.flags (warnings
# off, as compiler.warning_flags is by default); boards.txt: uno.build.*
BOARD = ["-mmcu=atmega328p", "-DF_CPU=16000000L", "-DARDUINO=10819", "-DARDUINO_AVR_UNO",
         "-DARDUINO_ARCH_AVR"]
C_FLAGS = ["-c", "-g", "-Os", "-w", "-std=gnu11", "-ffunction-sections", "-fdata-sections",
           "-flto", "-fno-fat-lto-objects"]
CPP_FLAGS = ["-c", "-g", "-Os", "-w", "-std=gnu++11", "-fpermissive", "-fno-exceptions",
             "-ffunction-sections", "-fdata-sections", "-fno-threadsafe-statics",
             "-Wno-error=narrowing", "-flto"]
S_FLAGS = ["-c", "-g", "-x", "assembler-with-cpp", "-flto"]
ELF_FLAGS = ["-w", "-Os", "-g", "-flto", "-fuse-linker-plugin", "-Wl,--gc-sections",
             "-mmcu=atmega328p"]


def sh(cmd, **kw):
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def build_core(core: Path, out: Path) -> Path:
    src = core / "cores" / "arduino"
    inc = ["-I", src, "-I", core / "variants" / "standard"]
    objs = []
    for f in sorted(src.iterdir()):
        o = out / (f.name + ".o")
        if f.suffix == ".c":
            sh(["avr-gcc", *C_FLAGS, *BOARD, *inc, f, "-o", o])
        elif f.suffix == ".cpp":
            sh(["avr-g++", *CPP_FLAGS, *BOARD, *inc, f, "-o", o])
        elif f.suffix == ".S":
            sh(["avr-gcc", *S_FLAGS, *BOARD, *inc, f, "-o", o])
        else:
            continue
        objs.append(o)
    lib = out / "core.a"
    sh(["avr-gcc-ar", "rcs", lib, *objs])
    return lib


def build_sketch(core: Path, lib: Path, sketch: Path, out: Path) -> Path:
    inc = ["-I", core / "cores" / "arduino", "-I", core / "variants" / "standard"]
    cpp = out / (sketch.name + ".cpp")
    cpp.write_text(f'#include <Arduino.h>\n#line 1 "{sketch.name}"\n' + sketch.read_text())
    obj = out / (sketch.name + ".o")
    elf = out / (sketch.stem + ".elf")
    sh(["avr-g++", *CPP_FLAGS, *BOARD, *inc, cpp, "-o", obj])
    sh(["avr-gcc", *ELF_FLAGS, "-o", elf, obj, lib, "-L", out, "-lm"])
    return elf


def run_sketch(embsim: Path, name: str, elf: Path, work: Path) -> bool:
    d = work / name
    d.mkdir()
    shutil.copy(HERE / "project.json", d / "project.json")
    shutil.copy(elf, d / "sketch.elf")
    scenario = HERE / "sketches" / name / "scenario.json"
    p = subprocess.run([str(embsim), "run", str(d / "project.json"), "--scenario", str(scenario), "--json"],
                       capture_output=True, text=True)
    try:
        report = json.loads(p.stdout)
    except json.JSONDecodeError:
        print(f"FAIL  {name}: embsim did not report\n{p.stdout[-2000:]}{p.stderr[-2000:]}")
        return False
    results = report.get("assertions") or []
    failed = [a for a in results if not a.get("ok")]
    for a in results:
        print(f"  {'ok  ' if a.get('ok') else 'FAIL'} {name}/{a.get('id')}"
              + ("" if a.get("ok") else f": expected {a.get('expected')}, observed {a.get('observed')!r}"))
    ok = p.returncode == 0 and report.get("ok") and results and not failed
    print(f"{'ok  ' if ok else 'FAIL'}  {name}: {len(results) - len(failed)}/{len(results)} assertions")
    return bool(ok)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--core", default=os.environ.get("ARDUINO_CORE_AVR"), help="an ArduinoCore-avr checkout")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--keep", help="build here, and keep it, instead of a temporary directory")
    ap.add_argument("sketches", nargs="*")
    args = ap.parse_args()
    missing = [t for t in ("avr-gcc", "avr-g++", "avr-gcc-ar") if not shutil.which(t)]
    if missing or not args.core:
        print(f"SKIP: {'needs ' + ', '.join(missing) if missing else 'no --core (ArduinoCore-avr checkout)'}")
        return 0
    core = Path(args.core).resolve()
    if not (core / "cores" / "arduino" / "Arduino.h").is_file():
        print(f"error: {core} is not an ArduinoCore-avr checkout")
        return 2
    names = args.sketches or sorted(d.name for d in (HERE / "sketches").iterdir() if d.is_dir())
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(args.keep).resolve() if args.keep else Path(tmp)
        if args.keep:
            shutil.rmtree(work, ignore_errors=True)
        work.mkdir(parents=True, exist_ok=True)
        lib = build_core(core, work)
        bad = 0
        for name in names:
            elf = build_sketch(core, lib, HERE / "sketches" / name / f"{name}.ino", work)
            bad += not run_sketch(Path(args.embsim), name, elf, work)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
