#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Applications built with Nordic's unmodified nrfx drivers and MDK on
EmbSIM's nRF52840 DK (01 §8.2: firmware written for the vendor's own
framework runs as it does on the part).

Nordic's files are not vendored: --nrfx names a checkout of
https://github.com/NordicSemiconductor/nrfx (its drivers, HAL and the MDK
it carries) and --cmsis one of https://github.com/ARM-software/CMSIS_5
(CMSIS/Core/Include is enough), CI's at pinned commits. Each application
here (apps/<name>/: its main.c and the nrfx_config.h, nrfx_glue.h and
nrfx_log.h an nrfx application supplies) is compiled with the drivers it
enables, the MDK's system_nrf52840.c and GCC startup file, unchanged, for
an nRF52840_XXAA with the hard-float ABI, and linked against newlib-nano
with the MDK's nrf52840_xxaa.ld. The image then runs this directory's
project.json under the application's scenario.json, whose assertions must
all pass.

    run.py --nrfx DIR --cmsis DIR [--embsim PATH] [--keep DIR] [APP...]

Without arm-none-eabi-gcc, or without the checkouts, it skips."""
import argparse
import os
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from vendor_run import run_scenario, sh  # noqa: E402

CC = "arm-none-eabi-gcc"
MCU = ["-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16"]
CFLAGS = ["-O2", "-g", "-Wall", "-ffunction-sections", "-fdata-sections", "-DNRF52840_XXAA"]
LDFLAGS = ["--specs=nano.specs", "--specs=nosys.specs", "-Wl,--gc-sections"]
# the drivers the applications enable, and what they need
SOURCES = ["drivers/src/nrfx_clock.c", "drivers/src/nrfx_clock_hfclk.c", "drivers/src/nrfx_clock_lfclk.c",
           "drivers/src/nrfx_gpiote.c", "drivers/src/nrfx_timer.c", "drivers/src/nrfx_rtc.c",
           "drivers/src/nrfx_uarte.c", "helpers/nrfx_flag32_allocator.c",
           "bsp/stable/mdk/nrf52/nrf52840/system_nrf52840.c"]
STARTUP = "bsp/stable/mdk/nrf52/nrf52840/gcc_startup_nrf52840.S"
# the MDK's directories, the part's first: its linker script includes the others'
MDK = ["bsp/stable/mdk/nrf52/nrf52840", "bsp/stable/mdk/nrf52", "bsp/stable/mdk/common", "bsp/stable/mdk"]
INCLUDES = [".", "templates", "bsp/stable", "bsp/stable/templates", "bsp/stable/soc", "drivers/include",
            "drivers", "hal", "haly", *MDK]


def build_app(nrfx: Path, cmsis: Path, app: Path, out: Path) -> Path:
    inc = ["-I", app] + [a for i in INCLUDES for a in ("-I", nrfx / i)] + ["-I", cmsis / "CMSIS" / "Core" / "Include"]
    d = out / "build" / app.name
    d.mkdir(parents=True, exist_ok=True)
    objs = []
    for src in [app / "main.c", *[nrfx / s for s in SOURCES], nrfx / STARTUP]:
        o = d / (src.name + ".o")
        sh([CC, "-c", *MCU, *CFLAGS, *inc, src, "-o", o])
        objs.append(o)
    elf = d / (app.name + ".elf")
    sh([CC, *MCU, *objs, *[a for m in MDK for a in ("-L", nrfx / m)], "-T", "nrf52840_xxaa.ld", *LDFLAGS, "-o", elf])
    return elf


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--nrfx", default=os.environ.get("NRFX_PATH"), help="an nrfx checkout")
    ap.add_argument("--cmsis", default=os.environ.get("CMSIS_5"), help="a CMSIS_5 checkout")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--keep", help="build here, and keep it, instead of a temporary directory")
    ap.add_argument("apps", nargs="*")
    args = ap.parse_args()
    if not shutil.which(CC):
        print(f"SKIP: needs {CC}")
        return 0
    if not args.nrfx or not args.cmsis:
        print("SKIP: no --nrfx and --cmsis (the nrfx and CMSIS_5 checkouts)")
        return 0
    nrfx, cmsis = Path(args.nrfx).resolve(), Path(args.cmsis).resolve()
    if not (nrfx / "nrfx.h").is_file() or not (cmsis / "CMSIS" / "Core" / "Include" / "core_cm4.h").is_file():
        print(f"error: {nrfx} or {cmsis} is not the checkout it should be")
        return 2
    names = args.apps or sorted(d.name for d in (HERE / "apps").iterdir() if d.is_dir())
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(args.keep).resolve() if args.keep else Path(tmp)
        work.mkdir(parents=True, exist_ok=True)
        bad = 0
        for name in names:
            elf = build_app(nrfx, cmsis, HERE / "apps" / name, work)
            bad += not run_scenario(Path(args.embsim), HERE / "project.json", name, elf,
                                    HERE / "apps" / name / "scenario.json", work)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
