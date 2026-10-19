#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""tools/importers/stm32pins.py on a small MCU file written for the test,
in the shape of ST's open pin data."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "importers"))
import stm32pins  # noqa: E402

XML = """<?xml version="1.0" encoding="UTF-8"?>
<Mcu xmlns="http://dummy.com" Package="LQFP8" RefName="STM32TOYTx" Family="STM32F4">
  <Pin Name="VBAT" Position="1" Type="Power"/>
  <Pin Name="PA0-WKUP" Position="2" Type="I/O">
    <Signal Name="ADC1_IN0"/><Signal Name="ADC2_IN0"/><Signal Name="ADC3_IN0"/><Signal Name="TIM2_CH1"/>
    <Signal Name="GPIO" IOModes="Input,Output,Analog,EVENTOUT,EXTI"/>
  </Pin>
  <Pin Name="PC4" Position="3" Type="I/O"><Signal Name="ADC2_IN14"/><Signal Name="ADC1_IN14"/></Pin>
  <Pin Name="PB2" Position="4" Type="I/O"><Signal Name="GPIO"/></Pin>
  <Pin Name="NRST" Position="5" Type="Reset"/>
</Mcu>
"""


class Stm32Pins(unittest.TestCase):
    def test_package_and_analog(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "toy.xml"
            p.write_text(XML)
            attrs, pins = stm32pins.read(p)
        pkg = stm32pins.package_of(attrs, pins, "toy/part/lqfp8", "toy.xml")
        self.assertEqual(pkg["name"], "LQFP8")
        self.assertEqual(pkg["pins"][0], {"number": 1, "signal": "VBAT"})
        self.assertEqual(pkg["pins"][1], {"number": 2, "signal": "PA0", "doc": "PA0-WKUP"})
        self.assertEqual(pkg["pins"][4], {"number": 5, "signal": "NRST"})
        self.assertEqual(stm32pins.analog_of(pins), {"PA0": "ADC123_IN0", "PC4": "ADC12_IN14"})


if __name__ == "__main__":
    unittest.main()
