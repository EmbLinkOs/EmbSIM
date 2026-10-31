# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""tools/importers/dwc2_regs.py: the OTG FS block as RM0390 30.15 lays it
out, and the F446's register descriptions holding it."""
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "importers"))
import dwc2_regs  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def offsets(block):
    return {r["name"]: int(r["offset"], 16) for r in block["registers"]}


class Dwc2RegsTest(unittest.TestCase):
    def test_layout(self):
        b = dwc2_regs.block()
        off = offsets(b)
        self.assertEqual(b["name"], "OTGFS")
        self.assertEqual(b["base"], "0x50000000")
        for name, at in [("GRXSTSP", 0x20), ("GCCFG", 0x38), ("GSNPSID", 0x40), ("DIEPTXF5", 0x114),
                         ("HPRT", 0x440), ("HCTSIZ11", 0x670), ("DCFG", 0x800), ("DIEPEMPMSK", 0x834),
                         ("DIEPCTL5", 0x9A0), ("DTXFSTS5", 0x9B8), ("DOEPTSIZ5", 0xBB0), ("PCGCCTL", 0xE00)]:
            self.assertEqual(off[name], at, name)
        ats = sorted(off.values())
        self.assertEqual(len(ats), len(set(ats)), "two registers at one offset")
        fields = {r["name"]: {f[0]: f for f in r["fields"]} for r in b["registers"]}
        self.assertEqual(fields["GCCFG"]["VBDEN"][1:3], [21, 1])
        self.assertEqual(fields["GINTSTS"]["USBRST"], ["USBRST", 12, 1, "rc_w1"])
        self.assertEqual(b["interrupts"][0], "OTG_FS")

    def test_catalog_has_it(self):
        for mcu in ("stm32f446re", "stm32f446ze"):
            with open(ROOT / "catalog" / "mcus" / "st" / mcu / "registers.json", encoding="utf-8") as f:
                d = json.load(f)
            names = [p["name"] for p in d["peripherals"]]
            self.assertIn("OTGFS", names)
            self.assertFalse([n for n in names if n.startswith("OTG_FS")])
            got = next(p for p in d["peripherals"] if p["name"] == "OTGFS")
            self.assertEqual(got, json.loads(json.dumps(dwc2_regs.block())))


if __name__ == "__main__":
    unittest.main()
