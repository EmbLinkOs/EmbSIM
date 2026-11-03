# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""An MCP server for EmbSIM (ADR-035): an AI assistant uses the same session
a person does, over the same protocol (SPEC-008).

    embsim-mcp [--project PROJECT] [--embsim PATH]
    python -m embsim.mcp [--project PROJECT]

It speaks the Model Context Protocol's JSON-RPC 2.0 over stdio (one message
a line) and drives one `embsim sidecar` session. Its tools are the
operations a person has in the workbench and the CLI: load, create, edit,
validate and save a project; search the catalog; build firmware; run,
pause and reset; press, release, set inputs and type into a serial port;
read the serial ports, probes, nets, outputs and diagnostics; run
scenarios; record; and inspect a board's registers and memory. Each tool
is one protocol call (or a transaction of edits), with the session's own
policy, models and diagnostics: no access a person lacks, no shortcut past
the models. A protocol error comes back as the tool's error.

No dependency beyond the Python standard library and this package.
"""
from __future__ import annotations

import argparse
import json
import sys
import traceback
from typing import Any, Callable, Optional

from .session import Session
from .transport import EmbsimError

SERVER_NAME = "embsim"
SERVER_VERSION = "0.1.0"
PROTOCOL_VERSIONS = ("2025-06-18", "2025-03-26", "2024-11-05")

_TIME = {"type": ["string", "integer"],
         "description": "a duration or time with a unit (\"10ms\", \"1.5us\", \"2s\") or picoseconds"}


def _obj(props: dict, required: Optional[list] = None) -> dict:
    schema: dict = {"type": "object", "properties": props, "additionalProperties": False}
    if required:
        schema["required"] = required
    return schema


class Server:
    """The tools, over one lazily opened session."""

    def __init__(self, project: Optional[str] = None, embsim: Optional[str] = None,
                 session_factory: Optional[Callable[..., Session]] = None):
        self.project_path = project
        self.embsim = embsim
        self.factory = session_factory or (lambda: Session(self.project_path, embsim=self.embsim,
                                                            client=f"embsim-mcp/{SERVER_VERSION}"))
        self._session: Optional[Session] = None
        self.tools = self._tools()

    @property
    def session(self) -> Session:
        if self._session is None:
            self._session = self.factory()
        return self._session

    def close(self) -> None:
        if self._session is not None:
            self._session.close()
            self._session = None

    # ---- the tools -------------------------------------------------------------

    def _tools(self) -> dict:
        t: dict = {}

        def tool(name: str, description: str, schema: dict):
            def wrap(fn):
                t[name] = {"description": description, "schema": schema, "fn": fn}
                return fn
            return wrap

        @tool("session_info", "The session: the engine, the project's name, its boards (id, board, MCU, firmware "
              "image), the simulated time and whether it runs.", _obj({}))
        def session_info(a):
            return self.session.info()

        @tool("project_load", "Load a project file (project.json) into the session, as the workbench opens one.",
              _obj({"path": {"type": "string"}}, ["path"]))
        def project_load(a):
            return self.session.call("project.load", path=a["path"])

        @tool("project_new", "Start a new, empty project in the session (no parts yet), under a policy: "
              "\"digital\" (logic levels) or \"electrical\" (voltages and currents).",
              _obj({"name": {"type": "string"}, "policy": {"type": "string", "enum": ["digital", "electrical"]},
                    "base_dir": {"type": "string", "description": "where relative paths (firmware) resolve"}},
                   ["name"]))
        def project_new(a):
            doc = {"schema_version": 1, "kind": "project", "name": a["name"],
                   "policy": a.get("policy", "electrical"), "components": [], "nets": {}}
            params: dict = {"json": doc}
            if a.get("base_dir"):
                params["base_dir"] = a["base_dir"]
            return self.session.call("project.load", **params)

        @tool("project_get", "The project as it stands: its boards, parts, parameters, nets, probes and layout.",
              _obj({}))
        def project_get(a):
            return self.session.call("project.get")

        @tool("project_edit", "Edit the project in one transaction, as the schematic editor does: add or remove "
              "a part, set a parameter, connect a pin to a net or disconnect it, rename a net, add a board, set "
              "a layout. Each edit is an object with one key: {\"add_component\": {\"id\", \"component\", \"params\"?}}, {\"remove_component\": {\"id\"}}, {\"set_param\": {\"id\", \"param\", \"value\"}}, {\"connect\": {\"net\", \"pin\": \"ID.PIN\"}}, {\"disconnect\": {\"pin\"}}, {\"rename_net\": {\"from\", \"to\"}}, {\"add_board\": {\"id\", \"board\", \"firmware\"?}}, {\"set_layout\": {\"id\", \"layout\"}}. "
              "All apply or none; the result lists what applied, "
              "whether the simulation restarted, and the diagnostics.",
              _obj({"edits": {"type": "array", "items": {"type": "object"}, "minItems": 1}}, ["edits"]))
        def project_edit(a):
            s = self.session
            tx = s.call("project.transaction", op="begin")["tx"]
            try:
                s.call("project.transaction", op="edit", tx=tx, edits=a["edits"])
            except Exception:
                try:
                    s.call("project.transaction", op="abort", tx=tx)
                except Exception:
                    pass
                raise
            return s.call("project.transaction", op="commit", tx=tx)

        @tool("project_validate", "Validate the project (or a project file) against the schemas and the engine's "
              "checks: the diagnostics a person sees.",
              _obj({"path": {"type": "string"}}))
        def project_validate(a):
            if a.get("path"):
                return self.session.call("project.validate", path=a["path"])
            return self.session.call("project.validate", json=self.session.call("project.get")["project"])

        @tool("project_save", "Save the project to a file (project.json).",
              _obj({"path": {"type": "string"}}))
        def project_save(a):
            return self.session.call("project.save", **({"path": a["path"]} if a.get("path") else {}))

        @tool("catalog_search", "List the catalog's parts and boards (ids, names, categories), filtered by a "
              "text query, as the library panel does.",
              _obj({"query": {"type": "string"}, "kind": {"type": "string", "enum": ["component", "board", "mcu"]}}))
        def catalog_search(a):
            items = self.session.call("catalog.list").get("items") or []
            q = (a.get("query") or "").lower()
            kind = a.get("kind")
            out = []
            for it in items:
                if kind and it.get("kind") != kind:
                    continue
                text = " ".join(str(it.get(k, "")) for k in ("id", "name", "category", "description")).lower()
                if q and q not in text:
                    continue
                out.append({k: it.get(k) for k in ("id", "kind", "name", "category") if k in it})
            return {"count": len(out), "items": out[:200]}

        @tool("catalog_get", "A catalog entry's description: its pins, parameters, inputs, outputs and fidelity "
              "(what it models and what it leaves out).",
              _obj({"id": {"type": "string"}}, ["id"]))
        def catalog_get(a):
            return self.session.call("catalog.get", id=a["id"])

        @tool("firmware_build", "Build a board's firmware with its toolchain profile, as the workbench's Build "
              "does; the log and the image.",
              _obj({"board": {"type": "string"}, "profile": {"type": "string"}}, ["board"]))
        def firmware_build(a):
            params = {"board": a["board"]}
            if a.get("profile"):
                params["profile"] = a["profile"]
            return self.session.call("toolchain.build", **params)

        @tool("run", "Run the simulation by a duration or until a time (simulated time, not the wall clock), "
              "then pause; the new time.",
              _obj({"by": _TIME, "until": _TIME}))
        def run(a):
            if ("by" in a) == ("until" in a):
                raise ValueError("give one of by or until")
            now = self.session.advance(by=a.get("by"), until=a.get("until"))
            return {"now_ps": now}

        @tool("reset", "Reset the circuit and its boards to power-on, as the workbench's reset does.", _obj({}))
        def reset(a):
            self.session.reset()
            return {"ok": True}

        @tool("inputs_list", "The inputs a person can work: buttons, switches, pots, sensors' values, terminals.",
              _obj({}))
        def inputs_list(a):
            return {"inputs": self.session.inputs()}

        @tool("press", "Press a part (a push button, a key) and keep it pressed.",
              _obj({"target": {"type": "string"}}, ["target"]))
        def press(a):
            return self.session.press(a["target"])

        @tool("release", "Release a pressed part.", _obj({"target": {"type": "string"}}, ["target"]))
        def release(a):
            return self.session.release(a["target"])

        @tool("set_input", "Set a part's input (a pot's position, a switch, a sensor's value, a terminal's "
              "tx_hex), now or at a simulated time.",
              _obj({"target": {"type": "string"}, "input": {"type": "string"}, "value": {}, "at": _TIME},
                   ["target", "input", "value"]))
        def set_input(a):
            return self.session.set(a["target"], a["input"], a["value"], at=a.get("at"))

        @tool("serial_send", "Type text into a board's serial port or a terminal part, as into its terminal.",
              _obj({"target": {"type": "string"}, "text": {"type": "string"}}, ["target", "text"]))
        def serial_send(a):
            return self.session.send(a["target"], a["text"])

        @tool("serial_read", "What a board has written to its serial console (or a UART) so far.",
              _obj({"board": {"type": "string"}, "unit": {"type": "string"},
                    "tail": {"type": "integer", "description": "the last characters only", "minimum": 1}},
                   ["board"]))
        def serial_read(a):
            text = self.session.serial(a["board"], a.get("unit"))
            if a.get("tail"):
                text = text[-int(a["tail"]):]
            return {"text": text}

        @tool("observe", "The circuit now: nets (levels and voltages), parts' outputs (an LED's light, a "
              "display's text, a meter's reading) and probes.",
              _obj({"include": {"type": "array", "items": {"type": "string", "enum": ["nets", "outputs", "probes"]}}}))
        def observe(a):
            return self.session.snapshot(tuple(a.get("include") or ("nets", "outputs")))

        @tool("probe_add", "Attach a probe (digital, analog, value or serial) to a net, a pin or a part's output, "
              "as with an instrument's lead.",
              _obj({"target": {"type": "string"}, "kind": {"type": "string",
                                                           "enum": ["digital", "analog", "value", "serial"]},
                    "rate_hz": {"type": "number"}}, ["target"]))
        def probe_add(a):
            params = {}
            if a.get("rate_hz"):
                params["rate_hz"] = a["rate_hz"]
            return {"probe": self.session.probe(a["target"], a.get("kind", "digital"), **params)}

        @tool("probe_read", "A probe's samples since it was attached (time and value).",
              _obj({"probe": {"type": "string"}, "limit": {"type": "integer", "minimum": 1}}, ["probe"]))
        def probe_read(a):
            samples = self.session.read(a["probe"])
            if a.get("limit"):
                samples = samples[-int(a["limit"]):]
            return {"samples": samples}

        @tool("diagnostics", "The session's diagnostics: warnings and errors a person sees (a part over its "
              "rating, an unmodelled register touched, a floating input).", _obj({}))
        def diagnostics(a):
            return {"diagnostics": self.session.diagnostics()}

        @tool("scenario_run", "Run a scenario file (timed inputs and assertions) on the project; its results.",
              _obj({"path": {"type": "string"}, "until": _TIME}, ["path"]))
        def scenario_run(a):
            return self.session.run_scenario(a["path"], until=a.get("until"))

        @tool("record_start", "Start recording the session's inputs and observations (for replay).",
              _obj({"path": {"type": "string"}}))
        def record_start(a):
            return {"from_ps": self.session.record(a.get("path"))}

        @tool("record_stop", "Stop recording; where the recording went and what it holds.", _obj({}))
        def record_stop(a):
            return self.session.stop_recording()

        @tool("debug_registers", "A board's CPU registers, as the Debug perspective shows them.",
              _obj({"board": {"type": "string"}}, ["board"]))
        def debug_registers(a):
            return self.session.registers(a["board"])

        @tool("debug_memory", "Read a board's memory (hex), as the Debug perspective's memory view.",
              _obj({"board": {"type": "string"}, "address": {"type": "integer", "minimum": 0},
                    "length": {"type": "integer", "minimum": 1, "maximum": 4096}},
                   ["board", "address", "length"]))
        def debug_memory(a):
            data = self.session.memory(a["board"], int(a["address"]), int(a["length"]))
            return {"address": a["address"], "hex": data.hex()}

        return t

    # ---- MCP -------------------------------------------------------------------

    def handle(self, msg: dict) -> Optional[dict]:
        """One JSON-RPC message; the response, or None for a notification."""
        mid = msg.get("id")
        method = msg.get("method")
        params = msg.get("params") or {}
        if method is None:
            return None  # a response to us: we send no requests
        try:
            result = self._dispatch(method, params)
        except _RpcError as e:
            return None if mid is None else {"jsonrpc": "2.0", "id": mid,
                                              "error": {"code": e.code, "message": e.message}}
        if mid is None:
            return None
        return {"jsonrpc": "2.0", "id": mid, "result": result}

    def _dispatch(self, method: str, params: dict) -> Any:
        if method == "initialize":
            asked = params.get("protocolVersion")
            version = asked if asked in PROTOCOL_VERSIONS else PROTOCOL_VERSIONS[0]
            return {"protocolVersion": version,
                    "capabilities": {"tools": {"listChanged": False}},
                    "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                    "instructions": "EmbSIM, a virtual electronics workbench: the tools are the operations a "
                                    "person has. Load or create a project, add and wire parts from the catalog, "
                                    "build firmware, run in simulated time, work the inputs and read what the "
                                    "circuit and the boards do. Times are strings with units (\"10ms\")."}
        if method in ("notifications/initialized", "notifications/cancelled"):
            return None
        if method == "ping":
            return {}
        if method == "tools/list":
            return {"tools": [{"name": n, "description": d["description"], "inputSchema": d["schema"]}
                              for n, d in self.tools.items()]}
        if method == "tools/call":
            name = params.get("name")
            if name not in self.tools:
                raise _RpcError(-32602, f"unknown tool: {name}")
            args = params.get("arguments") or {}
            if not isinstance(args, dict):
                raise _RpcError(-32602, "arguments must be an object")
            try:
                out = self.tools[name]["fn"](args)
            except EmbsimError as e:
                return _content({"error": str(e)}, True)
            except (ValueError, KeyError, TypeError, FileNotFoundError, OSError) as e:
                return _content({"error": f"{type(e).__name__}: {e}"}, True)
            return _content(out, False)
        raise _RpcError(-32601, f"method not found: {method}")


class _RpcError(Exception):
    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


def _content(value: Any, is_error: bool) -> dict:
    text = value if isinstance(value, str) else json.dumps(value, indent=1, default=str)
    out: dict = {"content": [{"type": "text", "text": text}], "isError": is_error}
    if isinstance(value, dict) and not is_error:
        out["structuredContent"] = value
    return out


def serve(server: Server, rd=None, wr=None) -> None:
    """Read JSON-RPC messages a line at a time from `rd`, answer on `wr`."""
    rd = rd or sys.stdin
    wr = wr or sys.stdout
    for line in rd:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            wr.write(json.dumps({"jsonrpc": "2.0", "id": None,
                                 "error": {"code": -32700, "message": "parse error"}}) + "\n")
            wr.flush()
            continue
        batch = msg if isinstance(msg, list) else [msg]
        replies = []
        for m in batch:
            try:
                r = server.handle(m) if isinstance(m, dict) else None
            except Exception as e:  # a bug of ours: report it, keep serving
                traceback.print_exc(file=sys.stderr)
                r = {"jsonrpc": "2.0", "id": m.get("id") if isinstance(m, dict) else None,
                     "error": {"code": -32603, "message": f"internal error: {e}"}}
            if r is not None:
                replies.append(r)
        if replies:
            wr.write(json.dumps(replies if isinstance(msg, list) else replies[0]) + "\n")
            wr.flush()


def main(argv: Optional[list] = None) -> int:
    ap = argparse.ArgumentParser(prog="embsim-mcp", description=__doc__.split("\n\n")[0])
    ap.add_argument("--project", help="a project to load at the start")
    ap.add_argument("--embsim", help="the embsim executable (else $EMBSIM, the PATH, or a repository build)")
    args = ap.parse_args(argv)
    server = Server(args.project, args.embsim)
    try:
        serve(server)
    finally:
        server.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
