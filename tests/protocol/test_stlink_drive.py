#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The NUCLEO-F446RE's ST-LINK drive: programming the board by copying a file.

A blank NUCLEO (its image not built yet) is plugged into a computer's USB
port on CN1. The computer enumerates the ST-LINK/V2-1's composite device
(0483:374B, "STM32 STLink": its debug interface, the NODE_F446RE drive, the
virtual COM port) and mounts the drive. examples/nucleo-f446re-blink-serial's
program, made into a .bin as `objcopy -O binary` makes it, is copied onto the
drive as a file manager does. The probe programs it through the STM32's
flash interface (sectors erased, words programmed at the flash's times),
resets the part and comes back on the bus with the file gone; the program
runs: its banner on the virtual COM port's USART2, LD2 blinking.

    test_stlink_drive.py --embsim PATH
"""
import argparse
import json
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402

EXAMPLE = ROOT / "examples" / "nucleo-f446re-blink-serial"
ELF = EXAMPLE / "firmware" / "blink-serial.elf"
failures = 0


def check(ok: bool, what: str) -> None:
    global failures
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures += 1


def elf_to_bin(elf: bytes, base: int = 0x08000000) -> bytes:
    """the loadable segments at their physical addresses from `base`, as objcopy -O binary does"""
    assert elf[:4] == b"\x7fELF" and elf[4] == 1, "a 32-bit ELF"
    phoff, = struct.unpack_from("<I", elf, 28)
    phentsize, phnum = struct.unpack_from("<HH", elf, 42)
    image: dict[int, int] = {}
    for i in range(phnum):
        p_type, p_offset, _vaddr, p_paddr, p_filesz = struct.unpack_from("<IIIII", elf, phoff + i * phentsize)
        if p_type != 1 or not p_filesz:
            continue
        for k in range(p_filesz):
            image[p_paddr + k] = elf[p_offset + k]
    top = max(a for a in image if a >= base) + 1
    return bytes(image.get(a, 0) for a in range(base, top))


def project(tmp: Path, binfile: str) -> Path:
    p = json.loads((EXAMPLE / "project.json").read_text(encoding="utf-8"))
    for b in p["boards"]:
        b["firmware"]["image"] = "not-built.elf"
    p["probes"] = [q for q in p.get("probes", []) if q["target"].split(":")[0] not in ("var", "reg", "mem")]
    p["components"] = p.get("components", []) + [
        {"id": "PC", "component": "emblink/usb_host", "params": {"copy_file": binfile}}]
    nets = p.setdefault("nets", {})
    nets["CN1_VBUS"] = ["PC.VBUS", "B1.USB_VBUS"]
    nets["CN1_DP"] = ["PC.DP", "B1.USB_DP"]
    nets["CN1_DM"] = ["PC.DM", "B1.USB_DM"]
    gnd = next((k for k, v in nets.items() if any(x.endswith(".GND") for x in v)), None)
    if gnd:
        nets[gnd].append("PC.GND")
    else:
        nets["GND_PC"] = ["PC.GND", "B1.GND"]
    out = tmp / "drive.json"
    out.write_text(json.dumps(p), encoding="utf-8")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-stlink-drive-") as tmp:
        t = Path(tmp)
        image = elf_to_bin(ELF.read_bytes())
        (t / "blink.bin").write_bytes(image)
        with StdioClient([args.embsim, "sidecar", str(project(t, "blink.bin"))]) as c:
            now = [0]

            def step(ms: float) -> dict:
                now[0] += int(ms * 1e9)
                c.call("control.advance", {"until": str(now[0])})
                return c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]

            out = step(800)
            host, u2 = out["PC.HOST"], out["B1.U2"]
            check(host["state"] == "configured" and host["vid"] == 0x0483 and host["pid"] == 0x374B,
                  f"the computer sees {host['vid']:04x}:{host['pid']:04x} {host['product']!r} ({host['state']})")
            check(u2["usb_configured"], "the ST-LINK's composite device is configured")
            c.call("input.set", {"target": "PC.HOST", "input": "copy", "value": True})
            for _ in range(60):
                out = step(250)
                if out["PC.HOST"]["copy_result"]:
                    break
            host = out["PC.HOST"]
            check(host["copy_result"] == "copied", f"blink.bin ({len(image)} bytes) copied onto NODE_F446RE: "
                  f"{host['copy_result']} {host.get('copy_error', '')}")
            for _ in range(40):
                out = step(250)
                if out["B1.U2"]["drive"] != "programming":
                    break
            u2 = out["B1.U2"]
            check(u2["drive"] == "programmed" and u2["drive_programmed"] == (len(image) + 3) // 4 * 4,
                  f"the probe programmed {u2['drive_programmed']} bytes ({u2['drive']}{', ' + u2['drive_fail'].strip() if u2['drive_fail'] else ''})")
            for _ in range(20):
                out = step(250)
                if "nucleo-f446re" in out["B1.U5"].get("serial.USART2", ""):
                    break
            check("nucleo-f446re: STM32F446RE" in out["B1.U5"].get("serial.USART2", ""),
                  f"the program runs: {out['B1.U5'].get('serial.USART2', '').strip()!r}")
            out = step(1000)
            check(out["PC.HOST"]["state"] == "configured" and out["B1.U2"]["usb_configured"],
                  "the probe came back on the bus and is configured again")
    print("stlink drive: %s" % ("ok" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
