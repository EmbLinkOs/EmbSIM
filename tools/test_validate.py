#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Unit tests for tools/validate.py: valid and invalid fixtures written to
temporary directories, exercised with both the jsonschema backend (when
installed) and the built-in checker."""
import copy
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import validate  # noqa: E402

SYMBOL = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 20"><style>.s{stroke:var(--symbol-stroke,#cdd6e4)}</style>'
          '<line class="s" x1="0" y1="10" x2="10" y2="10"/><line class="s" x1="60" y1="10" x2="50" y2="10"/></svg>')
ARTWORK = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><circle id="cap" cx="5" cy="5" r="4"/></svg>'

COMPONENT = {
    "schema_version": 1, "kind": "component", "id": "testlib/widget", "version": "1.0.0", "name": "Widget",
    "category": "passives", "description": "A test part.",
    "pins": [{"name": "1", "number": 1, "model": {"kind": "passive"}}, {"name": "2", "number": 2, "model": {"kind": "passive"}, "aliases": ["B"]}],
    "params": {"resistance": {"type": "number", "unit": "ohm", "default": "4.7k", "min": "1m", "max": "1G"},
               "mode": {"type": "enum", "enum": ["a", "b"], "default": "a"}},
    "inputs": [{"name": "pressed", "type": "boolean", "default": False}],
    "outputs": [{"name": "closed", "type": "boolean"}],
    "model": {"primitive": "resistor", "bind": {"r": "resistance"}},
    "functional": {"primitive": "wire"},
    "symbol": {"svg": "symbol.svg", "width": 60, "height": 20, "pins": {"1": {"x": 0, "y": 10, "side": "left"}, "2": {"x": 60, "y": 10, "side": "right"}}},
    "artwork": {"svg": "artwork.svg", "hotspots": [{"input": "pressed", "selector": "#cap", "gesture": "hold"}],
                "states": [{"output": "closed", "selector": "#cap", "attr": "opacity", "true": "1", "false": "0"}]},
    "fidelity": {"functional": {"claims": "A wire.", "omissions": []}, "timed": None,
                 "electrical": {"claims": "Linear r.", "omissions": ["noise"], "range": {"current_a": [-1, 1]}, "validation": ["tests/*.json"]}},
    "provenance": {"author": "test", "license": "Apache-2.0", "sources": []},
    "tests": ["tests/*.json"],
}
FIXTURE = {
    "schema_version": 1, "kind": "component_test", "name": "basic", "policy": "electrical", "until": "1ms",
    "circuit": {"components": [{"id": "DUT", "component": "testlib/widget", "params": {"resistance": "1k"}},
                               {"id": "V1", "component": "emblink/vsource", "params": {"kind": "dc", "dc": 3.3}},
                               {"id": "G", "component": "emblink/gnd"}],
                "nets": {"A": ["V1.+", "DUT.1"], "GND": ["V1.-", "DUT.2", "G.GND"]}},
    "inputs": [{"at": "0.1ms", "target": "DUT", "input": "pressed", "value": True}],
    "expect": {"device:DUT.i": {"at": "0.5ms", "min": 0.003, "max": 0.0034}},
}
PROJECT = {
    "schema_version": 1, "kind": "project", "name": "p", "policy": "functional",
    "components": [{"id": "V", "component": "emblink/vcc"}, {"id": "SW1", "component": "emblink/push_button"},
                   {"id": "R1", "component": "emblink/pull_down"}, {"id": "D1", "component": "emblink/indicator"}, {"id": "G", "component": "emblink/gnd"}],
    "nets": {"VCC": ["V.VCC", "SW1.1"], "BTN": ["SW1.2", "R1.1", "D1.A"], "GND": ["D1.K", "G.GND"]},
    "probes": [{"id": "btn", "target": "net:BTN", "kind": "digital"}],
    "scenarios": ["scenarios/press.json"],
}
SCENARIO = {
    "schema_version": 1, "kind": "scenario", "name": "press", "until": "50ms",
    "inputs": [{"at": "10ms", "target": "SW1", "input": "pressed", "value": True}],
    "assertions": [{"id": "high", "at": "12ms", "probe": "btn", "expect": {"logic": "1"}},
                   {"id": "clean", "always": True, "diagnostics": {"forbid": ["net.contention"]}}],
}


class Base(unittest.TestCase):
    backend = "jsonschema"

    @classmethod
    def setUpClass(cls):
        cls.schemas = validate.Schemas()
        if cls.backend == "builtin":
            cls.schemas._jsonschema = None
        cls.index = validate.Index([validate.CATALOG_DIR])

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="embsim-validate-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def write_component(self, desc=COMPONENT, name="widget", lib="testlib", symbol=SYMBOL, artwork=ARTWORK, fixture=FIXTURE):
        libdir = self.tmp / lib
        (libdir / "components" / name / "tests").mkdir(parents=True, exist_ok=True)
        (libdir / "library.json").write_text(json.dumps({"schema_version": 1, "kind": "library", "name": lib, "version": "0.1.0", "license": "Apache-2.0"}))
        d = libdir / "components" / name
        (d / "component.json").write_text(json.dumps(desc))
        if symbol is not None:
            (d / "symbol.svg").write_text(symbol)
        if artwork is not None:
            (d / "artwork.svg").write_text(artwork)
        if fixture is not None:
            (d / "tests" / "basic.json").write_text(json.dumps(fixture))
        return d / "component.json"

    def check(self, path, index=None):
        report = validate.Report()
        validate.validate_file(Path(path), self.schemas, index or self.index, report)
        return report

    def check_doc(self, doc, name="registers.json"):
        path = self.tmp / name
        path.write_text(json.dumps(doc))
        return self.check(path)

    def codes(self, report):
        return [f.code for f in report.findings if f.severity == "error"]

    def assertClean(self, report):
        self.assertEqual(self.codes(report), [], "\n".join(f.line() for f in report.findings))

    def assertCode(self, report, code):
        self.assertIn(code, self.codes(report), "\n".join(f.line() for f in report.findings))


class ComponentTests(Base):
    def test_valid_component_passes(self):
        path = self.write_component()
        idx = self.index.with_extra([self.tmp / "testlib"])
        self.assertClean(self.check(path, idx))
        self.assertClean(self.check(path.parent / "tests" / "basic.json", idx))

    def test_shared_model_must_stay_identical(self):
        desc = copy.deepcopy(COMPONENT)
        desc["model"] = {"script": "model.lua", "api": 1}
        del desc["functional"]
        script = "-- Shared by: widget, twin\nlocal M = {}\nreturn M\n"
        path = self.write_component(desc)
        (path.parent / "model.lua").write_text(script, encoding="utf-8")
        twin = self.write_component(dict(desc, id="testlib/twin"), name="twin")
        (twin.parent / "model.lua").write_text(script, encoding="utf-8")
        self.assertNotIn("catalog.shared_model_differs", self.codes(self.check(path)))
        (twin.parent / "model.lua").write_text(script + "-- changed\n", encoding="utf-8")
        self.assertCode(self.check(path), "catalog.shared_model_differs")

    def test_fixture_input_on_a_composite_member(self):
        fixture = {"schema_version": 1, "kind": "component_test", "name": "member", "policy": "electrical", "until": "1ms",
                   "circuit": {"components": [{"id": "KP", "component": "emblink/keypad_4x4"}], "nets": {"R": ["KP.R1"]}},
                   "inputs": [{"at": "0", "target": "KP.k5", "input": "pressed", "value": True}],
                   "expect": {"net:R": {"at": "1ms", "min_v": 0}}}
        path = self.tmp / "member.json"
        path.write_text(json.dumps(fixture), encoding="utf-8")
        self.assertNotIn("catalog.not_found", self.codes(self.check(path)))
        fixture["inputs"][0]["target"] = "KP.nope"
        path.write_text(json.dumps(fixture), encoding="utf-8")
        self.assertCode(self.check(path), "catalog.not_found")

    def test_fixture_expects_a_composite_members_output(self):
        fixture = {"schema_version": 1, "kind": "component_test", "name": "member-output", "policy": "electrical", "until": "1ms",
                   "circuit": {"components": [{"id": "KP", "component": "emblink/keypad_4x4"}], "nets": {"R": ["KP.R1"]}},
                   "expect": {"output:KP.k5.closed": {"at": "1ms", "equals": False}}}
        path = self.tmp / "member-output.json"
        path.write_text(json.dumps(fixture), encoding="utf-8")
        self.assertNotIn("catalog.output_unknown", self.codes(self.check(path)))
        fixture["expect"] = {"output:KP.k5.nope": {"at": "1ms", "equals": False}}
        path.write_text(json.dumps(fixture), encoding="utf-8")
        self.assertCode(self.check(path), "catalog.output_unknown")

    def test_unknown_field_rejected(self):
        desc = copy.deepcopy(COMPONENT)
        desc["colour"] = "red"
        self.assertCode(self.check(self.write_component(desc)), "schema.unknown_field")

    def test_missing_required_field(self):
        desc = copy.deepcopy(COMPONENT)
        del desc["fidelity"]
        self.assertCode(self.check(self.write_component(desc)), "schema.missing_field")

    def test_needs_model_or_functional(self):
        desc = copy.deepcopy(COMPONENT)
        del desc["model"]
        del desc["functional"]
        r = self.check(self.write_component(desc))
        self.assertTrue(self.codes(r))

    def test_duplicate_pin_name_and_number(self):
        desc = copy.deepcopy(COMPONENT)
        desc["pins"][1]["name"] = "1"
        desc["pins"][1]["number"] = 1
        desc["symbol"]["pins"] = {"1": {"x": 0, "y": 10, "side": "left"}}
        r = self.check(self.write_component(desc))
        self.assertCode(r, "catalog.pin_duplicate")

    def test_alias_clash(self):
        desc = copy.deepcopy(COMPONENT)
        desc["pins"][1]["aliases"] = ["1"]
        self.assertCode(self.check(self.write_component(desc)), "catalog.pin_duplicate")

    def test_symbol_pin_unknown_and_missing(self):
        desc = copy.deepcopy(COMPONENT)
        desc["symbol"]["pins"] = {"1": {"x": 0, "y": 10, "side": "left"}, "Z": {"x": 60, "y": 10, "side": "right"}}
        r = self.check(self.write_component(desc))
        self.assertCode(r, "catalog.pin_unknown")
        self.assertCode(r, "symbol.pin_missing")

    def test_bad_pin_model_kind(self):
        desc = copy.deepcopy(COMPONENT)
        desc["pins"][0]["model"] = {"kind": "wobbly"}
        codes = self.codes(self.check(self.write_component(desc)))
        self.assertTrue(codes and set(codes) <= {"schema.type", "schema.missing_field"}, codes)

    def test_bad_param_type_enum(self):
        desc = copy.deepcopy(COMPONENT)
        desc["params"]["resistance"]["type"] = "float"
        self.assertCode(self.check(self.write_component(desc)), "schema.type")

    def test_si_number_parsing(self):
        self.assertEqual(validate.parse_si("4.7k"), 4700.0)
        self.assertAlmostEqual(validate.parse_si("100n"), 1e-7)
        self.assertEqual(validate.parse_si("1meg"), 1e6)
        self.assertEqual(validate.parse_si("1M"), 1e6)
        self.assertEqual(validate.parse_si("2m"), 2e-3)
        self.assertEqual(validate.parse_si(220), 220.0)
        self.assertIsNone(validate.parse_si("4.7 kohm"))
        self.assertIsNone(validate.parse_si("abc"))
        self.assertIsNone(validate.parse_si(True))

    def test_bad_si_number_in_param(self):
        desc = copy.deepcopy(COMPONENT)
        desc["params"]["resistance"]["default"] = "4.7 kohm"
        self.assertCode(self.check(self.write_component(desc)), "schema.type")

    def test_default_outside_range(self):
        desc = copy.deepcopy(COMPONENT)
        desc["params"]["resistance"]["default"] = "10G"
        self.assertCode(self.check(self.write_component(desc)), "analog.param_out_of_range")

    def test_enum_default_not_in_enum(self):
        desc = copy.deepcopy(COMPONENT)
        desc["params"]["mode"]["default"] = "c"
        self.assertCode(self.check(self.write_component(desc)), "schema.type")

    def test_missing_symbol_file(self):
        self.assertCode(self.check(self.write_component(symbol=None)), "catalog.file_missing")

    def test_malformed_svg(self):
        self.assertCode(self.check(self.write_component(symbol="<svg><line></svg>")), "svg.malformed")

    def test_artwork_selector_must_exist(self):
        desc = copy.deepcopy(COMPONENT)
        desc["artwork"]["hotspots"][0]["selector"] = "#nope"
        self.assertCode(self.check(self.write_component(desc)), "artwork.selector")

    def test_artwork_input_unknown(self):
        desc = copy.deepcopy(COMPONENT)
        desc["artwork"]["hotspots"][0]["input"] = "held"
        self.assertCode(self.check(self.write_component(desc)), "catalog.input_unknown")

    def test_artwork_state_on_a_composite_member(self):
        src = Path(__file__).resolve().parents[1] / "catalog/components/uln2003_module"
        dst = self.tmp / "uln2003_module"
        shutil.copytree(src, dst)
        self.assertNotIn("catalog.output_unknown", self.codes(self.check(dst / "component.json")))
        doc = json.loads((dst / "component.json").read_text(encoding="utf-8"))
        doc["artwork"]["states"][0]["output"] = "LEDA.nope"
        (dst / "component.json").write_text(json.dumps(doc), encoding="utf-8")
        self.assertCode(self.check(dst / "component.json"), "catalog.output_unknown")

    def test_fidelity_all_null(self):
        desc = copy.deepcopy(COMPONENT)
        desc["fidelity"] = {"functional": None, "timed": None, "electrical": None}
        self.assertCode(self.check(self.write_component(desc)), "fidelity.missing")

    def test_fidelity_claims_empty(self):
        desc = copy.deepcopy(COMPONENT)
        desc["fidelity"]["electrical"]["claims"] = ""
        self.assertTrue(self.codes(self.check(self.write_component(desc))))

    def test_license_must_be_spdx(self):
        desc = copy.deepcopy(COMPONENT)
        desc["provenance"]["license"] = "Apache"
        self.assertCode(self.check(self.write_component(desc)), "provenance.license")
        self.assertTrue(validate.is_spdx("MIT OR Apache-2.0"))
        self.assertTrue(validate.is_spdx("LicenseRef-Vendor"))
        self.assertFalse(validate.is_spdx("Proprietary"))

    def test_bind_target_must_exist(self):
        desc = copy.deepcopy(COMPONENT)
        desc["model"]["bind"] = {"r": "ohms"}
        self.assertCode(self.check(self.write_component(desc)), "catalog.param_unknown")

    def test_param_ref_in_pin_model(self):
        desc = copy.deepcopy(COMPONENT)
        desc["pins"][0]["model"] = {"kind": "power", "role": "supply", "nominal_v": "$voltage"}
        self.assertCode(self.check(self.write_component(desc)), "catalog.param_unknown")
        desc["params"]["voltage"] = {"type": "number", "unit": "V", "default": 3.3}
        self.assertClean(self.check(self.write_component(desc)))

    def test_id_must_match_directory_and_library(self):
        desc = copy.deepcopy(COMPONENT)
        desc["id"] = "testlib/other"
        self.assertCode(self.check(self.write_component(desc)), "catalog.id_mismatch")
        desc["id"] = "otherlib/widget"
        self.assertCode(self.check(self.write_component(desc)), "catalog.id_mismatch")

    def test_tests_glob_must_match(self):
        self.assertCode(self.check(self.write_component(fixture=None)), "catalog.file_missing")

    def test_fixture_must_use_subject(self):
        fx = copy.deepcopy(FIXTURE)
        fx["circuit"]["components"][0] = {"id": "DUT", "component": "emblink/resistor"}
        path = self.write_component(fixture=fx)
        r = self.check(path.parent / "tests" / "basic.json")
        self.assertCode(r, "catalog.test_subject")

    def test_fixture_unknown_pin_and_input(self):
        fx = copy.deepcopy(FIXTURE)
        fx["circuit"]["nets"]["A"] = ["V1.+", "DUT.9"]
        fx["inputs"][0]["input"] = "kicked"
        path = self.write_component(fixture=fx)
        r = self.check(path.parent / "tests" / "basic.json", self.index.with_extra([self.tmp / "testlib"]))
        self.assertCode(r, "catalog.pin_unknown")
        self.assertCode(r, "catalog.input_unknown")

    def test_composite_model_checks(self):
        desc = copy.deepcopy(COMPONENT)
        desc["model"] = {"composite": {
            "components": [{"id": "Ra", "component": "emblink/resistor", "params": {"resistance": "$resistance"}},
                           {"id": "Rb", "component": "emblink/resistor", "params": {"resistance": "$nope"}}],
            "nets": {"M": ["Ra.2", "Rb.1"]}, "ports": {"1": "Ra.1", "2": "Rb.3"}}}
        r = self.check(self.write_component(desc))
        self.assertCode(r, "catalog.param_unknown")
        self.assertCode(r, "catalog.pin_unknown")


class ProjectTests(Base):
    def write_project(self, project=PROJECT, scenario=SCENARIO):
        d = self.tmp / "proj"
        (d / "scenarios").mkdir(parents=True, exist_ok=True)
        (d / "project.json").write_text(json.dumps(project))
        if scenario is not None:
            (d / "scenarios" / "press.json").write_text(json.dumps(scenario))
        return d / "project.json"

    def test_valid_project_and_scenario(self):
        path = self.write_project()
        self.assertClean(self.check(path))
        self.assertClean(self.check(path.parent / "scenarios" / "press.json"))

    def test_schematic_organization(self):
        p = copy.deepcopy(PROJECT)
        ids = [part["id"] for part in p["components"][:2]]
        p["components"][0]["layout"] = {"x": 0, "y": 0, "locked": True}
        p["layout"] = {"groups": [{"id": "input", "name": "Input", "members": ids}]}
        self.assertClean(self.check(self.write_project(p)))
        p["layout"]["groups"][0]["members"].append("missing")
        self.assertCode(self.check(self.write_project(p)), "catalog.not_found")
        p["layout"]["groups"][0]["members"] = ids
        p["layout"]["groups"].append({"id": "other", "name": "Other", "members": [ids[0]]})
        self.assertCode(self.check(self.write_project(p)), "catalog.duplicate")

    def test_unknown_component(self):
        p = copy.deepcopy(PROJECT)
        p["components"][1]["component"] = "emblink/no_such_part"
        self.assertCode(self.check(self.write_project(p)), "catalog.not_found")

    def test_unknown_pin(self):
        p = copy.deepcopy(PROJECT)
        p["nets"]["VCC"] = ["V.VCC", "SW1.3"]
        self.assertCode(self.check(self.write_project(p)), "catalog.pin_unknown")

    def test_alias_pin_accepted(self):
        p = copy.deepcopy(PROJECT)
        p["nets"]["VCC"] = ["V.VCC", "SW1.A"]
        self.assertClean(self.check(self.write_project(p)))

    def test_pin_in_two_nets(self):
        p = copy.deepcopy(PROJECT)
        p["nets"]["GND"].append("SW1.2")
        self.assertCode(self.check(self.write_project(p)), "catalog.pin_twice")

    def test_undeclared_parameter_and_range(self):
        p = copy.deepcopy(PROJECT)
        p["components"][2]["params"] = {"ohms": "10k"}
        self.assertCode(self.check(self.write_project(p)), "catalog.param_unknown")
        p["components"][2]["params"] = {"resistance": "1T"}
        self.assertCode(self.check(self.write_project(p)), "analog.param_out_of_range")

    def test_policy_requires_fidelity_tier(self):
        p = copy.deepcopy(PROJECT)
        p["components"].append({"id": "C1", "component": "emblink/capacitor"})
        p["nets"]["GND"].append("C1.1")
        p["nets"]["BTN"].append("C1.2")
        self.assertCode(self.check(self.write_project(p)), "fidelity.tier_missing")

    def test_probe_target_checks(self):
        p = copy.deepcopy(PROJECT)
        p["probes"].append({"id": "x", "target": "net:NOPE", "kind": "digital"})
        p["probes"].append({"id": "y", "target": "output:D1.brightness", "kind": "value"})
        r = self.check(self.write_project(p))
        self.assertCode(r, "catalog.not_found")
        self.assertCode(r, "catalog.output_unknown")

    def test_bad_policy_enum(self):
        p = copy.deepcopy(PROJECT)
        p["policy"] = "analog"
        self.assertCode(self.check(self.write_project(p)), "schema.type")

    def test_scenario_target_and_input(self):
        s = copy.deepcopy(SCENARIO)
        s["inputs"][0]["target"] = "SW9"
        s["inputs"].append({"at": "1ms", "target": "SW1", "input": "closed", "value": True})
        path = self.write_project(scenario=s)
        r = self.check(path.parent / "scenarios" / "press.json")
        self.assertCode(r, "catalog.not_found")
        self.assertCode(r, "catalog.input_unknown")

    def test_scenario_board_component_input(self):
        # a board's own push button, B1.S3: its component's inputs
        ex = self.tmp / "ex"
        shutil.copytree(validate.ROOT / "examples" / "explorer16-pic32mx", ex)
        scen = ex / "scenarios" / "blink.json"
        self.assertClean(self.check(scen))
        s = json.loads(scen.read_text())
        s["inputs"][0]["input"] = "kicked"
        scen.write_text(json.dumps(s))
        self.assertCode(self.check(scen), "catalog.input_unknown")

    def test_scenario_probe_must_exist(self):
        s = copy.deepcopy(SCENARIO)
        s["assertions"][0]["probe"] = "nope"
        path = self.write_project(scenario=s)
        self.assertCode(self.check(path.parent / "scenarios" / "press.json"), "catalog.not_found")

    def test_scenario_time_formats(self):
        self.assertEqual(validate.parse_time_ps("12ms"), 12 * 10**9)
        self.assertEqual(validate.parse_time_ps("1.5us"), 1500000)
        self.assertEqual(validate.parse_time_ps("1000"), 1000)
        self.assertIsNone(validate.parse_time_ps("0.0001ps"))
        self.assertIsNone(validate.parse_time_ps("12 minutes"))
        s = copy.deepcopy(SCENARIO)
        s["until"] = "12 minutes"
        path = self.write_project(scenario=s)
        self.assertCode(self.check(path.parent / "scenarios" / "press.json"), "schema.type")

    def test_missing_scenario_file(self):
        self.assertCode(self.check(self.write_project(scenario=None)), "catalog.file_missing")

    def test_invalid_json(self):
        d = self.tmp / "bad"
        d.mkdir()
        (d / "x.json").write_text("{not json")
        self.assertCode(self.check(d / "x.json"), "schema.syntax")

    def test_unknown_kind(self):
        d = self.tmp / "bad"
        d.mkdir()
        (d / "x.json").write_text(json.dumps({"kind": "gizmo"}))
        self.assertCode(self.check(d / "x.json"), "schema.unknown_kind")


class BoardTests(Base):
    def board(self):
        with open(validate.CATALOG_DIR / "boards" / "bluepill" / "board.json", "r", encoding="utf-8") as fh:
            return json.load(fh)

    def write_board(self, doc):
        d = self.tmp / "boards" / "bluepill"
        d.mkdir(parents=True)
        (d / "board.json").write_text(json.dumps(doc))
        shutil.copy(validate.CATALOG_DIR / "boards" / "bluepill" / "artwork.svg", d / "artwork.svg")
        shutil.copytree(validate.CATALOG_DIR / "boards" / "bluepill" / "tests", d / "tests")
        return d / "board.json"

    def test_catalog_board_valid(self):
        self.assertClean(self.check(self.write_board(self.board())))

    def test_unknown_signal_and_port_net(self):
        doc = self.board()
        doc["nets"]["PC13"][0] = "U1.PZ9"
        doc["ports"]["A0"]["net"] = "NOPE"
        r = self.check(self.write_board(doc))
        self.assertCode(r, "catalog.pin_unknown")
        self.assertCode(r, "catalog.not_found")

    def test_led_must_agree_with_nets(self):
        doc = self.board()
        doc["leds"][0]["gpio"] = "PA0"
        self.assertCode(self.check(self.write_board(doc)), "board.led_disagrees")

    def test_missing_board_oscillator(self):
        doc = self.board()
        del doc["oscillators"]["hse"]
        self.assertCode(self.check(self.write_board(doc)), "board.oscillator_missing")

    def test_todo_refused(self):
        doc = self.board()
        doc["components"][0]["todo"] = True
        self.assertCode(self.check(self.write_board(doc)), "catalog.todo")


class McuTests(Base):
    def test_package_signal_must_exist(self):
        src = validate.CATALOG_DIR / "mcus" / "st" / "stm32f103c8"
        d = self.tmp / "mcus" / "st" / "stm32f103c8"
        d.mkdir(parents=True)
        for f in ("mcu.json", "registers.json", "lqfp48.json", "lqfp48.svg"):
            shutil.copy(src / f, d / f)
        self.assertClean(self.check(d / "mcu.json"))
        pkg = json.loads((d / "lqfp48.json").read_text())
        pkg["pins"][1]["signal"] = "PX1"
        pkg["pins"][2]["number"] = 2
        (d / "lqfp48.json").write_text(json.dumps(pkg))
        r = self.check(d / "mcu.json")
        self.assertCode(r, "catalog.signal_unknown")
        self.assertCode(r, "catalog.pin_duplicate")

    def test_vendored_register_file_needs_file_and_license(self):
        src = validate.CATALOG_DIR / "mcus" / "st" / "stm32f103c8"
        d = self.tmp / "mcus" / "st" / "stm32f103c8"
        d.mkdir(parents=True)
        for f in ("lqfp48.json", "lqfp48.svg"):
            shutil.copy(src / f, d / f)
        mcu = json.loads((src / "mcu.json").read_text())
        mcu["register_file"] = {"format": "svd", "file": "STM32F103xx.svd", "vendored": True}
        (d / "mcu.json").write_text(json.dumps(mcu))
        r = self.check(d / "mcu.json")
        self.assertCode(r, "catalog.file_missing")
        self.assertCode(r, "provenance.license")

    def test_embsim_register_description(self):
        src = validate.CATALOG_DIR / "mcus" / "st" / "stm32f103c8"
        d = self.tmp / "mcus" / "st" / "stm32f103c8"
        d.mkdir(parents=True)
        for f in ("mcu.json", "lqfp48.json", "lqfp48.svg"):
            shutil.copy(src / f, d / f)
        self.assertCode(self.check(d / "mcu.json"), "catalog.file_missing")
        regs = json.loads((src / "registers.json").read_text())
        regs["peripherals"] = [p for p in regs["peripherals"] if not p["name"].startswith("TIM")]
        (d / "registers.json").write_text(json.dumps(regs))
        r = self.check(d / "mcu.json")
        self.assertIn("catalog.register_file_absent", [f.code for f in r.findings])

    def test_register_description_checks(self):
        regs = json.loads((validate.CATALOG_DIR / "mcus" / "st" / "stm32f103c8" / "registers.json").read_text())
        self.assertClean(self.check_doc(regs))
        bad = json.loads(json.dumps(regs))
        bad["peripherals"][0]["registers"][1]["offset"] = bad["peripherals"][0]["registers"][0]["offset"]
        self.assertCode(self.check_doc(bad), "catalog.duplicate")
        bad = json.loads(json.dumps(regs))
        bad["peripherals"][0]["interrupts"] = ["NOPE"]
        self.assertCode(self.check_doc(bad), "catalog.ref_unknown")
        bad = json.loads(json.dumps(regs))
        bad["peripherals"][0]["registers"][0]["fields"] = [["A", 0, 4], ["B", 2, 4]]
        self.assertCode(self.check_doc(bad), "catalog.duplicate")


class OtherKindsTests(Base):
    def test_toolchain_hex_needs_convert(self):
        doc = {"schema_version": 1, "kind": "toolchain", "id": "t", "name": "T", "detect": {"command": ["t", "--version"]},
               "targets": {"x": {"cc": ["t"]}}, "steps": {"compile": ["${cc}", "-c", "${source}", "-o", "${object}"], "link": ["${cc}", "${objects}", "-o", "${image}"], "convert": None},
               "outputs": {"image": "hex"}}
        d = self.tmp / "toolchains"
        d.mkdir()
        (d / "t.json").write_text(json.dumps(doc))
        self.assertCode(self.check(d / "t.json"), "schema.missing_field")
        doc["outputs"]["image"] = "elf"
        (d / "t.json").write_text(json.dumps(doc))
        self.assertClean(self.check(d / "t.json"))

    def test_recording_order(self):
        doc = {"schema_version": 1, "kind": "recording", "engine": {"version": "0.1.0", "abi": 1},
               "project": {"path": "p.json", "hash": "sha256:" + "0" * 64}, "policy": "functional",
               "inputs": [{"at_ps": "20", "seq": 1, "target": "SW1", "input": "pressed", "value": True},
                          {"at_ps": "10", "seq": 2, "target": "SW1", "input": "pressed", "value": False}],
               "observations": [{"at_ps": "5", "kind": "probe", "probe": "btn", "value": 0}],
               "end": {"at_ps": "100", "reason": "until"}}
        d = self.tmp / "rec"
        d.mkdir()
        (d / "r.json").write_text(json.dumps(doc))
        self.assertCode(self.check(d / "r.json"), "recording.order")

    def test_library_manifest(self):
        d = self.tmp / "lib"
        d.mkdir()
        (d / "library.json").write_text(json.dumps({"schema_version": 1, "kind": "library", "name": "x", "version": "1.0.0", "license": "Nope"}))
        self.assertCode(self.check(d / "library.json"), "provenance.license")

    def test_schema_files_are_sound(self):
        r = validate.Report()
        for p in sorted(validate.SCHEMA_DIR.glob("*.schema.json")):
            validate.validate_file(p, self.schemas, self.index, r)
        self.assertClean(r)


class BuiltinBackendTests(ComponentTests):
    backend = "builtin"

    def test_backend_is_builtin(self):
        self.assertEqual(self.schemas.backend, "builtin")


class BuiltinProjectTests(ProjectTests):
    backend = "builtin"


class CliTests(unittest.TestCase):
    def test_main_all_passes(self):
        import io
        from contextlib import redirect_stdout
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = validate.main(["--all", "--quiet"])
        self.assertEqual(code, 0, buf.getvalue())

    def test_main_usage_error(self):
        import io
        from contextlib import redirect_stderr
        with redirect_stderr(io.StringIO()):
            self.assertEqual(validate.main([]), 2)
            self.assertEqual(validate.main(["/no/such/file.json"]), 2)


if __name__ == "__main__":
    unittest.main()
