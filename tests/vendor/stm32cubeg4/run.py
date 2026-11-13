#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""STM32CubeG4's own examples for the NUCLEO-G474RE, built unmodified and
run on EmbSIM's board (01 §8.2).

ST's files are not vendored. --cube names a checkout of STM32CubeG4 with
its submodules (Drivers/STM32G4xx_HAL_Driver, Drivers/CMSIS/Device/ST/
STM32G4xx, Drivers/BSP/STM32G4xx_Nucleo; its Drivers/CMSIS/Include).
CI fetches it at the commit the workflow pins.

Each example under nucleo_g474re/ is one of CubeG4's for the board (the
EXAMPLES table names its directory). It is built as its STM32CubeIDE
project builds it:
- the sources its .project links;
- the defines and include paths of its .cproject's Debug configuration,
  at -Os;
- its own startup file, syscalls.c, sysmem.c and linker script
  (STM32G474RETX_FLASH.ld);
- newlib-nano.
The image then runs nucleo_g474re/project.json (or <example>/project.json)
under the example's scenario.json, whose assertions must all pass.

    run.py --cube DIR [--embsim PATH] [--keep DIR] [EXAMPLE...]

Without arm-none-eabi-gcc, or without --cube (or STM32CUBE_G4), it skips."""
import argparse
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from vendor_run import run_scenario, sh  # noqa: E402

NUCLEO = "Projects/NUCLEO-G474RE/"
EXAMPLES = {
    "cortexm_modeprivilege": NUCLEO + "Examples/Cortex/CORTEXM_ModePrivilege",
    "cortexm_processstack": NUCLEO + "Examples/Cortex/CORTEXM_ProcessStack",
    "cortexm_systick": NUCLEO + "Examples/Cortex/CORTEXM_SysTick",
    "crc_bytes_stream_7bit_crc": NUCLEO + "Examples/CRC/CRC_Bytes_Stream_7bit_CRC",
    "crc_data_reversing_16bit_crc": NUCLEO + "Examples/CRC/CRC_Data_Reversing_16bit_CRC",
    "crc_example": NUCLEO + "Examples/CRC/CRC_Example",
    "crc_userdefinedpolynomial": NUCLEO + "Examples/CRC/CRC_UserDefinedPolynomial",
    "dma_copyfromflashtomemory_init": NUCLEO + "Examples_LL/DMA/DMA_CopyFromFlashToMemory_Init",
    "dma_flashtoram": NUCLEO + "Examples/DMA/DMA_FLASHToRAM",
    "flash_eraseprogram": NUCLEO + "Examples/FLASH/FLASH_EraseProgram",
    "flash_fastprogram": NUCLEO + "Examples/FLASH/FLASH_FastProgram",
    "gpio_exti": NUCLEO + "Examples/GPIO/GPIO_EXTI",
    "gpio_iotoggle": NUCLEO + "Examples/GPIO/GPIO_IOToggle",
    "hal_timebase_tim": NUCLEO + "Examples/HAL/HAL_TimeBase_TIM",
    "iwdg_reset": NUCLEO + "Examples/IWDG/IWDG_Reset",
    "iwdg_windowmode": NUCLEO + "Examples/IWDG/IWDG_WindowMode",
    "pwr_stop0": NUCLEO + "Examples/PWR/PWR_STOP0",
    "rcc_clockconfig": NUCLEO + "Examples/RCC/RCC_ClockConfig",
    "tim_octoggle": NUCLEO + "Examples/TIM/TIM_OCToggle",
    "tim_pwmoutput": NUCLEO + "Examples/TIM/TIM_PWMOutput",
    "uart_hyperterminal_dma": NUCLEO + "Examples/UART/UART_HyperTerminal_DMA",
    "uart_printf": NUCLEO + "Examples/UART/UART_Printf",
    "uart_receptiontoidle_circulardma": NUCLEO + "Examples/UART/UART_ReceptionToIdle_CircularDMA",
    "wwdg_example": NUCLEO + "Examples/WWDG/WWDG_Example",
}
BOARD_DIR = HERE / "nucleo_g474re"
LD = "STM32G474RETX_FLASH.ld"
MCU = ["-mcpu=cortex-m4", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard", "-mthumb"]
CFLAGS = ["-std=gnu11", "-g3", "-Os", "-ffunction-sections", "-fdata-sections", "-Wall", "--specs=nano.specs"]
LDFLAGS = ["--specs=nosys.specs", "--specs=nano.specs", "-Wl,--gc-sections", "-static",
           "-Wl,--start-group", "-lc", "-lm", "-Wl,--end-group"]


def debug_config(cproject: str) -> str:
    for m in re.finditer(r"<cconfiguration\b.*?</cconfiguration>", cproject, re.S):
        if 'name="Debug"' in m.group(0):
            return m.group(0)
    raise SystemExit("no Debug configuration in .cproject")


def option_values(config: str, key: str) -> list:
    out = []
    for m in re.finditer(r'<option[^>]*superClass="[^"]*c\.compiler\.option\.' + key + r'"[^>]*>(.*?)</option>',
                         config, re.S):
        out += re.findall(r'<listOptionValue builtIn="false" value="([^"]*)"', m.group(1))
    return out


def project_sources(ide: Path) -> list:
    """the .project's linked C and assembly sources (CubeG4 writes them as
    ${PARENT-n-PROJECT_LOC}/..., URL-encoded), then the files the project
    directory holds (its startup, syscalls.c, sysmem.c)"""
    text = (ide / ".project").read_text(encoding="utf-8")
    out = []
    for loc in re.findall(r"<locationURI>([^<]*)</locationURI>", text):
        loc = loc.replace("$%7B", "").replace("%7D", "")
        m = re.match(r"PARENT-(\d+)-PROJECT_LOC/(.*)", loc)
        if not m:
            continue
        base = ide
        for _ in range(int(m.group(1))):
            base = base.parent
        p = base / m.group(2)
        if p.suffix in (".c", ".s"):
            out.append(p)
    for p in sorted(ide.rglob("*")):
        if p.suffix in (".c", ".s") and p.is_file():
            out.append(p)
    return out


def record(name: str) -> dict:
    e = EXAMPLES[name]
    return dict({"defines": [], "patches": {}}, **(e if isinstance(e, dict) else {"dir": e}))


def build(cube: Path, name: str, out: Path) -> Path:
    rec = record(name)
    ex = cube / rec["dir"]
    ide = ex / "STM32CubeIDE"
    config = debug_config((ide / ".cproject").read_text(encoding="utf-8"))
    defs = ["-D" + d for d in option_values(config, "definedsymbols") + rec["defines"]]
    incs = []
    for i in option_values(config, "includepaths"):
        incs += ["-I", (ide / "Debug" / i).resolve()]  # relative to the IDE's build directory
    d = out / name
    d.mkdir(parents=True, exist_ok=True)
    objs = []
    for n, src in enumerate(project_sources(ide)):
        o = d / f"{n:03d}_{src.stem}.o"
        if src.suffix == ".s":
            sh(["arm-none-eabi-gcc", "-x", "assembler-with-cpp", "-c", *MCU, "-g3", *defs, src, "-o", o])
        else:
            sh(["arm-none-eabi-gcc", "-c", *MCU, *CFLAGS, *defs, *incs, src, "-o", o])
        objs.append(o)
    elf = out / (name + ".elf")
    sh(["arm-none-eabi-gcc", *objs, *MCU, "-T", ide / LD, *LDFLAGS, f"-Wl,-Map={out / (name + '.map')}", "-o", elf])
    return elf


def project_of(name: str) -> Path:
    own = BOARD_DIR / name / "project.json"
    return own if own.exists() else BOARD_DIR / "project.json"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cube", default=os.environ.get("STM32CUBE_G4"), help="an STM32CubeG4 checkout with its submodules")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--keep", help="build here, and keep it, instead of a temporary directory")
    ap.add_argument("examples", nargs="*")
    args = ap.parse_args()
    if not shutil.which("arm-none-eabi-gcc") or not args.cube:
        print(f"SKIP: {'needs arm-none-eabi-gcc' if args.cube else 'no --cube (an STM32CubeG4 checkout)'}")
        return 0
    cube = Path(args.cube).resolve()
    for need in (NUCLEO, "Drivers/STM32G4xx_HAL_Driver/Inc/stm32g4xx_hal.h",
                 "Drivers/CMSIS/Device/ST/STM32G4xx/Include/stm32g4xx.h", "Drivers/BSP/STM32G4xx_Nucleo/stm32g4xx_nucleo.h",
                 "Drivers/CMSIS/Include/core_cm4.h"):
        if not (cube / need).exists():
            print(f"error: {cube} has no {need}")
            return 2
    names = args.examples or sorted(d.name for d in BOARD_DIR.iterdir() if d.is_dir())
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(args.keep).resolve() if args.keep else Path(tmp)
        work.mkdir(parents=True, exist_ok=True)
        bad = 0
        for name in names:
            elf = build(cube, name, work)
            bad += not run_scenario(Path(args.embsim), project_of(name), name, elf, BOARD_DIR / name / "scenario.json", work)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
