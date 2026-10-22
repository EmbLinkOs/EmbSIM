#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Arduino sketches built with Espressif's Arduino-ESP32 core, unmodified, on
EmbSIM's ESP32-C3-DevKitM-1 (the RTOS boards plan, part 2, step 7; 01 §8.2).

The core is not vendored. --cli names arduino-cli and --config its
configuration, in whose data directory the esp32 platform (3.2.0, on ESP-IDF
v5.4) is installed as the IDE's board manager installs it (`arduino-cli core
install esp32:esp32@3.2.0` with Espressif's package index). --examples names
a checkout of https://github.com/arduino/arduino-examples for the IDE's
built-in examples; the core's own come from its libraries.

Each example under examples/<name>/ is one of those sketches (EXAMPLES below
names where), compiled as the IDE compiles it for "ESP32C3 Dev Module"
(`arduino-cli compile --fqbn esp32:esp32:esp32c3`, the board's defaults:
UART0 as Serial, 4 MB flash in QIO at 80 MHz). The build's merged image
(bootloader, partition table, boot_app0 and the sketch at their offsets, the
rest erased) runs examples/<name>/project.json, or this directory's, under
the example's scenario.json, whose assertions must all pass.

    run.py --cli PATH --config FILE --examples DIR [--embsim PATH] [--keep DIR] [EXAMPLE...]

Without arduino-cli or the esp32 platform, it skips."""
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

FQBN = "esp32:esp32:esp32c3"
PLATFORM = "3.2.0"
# examples/<name>/ and its sketch: the IDE's built-in examples ("builtin",
# arduino-examples' examples/) or the core's ("core", its libraries/)
EXAMPLES = {
    "Blink": ("builtin", "01.Basics/Blink"),
    "DigitalReadSerial": ("builtin", "01.Basics/DigitalReadSerial"),
    "Fade": ("builtin", "01.Basics/Fade"),
    "GetChipID": ("core", "ESP32/examples/ChipID/GetChipID"),
    "RepeatTimer": ("core", "ESP32/examples/Timer/RepeatTimer"),
    "WiFiScan": ("core", "WiFi/examples/WiFiScan"),
    "WiFiClientConnect": ("core", "WiFi/examples/WiFiClientConnect"),
    "BLE_Server": ("core", "BLE/examples/Server"),
    "BLE_iBeacon": ("core", "BLE/examples/iBeacon"),
    "BasicHttpClient": ("core", "HTTPClient/examples/BasicHttpClient"),
}


def platform_dir(cli: str, config: str) -> Path:
    """The installed esp32 platform's directory, or None"""
    p = subprocess.run([cli, "--config-file", config, "config", "get", "directories.data"],
                       capture_output=True, text=True)
    if p.returncode:
        return None
    d = Path(p.stdout.strip()) / "packages" / "esp32" / "hardware" / "esp32" / PLATFORM
    return d if (d / "platform.txt").exists() else None


def build(cli: str, config: str, sketch: Path, out: Path) -> Path:
    """The sketch compiled as the IDE compiles it: its merged flash image"""
    work = out / sketch.name
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(sketch, work / sketch.name)
    sh([cli, "--config-file", config, "compile", "--fqbn", FQBN, "--output-dir", work / "out", work / sketch.name])
    return work / "out" / f"{sketch.name}.ino.merged.bin"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cli", default=shutil.which("arduino-cli"), help="arduino-cli")
    ap.add_argument("--config", help="arduino-cli's configuration file")
    ap.add_argument("--examples", default=os.environ.get("ARDUINO_EXAMPLES"),
                    help="a checkout of arduino/arduino-examples")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--keep", help="build here, and keep it, instead of a temporary directory")
    ap.add_argument("examples_", nargs="*", metavar="EXAMPLE")
    args = ap.parse_args()
    core = platform_dir(args.cli, args.config) if args.cli and args.config else None
    if not core or not args.examples:
        print(f"skip: arduino-cli with the esp32 platform {PLATFORM} (--cli, --config) and arduino-examples "
              "(--examples) are needed")
        return 0
    names = args.examples_ or sorted(EXAMPLES)
    unknown = [n for n in names if n not in EXAMPLES]
    if unknown:
        raise SystemExit(f"unknown example(s): {', '.join(unknown)}")
    tmp = None if args.keep else tempfile.TemporaryDirectory(prefix="embsim-arduino-esp32-")
    work = Path(args.keep or tmp.name)
    work.mkdir(parents=True, exist_ok=True)
    bad = 0
    try:
        for name in names:
            where, path = EXAMPLES[name]
            sketch = (Path(args.examples) / "examples" if where == "builtin" else core / "libraries") / path
            flash = build(args.cli, args.config, sketch, work / "build")
            mine = HERE / "examples" / name
            project = mine / "project.json" if (mine / "project.json").exists() else HERE / "project.json"
            bad += not run_scenario(Path(args.embsim), project, name, flash, mine / "scenario.json", work / "run",
                                    image_name="flash.bin")
    finally:
        if tmp:
            tmp.cleanup()
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
