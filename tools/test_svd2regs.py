# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""tools/importers/svd2regs.py on a small SVD written for the test."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "importers"))
import svd2regs  # noqa: E402

SVD = """<?xml version="1.0"?>
<device>
  <name>TOY</name>
  <size>32</size>
  <peripherals>
    <peripheral>
      <name>UART0</name>
      <baseAddress>0x40001000</baseAddress>
      <interrupt><name>UART0</name><value>3</value></interrupt>
      <registers>
        <register>
          <name>SR</name><addressOffset>0x0</addressOffset><resetValue>0xC0</resetValue>
          <fields>
            <field><name>RXNE</name><bitOffset>5</bitOffset><bitWidth>1</bitWidth>
              <access>read-write</access><modifiedWriteValues>zeroToClear</modifiedWriteValues></field>
            <field><name>TXE</name><bitRange>[7:7]</bitRange><access>read-only</access></field>
            <field><name>ERR</name><lsb>8</lsb><msb>9</msb><modifiedWriteValues>oneToClear</modifiedWriteValues></field>
          </fields>
        </register>
        <register><name>DR</name><addressOffset>0x4</addressOffset><size>16</size></register>
        <register><name>INT</name><addressOffset>0x8</addressOffset>
          <fields>
            <field><dim>2</dim><dimIncrement>0x1</dimIncrement><dimIndex>2-3</dimIndex>
              <name>CH%s_END</name><bitOffset>2</bitOffset><bitWidth>1</bitWidth></field>
          </fields>
        </register>
        <register><name>DR_ALT</name><addressOffset>0x4</addressOffset><alternateRegister>DR</alternateRegister></register>
        <register><name>BUF%s</name><addressOffset>0x10</addressOffset><dim>2</dim><dimIncrement>4</dimIncrement></register>
        <cluster><name>CH[%s]</name><addressOffset>0x20</addressOffset><dim>2</dim><dimIncrement>8</dimIncrement>
          <register><name>CTRL</name><addressOffset>0x0</addressOffset><access>write-only</access></register>
        </cluster>
      </registers>
    </peripheral>
    <peripheral derivedFrom="UART0">
      <name>UART1</name><baseAddress>0x40002000</baseAddress>
      <interrupt><name>UART1</name><value>4</value></interrupt>
    </peripheral>
  </peripherals>
</device>
"""


