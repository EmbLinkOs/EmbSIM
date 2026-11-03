# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The MCP server (ADR-035): its JSON-RPC over stdio, and an assistant's
session against a built embsim ($EMBSIM, or a build in the repository):
the Blue Pill example run and its console read, a button pressed, then a
circuit built from nothing (a 5 V source, 330 ohm and an LED), run and its
LED current read, the way an assistant builds and verifies one.

    EMBSIM=build/dev/bin/embsim python3 python/tests/test_mcp.py
"""
import io
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))

from embsim import EmbsimError  # noqa: E402
from embsim.mcp import Server, serve  # noqa: E402


class FakeSession:
    def __init__(self):
        self.calls = []

    def call(self, method, **params):
        self.calls.append((method, params))
        if method == "project.transaction" and params.get("op") == "begin":
            return {"tx": 7}
        if method == "project.transaction" and params.get("op") == "edit":
            if any("bad" in e for e in params["edits"]):
                raise EmbsimError(-32602, "an edit is an object with one key")
            return {}
        if method == "project.transaction" and params.get("op") == "commit":
            return {"applied": len(params) and 1, "restarted": True, "diagnostics": []}
        return {}

    def advance(self, by=None, until=None):
        return 5_000_000_000

    def close(self):
        pass


def rpc(server, *msgs):
    out = io.StringIO()
    serve(server, io.StringIO("".join(json.dumps(m) + "\n" for m in msgs)), out)
    return [json.loads(line) for line in out.getvalue().splitlines()]


class ProtocolTest(unittest.TestCase):
    def setUp(self):
        self.fake = FakeSession()
        self.server = Server(session_factory=lambda: self.fake)

    def test_initialize_and_list(self):
        r = rpc(self.server,
                {"jsonrpc": "2.0", "id": 1, "method": "initialize",
                 "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "t"}}},
                {"jsonrpc": "2.0", "method": "notifications/initialized"},
                {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
                {"jsonrpc": "2.0", "id": 3, "method": "ping"})
        self.assertEqual(len(r), 3)  # the notification is not answered
        self.assertEqual(r[0]["result"]["protocolVersion"], "2025-06-18")
        self.assertIn("tools", r[0]["result"]["capabilities"])
        names = {t["name"] for t in r[1]["result"]["tools"]}
        for n in ("project_load", "project_new", "project_edit", "catalog_search", "firmware_build", "run",
                  "press", "set_input", "serial_send", "serial_read", "observe", "probe_add", "diagnostics",
                  "scenario_run", "record_start", "debug_registers"):
            self.assertIn(n, names)
        for t in r[1]["result"]["tools"]:
            self.assertEqual(t["inputSchema"]["type"], "object")
        self.assertEqual(r[2]["result"], {})
        self.assertEqual(self.fake.calls, [])  # nothing started the engine yet

    def test_unknown_version_and_method(self):
        r = rpc(self.server,
                {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "1999-01-01"}},
                {"jsonrpc": "2.0", "id": 2, "method": "resources/list"},
                {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "nope"}})
        self.assertEqual(r[0]["result"]["protocolVersion"], "2025-06-18")
        self.assertEqual(r[1]["error"]["code"], -32601)
        self.assertEqual(r[2]["error"]["code"], -32602)

    def test_edit_is_one_transaction(self):
        r = rpc(self.server, {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                              "params": {"name": "project_edit",
                                         "arguments": {"edits": [{"connect": {"net": "A", "pin": "R1.1"}}]}}})
        self.assertFalse(r[0]["result"]["isError"])
        ops = [p.get("op") for m, p in self.fake.calls if m == "project.transaction"]
        self.assertEqual(ops, ["begin", "edit", "commit"])

    def test_refused_edit_aborts_and_reports(self):
        r = rpc(self.server, {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                              "params": {"name": "project_edit", "arguments": {"edits": [{"bad": 1}]}}})
        self.assertTrue(r[0]["result"]["isError"])
        self.assertIn("one key", r[0]["result"]["content"][0]["text"])
        ops = [p.get("op") for m, p in self.fake.calls if m == "project.transaction"]
        self.assertEqual(ops, ["begin", "edit", "abort"])

    def test_run_needs_one_bound(self):
        r = rpc(self.server, {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                              "params": {"name": "run", "arguments": {}}})
        self.assertTrue(r[0]["result"]["isError"])

    def test_parse_error_and_batch(self):
        out = io.StringIO()
        serve(self.server, io.StringIO("{nope\n" + json.dumps([
            {"jsonrpc": "2.0", "id": 1, "method": "ping"},
            {"jsonrpc": "2.0", "method": "notifications/initialized"}]) + "\n"), out)
        lines = [json.loads(x) for x in out.getvalue().splitlines()]
        self.assertEqual(lines[0]["error"]["code"], -32700)
        self.assertEqual(lines[1], [{"jsonrpc": "2.0", "id": 1, "result": {}}])


@unittest.skipUnless(os.environ.get("EMBSIM") or any((ROOT / "build").glob("*/bin/embsim*")),
                     "no embsim built")
class AssistantTest(unittest.TestCase):
    """The server as an assistant runs it: a subprocess on stdio."""

    def setUp(self):
        env = dict(os.environ, PYTHONPATH=str(ROOT / "python"))
        argv = [sys.executable, "-m", "embsim.mcp"]
        if os.environ.get("EMBSIM"):
            argv += ["--embsim", os.environ["EMBSIM"]]
        self.p = subprocess.Popen(argv, cwd=ROOT, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True)
        self.n = 0
        r = self.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                    "clientInfo": {"name": "test", "version": "0"}})
        self.assertEqual(r["result"]["serverInfo"]["name"], "embsim")
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")

    def tearDown(self):
        self.p.stdin.close()
        self.p.wait(timeout=30)

    def rpc(self, method, params):
        self.n += 1
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.n, "method": method, "params": params}) + "\n")
        self.p.stdin.flush()
        return json.loads(self.p.stdout.readline())

    def tool(self, _tool, **args):
        r = self.rpc("tools/call", {"name": _tool, "arguments": args})["result"]
        self.assertFalse(r["isError"], r["content"][0]["text"])
        return json.loads(r["content"][0]["text"])

    def test_firmware_board(self):
        self.tool("project_load", path="examples/bluepill-button-led/project.json")
        self.assertEqual(self.tool("run", until="30ms")["now_ps"], 30_000_000_000)
        self.assertIn("EmbSIM Blue Pill demo", self.tool("serial_read", board="B1")["text"])
        targets = [i["target"] for i in self.tool("inputs_list")["inputs"]]
        self.assertIn("B1.SW_RESET", targets)
        info = self.tool("session_info")
        self.assertTrue(any(b["id"] == "B1" for b in info["boards"]))
        regs = self.tool("debug_registers", board="B1")
        self.assertIn("pc", json.dumps(regs).lower())

    def test_assistant_builds_and_verifies_a_circuit(self):
        found = self.tool("catalog_search", query="led", kind="component")["items"]
        self.assertTrue(any(i["id"] == "emblink/led" for i in found))
        self.tool("project_new", name="assistant-led", policy="electrical")
        res = self.tool("project_edit", edits=[
            {"add_component": {"id": "V1", "component": "emblink/vsource", "params": {"kind": "dc", "dc": 5}}},
            {"add_component": {"id": "R1", "component": "emblink/resistor", "params": {"resistance": 330}}},
            {"add_component": {"id": "D1", "component": "emblink/led"}},
            {"add_component": {"id": "G1", "component": "emblink/gnd"}},
            {"connect": {"net": "VCC", "pin": "V1.+"}}, {"connect": {"net": "VCC", "pin": "R1.1"}},
            {"connect": {"net": "A", "pin": "R1.2"}}, {"connect": {"net": "A", "pin": "D1.A"}},
            {"connect": {"net": "GND", "pin": "D1.K"}}, {"connect": {"net": "GND", "pin": "V1.-"}},
            {"connect": {"net": "GND", "pin": "G1.GND"}}])
        self.assertTrue(res.get("restarted") or res.get("applied"))
        self.tool("run", by="5ms")
        snap = self.tool("observe", include=["nets", "outputs"])
        i = snap["outputs"]["D1"]["current"]
        vf = snap["nets"]["A"]["voltage"]
        r1 = snap["outputs"]["R1"]
        ohms = r1["p"] / (r1["i"] ** 2)  # R1 as drawn: tolerances are on (ADR-034)
        self.assertLess(abs(ohms - 330.0) / 330.0, 0.05)
        self.assertAlmostEqual(i, (5.0 - vf) / ohms, delta=1e-5)  # Ohm's law across R1
        self.assertGreater(i, 0.008)
        self.assertLess(i, 0.012)
        self.assertEqual(self.tool("diagnostics")["diagnostics"], [])
        # a refused edit is the tool's error, the project unchanged
        r = self.rpc("tools/call", {"name": "project_edit",
                                    "arguments": {"edits": [{"connect": {"net": "X", "pin": "NOPE.1"}}]}})["result"]
        self.assertTrue(r["isError"])
        self.assertIn("D1", json.dumps(self.tool("project_get")))


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--embsim":
        os.environ["EMBSIM"] = sys.argv.pop(2)
        sys.argv.pop(1)
    unittest.main()
