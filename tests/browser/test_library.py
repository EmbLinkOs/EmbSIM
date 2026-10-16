# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Exercise discovery, placement, waveforms and live controls against the native engine."""
import argparse
import json
import re
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.request

from playwright.sync_api import expect, sync_playwright


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Real-engine library, placement, waveform and live-control browser regression."
    )
    parser.add_argument("--embsim", required=True, type=Path)
    parser.add_argument("--chromium")
    parser.add_argument("--screenshots", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]

    _tmp = tempfile.TemporaryDirectory(prefix="embsim-library-browser-")
    out = args.screenshots or Path(_tmp.name)
    out.mkdir(parents=True, exist_ok=True)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]

    server = subprocess.Popen(
        [
            str(args.embsim.resolve()),
            "serve",
            str(root / "examples/optocoupled-input/project.json"),
            "--port",
            str(port),
            "--ui",
            str(root / "ui/apps/workbench/dist"),
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        for i in range(50):
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        with sync_playwright() as p:
            browser = p.chromium.launch(
                executable_path=args.chromium,
                headless=True,
                args=["--no-sandbox", "--no-proxy-server"],
            )
            page = browser.new_page(
                viewport={"width": 1440, "height": 1000}, color_scheme="light"
            )
            errors = []
            frames = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.on(
                "websocket",
                lambda ws: ws.on("framereceived", lambda f: frames.append(f)),
            )
            page.goto(f"http://127.0.0.1:{port}/")
            expect(
                page.get_by_role("application", name="Schematic canvas")
            ).to_be_visible(timeout=15000)
            page.get_by_role("button", name="Filter components", exact=True).click()
            page.get_by_label("Manufacturer", exact=True).select_option("Vishay")
            page.get_by_label("Package", exact=True).select_option("DIP-4")
            page.get_by_label("Search components", exact=True).fill("opto 617")
            expect(
                page.get_by_role("button", name="Preview VO617A-3", exact=True)
            ).to_be_visible()
            page.get_by_role("button", name="Preview VO617A-3", exact=True).click()
            expect(
                page.get_by_role("region", name="Component preview")
            ).to_contain_text("Source reviewed")
            page.screenshot(path=str(out / "library-light.png"), full_page=True)
            page.get_by_role("button", name="pins", exact=True).click()
            expect(
                page.get_by_role("region", name="Component preview")
            ).to_contain_text("Terminal")
            page.get_by_role("button", name="models", exact=True).click()
            expect(
                page.get_by_role("region", name="Component preview")
            ).to_contain_text("Not provided")
            page.get_by_role("button", name="Clear filters", exact=True).click()
            page.locator("summary").filter(has_text="Browse categories").click()
            page.get_by_role("button", name=re.compile(r"^Optoelectronics")).click()
            expect(
                page.get_by_role("button", name="Preview BPW34", exact=True)
            ).to_be_visible()
            expect(
                page.get_by_role("button", name="Preview 1N4007", exact=True)
            ).to_have_count(0)
            page.locator("summary").filter(has_text="Optoelectronics").click()
            page.get_by_role("button", name="Clear filters", exact=True).click()
            page.get_by_label("Search components", exact=True).fill("B3F-1000")
            page.get_by_role("button", name="Place B3F-1000", exact=True).click()
            expect(
                page.get_by_role("button", name="Preview B3F-1000", exact=True)
            ).to_be_visible()
            page.get_by_role("button", name="Switch to dark theme").click()
            page.get_by_role("button", name="Preview B3F-1000", exact=True).click()
            page.screenshot(path=str(out / "library-dark.png"), full_page=True)
            page.set_viewport_size({"width": 1024, "height": 700})
            expect(page.get_by_role("region", name="Component preview")).to_be_visible()
            assert page.evaluate(
                "document.documentElement.scrollWidth <= window.innerWidth"
            ), "The explorer must fit the supported compact viewport"
            page.set_viewport_size({"width": 1440, "height": 1000})
            page.get_by_role("radio", name="Run", exact=True).click()
            page.get_by_role("button", name="Run", exact=True).click()
            time.sleep(0.25)
            expect(
                page.get_by_role("button", name="Hold U2 pressed", exact=True)
            ).to_be_visible()
            holds = page.get_by_role("button", name="Hold SW1 pressed", exact=True)
            expect(holds).to_be_visible()
            box = holds.bounding_box()
            page.mouse.move(box["x"] + box["width"] / 2, box["y"] + box["height"] / 2)
            page.mouse.down()
            time.sleep(0.3)
            page.mouse.up()
            time.sleep(0.3)
            page.get_by_role("button", name="Pause", exact=True).click()
            expect(page.get_by_role("button", name="Run", exact=True)).to_be_visible()
            time.sleep(0.1)
            page.screenshot(path=str(out / "optocoupled-run.png"), full_page=True)
            packets = [json.loads(f) for f in frames if isinstance(f, str)]
            samples = [
                sample
                for f in packets
                if f.get("method") == "waveform.samples"
                and f["params"].get("probe") == "out"
                for sample in f["params"].get("samples", [])
            ]
            assert (
                len(samples) > 10
            ), "Waveform stream must survive component placement and engine restart"
            assert any(
                float(s[1]) < 0.05 for s in samples
            ), "Live press must drive the optocoupler output low"
            assert any(
                float(s[1]) > 3.2 for s in samples
            ), "Live release must restore the pulled-up output"
            assert any(
                f.get("method") == "observe.update"
                and f["params"].get("running") is False
                for f in packets
            )
            assert not errors, errors
            (out / "frames.json").write_text(json.dumps(frames[-200:]))
            print(
                "PASS real-engine discovery, metadata filters, preview tabs, placement, themes, Run and live press/release; no page errors"
            )
            browser.close()
    except Exception:
        try:
            page.screenshot(path=str(out / "failure.png"), full_page=True)
        except Exception:
            pass
        raise
    finally:
        server.terminate()
        server.wait(timeout=5)


if __name__ == "__main__":
    main()
