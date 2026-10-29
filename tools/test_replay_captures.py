# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""tools/verify/sigrok.py and replay_captures.py's AM230x decoder on a real
AM2302 capture (tests/captures/am2302) and on made-up edges."""
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "verify"))
import replay_captures as rc  # noqa: E402
from sigrok import read_sr  # noqa: E402

CAPTURE = HERE.parent / "tests" / "captures" / "am2302" / "am2302_1mhz.sr"
US = rc.US


class SigrokTest(unittest.TestCase):
    def test_reads_the_session(self):
        c = read_sr(CAPTURE)
        self.assertEqual(c.samplerate, 1e6)
        self.assertEqual(c.names[0], "SDA")
        e = c.edges("SDA")
        self.assertEqual(e[0], (0, 1))  # the line idles high
        self.assertTrue(all(e[i][0] < e[i + 1][0] for i in range(len(e) - 1)))
        self.assertTrue(all(e[i][1] != e[i + 1][1] for i in range(len(e) - 1)))


class Am230xTest(unittest.TestCase):
    def test_decodes_the_real_read(self):
        r = rc.am230x_reads(read_sr(CAPTURE).edges(0))
        self.assertEqual(len(r), 1)
        self.assertTrue(r[0]["answered"])
        self.assertEqual(r[0]["bytes"], [2, 209, 0, 238, 193])  # 72.1 %RH, 23.8 C
        self.assertEqual(rc.am230x_values(r[0]["bytes"]), (72.1, 23.8))
        self.assertTrue(rc.checksum_ok(r[0]["bytes"]))
        self.assertAlmostEqual(r[0]["start"] / US, 966, delta=1)

    def test_a_start_without_an_answer(self):
        # high, a 300 us start, then the line stays high
        edges = [(0, 1), (1000 * US, 0), (1300 * US, 1), (3000 * US, 0)]
        r = rc.am230x_reads(edges)
        self.assertEqual(len(r), 1)
        self.assertFalse(r[0]["answered"])

    def test_waveform_replays_the_starts(self):
        reads = [{"at": 10 * US, "start": 1000 * US}, {"at": 5000 * US, "start": 300 * US}]
        w = rc.am230x_waveform(reads, 1_000_000 * US, 2_000_000 * US)
        self.assertEqual(w.split(","), ["1@1000010.000us", "0@1000.000us", "1@3990.000us", "0@300.000us",
                                        "1@994700.000us"])

    def test_negative_temperature(self):
        self.assertEqual(rc.am230x_values([0x01, 0x90, 0x80, 0x65, 0]), (40.0, -10.1))


if __name__ == "__main__":
    unittest.main()
