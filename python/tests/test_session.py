# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The Python client against a built embsim ($EMBSIM, or a build in the
repository): a Session drives the Blue Pill example (time, inputs, serial
text, probes, a scenario, a recording replayed and turned into a scenario,
coverage and the profile, the debugger), and run() and replay() keep the
command line's contract.

    EMBSIM=build/dev/bin/embsim python3 -m unittest discover -s python/tests
"""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))

import embsim  # noqa: E402
from embsim import EmbsimError, Session  # noqa: E402

EX = ROOT / "examples" / "bluepill-button-led"
PROJECT = EX / "project.json"
SCENARIO = EX / "scenarios" / "press.json"


class SessionTest(unittest.TestCase):
    def setUp(self):
        self.s = Session(PROJECT, cwd=ROOT)

    def tearDown(self):
        self.s.close()

    def test_time_serial_and_inputs(self):
        s = self.s
        self.assertIn("scenario.edit", s.capabilities)
        self.assertEqual(s.advance(until="20ms"), 20_000_000_000)
        self.assertEqual(s.now, 20_000_000_000)
        self.assertIn("Blue Pill", s.serial("B1"))
        self.assertIn("Blue Pill", s.serial("B1", "USART1"))
        self.assertEqual(s.output("B1.U1")["state"], "sleeping")
        s.press("SW1")
        s.advance(by="5ms")
        s.release("SW1")
        s.advance(by="5ms")
        text = s.serial("B1")
        self.assertIn("pressed", text)
        self.assertIn("released", text)
        with self.assertRaises(KeyError):
            s.serial("B9")
        with self.assertRaises(ValueError):
            s.advance()

    def test_probe(self):
        s = self.s
        p = s.probe("net:BTN")
        s.advance(until="5ms")
        s.press("SW1")
        s.advance(by="2ms")
        samples = s.read(p)
        # the pull-up holds BTN high until the press pulls it low
        self.assertIn(1, {v for _, v in samples})
        self.assertEqual(samples[-1][1], 0)

    def test_scenario_recording_and_replay(self):
        s = self.s
        report = s.run_scenario(SCENARIO)
        self.assertTrue(report["assertions"])
        self.assertTrue(all(a["ok"] for a in report["assertions"]), report["assertions"])
        s.reset()
        s.record()
        s.advance(until="5ms")
        s.press("SW1")
        s.advance(by="10ms")
        rec = s.stop_recording()["recording"]
        self.assertEqual(rec["kind"], "recording")
        self.assertTrue(rec["inputs"])
        sc = s.to_scenario(rec, name="from-python")
        self.assertEqual(sc["kind"], "scenario")
        self.assertEqual(sc["name"], "from-python")
        self.assertTrue(sc["assertions"])

    def test_interactive_recording_replays(self):
        # recorded from the start, from a running session, and after an input
        # given before recording: each replays identically
        for before in ("", "7ms", "7ms+press"):
            with Session(PROJECT, cwd=ROOT) as s:
                if before:
                    s.advance(until="7ms")
                if before.endswith("press"):
                    s.press("SW1")
                    s.advance(by="1ms")
                s.record()
                s.advance(by="2ms")
                s.press("SW1")
                s.advance(by="3ms")
                s.release("SW1")
                s.advance(by="3ms")
                rec = s.stop_recording()["recording"]
                r = s.replay(rec)
                self.assertTrue(r["ok"], (before, r.get("divergence")))
                self.assertEqual(r["compared"], len(rec["observations"]))
                # and as a scenario, run from reset, it passes
                report = s.run_scenario(s.to_scenario(rec))
                self.assertTrue(report["assertions"])
                self.assertFalse([a for a in report["assertions"] if not a["ok"]], before)

    def test_analysis_and_debugger(self):
        s = self.s
        with self.assertRaises(EmbsimError):
            s.coverage()
        s.analyze(stack=True)
        s.advance(until="20ms")
        cov = s.coverage()["B1"]
        self.assertGreater(cov["lines"]["hit"], 0)
        self.assertIn("SF:", s.coverage(format="lcov"))
        prof = s.profile("B1")["B1"]
        self.assertEqual(prof["functions"][0]["name"], "main")
        self.assertIn("Reset_Handler;main", s.profile(format="collapsed"))
        msp = s.stack("B1")["B1"]["stacks"][0]
        self.assertEqual(msp["top"], 0x20005000)
        self.assertGreater(msp["used"], 0)
        self.assertIn("main stack (MSP)", s.stack(format="text"))
        where = s.where("B1")
        self.assertIn("pc", where)
        regs = s.registers("B1")
        self.assertIn("pc", {k.lower() for k in regs})
        self.assertEqual(len(s.memory("B1", 0x08000000, 8)), 8)


class RunnerTest(unittest.TestCase):
    def test_run_and_replay(self):
        with tempfile.TemporaryDirectory() as tmp:
            rec, cov = Path(tmp) / "run.rec", Path(tmp) / "cov.info"
            r = embsim.run(PROJECT, scenario=SCENARIO, record=rec, coverage=cov, cwd=ROOT)
            self.assertTrue(r.ok, r.stderr)
            self.assertFalse(r.failed)
            self.assertIn("B1", r.coverage)
            self.assertIn("SF:", cov.read_text())
            back = embsim.replay(rec, cwd=ROOT)
            self.assertEqual(back.code, embsim.EXIT_OK, back.stderr)
            doc = json.loads(rec.read_text())
            doc["observations"][-2]["at_ps"] = str(int(doc["observations"][-2]["at_ps"]) + 1)
            rec.write_text(json.dumps(doc))
            bad = embsim.replay(rec, cwd=ROOT)
            self.assertEqual(bad.code, embsim.EXIT_DIVERGED)
            self.assertIn("divergence", bad.report)

    def test_usage_error(self):
        r = embsim.run(ROOT / "examples" / "button-led" / "project.json", coverage="x.info", cwd=ROOT)
        self.assertEqual(r.code, embsim.EXIT_USAGE)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--embsim":
        os.environ["EMBSIM"] = sys.argv.pop(2)
        sys.argv.pop(1)
    unittest.main()