class Svd2Regs(unittest.TestCase):
    def test_convert(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "toy.svd"
            p.write_text(SVD)
            doc = svd2regs.convert(p, "toy/registers")
        self.assertEqual(doc["device"], "TOY")
        self.assertEqual(doc["interrupts"], {"UART0": 3, "UART1": 4})
        u0, u1 = doc["peripherals"]
        self.assertEqual(u1, {"name": "UART1", "base": "0x40002000", "interrupts": ["UART1"], "derived_from": "UART0"})
        regs = {r["name"]: r for r in u0["registers"]}
        self.assertEqual(sorted(regs), ["BUF0", "BUF1", "CH0_CTRL", "CH1_CTRL", "DR", "INT", "SR"])
        # a field array steps by dimIncrement bits
        self.assertEqual(regs["INT"]["fields"], [["CH2_END", 2, 1], ["CH3_END", 3, 1]])
        self.assertEqual(regs["SR"]["reset"], "0x000000C0")
        self.assertEqual(regs["SR"]["fields"], [["RXNE", 5, 1, "rc_w0"], ["TXE", 7, 1, "r"], ["ERR", 8, 2, "rc_w1"]])
        self.assertEqual(regs["DR"]["size"], 16)
        self.assertEqual(regs["BUF1"]["offset"], "0x14")
        self.assertEqual(regs["CH1_CTRL"], {"name": "CH1_CTRL", "offset": "0x28", "fields": [["CH1_CTRL", 0, 32, "w"]]})
        json.dumps(doc)

    def test_reset_corrections(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "toy.svd"
            p.write_text(SVD)
            doc = svd2regs.convert(p, "toy/registers")
        svd2regs.apply_resets(doc, ["UART0.DR=0x5A", "UART0.SR=0"])
        regs = {r["name"]: r for r in doc["peripherals"][0]["registers"]}
        self.assertEqual(regs["DR"]["reset"], "0x0000005A")
        self.assertNotIn("reset", regs["SR"])
        self.assertIn("UART0.DR=0x0000005A", doc["source"])
        # a derived peripheral shares its base's registers: correct the base
        with self.assertRaises(SystemExit):
            svd2regs.apply_resets(doc, ["UART1.DR=1"])
        with self.assertRaises(SystemExit):
            svd2regs.apply_resets(doc, ["UART0.NOPE=1"])

    def test_fields_and_merges(self):
        doc = {"source": "s", "interrupts": {}, "peripherals": [{"name": "RCC", "base": "0x0", "registers": [
            {"name": "PLLCFGR", "offset": "0x04", "fields": [["PLLM2", 2, 1], ["PLLM1", 1, 1], ["PLLM0", 0, 1],
                                                           ["PLLQ0", 24, 1], ["LAT", 8, 3]]}]}]}
        svd2regs.apply_merges(doc, ["RCC.PLLCFGR.PLLM"])
        svd2regs.apply_fields(doc, ["RCC.PLLCFGR=PLLR:28:3", "RCC.PLLCFGR=LATENCY:8:4"])
        f = doc["peripherals"][0]["registers"][0]["fields"]
        self.assertEqual(f, [["PLLR", 28, 3], ["PLLQ0", 24, 1], ["LATENCY", 8, 4], ["PLLM", 0, 3]])
        self.assertIn("RCC.PLLCFGR.PLLM", doc["source"])
        with self.assertRaises(SystemExit):
            svd2regs.apply_merges(doc, ["RCC.PLLCFGR.PLLN"])

    def test_added_register(self):
        doc = {"source": "s", "interrupts": {}, "peripherals": [{"name": "SYSCFG", "base": "0x0", "registers": [
            {"name": "CMPCR", "offset": "0x20", "fields": []}]}]}
        svd2regs.apply_registers(doc, ["SYSCFG.CFGR=0x2C"])
        svd2regs.apply_fields(doc, ["SYSCFG.CFGR=FMPI2C1_SCL:0:1", "SYSCFG.CFGR=FMPI2C1_SDA:1:1"])
        regs = doc["peripherals"][0]["registers"]
        self.assertEqual([r["name"] for r in regs], ["CMPCR", "CFGR"])
        self.assertEqual(regs[1]["offset"], "0x2C")
        self.assertEqual(regs[1]["fields"], [["FMPI2C1_SDA", 1, 1], ["FMPI2C1_SCL", 0, 1]])
        self.assertIn("registers added from the reference manual: SYSCFG.CFGR=0x2C", doc["source"])
        with self.assertRaises(SystemExit):
            svd2regs.apply_registers(doc, ["SYSCFG.CFGR=0x30"])

    def test_interrupt_reassignment(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "toy.svd"
            p.write_text(SVD)
            doc = svd2regs.convert(p, "toy/registers")
        svd2regs.apply_interrupts(doc, ["UART1=UART0,UART1", "UART0="])
        u0, u1 = doc["peripherals"]
        self.assertEqual(u1["interrupts"], ["UART0", "UART1"])
        self.assertNotIn("interrupts", u0)
        self.assertIn("UART1=UART0,UART1", doc["source"])
        with self.assertRaises(SystemExit):
            svd2regs.apply_interrupts(doc, ["UART0=NOPE"])

    def test_fill_and_drop(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "toy.svd"
            p.write_text(SVD)
            doc = svd2regs.convert(p, "toy/registers")
        doc["peripherals"].append({"name": "UART9", "base": "0x40009000",
                                   "registers": [{"name": "SR", "offset": "0x00", "fields": [["X", 0, 1]]}]})
        svd2regs.apply_fills(doc, ["UART9=UART0:one layout"])
        regs = {r["name"]: r for r in doc["peripherals"][-1]["registers"]}
        self.assertEqual(regs["SR"]["fields"], [["X", 0, 1]])  # its own kept
        self.assertIn("DR", regs)
        self.assertIn("UART9 from UART0", doc["source"])
        svd2regs.apply_drops(doc, ["UART9=a copy"])
        self.assertNotIn("UART9", [p["name"] for p in doc["peripherals"]])
        self.assertIn("UART9 (a copy)", doc["source"])

    def test_memory(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "toy.svd"
            p.write_text(SVD)
            doc = svd2regs.convert(p, "toy/registers")
        svd2regs.apply_memories(doc, ["UART0.RAM=0x400:3:its buffer"])
        regs = {r["name"]: r for r in doc["peripherals"][0]["registers"]}
        self.assertEqual([regs[f"RAM{i}"]["offset"] for i in range(3)], ["0x400", "0x404", "0x408"])
        self.assertEqual(regs["RAM2"]["fields"], [["DATA", 0, 32]])
        self.assertIn("UART0.RAM at 0x400, 3 words (its buffer)", doc["source"])
        with self.assertRaises(SystemExit):
            svd2regs.apply_memories(doc, ["UART0.RAM=0x404:2:again"])


if __name__ == "__main__":
    unittest.main()
