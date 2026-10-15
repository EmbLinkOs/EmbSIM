"""Optional real-browser check. Requires Playwright and an installed Chromium.

Run with EMB_BROWSER=/path/to/chromium python tests/browser_smoke.py.
No browser dependency is required for the native/core test suite.
"""
import os
from pathlib import Path
import re
import subprocess
import sys

from playwright.sync_api import expect, sync_playwright

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "build/browser"


def main():
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    process = subprocess.Popen([sys.executable, "scripts/embsim.py", "serve", "examples/button-led.json", "--port", "0"],
                               cwd=ROOT, stdout=subprocess.PIPE, text=True)
    try:
        line = process.stdout.readline()
        match = re.search(r"port (\d+)", line)
        if not match:
            raise RuntimeError(f"server did not start: {line}")
        with sync_playwright() as playwright:
            options = {"headless": True}
            if os.environ.get("EMB_BROWSER"):
                options["executable_path"] = os.environ["EMB_BROWSER"]
            browser = playwright.chromium.launch(**options)
            try:
                page = browser.new_page(viewport={"width": 1440, "height": 1050})
                errors = []
                page.on("pageerror", lambda error: errors.append(str(error)))
                page.goto(f"http://127.0.0.1:{match.group(1)}")
                expect(page.get_by_role("heading", name="button-led", exact=True)).to_be_visible()
                button, indicator = page.locator("#component-SW1 button"), page.locator("#component-LED1")
                button.click()
                expect(indicator).to_have_class(re.compile(r"\blit\b"))
                page.screenshot(path=str(ARTIFACTS / "workbench-desktop.png"), full_page=True)
                button.click()
                expect(indicator).not_to_have_class(re.compile(r"\blit\b"))
                page.locator("#step").click()
                expect(page.locator("#clock")).to_contain_text("1,000,000")
                page.locator("#run").click()
                expect(page.locator("#clock")).to_contain_text("Running")
                page.locator("#pause").click()
                expect(page.locator("#clock")).to_contain_text("Paused")
                page.locator("#reset").click()
                expect(page.locator("#clock")).to_contain_text("0 ns")
                with page.expect_download() as download:
                    page.locator("#export").click()
                assert download.value.suggested_filename == "button-led-snapshot.json"
                page.set_viewport_size({"width": 390, "height": 844})
                page.screenshot(path=str(ARTIFACTS / "workbench-mobile.png"), full_page=True)
                assert page.evaluate("() => document.documentElement.scrollWidth <= innerWidth"), "horizontal overflow"
                button.click()
                expect(indicator).to_have_class(re.compile(r"\blit\b"))
                assert not errors, errors
                print("PASS: desktop/mobile controls, stepping, run/pause/reset, export; no page errors or mobile overflow")
            finally:
                browser.close()
    finally:
        process.terminate()
        process.wait(timeout=5)
        process.stdout.close()


if __name__ == "__main__":
    main()
