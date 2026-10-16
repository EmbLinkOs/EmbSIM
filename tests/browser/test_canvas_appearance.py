# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real-engine canvas backgrounds, grid preferences and pan/reload regression."""
import argparse
import re
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.request
from playwright.sync_api import expect, sync_playwright
from PIL import Image


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--embsim", required=True, type=Path)
    parser.add_argument("--chromium")
    parser.add_argument("--screenshots", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="embsim-appearance-") as tmp:
        out = args.screenshots or Path(tmp)
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
            for _ in range(50):
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
                    viewport={"width": 1600, "height": 1000}, color_scheme="light"
                )
                errors = []
                page.on("pageerror", lambda e: errors.append(str(e)))
                page.goto(f"http://127.0.0.1:{port}/")
                canvas = page.get_by_role("application", name="Schematic canvas")
                expect(canvas).to_be_visible(timeout=15000)

                def choose(name: str) -> None:
                    radio = page.get_by_role("radio", name=name, exact=True)
                    radio.focus()
                    radio.press("Space")
                    expect(radio).to_be_checked()

                page.get_by_role("button", name="View", exact=True).click()
                choose("Engineering")
                page.screenshot(path=str(out / "engineering-paper.png"), full_page=True)
                for style in ["Lines", "Dots", "Crosses", "Plain", "Engineering"]:
                    choose(style)
                    image = page.locator(".es-scene__background").evaluate(
                        "(el) => getComputedStyle(el).backgroundImage"
                    )
                    assert (image == "none") == (style == "Plain"), style
                choose("Dots")
                choose("Warm")
                page.get_by_label("Grid spacing", exact=True).select_option("40")
                for _ in range(3):
                    page.get_by_role(
                        "slider", name="Line / dot size", exact=True
                    ).press("ArrowRight")
                for _ in range(6):
                    page.get_by_role("slider", name="Grid contrast", exact=True).press(
                        "ArrowRight"
                    )
                page.screenshot(path=str(out / "warm-dots.png"), full_page=True)
                for _ in range(3):
                    page.reload()
                    expect(canvas).to_be_visible(timeout=15000)
                page.get_by_role("button", name="View", exact=True).click()
                expect(
                    page.get_by_role("radio", name="Dots", exact=True)
                ).to_be_checked()
                expect(
                    page.get_by_role("radio", name="Warm", exact=True)
                ).to_be_checked()
                expect(page.get_by_label("Grid spacing", exact=True)).to_have_value(
                    "40"
                )
                expect(
                    page.get_by_role("slider", name="Line / dot size", exact=True)
                ).to_have_value("1.1")
                expect(
                    page.get_by_role("slider", name="Grid contrast", exact=True)
                ).to_have_value("0.18")
                choose("Blueprint")
                choose("Engineering")
                page.get_by_role("button", name="Switch to dark theme").click()
                page.screenshot(path=str(out / "blueprint-dark.png"), full_page=True)
                choose("Graphite")
                choose("Crosses")
                page.screenshot(path=str(out / "graphite-crosses.png"), full_page=True)
                # Plain paper makes the old finite-sheet strip measurable: pan
                # every component out of view, then compare pixels across it.
                choose("Paper")
                choose("Plain")
                page.get_by_role("button", name="Close tools", exact=True).click()
                canvas.focus()
                for _ in range(45):
                    canvas.press("ArrowLeft")
                page.wait_for_timeout(150)
                coverage = page.locator(".es-scene__background").evaluate(
                    "(el) => { const a=el.getBoundingClientRect(), b=el.parentElement.getBoundingClientRect(); return a.width===b.width && a.height===b.height && a.left===b.left && a.top===b.top; }"
                )
                assert coverage, "Background does not cover the full viewport"
                box = canvas.bounding_box()
                shot = out / "panned-plain.png"
                page.screenshot(path=str(shot))
                pixels = Image.open(shot).convert("RGB")
                samples = [
                    pixels.getpixel(
                        (
                            int(box["x"] + box["width"] * f),
                            int(box["y"] + box["height"] * 0.5),
                        )
                    )
                    for f in [0.05, 0.25, 0.5, 0.75, 0.95]
                ]
                color = page.locator(".es-scene__background").evaluate(
                    "(el) => getComputedStyle(el).backgroundColor"
                )
                expected = tuple(int(v) for v in re.findall(r"[0-9]+", color)[:3])
                assert set(samples) == {
                    expected
                }, f"Panned background has a seam or wrong surface: {samples}, expected {expected}"

                page.get_by_role("button", name="View", exact=True).click()
                page.get_by_role("button", name="Reset appearance", exact=True).click()
                expect(
                    page.get_by_role("radio", name="Engineering", exact=True)
                ).to_be_checked()
                expect(
                    page.get_by_role("radio", name="Paper", exact=True)
                ).to_be_checked()
                page.set_viewport_size({"width": 1024, "height": 700})
                assert page.evaluate(
                    "document.documentElement.scrollWidth <= innerWidth"
                ), "Appearance panel overflows compact viewport"
                expect(
                    page.get_by_role("region", name="Canvas appearance", exact=True)
                ).to_be_visible()
                page.set_viewport_size({"width": 1600, "height": 1000})
                page.get_by_role(
                    "checkbox", name="Focus canvas (hide side panels)", exact=True
                ).check()
                page.get_by_role("button", name="Close tools", exact=True).click()
                page.get_by_role("button", name="Switch to light theme").click()
                page.get_by_role("button", name="Zoom to fit", exact=True).click()
                page.wait_for_timeout(150)
                page.screenshot(path=str(out / "paper-workspace.png"), full_page=True)
                assert not errors, errors
                browser.close()
                print(
                    "PASS: five patterns, four palettes, spacing/weight/contrast, reload persistence, dark theme, seamless negative-world pan, reset and compact viewport"
                )
        finally:
            server.terminate()
            server.wait(timeout=10)


if __name__ == "__main__":
    main()
