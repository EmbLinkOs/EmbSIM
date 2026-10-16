# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The board and netlist importers on their test data: what they read, and
that their output passes the schema and semantic checks of validate.py."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "importers"))
import dts2board  # noqa: E402
import kicad2project  # noqa: E402
from boardgen import write_board  # noqa: E402

DATA = HERE / "importers" / "testdata"


def validate(path):
    return subprocess.run([sys.executable, str(HERE / "validate.py"), str(path)], capture_output=True, text=True)


class DtsTest(unittest.TestCase):
    def test_reads_the_board(self):
        m = dts2board.load(DATA / "f103_dev.dts")
        self.assertEqual(m.title, "F103 development board")
        self.assertEqual(m.mcu, "stm32f103X8.dtsi")
        self.assertEqual(m.oscillators, {"hse": 8000000})
        self.assertEqual(m.console, {"peripheral": "usart1", "tx": "PA9", "rx": "PA10", "baud": 115200})
        self.assertEqual([(l["name"], l["gpio"], l["active"]) for l in m.leds], [("blue_led", "PC13", "low"), ("led_1", "PB12", "high")])
        self.assertEqual([(b["name"], b["gpio"]) for b in m.buttons], [("user", "PA0")])
        self.assertEqual(m.buses["i2c1"], {"scl": "PB6", "sda": "PB7"})
        devs = {d.name: d for d in m.devices}
        self.assertEqual(devs["ssd1306"].address, 0x3C)
        self.assertEqual(devs["adc"].pins, {"cs": "PA4"})
        self.assertEqual(devs["dht22"].pins, {"data": "PA1"})

    def test_draft_validates(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_board(dts2board.load(DATA / "f103_dev.dts"), "imported", Path(tmp))
            board = json.loads((Path(tmp) / "boards/f103_dev/board.json").read_text())
            self.assertEqual(board["mcu"]["ref"], "st/stm32f103c8")
            ids = {c["id"]: c["component"] for c in board["components"]}
            self.assertEqual(ids["SSD1306"], "emblink/ssd1306")
            self.assertEqual(ids["RTC"], "emblink/ds1307")
            self.assertIn("U1.PB7", board["nets"]["PB7"])
            r = validate(tmp)
            self.assertEqual(r.returncode, 0, r.stdout)


class RtosTest(unittest.TestCase):
    def setUp(self):
        try:
            import yaml  # noqa: F401
        except ImportError:
            self.skipTest("PyYAML is not installed")

    def test_placeholder_for_an_unknown_device(self):
        import rtos2board
        m = rtos2board.load(DATA / "rtos_f103_board.yaml")
        self.assertEqual(m.name, "f103_sensor_board")
        self.assertEqual(m.oscillators, {"hse": 8000000, "lse": 32768})
        with tempfile.TemporaryDirectory() as tmp:
            write_board(m, "imported", Path(tmp))
            board = json.loads((Path(tmp) / "boards/f103_sensor_board/board.json").read_text())
            todo = [c for c in board["components"] if c.get("todo")]
            self.assertEqual([c["id"] for c in todo], ["IMU"])
            r = validate(tmp)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("catalog.todo", r.stdout)


class KicadTest(unittest.TestCase):
    def test_flasher(self):
        project, warnings = kicad2project.convert(DATA / "flasher.net", "electrical")
        parts = {c["id"]: c for c in project["components"]}
        self.assertEqual(parts["U2"]["component"], "emblink/timer_555")
        self.assertEqual(parts["U2"]["params"], {"mode": "astable", "r1": "1k", "r2": "10k", "c": "10u"})
        self.assertEqual(parts["BT1"]["params"], {"part": "9V"})
        self.assertEqual(parts["U1"]["params"], {"part": "7805"})
        self.assertEqual(parts["D1"]["params"], {"color": "red"})
        self.assertNotIn("U3", parts)
        self.assertTrue(any("U3" in w for w in warnings))
        self.assertIn("D1.A", project["nets"]["N_D1_A"])
        self.assertIn("C1.+", project["nets"]["THR"])
        # +5V is driven by the regulator: no supply symbol; GND gets one
        self.assertFalse(any(p.startswith("PWR") for p in project["nets"]["+5V"]))
        self.assertTrue(any(p.endswith(".GND") and p.startswith("PWR") for p in project["nets"]["GND"]))
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "project.json"
            out.write_text(json.dumps(project))
            r = validate(out)
            self.assertEqual(r.returncode, 0, r.stdout)

    def test_values(self):
        self.assertEqual(kicad2project.si_value("4k7"), 4700)
        self.assertAlmostEqual(kicad2project.si_value("2.2uF"), 2.2e-6)
        self.assertAlmostEqual(kicad2project.si_value("0R1"), 0.1)
        self.assertEqual(kicad2project.si_text(4700), "4.7k")
        self.assertIsNone(kicad2project.si_value("LED red"))


if __name__ == "__main__":
    unittest.main()


class EdcTest(unittest.TestCase):
    """edc2regs.py on a made-up PIC32's EDC and ATDF (testdata/mini.PIC)."""

    def test_converts(self):
        with tempfile.TemporaryDirectory() as tmp:
            out, pkg = Path(tmp) / "registers.json", Path(tmp) / "pkg.json"
            r = subprocess.run([sys.executable, str(HERE / "importers" / "edc2regs.py"), str(DATA / "mini.PIC"),
                                "--atdf", str(DATA / "mini.atdf"), "-o", str(out), "--package", str(pkg),
                                "--package-id", "test/pic32mxtest/pkg3", "--package-pins", "3"],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            d = json.loads(out.read_text())
            self.assertEqual(d["device"], "PIC32MXTEST")
            p = {x["name"]: x for x in d["peripherals"]}
            self.assertEqual(set(p), {"UART1", "CFG"})
            self.assertEqual(p["UART1"]["base"], "0x1F806000")
            mode, rx = p["UART1"]["registers"]
            self.assertTrue(mode["clr_set_inv"])
            self.assertNotIn("clr_set_inv", rx)
            self.assertEqual(rx["offset"], "0x30")
            # the datasheet mode's fields past their gap, and the implemented
            # bits no field names (9:1 and 13:11; 10 and 14 are not) as
            # fields of their own
            self.assertEqual([f[0] for f in mode["fields"]], ["STSEL", "U1MODE_9_1", "U1MODE_13_11", "ON"])
            self.assertIn(["ON", 15, 1], mode["fields"])
            self.assertEqual(rx["fields"], [["U1RXREG_8_0", 0, 9, "r"]])
            # DEVID: the ID with the latest revision, read-only
            devid = p["CFG"]["registers"][0]
            self.assertEqual(devid["reset"], "0x54307053")
            self.assertEqual(d["interrupts"], {"UART1_RX": 27, "UART1_TX": 28})
            self.assertEqual(d["vectors"], {"UART1_RX": 24, "UART1_TX": 24})
            self.assertEqual(d["pins"], {"CN15": "RD6", "U1RX": "RF2"})
            v = validate(out)
            self.assertEqual(v.returncode, 0, v.stdout + v.stderr)
            pk = json.loads(pkg.read_text())
            self.assertEqual([x["signal"] for x in pk["pins"]], ["RF2", "VDD", "RD6"])
