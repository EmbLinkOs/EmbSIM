# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""A session with the simulator, from Python (SPEC-008): one `embsim
sidecar` process, its project loaded, driven method by method.

    from embsim import Session

    with Session("examples/bluepill-button-led/project.json") as s:
        s.advance(until="20ms")
        assert "Blue Pill" in s.serial("B1")
        s.press("SW1")
        s.advance(by="5ms")
        print(s.output("B1.U1")["serial.USART1"])

Times are strings with a unit ("10ms", "1.5us") or integers in picoseconds;
the session's own times come back in picoseconds, as ints.
"""
from __future__ import annotations

import base64
import os
import shutil
from pathlib import Path
from typing import Any, Iterable, Optional, Union

from .transport import EmbsimError, StdioClient

Time = Union[str, int]

PROTOCOL = "1.0"


def find_embsim(explicit: Optional[str] = None) -> str:
    """The embsim executable: `explicit`, $EMBSIM, `embsim` on the PATH, or a
    build of the repository this package sits in (build/*/bin/embsim)."""
    for cand in (explicit, os.environ.get("EMBSIM")):
        if cand:
            return cand
    found = shutil.which("embsim")
    if found:
        return found
    root = Path(__file__).resolve().parents[2]
    for preset in ("release", "dev", "ci-gcc", "ci-clang", "ci-msvc"):
        for name in ("embsim", "embsim.exe"):
            p = root / "build" / preset / "bin" / name
            if p.exists():
                return str(p)
    raise FileNotFoundError("embsim not found: pass embsim=, set EMBSIM, or put it on the PATH")


def _time(t: Time) -> Union[str, int]:
    return str(t) if isinstance(t, int) else t


class Session:
    """A protocol session over `embsim sidecar` (stdio). `project` is loaded
    at the start when given; `embsim` names the executable (find_embsim)."""

    def __init__(self, project: Optional[Union[str, os.PathLike]] = None, *, embsim: Optional[str] = None,
                 cwd: Optional[Union[str, os.PathLike]] = None, client: str = "embsim-python/0.1",
                 timeout: float = 120.0):
        argv = [find_embsim(embsim), "sidecar"]
        if project is not None:
            argv.append(str(project))
        self.timeout = timeout
        self.client = StdioClient(argv, cwd=str(cwd) if cwd is not None else None)
        opened = self.call("session.open", client=client, protocol=PROTOCOL, streams="base64")
        self.engine: str = opened.get("engine", "")
        self.capabilities: list[str] = list(opened.get("capabilities") or [])

    # ---- the session -------------------------------------------------------

    def call(self, method: str, **params: Any) -> Any:
        """Any protocol method by name; EmbsimError on a JSON-RPC error."""
        return self.client.call(method, params, timeout=self.timeout)

    def close(self) -> None:
        if not self.client.closed:
            try:
                self.call("session.close")
            except Exception:  # the process may already be gone
                pass
            self.client.close()

    def __enter__(self) -> "Session":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()

    def info(self) -> dict:
        return self.call("session.info")

    @property
    def now(self) -> int:
        """The session's time in picoseconds."""
        return int(self.info()["now_ps"])

    def boards(self) -> list[dict]:
        """The project's boards: {id, board, mcu, runtime, component, image}."""
        return list(self.info().get("boards") or [])

    # ---- the project -------------------------------------------------------

    def load(self, project: Union[str, os.PathLike]) -> dict:
        return self.call("project.load", path=str(project))

    def project(self) -> dict:
        return self.call("project.get")["project"]

    # ---- time --------------------------------------------------------------

    def advance(self, by: Optional[Time] = None, until: Optional[Time] = None) -> int:
        """Run the simulation `by` a duration or `until` a time; the new time
        in picoseconds."""
        if (by is None) == (until is None):
            raise ValueError("advance needs one of by= or until=")
        params = {"by": _time(by)} if by is not None else {"until": _time(until)}
        return int(self.call("control.advance", **params)["now_ps"])

    def reset(self) -> None:
        self.call("control.reset")

    # ---- inputs ------------------------------------------------------------

    def set(self, target: str, input: str, value: Any, at: Optional[Time] = None) -> dict:
        params: dict = {"target": target, "input": input, "value": value}
        if at is not None:
            params["at"] = _time(at)
        return self.call("input.set", **params)

    def press(self, target: str) -> dict:
        return self.call("input.press", target=target)

    def release(self, target: str) -> dict:
        return self.call("input.release", target=target)

    def send(self, target: str, data: Union[str, bytes]) -> dict:
        """Bytes into a serial terminal or a board's UART (input.serial)."""
        if isinstance(data, bytes):
            return self.call("input.serial", target=target, bytes=base64.b64encode(data).decode("ascii"))
        return self.call("input.serial", target=target, text=data)

    def inputs(self) -> list[dict]:
        return list(self.call("input.list")["inputs"])

    # ---- what the circuit and the firmware do ------------------------------

    def snapshot(self, include: Iterable[str] = ("nets", "outputs", "probes")) -> dict:
        return self.call("observe.snapshot", include=list(include))

    def output(self, component: str) -> dict:
        """A component's outputs now (an MCU's: state, pc, serial.UNIT...)."""
        return self.snapshot(["outputs"]).get("outputs", {}).get(component, {})

    def net(self, name: str) -> dict:
        """A net's state now: {logic, v, ...}."""
        nets = self.snapshot(["nets"]).get("nets", {})
        if isinstance(nets, dict):
            return nets.get(name, {})
        return next((n for n in nets if n.get("name") == name), {})

    def serial(self, board: str, unit: Optional[str] = None) -> str:
        """What a board's firmware wrote to a UART (its console when no unit
        is named)."""
        b = next((x for x in self.boards() if x.get("id") == board), None)
        if b is None:
            raise KeyError(f"no board {board}")
        out = self.output(b["component"])
        if unit:
            return out.get(f"serial.{unit}", "")
        return out.get("rx_text", "")

    def probe(self, target: str, kind: str = "digital", **params: Any) -> str:
        """A probe on `net:NAME`, `device:ID.quantity` or `serial:BOARD`; its id,
        for read()."""
        return self.call("probe.add", target=target, kind=kind, **params)["probe"]

    def read(self, probe: str, **params: Any) -> list:
        """A probe's samples: [[time_ps, value], ...]."""
        return list(self.call("probe.read", probe=probe, **params).get("samples") or [])

    def diagnostics(self) -> list[dict]:
        return list(self.call("diagnostics.list").get("records") or [])

    # ---- scenarios and recordings (SPEC-007, SPEC-012) ---------------------

    def run_scenario(self, scenario: Union[str, os.PathLike, dict], until: Optional[Time] = None) -> dict:
        """A scenario file (a path) or a scenario object: the session is
        reset, its inputs applied, its assertions checked; the report."""
        params: dict = {"scenario": scenario} if isinstance(scenario, dict) else {"path": str(scenario)}
        if until is not None:
            params["until"] = _time(until)
        r = self.call("scenario.run", **params)
        return r.get("report", r)

    def record(self, path: Optional[str] = None) -> int:
        """Start recording from now (record.start); the start in picoseconds."""
        params = {"path": path} if path else {}
        return int(self.call("record.start", **params).get("from_ps", "0"))

    def stop_recording(self) -> dict:
        """record.stop: the recording itself (in `recording`) or where it went."""
        return self.call("record.stop")

    def replay(self, recording: Union[str, os.PathLike, dict], allow_drift: bool = False) -> dict:
        """A recording replayed: {ok, compared, divergence?, drift}."""
        params: dict = {"recording": recording} if isinstance(recording, dict) else {"path": str(recording)}
        return self.call("record.replay", allow_drift=allow_drift, **params)

    def to_scenario(self, recording: Union[str, os.PathLike, dict], name: Optional[str] = None,
                    assertions: Iterable[str] = ("probes", "serial")) -> dict:
        """A recording as a scenario (SPEC-012 §3): its inputs, and assertions
        on its probes' levels and edges and its serial text."""
        params: dict = {"recording": recording} if isinstance(recording, dict) else {"path": str(recording)}
        params["assert"] = list(assertions)
        if name:
            params["name"] = name
        return self.call("record.to_scenario", **params)["scenario"]

    # ---- the firmware's coverage and profile (SPEC-008 §5 analysis) --------

    def analyze(self, coverage: bool = True, profile: bool = True, board: Optional[str] = None) -> None:
        params: dict = {"coverage": coverage, "profile": profile}
        if board:
            params["board"] = board
        self.call("analysis.start", **params)

    def coverage(self, board: Optional[str] = None, format: str = "json") -> Any:
        """Each board's coverage ({board: {...}}), or the report as text
        (format "text" or "lcov")."""
        params: dict = {"format": format}
        if board:
            params["board"] = board
        r = self.call("analysis.coverage", **params)
        return r["boards"] if format == "json" else r["text"]

    def profile(self, board: Optional[str] = None, format: str = "json") -> Any:
        """Each board's profile, or the report as text (format "text",
        "collapsed" or "collapsed-insns")."""
        params: dict = {"format": format}
        if board:
            params["board"] = board
        r = self.call("analysis.profile", **params)
        return r["boards"] if format == "json" else r["text"]

    # ---- the debugger (SPEC-008 debug) -------------------------------------

    def where(self, board: str) -> dict:
        return self.call("debug.where", board=board)

    def registers(self, board: str) -> dict:
        return {r["name"]: r["value"] for r in self.call("debug.registers", board=board)["regs"]}

    def memory(self, board: str, address: int, length: int) -> bytes:
        return base64.b64decode(self.call("debug.memory", board=board, address=address, length=length)["base64"])

    def breakpoint(self, board: str, symbol: Optional[str] = None, address: Optional[int] = None,
                   file: Optional[str] = None, line: Optional[int] = None) -> Any:
        params: dict = {"board": board, "op": "add", "kind": "code"}
        for k, v in (("symbol", symbol), ("address", address), ("file", file), ("line", line)):
            if v is not None:
                params[k] = v
        return self.call("debug.breakpoint", **params).get("id")


__all__ = ["Session", "find_embsim", "EmbsimError"]
