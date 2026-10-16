#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Applications built with ST's unmodified STM32F1 HAL on EmbSIM's Blue
Pill (01 §8.2: firmware written for the vendor's own framework runs as it
does on the part).

ST's files are not vendored: --st names a directory holding checkouts of
https://github.com/STMicroelectronics/stm32f1xx_hal_driver,
https://github.com/STMicroelectronics/cmsis_device_f1 and
https://github.com/ARM-software/CMSIS_5 (CMSIS/Core/Include is enough), CI's
at pinned commits. Each application here (apps/<name>/: its main.c and its
stm32f1xx_hal_conf.h, as a CubeMX project has them) is compiled with the
HAL's sources, the device's system_stm32f1xx.c, its GCC startup file and
its GCC linker script, unchanged, with the flags of a CubeMX-generated
Makefile for an STM32F103xB, and linked against newlib-nano. The image
then runs this directory's project.json under the application's
scenario.json, whose assertions must all pass.

    run.py --st DIR [--embsim PATH] [--keep DIR] [APP...]

Without arm-none-eabi-gcc, or without --st (or STM32CUBE_F1), it skips."""
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

# a CubeMX Makefile's: MCU, C_DEFS, CFLAGS (OPT -Og, DEBUG 1) and LDFLAGS
MCU = ["-mcpu=cortex-m3", "-mthumb"]
DEFS = ["-DUSE_HAL_DRIVER", "-DSTM32F103xB"]
CFLAGS = ["-Og", "-Wall", "-fdata-sections", "-ffunction-sections", "-g", "-gdwarf-2"]
LDFLAGS = ["-specs=nano.specs", "-Wl,--gc-sections", "-lc", "-lm", "-lnosys"]
HAL = ["stm32f1xx_hal.c", "stm32f1xx_hal_adc.c", "stm32f1xx_hal_adc_ex.c", "stm32f1xx_hal_cortex.c",
       "stm32f1xx_hal_dma.c", "stm32f1xx_hal_exti.c", "stm32f1xx_hal_flash.c", "stm32f1xx_hal_flash_ex.c",
       "stm32f1xx_hal_gpio.c", "stm32f1xx_hal_gpio_ex.c", "stm32f1xx_hal_pwr.c", "stm32f1xx_hal_rcc.c",
       "stm32f1xx_hal_rcc_ex.c", "stm32f1xx_hal_tim.c", "stm32f1xx_hal_tim_ex.c", "stm32f1xx_hal_uart.c"]


def build_app(st: Path, app: Path, out: Path) -> Path:
    hal, dev = st / "stm32f1xx_hal_driver", st / "cmsis_device_f1"
    tpl = dev / "Source" / "Templates"
    inc = ["-I", app, "-I", hal / "Inc", "-I", dev / "Include", "-I", st / "CMSIS_5" / "CMSIS" / "Core" / "Include"]
    sources = [app / "main.c", tpl / "system_stm32f1xx.c", *[hal / "Src" / f for f in HAL]]
    objs = []
    d = out / app.name
    d.mkdir(parents=True, exist_ok=True)
    for src in sources:
        o = d / (src.stem + ".o")
        sh(["arm-none-eabi-gcc", "-c", *MCU, *DEFS, *inc, *CFLAGS, src, "-o", o])
        objs.append(o)
    o = d / "startup_stm32f103xb.o"
    sh(["arm-none-eabi-gcc", "-x", "assembler-with-cpp", "-c", *MCU, "-g", tpl / "gcc" / "startup_stm32f103xb.s",
        "-o", o])
    objs.append(o)
    elf = out / (app.name + ".elf")
    sh(["arm-none-eabi-gcc", *objs, *MCU, "-T", tpl / "gcc" / "linker" / "STM32F103XB_FLASH.ld", *LDFLAGS,
        "-o", elf])
    return elf


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--st", default=os.environ.get("STM32CUBE_F1"),
                    help="a directory holding stm32f1xx_hal_driver, cmsis_device_f1 and CMSIS_5")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--keep", help="build here, and keep it, instead of a temporary directory")
    ap.add_argument("apps", nargs="*")
    args = ap.parse_args()
    if not shutil.which("arm-none-eabi-gcc") or not args.st:
        print(f"SKIP: {'needs arm-none-eabi-gcc' if args.st else 'no --st (the ST and CMSIS checkouts)'}")
        return 0
    st = Path(args.st).resolve()
    for need in ("stm32f1xx_hal_driver/Inc/stm32f1xx_hal.h", "cmsis_device_f1/Include/stm32f1xx.h",
                 "CMSIS_5/CMSIS/Core/Include/core_cm3.h"):
        if not (st / need).is_file():
            print(f"error: {st} has no {need}")
            return 2
    names = args.apps or sorted(d.name for d in (HERE / "apps").iterdir() if d.is_dir())
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(args.keep).resolve() if args.keep else Path(tmp)
        work.mkdir(parents=True, exist_ok=True)
        bad = 0
        for name in names:
            elf = build_app(st, HERE / "apps" / name, work)
            bad += not run_scenario(Path(args.embsim), HERE / "project.json", name, elf,
                                    HERE / "apps" / name / "scenario.json", work)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
