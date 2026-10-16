#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Organization metadata persists through real transactions, save and reload."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient, EmbsimError  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--embsim", required=True)
    args = parser.parse_args()
    source = ROOT / "examples/button-led/project.json"
    with tempfile.TemporaryDirectory(prefix="embsim-organization-") as tmp:
        saved = Path(tmp) / "organized.json"
        with StdioClient([args.embsim, "sidecar", str(source)]) as client:
            original = client.call("project.get")["project"]
            ids = [part["id"] for part in original["components"][:2]]
            part = original["components"][0]
            placement = {**part.get("layout", {"x": 0, "y": 0}), "locked": True}
            groups = [{"id": "input", "name": "Input stage", "members": ids}]
            client.call("control.advance", {"by": "1ms"})
            now = client.call("session.info")["now_ps"]
            tx = client.call("project.transaction", {"op": "begin"})["tx"]
            client.call(
                "project.transaction",
                {
                    "op": "edit",
                    "tx": tx,
                    "edits": [
                        {"set_layout": {"id": part["id"], "layout": placement}},
                        {"set_layout": {"groups": groups}},
                    ],
                },
            )
            result = client.call("project.transaction", {"op": "commit", "tx": tx})
            assert result["applied"] and not result["restarted"]
            assert client.call("session.info")["now_ps"] == now
            tx = client.call("project.transaction", {"op": "begin"})["tx"]
            try:
                client.call(
                    "project.transaction",
                    {
                        "op": "edit",
                        "tx": tx,
                        "edits": [
                            {
                                "set_layout": {
                                    "id": part["id"],
                                    "layout": {**placement, "x": placement["x"] + 100},
                                }
                            }
                        ],
                    },
                )
                raise AssertionError("locked move was accepted")
            except EmbsimError:
                pass
            client.call("project.transaction", {"op": "abort", "tx": tx})
            after = client.call("project.get")["project"]
            assert after["nets"] == original["nets"]
            client.call("project.save", {"path": str(saved)})
            persisted = json.loads(saved.read_text())
            assert persisted["layout"]["groups"] == groups
            client.call(
                "project.load", {"json": persisted, "base_dir": str(source.parent)}
            )
            reloaded = client.call("project.get")["project"]
            assert reloaded["layout"]["groups"] == groups
            assert reloaded["components"][0]["layout"] == placement
        print(
            "PASS: groups and locks persist; locked moves rejected; layout edits preserve circuit and simulation time"
        )


if __name__ == "__main__":
    main()
