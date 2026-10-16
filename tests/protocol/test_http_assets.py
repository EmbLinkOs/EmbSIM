#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Static requests and reload assets must not wait for an active WebSocket."""
import argparse
import http.client
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import WebSocketClient  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--embsim", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-http-assets-") as tmp:
        ui = Path(tmp)
        (ui / "index.html").write_text("<!doctype html><title>Reload test</title>")
        (ui / "asset.js").write_text("/* asset during active simulation */")
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        server = subprocess.Popen(
            [
                str(args.embsim.resolve()),
                "serve",
                str(ROOT / "examples/button-led/project.json"),
                "--port",
                str(port),
                "--ui",
                str(ui),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        client = None
        try:
            for _ in range(100):
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                        break
                except OSError:
                    time.sleep(0.05)
            client = WebSocketClient(f"ws://127.0.0.1:{port}/ws")
            before = client.call("session.info", timeout=2)
            client.call("control.run", {}, timeout=2)
            time.sleep(0.05)

            def request(path: str, headers=None):
                conn = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
                try:
                    conn.request("GET", path, headers=headers or {})
                    response = conn.getresponse()
                    return response.status, response.read()
                finally:
                    conn.close()

            assert request("/")[0] == 200
            status, body = request("/asset.js")
            assert status == 200 and body == (ui / "asset.js").read_bytes()
            assert request("/", {"Host": "untrusted.invalid"})[0] == 403
            upgrade = {
                "Connection": "Upgrade",
                "Upgrade": "websocket",
                "Sec-WebSocket-Version": "13",
                "Sec-WebSocket-Key": "dGhlIHNhbXBsZSBub25jZQ==",
                "Origin": f"http://127.0.0.1:{port}",
            }
            assert request("/ws", upgrade)[0] == 409
            assert (
                request("/ws", {**upgrade, "Origin": "https://untrusted.invalid"})[0]
                == 403
            )
            # An incomplete auxiliary request cannot hold the active channel for
            # the ordinary ten-second initial-handshake deadline.
            with socket.create_connection(("127.0.0.1", port), timeout=2) as partial:
                partial.sendall(b"GET / HTTP/1.1\r\n")
                after = client.call("session.info", timeout=2)
                assert after["project"] == before["project"]
                assert after["running"] and int(after["now_ps"]) > int(before["now_ps"])
            assert (
                client.call("session.info", timeout=2)["project"] == before["project"]
            )
            client.call("control.pause", {}, timeout=2)
            print(
                "PASS: assets during WebSocket session, host/origin checks, busy-session rejection and bounded incomplete requests"
            )
        finally:
            if client:
                client.close()
            server.terminate()
            server.wait(timeout=10)


if __name__ == "__main__":
    main()
