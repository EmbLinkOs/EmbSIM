#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""STM32CubeF4's own examples for the NUCLEO-F446RE and the NUCLEO-F446ZE,
built unmodified and run on EmbSIM's boards (the NUCLEO-F446RE plan, step
10; 01 §8.2).

ST's files are not vendored. --st names a directory holding checkouts of:
- STM32CubeF4 (`cubef4`, its Projects/STM32446E-Nucleo and
  Projects/STM32F446ZE-Nucleo);
- the submodules it pins: stm32f4xx_hal_driver, cmsis_device_f4,
  stm32f4xx-nucleo-bsp and (the NUCLEO-F446ZE's) stm32f4xx-nucleo-144-bsp
  and stm32-mw-usb-device (its USB device applications);
- CMSIS_5's CMSIS/Core/Include.
CI fetches them at the commits CubeF4 pins.

--board chooses the board: nucleo_f446re (the default; its examples under
examples/) or nucleo_f446ze (under nucleo_f446ze/). Each example there is
one of CubeF4's for that board (the board's EXAMPLES table names its
directory, and the defines that choose among its variants where its readme
has a line uncommented for that: PWR_CurrentConsumption's low-power mode).
It is built as its STM32CubeIDE project builds it:
- the sources its .project links;
- the defines and include paths of its .cproject's Debug configuration,
  at -Os;
- its own startup file, syscalls.c, sysmem.c and linker script
  (STM32F446RETX_FLASH.ld, STM32F446ZETX_FLASH.ld);
- newlib-nano.
The image then runs <example>/project.json (a variant's, its example's),
the project the table names, or the board directory's, under the example's
scenario.json, whose assertions must all pass. An example for two boards
(I2C_TwoBoards_*) is built twice: as it is, the master board's sketch.elf,
and with its main.c's `#define MASTER_BOARD` commented out as its readme
asks for the slave board's, slave.elf.

    run.py --st DIR [--board B] [--embsim PATH] [--keep DIR] [EXAMPLE...]

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
ZE = "Projects/STM32F446ZE-Nucleo/"
EXAMPLES_ZE = {
    "adc_regularconversion_dma": ZE + "Examples/ADC/ADC_RegularConversion_DMA",
    "adc_regularconversion_interrupt": ZE + "Examples/ADC/ADC_RegularConversion_Interrupt",
    "cortexm_mpu": ZE + "Examples/Cortex/CORTEXM_MPU",
    "cortexm_systick": ZE + "Examples/Cortex/CORTEXM_SysTick",
    "crc_example": ZE + "Examples/CRC/CRC_Example",
    "dma_flashtoram": ZE + "Examples/DMA/DMA_FLASHToRAM",
    "flash_eraseprogram": ZE + "Examples/FLASH/FLASH_EraseProgram",
    "gpio_exti": ZE + "Examples/GPIO/GPIO_EXTI",
    "gpio_iotoggle": ZE + "Examples/GPIO/GPIO_IOToggle",
    "hal_timebase_tim": ZE + "Examples/HAL/HAL_TimeBase_TIM",
    "iwdg_example": ZE + "Examples/IWDG/IWDG_Example",
    "rtc_calendar": ZE + "Examples/RTC/RTC_Calendar",
    "tim_dma": ZE + "Examples/TIM/TIM_DMA",
    "tim_inputcapture": ZE + "Examples/TIM/TIM_InputCapture",
    "tim_ocactive": ZE + "Examples/TIM/TIM_OCActive",
    "tim_octoggle": ZE + "Examples/TIM/TIM_OCToggle",
    "tim_onepulse": ZE + "Examples/TIM/TIM_OnePulse",
    "tim_pwminput": ZE + "Examples/TIM/TIM_PWMInput",
    "tim_pwmoutput": ZE + "Examples/TIM/TIM_PWMOutput",
    "uart_printf": ZE + "Examples/UART/UART_Printf",
    "usb_device_hid_standalone": ZE + "Applications/USB_Device/HID_Standalone",
    "wwdg_example": ZE + "Examples/WWDG/WWDG_Example",
}
# each board: its examples, their directory here, its project, the linker
# script its IDE projects carry, and the BSP checkout its examples include
BOARDS = {
    "nucleo_f446re": {"examples": EXAMPLES, "dir": HERE / "examples", "project": HERE / "project.json",
                      "ld": "STM32F446RETX_FLASH.ld", "cube": NUCLEO,
                      "bsp": "stm32f4xx-nucleo-bsp/stm32f4xx_nucleo.h"},
    "nucleo_f446ze": {"examples": EXAMPLES_ZE, "dir": HERE / "nucleo_f446ze",
                      "project": HERE / "nucleo_f446ze" / "project.json", "ld": "STM32F446ZETX_FLASH.ld",
                      "cube": ZE, "bsp": "stm32f4xx-nucleo-144-bsp/stm32f4xx_nucleo_144.h"},
}
BOARD = BOARDS["nucleo_f446re"]
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
        "Drivers/BSP/STM32F4xx_Nucleo_144": st / "stm32f4xx-nucleo-144-bsp",
        "Drivers/CMSIS/Include": st / "CMSIS_5" / "CMSIS" / "Core" / "Include",
        "Middlewares/ST/STM32_USB_Device_Library": st / "stm32-mw-usb-device",
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
    e = BOARD["examples"][name]
    return dict({"defines": [], "project": None, "two_boards": False}, **(e if isinstance(e, dict) else {"dir": e}))


def project_of(name: str) -> Path:
    """<name>/project.json, a variant's example's, the record's, or the
    board's"""
    for n in [name] + [k for k in BOARD["examples"] if record(k)["dir"] == record(name)["dir"]]:
        if (BOARD["dir"] / n / "project.json").exists():
            return BOARD["dir"] / n / "project.json"
    return HERE / record(name)["project"] if record(name)["project"] else BOARD["project"]


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
    sh(["arm-none-eabi-gcc", *objs, *MCU, "-T", ide / BOARD["ld"], *LDFLAGS,
        f"-Wl,-Map={out / (tag + '.map')}", "-o", elf])
    return elf


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--st", default=os.environ.get("STM32CUBE_F4"),
                    help="a directory holding cubef4, stm32f4xx_hal_driver, cmsis_device_f4, "
                         "stm32f4xx-nucleo-bsp and CMSIS_5")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--board", choices=sorted(BOARDS), default="nucleo_f446re")
    ap.add_argument("--keep", help="build here, and keep it, instead of a temporary directory")
    ap.add_argument("examples", nargs="*")
    args = ap.parse_args()
    if not shutil.which("arm-none-eabi-gcc") or not args.st:
        print(f"SKIP: {'needs arm-none-eabi-gcc' if args.st else 'no --st (the ST and CMSIS checkouts)'}")
        return 0
    global BOARD
    BOARD = BOARDS[args.board]
    st = Path(args.st).resolve()
    for need in ("cubef4/" + BOARD["cube"], "stm32f4xx_hal_driver/Inc/stm32f4xx_hal.h",
                 "cmsis_device_f4/Include/stm32f4xx.h", BOARD["bsp"],
                 "CMSIS_5/CMSIS/Core/Include/core_cm4.h"):
        if not (st / need).exists():
            print(f"error: {st} has no {need}")
            return 2
    names = args.examples or sorted(d.name for d in BOARD["dir"].iterdir() if d.is_dir())
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(args.keep).resolve() if args.keep else Path(tmp)
        work.mkdir(parents=True, exist_ok=True)
        bad = 0
        for name in names:
            scenario = BOARD["dir"] / name / "scenario.json"
            elf = build(st, name, work)
            extra = {"slave.elf": build(st, name, work, slave=True)} if record(name)["two_boards"] else None
            bad += not run_scenario(Path(args.embsim), project_of(name), name, elf, scenario, work, extra)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
