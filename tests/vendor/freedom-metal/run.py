#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""SiFive's freedom-e-sdk examples, built with the unmodified freedom-metal
library and the HiFive1 Rev B's BSP, on EmbSIM's HiFive1 (01 §8.2:
firmware written for the vendor's own framework runs as it does on the
part).

SiFive's sources are not vendored: --sdk names a checkout of
https://github.com/sifive/freedom-e-sdk (its bsp/sifive-hifive1-revb and
the software/ examples below) and --metal one of
https://github.com/sifive/freedom-metal at the commit the SDK pins, CI's at
pinned commits. As the SDK's scripts/standalone.mk does for the board
(its settings.mk, the release configuration), the BSP's metal.h,
metal-platform.h and metal-inline.h become metal/machine.h,
metal/machine/platform.h and metal/machine/inline.h; freedom-metal's src
and gloss build libmetal.a and libmetal-gloss.a; each example links
against them with metal.default.lds. Two things differ from the SDK's
build, both for the toolchain: GCC 12 and later name the CSR and fence.i
extensions (-march=rv32imac_zicsr_zifencei), and Ubuntu's toolchain has
picolibc where SiFive's has newlib, so picolibc_stdio.c gives picolibc's
stdio freedom-metal's tty as freedom-metal's gloss gives newlib's.

Each example under apps/ (its scenario.json; the sources are the SDK's)
then runs this directory's project.json under it, and its assertions must
all pass.

    run.py --sdk DIR --metal DIR [--embsim PATH] [--keep DIR] [APP...]

Without riscv64-unknown-elf-gcc and picolibc, or without the checkouts, it
skips."""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from vendor_run import run_scenario, sh  # noqa: E402

CC = "riscv64-unknown-elf-gcc"
AR = "riscv64-unknown-elf-ar"
BSP = "bsp/sifive-hifive1-revb"
# settings.mk (RISCV_ARCH, ABI, CMODEL), standalone.mk (picolibc in place
# of nano; MTIME_RATE_HZ_DEF; the sections), release.mk (-Os), the
# examples' XCFLAGS (METAL_WAIT_CYCLE, TARGET_INTR_WAIT_CYCLE's 0)
CFLAGS = ["-march=rv32imac_zicsr_zifencei", "-mabi=ilp32", "-mcmodel=medlow", "-ffunction-sections",
          "-fdata-sections", "--specs=picolibc.specs", "-DMTIME_RATE_HZ_DEF=32768", "-Os", "-g",
          "-DMETAL_WAIT_CYCLE=0"]
LDFLAGS = ["-Wl,--gc-sections", "-nostartfiles", "-nostdlib"]
LDLIBS = ["-Wl,--start-group", "-lc", "-lgcc", "-lm", "-lmetal", "-lmetal-gloss", "-Wl,--end-group"]


def picolibc() -> bool:
    p = subprocess.run([CC, "-print-file-name=picolibc.specs"], capture_output=True, text=True)
    return p.returncode == 0 and os.path.isabs(p.stdout.strip())


def build_metal(sdk: Path, metal: Path, out: Path) -> list:
    inc = out / "include"
    (inc / "metal" / "machine").mkdir(parents=True, exist_ok=True)
    shutil.copy(sdk / BSP / "metal.h", inc / "metal" / "machine.h")
    shutil.copy(sdk / BSP / "metal-platform.h", inc / "metal" / "machine" / "platform.h")
    shutil.copy(sdk / BSP / "metal-inline.h", inc / "metal" / "machine" / "inline.h")
    flags = CFLAGS + ["-I", str(inc), "-I", str(metal)]
    for lib, sources in (("metal", sorted((metal / "src").glob("*.[cS]")) + sorted((metal / "src" / "drivers").glob("*.c"))),
                         ("metal-gloss", sorted((metal / "gloss").glob("*.[cS]")))):
        objs = []
        d = out / lib
        d.mkdir(exist_ok=True)
        for src in sources:
            o = d / (src.name + ".o")
            sh([CC, *flags, "-c", src, "-o", o])
            objs.append(o)
        sh([AR, "rcs", out / f"lib{lib}.a", *objs])
    return flags


def build_app(sdk: Path, flags: list, name: str, out: Path) -> Path:
    src = sdk / "software" / name
    elf = out / (name + ".elf")
    sh([CC, *flags, *LDFLAGS, "-L", out, "-T", sdk / BSP / "metal.default.lds",
        *sorted(src.glob("*.[cS]")), HERE / "picolibc_stdio.c", *LDLIBS, "-o", elf])
    return elf


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk", default=os.environ.get("FREEDOM_E_SDK"), help="a freedom-e-sdk checkout")
    ap.add_argument("--metal", default=os.environ.get("FREEDOM_METAL"), help="a freedom-metal checkout")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--keep", help="build here, and keep it, instead of a temporary directory")
    ap.add_argument("apps", nargs="*")
    args = ap.parse_args()
    if not shutil.which(CC) or not picolibc():
        print(f"SKIP: needs {CC} and picolibc")
        return 0
    if not args.sdk or not args.metal:
        print("SKIP: no --sdk and --metal (the freedom-e-sdk and freedom-metal checkouts)")
        return 0
    sdk, metal = Path(args.sdk).resolve(), Path(args.metal).resolve()
    if not (sdk / BSP / "metal.h").is_file() or not (metal / "src" / "init.c").is_file():
        print(f"error: {sdk} or {metal} is not the checkout it should be")
        return 2
    names = args.apps or sorted(d.name for d in (HERE / "apps").iterdir() if d.is_dir())
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(args.keep).resolve() if args.keep else Path(tmp)
        build = work / "build"
        build.mkdir(parents=True, exist_ok=True)
        flags = build_metal(sdk, metal, build)
        bad = 0
        for name in names:
            elf = build_app(sdk, flags, name, build)
            bad += not run_scenario(Path(args.embsim), HERE / "project.json", name, elf,
                                    HERE / "apps" / name / "scenario.json", work)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
