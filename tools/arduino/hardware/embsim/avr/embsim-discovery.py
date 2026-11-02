#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""EmbSIM's serial ports for the Arduino IDE and arduino-cli (ADR-032): a
pluggable discovery (arduino-cli's discovery protocol, version 1) that lists
the boards' serial ports an EmbSIM session has open on this computer
(`embsim serve --serial B1`, the workbench's "Open on this computer"), as
the IDE lists a board plugged in by USB.

Each port is a "serial" port with its bridge's USB identity (the Uno's
2341:0043), so the IDE names the board itself ("Arduino Uno") and its
Upload button runs the platform's own avrdude against it, through the
board's boot loader, as on a real board.

EmbSIM writes a file per open port in <temp>/embsim-ports (TMPDIR, else
/tmp); a port whose process is gone is not listed.

    embsim-discovery.py            speak the protocol on stdin/stdout
    embsim-discovery.py --list     print the ports found, one per line
"""
import json
import os
import sys
import tempfile
import threading
import time
from pathlib import Path

REGISTRY = Path(os.environ.get("TMPDIR") or ("/tmp" if os.name == "posix" else tempfile.gettempdir())) / "embsim-ports"


def alive(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def ports() -> dict:
    """address -> the discovery's port object."""
    found = {}
    try:
        entries = sorted(REGISTRY.glob("*.json"))
    except OSError:
        return found
    for f in entries:
        try:
            e = json.loads(f.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        if not alive(int(e.get("pid", 0))) or not os.path.exists(e.get("address", "")):
            continue
        name = e.get("board_name") or e.get("board") or "board"
        label = f"{e['address']} (EmbSIM {e.get('target', '')}: {name}"
        label += f", {e['project']})" if e.get("project") else ")"
        props = {"serialNumber": f"EMBSIM-{e.get('pid')}-{e.get('target', '')}", "embsim": "true"}
        if e.get("vid") and e.get("usb_pid"):
            props["vid"] = e["vid"].lower()
            props["pid"] = e["usb_pid"].lower()
        found[e["address"]] = {
            "address": e["address"],
            "label": label,
            "protocol": "serial",
            "protocolLabel": "Serial Port (EmbSIM)",
            "hardwareId": props["serialNumber"],
            "properties": props,
        }
    return found


class Discovery:
    def __init__(self, out=sys.stdout):
        self.out = out
        self.lock = threading.Lock()
        self.started = False
        self.sync = None

    def send(self, obj: dict) -> None:
        with self.lock:
            self.out.write(json.dumps(obj) + "\n")
            self.out.flush()

    def watch(self, stop: threading.Event) -> None:
        known = {}
        while not stop.is_set():
            now = ports()
            for a, p in now.items():
                if a not in known:
                    self.send({"eventType": "add", "port": p})
            for a, p in known.items():
                if a not in now:
                    self.send({"eventType": "remove", "port": {"address": a, "protocol": "serial"}})
            known = now
            stop.wait(0.5)

    def command(self, line: str) -> bool:
        words = line.strip().split(" ", 1)
        cmd = words[0].upper() if words and words[0] else ""
        if cmd == "HELLO":
            self.send({"eventType": "hello", "protocolVersion": 1, "message": "OK"})
        elif cmd == "START":
            self.started = True
            self.send({"eventType": "start", "message": "OK"})
        elif cmd == "LIST":
            if not self.started:
                self.send({"eventType": "list", "error": True, "message": "discovery not STARTed"})
            else:
                self.send({"eventType": "list", "ports": list(ports().values())})
        elif cmd == "START_SYNC":
            if self.sync is None:
                stop = threading.Event()
                t = threading.Thread(target=self.watch, args=(stop,), daemon=True)
                self.sync = (stop, t)
                self.send({"eventType": "start_sync", "message": "OK"})
                t.start()
            else:
                self.send({"eventType": "start_sync", "error": True, "message": "already syncing"})
        elif cmd == "STOP":
            if self.sync:
                self.sync[0].set()
                self.sync = None
            self.started = False
            self.send({"eventType": "stop", "message": "OK"})
        elif cmd == "QUIT":
            if self.sync:
                self.sync[0].set()
            self.send({"eventType": "quit", "message": "OK"})
            return False
        elif cmd:
            self.send({"eventType": "command_error", "error": True, "message": f"Command {cmd} not supported"})
        return True


def main() -> int:
    if "--list" in sys.argv[1:]:
        for p in ports().values():
            print(p["label"])
        return 0
    d = Discovery()
    for line in sys.stdin:
        if not d.command(line):
            break
    time.sleep(0.05)
    return 0


if __name__ == "__main__":
    sys.exit(main())
