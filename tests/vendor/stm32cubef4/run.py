#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""STM32CubeF4's own examples for the NUCLEO-F446RE, built unmodified and
run on EmbSIM's NUCLEO-F446RE (the NUCLEO-F446RE plan, step 10; 01 §8.2).

ST's files are not vendored. --st names a directory holding checkouts of:
- STM32CubeF4 (`cubef4`, its Projects/STM32446E-Nucleo);
- the submodules it pins: stm32f4xx_hal_driver, cmsis_device_f4 and
  stm32f4xx-nucleo-bsp;
- CMSIS_5's CMSIS/Core/Include.
CI fetches them at the commits CubeF4 pins.

Each example under examples/<name>/ is one of CubeF4's (EXAMPLES below
names its directory, and the defines that choose among its variants where
its readme has a line uncommented for that: PWR_CurrentConsumption's
low-power mode). It is built as its STM32CubeIDE project builds it:
- the sources its .project links;
- the defines and include paths of its .cproject's Debug configuration,
  at -Os;
- its own startup file, syscalls.c, sysmem.c and STM32F446RETX_FLASH.ld;
- newlib-nano.
The image then runs examples/<name>/project.json (a variant's, its
example's), the project EXAMPLES names, or this directory's, under the
example's scenario.json, whose assertions must all pass. An example for two
boards (I2C_TwoBoards_*) is built twice: as it is, the master board's
sketch.elf, and with its main.c's `#define MASTER_BOARD` commented out as
its readme asks for the slave board's, slave.elf.

    run.py --st DIR [--embsim PATH] [--keep DIR] [EXAMPLE...]

Without arm-none-eabi-gcc, or without --st (or STM32CUBE_F4), it skips."""
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

# examples/<name>/ and its directory in STM32CubeF4, or a record of it:
# `dir`, the `defines` its readme's choice adds, the `project` it runs in,
# `two_boards` for a master and a slave
NUCLEO = "Projects/STM32446E-Nucleo/"
PWR = NUCLEO + "Examples/PWR/PWR_CurrentConsumption"
I2C = NUCLEO + "Examples/I2C/I2C_TwoBoards_"
TWO = {"project": "two_boards.json", "two_boards": True}
EXAMPLES = {
    "gpio_iotoggle": NUCLEO + "Examples/GPIO/GPIO_IOToggle",
    "hal_timebase_rtc_alarm": NUCLEO + "Examples/HAL/HAL_TimeBase_RTC_ALARM",
    "hal_timebase_rtc_wkup": NUCLEO + "Examples/HAL/HAL_TimeBase_RTC_WKUP",
    "hal_timebase_tim": NUCLEO + "Examples/HAL/HAL_TimeBase_TIM",
    "i2c_twoboards_advcomit": {"dir": I2C + "AdvComIT", **TWO},
    "i2c_twoboards_comdma": {"dir": I2C + "ComDMA", **TWO},
    "i2c_twoboards_comit": {"dir": I2C + "ComIT", **TWO},
    "i2c_twoboards_compolling": {"dir": I2C + "ComPolling", **TWO},
    "pwr_currentconsumption": PWR,  # stm32f4xx_lp_modes.h's own choice: SLEEP_MODE
    "pwr_currentconsumption_stop": {"dir": PWR, "defines": ["STOP_MODE"]},
    "pwr_currentconsumption_stop_underdrive": {"dir": PWR, "defines": ["STOP_UNDERDRIVE_MODE"]},
    "pwr_currentconsumption_standby": {"dir": PWR, "defines": ["STANDBY_MODE"]},
    "pwr_currentconsumption_standby_rtc": {"dir": PWR, "defines": ["STANDBY_RTC_MODE"]},
    "pwr_currentconsumption_standby_rtc_bkpsram": {"dir": PWR, "defines": ["STANDBY_RTC_BKPSRAM_MODE"]},
    "rcc_clockconfig": NUCLEO + "Examples/RCC/RCC_ClockConfig",
    "rtc_calendar": NUCLEO + "Examples/RTC/RTC_Calendar",
    "tim_pwminput": NUCLEO + "Examples/TIM/TIM_PWMInput",
    "uart_printf": NUCLEO + "Examples/UART/UART_Printf",
    "uart_receptiontoidle_circulardma": NUCLEO + "Examples/UART/UART_ReceptionToIdle_CircularDMA",
    "eeprom_emulation": NUCLEO + "Applications/EEPROM/EEPROM_Emulation",
}
MCU = ["-mcpu=cortex-m4", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard", "-mthumb"]
CFLAGS = ["-std=gnu11", "-g3", "-Os", "-ffunction-sections", "-fdata-sections", "-Wall", "--specs=nano.specs"]
LDFLAGS = ["--specs=nosys.specs", "--specs=nano.specs", "-Wl,--gc-sections", "-static",
           "-Wl,--start-group", "-lc", "-lm", "-Wl,--end-group"]


def checkouts(st: Path) -> dict:
    """CubeF4's paths, as its projects name them, to the checkouts here"""
    return {
        "Drivers/STM32F4xx_HAL_Driver": st / "stm32f4xx_hal_driver",
        "Drivers/CMSIS/Device/ST/STM32F4xx": st / "cmsis_device_f4",
        "Drivers/BSP/STM32F4xx-Nucleo": st / "stm32f4xx-nucleo-bsp",
        "Drivers/CMSIS/Include": st / "CMSIS_5" / "CMSIS" / "Core" / "Include",
    }


def resolve(st: Path, path: Path) -> Path:
    """a path inside the CubeF4 checkout, with its submodules' parts mapped"""
    cube = (st / "cubef4").resolve()
    p = path.resolve()
    try:
        rel = p.relative_to(cube).as_posix()
    except ValueError:
        return p
    for prefix, there in checkouts(st).items():
        if rel == prefix or rel.startswith(prefix + "/"):
            return there / rel[len(prefix):].lstrip("/")
    return p


def debug_config(cproject: str) -> str:
    """the .cproject's Debug configuration"""
    for m in re.finditer(r"<cconfiguration\b.*?</cconfiguration>", cproject, re.S):
        if 'name="Debug"' in m.group(0):
            return m.group(0)
    raise SystemExit("no Debug configuration in .cproject")


def option_values(config: str, key: str) -> list:
    """the listOptionValue entries of the C compiler's option `key`"""
    out = []
    for m in re.finditer(r'<option[^>]*superClass="[^"]*c\.compiler\.option\.' + key + r'"[^>]*>(.*?)</option>',
                         config, re.S):
        out += re.findall(r'<listOptionValue builtIn="false" value="([^"]*)"', m.group(1))
    return out


def project_sources(ide: Path) -> list:
    """the .project's linked C sources, and the C and assembly files the
    project directory holds"""
    text = (ide / ".project").read_text(encoding="utf-8")
    out = []
    for loc in re.findall(r"<locationURI>([^<]*)</locationURI>", text):
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
    """the example's record: dir, defines, project, two_boards"""
    e = EXAMPLES[name]
    return dict({"defines": [], "project": None, "two_boards": False}, **(e if isinstance(e, dict) else {"dir": e}))


def project_of(name: str) -> Path:
    """examples/<name>/project.json, a variant's example's, the record's, or
    this directory's"""
    for n in [name] + [k for k in EXAMPLES if record(k)["dir"] == record(name)["dir"]]:
        if (HERE / "examples" / n / "project.json").exists():
            return HERE / "examples" / n / "project.json"
    return HERE / (record(name)["project"] or "project.json")


def build(st: Path, name: str, out: Path, slave: bool = False) -> Path:
    """the example's image; `slave`: with main.c's `#define MASTER_BOARD`
    commented out, as the two-board examples' readmes ask"""
    cube_dir, extra = record(name)["dir"], record(name)["defines"]
    ex = st / "cubef4" / cube_dir
    ide = ex / "STM32CubeIDE"
    config = debug_config((ide / ".cproject").read_text(encoding="utf-8"))
    defs = ["-D" + d for d in option_values(config, "definedsymbols") + extra]
    incs = []
    for i in option_values(config, "includepaths"):
        incs += ["-I", resolve(st, ide / "Debug" / i)]  # relative to the IDE's build directory
    tag = name + ("_slave" if slave else "")
    d = out / tag
    d.mkdir(parents=True, exist_ok=True)
    objs = []
    for n, src in enumerate(project_sources(ide)):
        src = resolve(st, src)
        if slave and src.name == "main.c":
            text = src.read_bytes()  # ST's sources are not all UTF-8: the bytes as they are
            if b"\n#define MASTER_BOARD" not in text:
                raise SystemExit(f"{src} has no #define MASTER_BOARD to comment out")
            edited = d / "main.c"
            edited.write_bytes(text.replace(b"\n#define MASTER_BOARD", b"\n/* #define MASTER_BOARD */", 1))
            incs_here = ["-I", src.parent.parent / "Inc"]
            src = edited
        else:
            incs_here = []
        o = d / f"{n:03d}_{src.stem}.o"
        if src.suffix == ".s":
            sh(["arm-none-eabi-gcc", "-x", "assembler-with-cpp", "-c", *MCU, "-g3", *defs, src, "-o", o])
        else:
            sh(["arm-none-eabi-gcc", "-c", *MCU, *CFLAGS, *defs, *incs, *incs_here, src, "-o", o])
        objs.append(o)
    elf = out / (tag + ".elf")
    sh(["arm-none-eabi-gcc", *objs, *MCU, "-T", ide / "STM32F446RETX_FLASH.ld", *LDFLAGS,
        f"-Wl,-Map={out / (tag + '.map')}", "-o", elf])
    return elf


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--st", default=os.environ.get("STM32CUBE_F4"),
                    help="a directory holding cubef4, stm32f4xx_hal_driver, cmsis_device_f4, "
                         "stm32f4xx-nucleo-bsp and CMSIS_5")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--keep", help="build here, and keep it, instead of a temporary directory")
    ap.add_argument("examples", nargs="*")
    args = ap.parse_args()
    if not shutil.which("arm-none-eabi-gcc") or not args.st:
        print(f"SKIP: {'needs arm-none-eabi-gcc' if args.st else 'no --st (the ST and CMSIS checkouts)'}")
        return 0
    st = Path(args.st).resolve()
    for need in ("cubef4/Projects/STM32446E-Nucleo", "stm32f4xx_hal_driver/Inc/stm32f4xx_hal.h",
                 "cmsis_device_f4/Include/stm32f4xx.h", "stm32f4xx-nucleo-bsp/stm32f4xx_nucleo.h",
                 "CMSIS_5/CMSIS/Core/Include/core_cm4.h"):
        if not (st / need).exists():
            print(f"error: {st} has no {need}")
            return 2
    names = args.examples or sorted(d.name for d in (HERE / "examples").iterdir() if d.is_dir())
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(args.keep).resolve() if args.keep else Path(tmp)
        work.mkdir(parents=True, exist_ok=True)
        bad = 0
        for name in names:
            scenario = HERE / "examples" / name / "scenario.json"
            elf = build(st, name, work)
            extra = {"slave.elf": build(st, name, work, slave=True)} if record(name)["two_boards"] else None
            bad += not run_scenario(Path(args.embsim), project_of(name), name, elf, scenario, work, extra)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
