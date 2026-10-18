#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Build the committed test firmware (tests/firmware/*/) with Clang and LLD:
Cortex-M images, and the Uno's (AVR) for the host serial port's test.

The ELF images are committed so every platform's tests run them without a
cross toolchain. This script rebuilds them byte for byte: absolute paths are
mapped away and no build id is written. `--check` rebuilds into a temporary
directory and fails if any committed image differs (CI runs it where Clang
and LLD exist)."""
import argparse
import filecmp
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FLAGS = ["-O2", "-g", "-ffreestanding", "-fno-builtin", "-nostdlib", "-fuse-ld=lld",
         "-Wl,--build-id=none", f"-ffile-prefix-map={ROOT}=.", "-I", str(HERE / "common")]
CM3 = ["--target=thumbv7m-none-eabi", "-mcpu=cortex-m3", "-mthumb"]
CM4 = ["--target=thumbv7em-none-eabi", "-mcpu=cortex-m4", "-mthumb"]
# the Uno's ATmega328P, with the uno-blink-serial example's header and startup
UNO = "../../examples/uno-blink-serial/firmware"
AVR = ["--target=avr", "-mmcu=atmega328p", "-Wl,--gc-sections", "-I", str(HERE / UNO)]
# image: (sources relative to tests/firmware, linker script[, the target and CPU; a
# Cortex-M3 by default])
IMAGES = {
    "hello/hello.elf": (["common/startup.c", "hello/main.c"], "common/stm32f103c8.ld"),
    "bluepill-golden/bluepill.elf": (["common/startup.c", "bluepill-golden/bluepill.c"],
                                     "common/stm32f103c8.ld"),
    "stack-overflow/stack-overflow.elf": (["common/startup.c", "stack-overflow/main.c"],
                                          "stack-overflow/stack.ld"),
    "fault/fault.elf": (["common/startup.c", "fault/main.c"], "common/stm32f103c8.ld"),
    "dsp/dsp.elf": (["common/startup.c", "dsp/main.c"], "dsp/dsp.ld", CM4),
    "uno-echo/uno-echo.elf": ([f"{UNO}/startup.S", "uno-echo/main.c"], f"{UNO}/atmega328p.ld", AVR),
}


def build(out_dir: Path) -> None:
    for image, (sources, script, *cpu) in IMAGES.items():
        out = out_dir / image
        out.parent.mkdir(parents=True, exist_ok=True)
        # clang's AVR driver takes the linker script only through -Wl
        link = [f"-Wl,-T,{HERE / script}"] if cpu and cpu[0] is AVR else ["-T", str(HERE / script)]
        cmd = ["clang", *(cpu[0] if cpu else CM3), *FLAGS, *link,
               *[str(HERE / s) for s in sources], "-o", str(out)]
        subprocess.run(cmd, check=True, cwd=ROOT)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="fail if a committed image is stale")
    args = ap.parse_args()
    if not shutil.which("clang") or not shutil.which("ld.lld"):
        print("SKIP: clang and ld.lld are needed to build the test firmware")
        return 0
    if not args.check:
        build(HERE)
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        build(Path(tmp))
        stale = [i for i in IMAGES if not filecmp.cmp(HERE / i, Path(tmp) / i, shallow=False)]
    for i in stale:
        print(f"stale: tests/firmware/{i} (run tests/firmware/build.py)")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
