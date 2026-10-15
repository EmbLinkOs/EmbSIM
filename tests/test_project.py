import copy
import json
import os
import subprocess
import sys
import unittest

from embsim.kernel import ROOT
from embsim.project import Project, load, validate


class ProjectTests(unittest.TestCase):
    def setUp(self):
        self.data = load(ROOT / "examples/button-led.json")

    def project(self, data=None):
        project = Project(data or self.data)
        self.addCleanup(project.close)
        return project

    def test_scheduled_press_release_and_live_input(self):
        project = self.project()
        self.assertFalse(project.snapshot()["components"]["LED1"]["lit"])
        project.advance(10_000_000)
        self.assertTrue(project.snapshot()["components"]["LED1"]["lit"])
        project.advance(30_000_000)
        self.assertFalse(project.snapshot()["components"]["LED1"]["lit"])
        project.set_button("SW1", True)
        project.advance(30_000_000)
        self.assertTrue(project.snapshot()["components"]["LED1"]["lit"])

    def test_contending_buttons_and_floating_indicator(self):
        data = {"schema_version": 1, "name": "conflict", "nets": ["n"], "components": [
            {"id": "A", "kind": "button", "net": "n"},
            {"id": "B", "kind": "button", "net": "n", "active_level": 0},
            {"id": "L", "kind": "indicator", "net": "n"}]}
        project = self.project(data)
        self.assertEqual(project.snapshot()["nets"]["n"], "Z")
        project.set_button("A", True)
        project.set_button("B", True)
        project.advance(0)
        self.assertEqual(project.snapshot()["nets"]["n"], "X")
        self.assertFalse(project.snapshot()["components"]["L"]["lit"])

    def test_schema_rejects_unsupported_or_ambiguous_data(self):
        mutations = [lambda d: d.update(schema_version=2),
                     lambda d: d.update(firmware="pretend.elf"),
                     lambda d: d["nets"].append("signal"),
                     lambda d: d["components"][0].update(net="missing"),
                     lambda d: d["components"][0].update(kind="transistor"),
                     lambda d: d["components"][0].update(active_level=True),
                     lambda d: d["events"][0].update(pressed="true"),
                     lambda d: d["events"][0].update(component="LED1"),
                     lambda d: d["events"][0].update(at_ns=-1)]
        for mutate in mutations:
            data = copy.deepcopy(self.data)
            mutate(data)
            with self.subTest(data=data), self.assertRaises(ValueError):
                validate(data)

    def test_repeated_runs_match_and_cli_uses_same_runtime(self):
        project = self.project()
        project.advance(20_000_000)
        expected = project.snapshot()
        another = self.project()
        another.advance(20_000_000)
        self.assertEqual(expected, another.snapshot())
        command = [sys.executable, "-m", "embsim", "run", "examples/button-led.json", "--until-ns", "20000000"]
        result = subprocess.run(command, cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(ROOT / "python")), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), expected)
        command[-1] = "-1"
        result = subprocess.run(command, cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(ROOT / "python")), capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("unsigned", result.stderr)
