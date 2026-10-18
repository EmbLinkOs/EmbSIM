# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
import json
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / "catalog"))
import stimulus  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


class StimulusTest(unittest.TestCase):
    def test_matches_the_catalog_fixtures(self):
        # the SSD1306 and MAX7219 fixtures were made with these helpers
        oled = json.loads((ROOT / "catalog/components/ssd1306/tests/pixels.json").read_text())
        pg = next(c for c in oled["circuit"]["components"] if c["id"] == "PG")
        words = stimulus.i2c_words([(0x3C, 0, [0x00, 0x8D, 0x14, 0xAF]), (0x3C, 0, [0x00, 0x21, 0, 127, 0x22, 0, 7, 0x20, 0x00]),
                                    (0x3C, 0, [0x40, 0xFF, 0x81])])
        self.assertEqual(pg["params"]["pattern"], ",".join(f"{x:x}" for x in words))
        mx = json.loads((ROOT / "catalog/components/max7219_display/tests/digits.json").read_text())
        pg = next(c for c in mx["circuit"]["components"] if c["id"] == "PG")
        words = stimulus.spi_words([[0x0C, 0x01], [0x09, 0xFF], [0x0B, 0x07], [0x01, 0x05], [0x02, 0x04]])
        self.assertEqual(pg["params"]["pattern"], ",".join(f"{x:x}" for x in words))

    def test_i2c_shape(self):
        w = stimulus.i2c_words([(0x50, 0, [0xA5])])
        self.assertEqual(w[:4], [3, 3, 2, 0])          # idle, START
        self.assertEqual(w[-3:], [0, 2, 3])            # STOP
        self.assertEqual(len(w), 1 + 3 + 2 * (8 * 3 + 3) + 3)

    def test_tm1637_shape(self):
        w = stimulus.tm1637_words([[0x40]])
        self.assertEqual(w[:4], [3, 3, 2, 0])          # idle, START
        self.assertEqual(w[4:7], [0, 2, 0])            # bit 0 of 0x40 first
        self.assertEqual(w[-6:], [1, 3, 0, 0, 2, 3])   # the ACK clock, STOP
        self.assertEqual(len(w), 1 + 3 + 8 * 3 + 3 + 3)


if __name__ == "__main__":
    unittest.main()
