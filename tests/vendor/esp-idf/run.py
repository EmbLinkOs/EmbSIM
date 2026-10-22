#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""ESP-IDF's own examples for the ESP32-C3, built unmodified by ESP-IDF's
build system and run on EmbSIM's ESP32-C3-DevKitM-1 (the RTOS boards plan,
part 2, step 7; 01 §8.2).

ESP-IDF is not vendored. --idf names an ESP-IDF checkout (v5.4) whose tools
are installed, or IDF_PATH does; the riscv32-esp-elf toolchain must be on
PATH and ESP-IDF's Python environment the one running `idf.py` (its
export.sh sets both up). An example's managed components (blink's
led_strip) come from Espressif's component registry, as `idf.py` fetches
them.

Each example under examples/<name>/ is one of ESP-IDF's (EXAMPLES below
names its directory). It is copied out of the checkout and built as a user
builds it, `idf.py set-target esp32c3` then `idf.py build`, with its own
sdkconfig.defaults and, where a user would choose among its options for
this board, the lines of examples/<name>/sdkconfig.defaults (a choice made
in menuconfig, not a change to the example). The bootloader, the partition
table and the application are put into a 4 MB flash image at the offsets
the build's flasher_args.json gives, the rest erased (0xFF), as `idf.py
merge-bin` and a flasher leave it. The image then runs examples/<name>/
project.json, or this directory's, under the example's scenario.json, whose
assertions must all pass. A project with more boards names their examples
in COMPANIONS: they are built too, and their images put beside it under the
names the project gives (blecent's peripheral runs bleprph).

    run.py --idf DIR [--embsim PATH] [--keep DIR] [EXAMPLE...]

Without idf.py's tools (riscv32-esp-elf-gcc), or without --idf (or
IDF_PATH), it skips."""
import argparse
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from vendor_run import run_scenario, sh  # noqa: E402

# examples/<name>/ and its directory in ESP-IDF
EXAMPLES = {
    "adc_oneshot_read": "examples/peripherals/adc/oneshot_read",
    "bleprph": "examples/bluetooth/nimble/bleprph",
    "blecent": "examples/bluetooth/nimble/blecent",
    "ble_ibeacon": "examples/bluetooth/bluedroid/ble/ble_ibeacon",
    "blink": "examples/get-started/blink",
    "espnow": "examples/wifi/espnow",
    "gptimer": "examples/peripherals/timer_group/gptimer",
    "hello_world": "examples/get-started/hello_world",
    "i2c_eeprom": "examples/peripherals/i2c/i2c_eeprom",
    "ledc_basic": "examples/peripherals/ledc/ledc_basic",
    "real_time_stats": "examples/system/freertos/real_time_stats",
    "temp_sensor": "examples/peripherals/temperature_sensor/temp_sensor",
    "uart_echo": "examples/peripherals/uart/uart_echo",
    "wifi_scan": "examples/wifi/scan",
    "wifi_softap": "examples/wifi/getting_started/softAP",
    "wifi_station": "examples/wifi/getting_started/station",
    "wifi_station_open": "examples/wifi/getting_started/station",
}
# examples built only for another's project (with examples/<name>/'s
# sdkconfig.defaults), not run on their own
VARIANTS = {
    "ble_ibeacon_receiver": "examples/bluetooth/bluedroid/ble/ble_ibeacon",
}
# a project's other boards: their images' names and examples
COMPANIONS = {
    "blecent": {"bleprph.bin": "bleprph"},
    "ble_ibeacon": {"receiver.bin": "ble_ibeacon_receiver"},
    "espnow": {"peer.bin": "espnow"},
    "wifi_softap": {"station.bin": "wifi_station"},
}
FLASH_SIZE = 4 * 1024 * 1024


def build(idf: Path, name: str, out: Path) -> Path:
    """The example built as a user builds it: its flash image"""
    src = idf / (EXAMPLES.get(name) or VARIANTS[name])
    proj = out / name
    shutil.rmtree(proj, ignore_errors=True)
    shutil.copytree(src, proj, ignore=shutil.ignore_patterns("build", "managed_components", "sdkconfig"))
    defaults = [proj / "sdkconfig.defaults"] if (proj / "sdkconfig.defaults").exists() else []
    ours = HERE / "examples" / name / "sdkconfig.defaults"
    if ours.exists():
        defaults.append(ours)
    env = dict(os.environ, IDF_PATH=str(idf))
    cmd = [sys.executable, idf / "tools" / "idf.py", "-C", proj, "-B", proj / "build"]
    if defaults:
        cmd += ["-D", "SDKCONFIG_DEFAULTS=" + ";".join(str(d) for d in defaults)]
    sh(cmd + ["set-target", "esp32c3"], env=env)
    sh(cmd + ["build"], env=env)
    args = json.loads((proj / "build" / "flasher_args.json").read_text(encoding="utf-8"))
    image = bytearray(b"\xff" * FLASH_SIZE)
    for off, file in args["flash_files"].items():
        data = (proj / "build" / file).read_bytes()
        at = int(off, 16)
        image[at:at + len(data)] = data
    flash = out / f"{name}.bin"
    flash.write_bytes(bytes(image))
    return flash


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--idf", default=os.environ.get("IDF_PATH"), help="an ESP-IDF v5.4 checkout with its tools")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--keep", help="build here, and keep it, instead of a temporary directory")
    ap.add_argument("examples", nargs="*")
    args = ap.parse_args()
    if not args.idf or not shutil.which("riscv32-esp-elf-gcc"):
        print("skip: ESP-IDF (--idf or IDF_PATH) and its riscv32-esp-elf toolchain are needed")
        return 0
    idf = Path(args.idf).resolve()
    names = args.examples or sorted(EXAMPLES)
    unknown = [n for n in names if n not in EXAMPLES]
    if unknown:
        raise SystemExit(f"unknown example(s): {', '.join(unknown)}")
    tmp = None if args.keep else tempfile.TemporaryDirectory(prefix="embsim-idf-")
    work = Path(args.keep or tmp.name)
    work.mkdir(parents=True, exist_ok=True)
    bad = 0
    built = {}

    def image(name: str) -> Path:
        if name not in built:
            built[name] = build(idf, name, work / "build")
        return built[name]

    try:
        for name in names:
            flash = image(name)
            extra = {file: image(other) for file, other in COMPANIONS.get(name, {}).items()}
            mine = HERE / "examples" / name
            project = mine / "project.json" if (mine / "project.json").exists() else HERE / "project.json"
            bad += not run_scenario(Path(args.embsim), project, name, flash, mine / "scenario.json", work / "run",
                                    extra=extra, image_name="flash.bin")
    finally:
        if tmp:
            tmp.cleanup()
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
