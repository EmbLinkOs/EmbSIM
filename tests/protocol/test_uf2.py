#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Programming the Pico the real way: BOOTSEL and a UF2 file (ADR-031).

A Pico running one program (examples/pico-dual-core) is plugged into a
computer's USB port with BOOTSEL held: its bootrom goes to its USB boot and
the computer sees the RPI-RP2 drive (VID 0x2E8A, PID 0x0003). BOOTSEL is
let go; examples/pico-blink-serial's program, made into a UF2 file as
elf2uf2 makes it (each loadable segment at its physical address in 256-byte
blocks of RP2040's family), is copied onto the drive as a file manager does
(the FAT and the directory, then the data). With the last block written the
Pico reboots and runs the new program, which prints its banner on UART0.
A UF2 file of another family is ignored, as the bootrom ignores it.

    test_uf2.py --embsim PATH
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

RP2040_FAMILY = 0xE48BFF56


def elf_to_uf2(elf: bytes, family: int = RP2040_FAMILY) -> bytes:
    """The loadable segments at their physical addresses, as elf2uf2 does."""
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
    pages = sorted({a & ~255 for a in image if 0x10000000 <= a < 0x11000000})
    out = bytearray()
    for n, page in enumerate(pages):
        data = bytes(image.get(page + k, 0) for k in range(256))
        block = struct.pack("<8I", 0x0A324655, 0x9E5D5157, 0x2000, page, 256, n, len(pages), family)
        block += data + bytes(476 - 256) + struct.pack("<I", 0x0AB16F30)
        assert len(block) == 512
        out += block
    return bytes(out)


def project(firmware: str, uf2: str) -> dict:
    return {
        "schema_version": 1, "kind": "project", "name": "pico-uf2", "policy": "electrical",
        "boards": [{"id": "B1", "board": "emblink/pico", "firmware": {"image": firmware, "format": "elf"}}],
        "components": [
            {"id": "PC", "component": "emblink/usb_host", "params": {"copy_file": uf2}},
            {"id": "G", "component": "emblink/gnd"},
        ],
        "nets": {
            "VBUS": ["PC.VBUS", "B1.VBUS"], "DP": ["PC.DP", "B1.USB_DP"], "DM": ["PC.DM", "B1.USB_DM"],
            "GND": ["PC.GND", "B1.GND", "G.GND"],
        },
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True)
    args = ap.parse_args()
    failures = 0

    def check(ok: bool, what: str) -> None:
        nonlocal failures
        print(("ok    " if ok else "FAIL  ") + what)
        failures += 0 if ok else 1

    with tempfile.TemporaryDirectory(prefix="embsim-uf2-") as tmp:
        t = Path(tmp)
        elf = (ROOT / "examples/pico-blink-serial/firmware/blink-serial.elf").read_bytes()
        (t / "blink.uf2").write_bytes(elf_to_uf2(elf))
        (t / "other.uf2").write_bytes(elf_to_uf2(elf, family=0xADA52840))  # an nRF52840's
        old = str(ROOT / "examples/pico-dual-core/firmware/dual-core.elf")
        for name, uf2 in (("blink", "blink.uf2"), ("other", "other.uf2")):
            src = t / f"{name}.json"
            src.write_text(json.dumps(project(old, uf2)), encoding="utf-8")
            with StdioClient([args.embsim, "sidecar", str(src)]) as c:
                now = [0]

                def step(ms: float) -> dict:
                    now[0] += int(ms * 1e9)
                    c.call("control.advance", {"until": str(now[0])})
                    return c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]

                c.call("input.set", {"target": "B1.SW_BOOTSEL", "input": "pressed", "value": True})
                out = step(600)
                host = out["PC.HOST"]
                if name == "blink":
                    check(host["state"] == "configured" and host["vid"] == 0x2E8A and host["pid"] == 0x0003,
                          f"BOOTSEL held: the computer sees {host['vid']:04x}:{host['pid']:04x} {host['product']!r}"
                          f" ({host['state']})")
                c.call("input.set", {"target": "B1.SW_BOOTSEL", "input": "pressed", "value": False})
                c.call("input.set", {"target": "PC.HOST", "input": "copy", "value": True})
                for _ in range(40):
                    out = step(250)
                    if out["PC.HOST"]["copy_result"]:
                        break
                host = out["PC.HOST"]
                log = host["log"]
                if name == "blink":
                    check(host["copy_result"] == "copied", f"the UF2 file copied: {host['copy_result']} "
                          f"{host['copy_bytes']} bytes {host['copy_error']}")
                    check('"RPI     " "RP2             "' in log and "262144 sectors" in log,
                          "the drive is the bootrom's: RPI RP2, 128 MiB")
                    out = step(300)
                    text = str(out["B1.U1"].get("serial.UART0", ""))
                    check("pico: RP2040" in text, f"the Pico rebooted into the new program: {text[:60]!r}")
                    diags = json.dumps(c.call("diagnostics.list", {}))
                    check("mcu.uf2_written" in diags and "mcu.usb_boot" in diags,
                          "the USB boot and the UF2 file are diagnostics")
                else:
                    out = step(300)
                    text = str(out["B1.U1"].get("serial.UART0", ""))
                    check(host["state"] == "configured" and "pico: RP2040" not in text,
                          f"another family's UF2 file is ignored: still in USB boot ({host['state']})")
    print("uf2: %s" % ("ok" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
