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
entry, with the core's libraries the sketch includes (SPI, Wire) compiled
beside it. The image then runs this directory's project.json (or the
sketch's own, when it has one) under the sketch's scenario.json, whose
assertions must all pass.

    run.py --core DIR [--libs DIR [--fetch]] [--embsim PATH] [--keep DIR] [SKETCH...]

Libraries beyond the core's are listed in libraries.json, each at a pinned
commit; --fetch clones them into --libs. A library's own example runs
unmodified when examples/<library>/<example>/ holds its scenario.json (and
its project.json, if the shared one does not fit).

Without avr-gcc, or without --core (or ARDUINO_CORE_AVR), it skips."""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from vendor_run import run_scenario, sh  # noqa: E402
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


def lib_src(root: Path) -> Path:
    """A library's source directory: src/ in the 1.5 layout, else its root."""
    return root / "src" if (root / "src").is_dir() else root


def lib_sources(root: Path) -> list:
    """The files the IDE's builder compiles: all of src/ (1.5 layout), or the
    root and utility/ (1.0 layout)."""
    if (root / "src").is_dir():
        return sorted(f for f in (root / "src").rglob("*") if f.is_file())
    files = [f for f in root.iterdir() if f.is_file()]
    if (root / "utility").is_dir():
        files += [f for f in (root / "utility").rglob("*") if f.is_file()]
    return sorted(files)


INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([\w./-]+\.h)[>"]', re.M)


def libraries(roots: list, sketch: Path) -> list:
    """The libraries (the core's, then the fetched ones) whose headers the
    sketch includes, and those their own sources include, as the IDE's
    builder finds them."""
    index = {}
    for root in roots:
        for h in lib_src(root).glob("*.h"):
            index.setdefault(h.name, root)
    found, todo = [], INCLUDE.findall(sketch.read_text(encoding="utf-8", errors="replace"))
    while todo:
        root = index.get(Path(todo.pop()).name)
        if root is None or root in found:
            continue
        found.append(root)
        for f in lib_sources(root):
            if f.suffix in (".h", ".c", ".cpp"):
                todo += INCLUDE.findall(f.read_text(encoding="utf-8", errors="replace"))
    return found


def fetch_libraries(dest: Path) -> None:
    """libraries.json's libraries, each at its pinned commit, into dest/<name>."""
    for lib in json.loads((HERE / "libraries.json").read_text(encoding="utf-8"))["libraries"]:
        d = dest / lib["name"]
        if (d / ".git").is_dir() and sh_out(["git", "-C", d, "rev-parse", "HEAD"]) == lib["commit"]:
            continue
        shutil.rmtree(d, ignore_errors=True)
        sh(["git", "init", "-q", d])
        sh(["git", "-C", d, "fetch", "-q", "--depth", "1", lib["repo"], lib["commit"]])
        sh(["git", "-C", d, "checkout", "-q", "FETCH_HEAD"])


def sh_out(cmd: list) -> str:
    p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    return p.stdout.strip()


def build_library(root: Path, inc: list, out: Path) -> list:
    objs = []
    tag = re.sub(r"\W", "_", root.name)
    for f in lib_sources(root):
        # src/avr/Servo.cpp and src/sam/Servo.cpp are different objects
        o = out / ("lib_" + tag + "_" + re.sub(r"\W", "_", str(f.relative_to(root))) + ".o")
        if f.suffix == ".c":
            sh(["avr-gcc", *C_FLAGS, *BOARD, *inc, f, "-o", o])
        elif f.suffix == ".cpp":
            sh(["avr-g++", *CPP_FLAGS, *BOARD, *inc, f, "-o", o])
        else:
            continue
        objs.append(o)
    return objs


def build_sketch(core: Path, lib: Path, roots: list, sketch: Path, out: Path) -> Path:
    libs = libraries(roots, sketch)
    inc = ["-I", core / "cores" / "arduino", "-I", core / "variants" / "standard"]
    for root in libs:
        inc += ["-I", lib_src(root)]
        if not (root / "src").is_dir() and (root / "utility").is_dir():
            inc += ["-I", root / "utility"]
    objs = [o for root in libs for o in build_library(root, inc, out)]
    cpp = out / (sketch.name + ".cpp")
    cpp.write_text(f'#include <Arduino.h>\n#line 1 "{sketch.name}"\n' + sketch.read_text(encoding="utf-8"),
                   encoding="utf-8")
    obj = out / (sketch.name + ".o")
    elf = out / (sketch.stem + ".elf")
    sh(["avr-g++", *CPP_FLAGS, *BOARD, *inc, cpp, "-o", obj])
    sh(["avr-gcc", *ELF_FLAGS, "-o", elf, obj, *objs, lib, "-L", out, "-lm"])
    return elf


def runs(libs: Path) -> list:
    """(name, sketch, project, scenario): this directory's sketches, then the
    fetched libraries' own examples that examples/<library>/<example> holds a
    scenario for."""
    out = []
    for d in sorted(p for p in (HERE / "sketches").iterdir() if p.is_dir()):
        own = d / "project.json"
        out.append((d.name, d / f"{d.name}.ino", own if own.is_file() else HERE / "project.json",
                    d / "scenario.json"))
    for d in sorted((HERE / "examples").glob("*/*")):
        if not d.is_dir() or libs is None:
            continue
        ino = libs / d.parent.name / "examples" / d.name / f"{d.name}.ino"
        own = d / "project.json"
        out.append((d.name, ino, own if own.is_file() else HERE / "project.json", d / "scenario.json"))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--core", default=os.environ.get("ARDUINO_CORE_AVR"), help="an ArduinoCore-avr checkout")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--keep", help="build here, and keep it, instead of a temporary directory")
    ap.add_argument("--libs", default=os.environ.get("ARDUINO_LIBS"),
                    help="where libraries.json's libraries are (or are fetched to, with --fetch)")
    ap.add_argument("--fetch", action="store_true", help="fetch libraries.json's libraries into --libs")
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
    libs = Path(args.libs).resolve() if args.libs else None
    if libs and args.fetch:
        libs.mkdir(parents=True, exist_ok=True)
        fetch_libraries(libs)
    roots = sorted(d for d in (core / "libraries").iterdir() if d.is_dir())
    if libs:
        roots += sorted(d for d in libs.iterdir() if d.is_dir())
    todo = [r for r in runs(libs) if not args.sketches or r[0] in args.sketches]
    if libs is None and (HERE / "examples").is_dir():
        print("note: no --libs: the libraries' examples are not run")
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(args.keep).resolve() if args.keep else Path(tmp)
        if args.keep:
            shutil.rmtree(work, ignore_errors=True)
        work.mkdir(parents=True, exist_ok=True)
        lib = build_core(core, work)
        bad = 0
        for name, ino, project, scenario in todo:
            elf = build_sketch(core, lib, roots, ino, work)
            bad += not run_scenario(Path(args.embsim), project, name, elf, scenario, work)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
