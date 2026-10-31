# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""tools/verify/sigrok.py and replay_captures.py's AM230x and I2C decoders on
real captures (tests/captures/am2302, ds3231) and on made-up edges."""
import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "verify"))
import replay_captures as rc  # noqa: E402
from sigrok import read_sr  # noqa: E402

CAPTURE = HERE.parent / "tests" / "captures" / "am2302" / "am2302_1mhz.sr"
DS3231 = HERE.parent / "tests" / "captures" / "ds3231" / "ds3231_ex2.sr"
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


    def test_old_metadata_with_indented_keys(self):
        # sigrok 0.2 wrote " trigger2 = 0" indented, which configparser takes as a continuation
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("metadata", "[global]\nsigrok version = 0.2.0\n[device 1]\ncapturefile = logic-1\n"
                                   "unitsize = 1\ntotal probes = 2\nsamplerate = 200 kHz\nprobe1 = SCL\n"
                                   "probe2 = SDA\n trigger2 = 0\n")
            z.writestr("logic-1", bytes([3, 1, 3]))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "old.sr"
            path.write_bytes(buf.getvalue())
            c = read_sr(path)
        self.assertEqual(c.names[:2], ["SCL", "SDA"])
        self.assertEqual(c.samplerate, 200e3)
        self.assertEqual(c.edges("SDA"), [(0, 1), (5_000_000, 0), (10_000_000, 1)])


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


def i2c_edges(frames, half=5 * US):
    """SCL and SDA edges for transactions given as (START, bytes with each 9th bit's level)."""
    scl, sda, t = [(0, 1)], [(0, 1)], 10 * US
    for bits in frames:
        sda.append((t, 0))  # START
        t += half
        for b in bits:
            scl.append((t, 0))
            if b != sda[-1][1]:
                sda.append((t + US, b))
            t += half
            scl.append((t, 1))
            t += half
        scl.append((t, 0))
        if sda[-1][1] != 0:
            sda.append((t + US, 0))
        t += half
        scl.append((t, 1))
        t += half
        sda.append((t, 1))  # STOP
        t += 4 * half
    return scl, sda


def byte_bits(v, ack):
    return [(v >> (7 - i)) & 1 for i in range(8)] + [0 if ack else 1]


class I2cTest(unittest.TestCase):
    def test_owners_of_a_write_and_a_read(self):
        scl, sda = i2c_edges([byte_bits(0xA0, True) + byte_bits(0x12, True),
                              byte_bits(0xA1, True) + byte_bits(0x5A, False)])
        bits, conds = rc.i2c_bits(scl, sda)
        self.assertEqual([c for _, c in conds], ["start", "stop", "start", "stop"])
        owner, txs = rc.i2c_transactions(bits, conds, {0x50})
        self.assertEqual([(t["address"], t["read"], t["bytes"]) for t in txs],
                         [(0x50, False, [(0x12, True)]), (0x50, True, [(0x5A, False)])])
        parts = [i for i, o in enumerate(owner) if o == "part"]
        w, r = txs[0]["bits"], txs[1]["bits"]
        # the part: both ACKs of the write, the read's address ACK and its eight data bits
        self.assertEqual(parts, [w[8], w[17], r[8]] + r[9:17])

    def test_another_address_is_played_back(self):
        scl, sda = i2c_edges([byte_bits(0xD0, True) + byte_bits(0x00, True)])
        bits, conds = rc.i2c_bits(scl, sda)
        owner, txs = rc.i2c_transactions(bits, conds, {0x50})
        self.assertEqual(txs[0]["address"], 0x68)
        self.assertTrue(all(o == "host" for o in owner))

    def test_the_waveform_releases_the_parts_slots(self):
        scl, sda = i2c_edges([byte_bits(0xA0, True)])
        bits, conds = rc.i2c_bits(scl, sda)
        owner, _ = rc.i2c_transactions(bits, conds, {0x50})
        steps = [tok.split("@") for tok in rc.i2c_waveform(scl, sda, bits, owner, 0, 300 * US).split(",")]
        t, level = 0, []
        for w, d in steps:
            level.append((t, int(w, 16)))
            t += int(d[:-2])
        ack_rise = bits[8]["rise"]
        held = [w for x, w in level if x <= ack_rise][-1]
        self.assertEqual(held, 3)  # SCL high, SDA released for the part's ACK
        self.assertEqual(t, 300 * US)

    def test_preload_from_reads_before_the_first_write(self):
        txs = [{"address": 0x50, "read": False, "bytes": [(0x10, True)]},
               {"address": 0x50, "read": True, "bytes": [(0xAB, True), (0xCD, False)]},
               {"address": 0x50, "read": False, "bytes": [(0x10, True), (0x00, True)]},
               {"address": 0x50, "read": True, "bytes": [(0x00, False)]}]
        self.assertEqual(rc.i2c_preload(txs, 128), "FF" * 16 + "ABCD")

    def test_decodes_the_real_ds3231(self):
        cap = read_sr(DS3231)
        bits, conds = rc.i2c_bits(cap.edges("SCL"), cap.edges("SDA"))
        _, txs = rc.i2c_transactions(bits, conds, {0x68})
        time = next(t for t in txs if t["read"] and len(t["bytes"]) == 7)
        # 13:56:00, day 1, 07/09/20, as README.sigrok's setup left it
        self.assertEqual([v for v, _ in time["bytes"]], [0x00, 0x56, 0x13, 0x01, 0x07, 0x09, 0x20])
        self.assertEqual([a for _, a in time["bytes"]], [True] * 6 + [False])


if __name__ == "__main__":
    unittest.main()
