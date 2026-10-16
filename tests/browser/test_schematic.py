# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real-engine layout preview, apply/undo/redo and navigation regression."""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.request

from playwright.sync_api import expect, sync_playwright


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--embsim", required=True, type=Path)
    parser.add_argument("--chromium")
    parser.add_argument("--screenshots", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="embsim-schematic-") as tmp:
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
                sent, received, errors = [], [], []
                page.on("pageerror", lambda e: errors.append(str(e)))

                def websocket(ws):
                    ws.on(
                        "framesent",
                        lambda f: (
                            sent.append(json.loads(f)) if isinstance(f, str) else None
                        ),
                    )
                    ws.on(
                        "framereceived",
                        lambda f: (
                            received.append(json.loads(f))
                            if isinstance(f, str)
                            else None
                        ),
                    )

                page.on("websocket", websocket)
                page.goto(f"http://127.0.0.1:{port}/")
                expect(
                    page.get_by_role("application", name="Schematic canvas")
                ).to_be_visible(timeout=15000)
                expect(
                    page.get_by_role("button", name="Auto layout", exact=True)
                ).to_be_enabled()

                def projects():
                    ids = {f["id"] for f in sent if f.get("method") == "project.get"}
                    return [
                        f["result"]["project"]
                        for f in received
                        if f.get("id") in ids and "result" in f
                    ]

                page.wait_for_timeout(250)
                original = projects()[-1]
                # The protocol materializes an omitted empty bus list on layout edits.
                original.setdefault("layout", {}).setdefault("buses", [])
                before_transactions = len(
                    [f for f in sent if f.get("method") == "project.transaction"]
                )
                page.get_by_role("button", name="Auto layout", exact=True).click()
                expect(
                    page.get_by_role("region", name="Layout studio", exact=True)
                ).to_be_visible()
                page.get_by_role("button", name="Preview layout", exact=True).click()
                expect(
                    page.get_by_text("Preview · not applied", exact=True)
                ).to_be_visible()
                expect(page.get_by_text("Layout preview", exact=True)).to_be_visible()
                assert (
                    len([f for f in sent if f.get("method") == "project.transaction"])
                    == before_transactions
                ), "Preview mutated the engine"
                page.wait_for_timeout(200)
                page.screenshot(
                    path=str(out / "studio-preview-light.png"), full_page=True
                )
                page.get_by_role("button", name="Apply layout", exact=True).click()
                expect(page.get_by_text("Layout applied", exact=True)).to_be_visible()
                page.wait_for_timeout(200)
                applied = projects()[-1]
                assert (
                    original["nets"] == applied["nets"]
                ), "Layout changed electrical nets"
                assert original.get("boards", []) == applied.get(
                    "boards", []
                ), "This example has no moved boards"

                def electrical(project):
                    return [
                        {k: v for k, v in c.items() if k != "layout"}
                        for c in project["components"]
                    ]

                assert electrical(original) == electrical(
                    applied
                ), "Layout changed component values"
                assert applied != original, "Apply must change actual engine geometry"
                edits = [
                    e
                    for f in sent
                    if f.get("method") == "project.transaction"
                    for e in f.get("params", {}).get("edits", [])
                ]
                assert edits and all(
                    "set_layout" in e for e in edits
                ), "Only layout edits are allowed"
                commits = {
                    f["id"]
                    for f in sent
                    if f.get("method") == "project.transaction"
                    and f.get("params", {}).get("op") == "commit"
                }
                commit_results = [
                    f["result"]
                    for f in received
                    if f.get("id") in commits and isinstance(f.get("result"), dict)
                ]
                assert commit_results and all(
                    not r.get("restarted", False) for r in commit_results
                ), "Layout restarted simulation or lacked a commit result"
                # Undo and redo go through the server, not only the visible preview.
                page.get_by_role("button", name="Undo", exact=False).first.click()
                page.wait_for_timeout(250)
                assert (
                    projects()[-1] == original
                ), "One Undo must restore all old layout fields"
                page.get_by_role("button", name="Redo", exact=False).first.click()
                page.wait_for_timeout(250)
                assert projects()[-1] == applied, "Redo must restore the applied plan"
                page.get_by_role("button", name="Find", exact=True).click()
                page.get_by_label("Component or net", exact=True).fill("SW1")
                page.get_by_label("Component or net", exact=True).press("Enter")
                page.get_by_role("button", name="Find", exact=True).click()
                page.get_by_label("Component or net", exact=True).fill("OUT")
                page.get_by_label("Component or net", exact=True).press("Enter")
                page.get_by_role("button", name="View", exact=True).click()
                page.get_by_label("Show grid", exact=True).uncheck()
                page.get_by_role(
                    "checkbox", name="Sheet navigator", exact=True
                ).uncheck()
                expect(
                    page.get_by_role("checkbox", name="Sheet navigator", exact=True)
                ).not_to_be_checked()
                page.get_by_role(
                    "checkbox", name="Focus canvas (hide side panels)", exact=True
                ).check()
                expect(
                    page.get_by_role("region", name="Library", exact=True)
                ).to_have_count(0)
                page.get_by_role(
                    "checkbox", name="Focus canvas (hide side panels)", exact=True
                ).uncheck()
                expect(
                    page.get_by_role("region", name="Library", exact=True)
                ).to_be_visible()
                page.get_by_role("button", name="Close tools", exact=True).click()
                page.get_by_role("button", name="Switch to dark theme").click()
                page.get_by_role("button", name="Zoom to fit", exact=True).click()
                page.wait_for_timeout(200)
                page.screenshot(path=str(out / "studio-dark.png"), full_page=True)
                page.set_viewport_size({"width": 1024, "height": 700})
                assert page.evaluate(
                    "document.documentElement.scrollWidth <= innerWidth"
                ), "Compact viewport overflows"
                expect(
                    page.get_by_role("button", name="Preview layout", exact=True)
                ).to_be_visible()
                page.screenshot(path=str(out / "studio-compact.png"), full_page=True)
                page.set_viewport_size({"width": 1600, "height": 1000})
                # The requested connect-then-arrange workflow: add two real parts,
                # wire their pins from the keyboard, then undo connection + layout.
                page.get_by_label("Search components", exact=True).fill(
                    "emblink/resistor"
                )

                def wait_component_count(count):
                    for _ in range(100):
                        if projects() and len(projects()[-1]["components"]) == count:
                            return projects()[-1]
                        page.wait_for_timeout(50)
                    raise AssertionError(f"Engine did not reach {count} components")

                count = len(projects()[-1]["components"])
                page.get_by_role("button", name="Place Resistor", exact=True).click()
                wait_component_count(count + 1)
                page.get_by_role("button", name="Place Resistor", exact=True).click()
                added = wait_component_count(count + 2)
                new_ids = [
                    c["id"]
                    for c in added["components"]
                    if c["id"] not in {x["id"] for x in applied["components"]}
                ]
                assert len(new_ids) == 2, new_ids
                page.get_by_role("button", name="View", exact=True).click()
                page.get_by_role(
                    "checkbox", name="Auto arrange after connecting", exact=True
                ).check()
                page.get_by_role("button", name="Close tools", exact=True).click()
                page.get_by_role("button", name="Zoom to fit", exact=True).click()
                page.wait_for_timeout(200)
                page.locator(f'[data-pin="{new_ids[0]}.2"]').press("Enter")
                page.locator(f'[data-pin="{new_ids[1]}.1"]').press("Enter")
                page.wait_for_timeout(300)
                connected = projects()[-1]
                assert any(
                    {f"{new_ids[0]}.2", f"{new_ids[1]}.1"} <= set(pins)
                    for pins in connected["nets"].values()
                ), "Keyboard wire did not connect both pins"
                assert [
                    c.get("layout")
                    for c in connected["components"]
                    if c["id"] in new_ids
                ] != [
                    c.get("layout") for c in added["components"] if c["id"] in new_ids
                ], "Connect should automatically arrange the joined parts"
                page.get_by_role("button", name="Undo", exact=True).click()
                page.wait_for_timeout(250)
                assert (
                    projects()[-1] == added
                ), "One Undo must restore both connectivity and placement"
                # Explicit manual routing wins over the enabled auto-arrange preference.
                page.locator(f'[data-pin="{new_ids[0]}.2"]').press("Enter")
                for _ in range(6):
                    page.keyboard.press("Shift+ArrowDown")
                page.keyboard.press("Space")
                page.keyboard.press("Enter")
                marker = page.locator(".es-scene__wire-waypoint")
                expect(marker).to_have_count(1)
                waypoint = marker.evaluate(
                    "e => [Number(e.getAttribute('cx')), Number(e.getAttribute('cy'))]"
                )
                assert (
                    projects()[-1] == added
                ), "Unfinished bends must not mutate the project"
                page.locator(f'[data-pin="{new_ids[1]}.1"]').press("Enter")
                page.wait_for_timeout(300)
                manual = projects()[-1]
                assert any(
                    {f"{new_ids[0]}.2", f"{new_ids[1]}.1"} <= set(pins)
                    for pins in manual["nets"].values()
                ), "Manual route must connect the requested pins"
                assert [c.get("layout") for c in manual["components"]] == [
                    c.get("layout") for c in added["components"]
                ], "Manual bends must bypass automatic arrangement"
                assert any(
                    waypoint in w["points"] for w in manual["layout"]["wires"]
                ), "Native project lost the explicit bend"
                page.get_by_role("button", name="Undo", exact=True).click()
                page.wait_for_timeout(250)
                assert (
                    projects()[-1] == added
                ), "One Undo must remove the entire manual connection"
                # Repair an existing track: only layout edits, unchanged electrical data.
                index = next(
                    i
                    for i, w in enumerate(added["layout"]["wires"])
                    if len(w["points"]) > 1
                    and w["points"][0] != w["points"][1]
                    and (
                        w["points"][0][0] == w["points"][1][0]
                        or w["points"][0][1] == w["points"][1][1]
                    )
                )
                page.locator(f'[data-wire="{index}"]').press("Enter")
                start_frames = len(sent)
                page.get_by_label("Segment offset", exact=True).fill("30")
                page.get_by_role(
                    "button", name="Move track segment", exact=True
                ).click()
                page.wait_for_timeout(300)
                reshaped = projects()[-1]
                assert reshaped["nets"] == added["nets"]
                assert reshaped["components"] == added["components"]
                original_track = added["layout"]["wires"][index]["points"]
                changed_track = reshaped["layout"]["wires"][index]["points"]
                assert changed_track != original_track
                assert (
                    changed_track[0] == original_track[0]
                    and changed_track[-1] == original_track[-1]
                )
                edits = [
                    e
                    for f in sent[start_frames:]
                    if f.get("method") == "project.transaction"
                    for e in f.get("params", {}).get("edits", [])
                ]
                assert edits and all("set_layout" in e for e in edits), edits
                page.get_by_role("button", name="Undo", exact=True).click()
                page.wait_for_timeout(250)
                assert projects()[-1] == added
                # Set both fresh resistors in one transaction, then restore exactly.
                page.get_by_role("application", name="Schematic canvas").press("Escape")
                for component_id in new_ids:
                    page.locator(
                        f'[data-component="{component_id}"] [role="button"]'
                    ).first.press("Space")
                page.get_by_role("checkbox", name="Set resistance", exact=True).check()
                page.get_by_label("Shared resistance", exact=False).fill("2.2k")
                page.get_by_role(
                    "button", name="Apply shared properties", exact=True
                ).click()
                page.wait_for_timeout(400)
                batch = projects()[-1]
                assert all(
                    c.get("params", {}).get("resistance") in ("2.2k", 2200)
                    for c in batch["components"]
                    if c["id"] in new_ids
                )
                assert batch["nets"] == added["nets"]
                page.get_by_role("button", name="Undo", exact=True).click()
                page.wait_for_timeout(300)
                assert projects()[-1] == added
                # Erasing drawing must keep the electrical net and become a navigable check.
                page.locator(f'[data-wire="{index}"]').press("Enter")
                net_name = added["layout"]["wires"][index]["net"]
                page.get_by_role("button", name="Clear drawing", exact=True).click()
                page.wait_for_timeout(250)
                erased = projects()[-1]
                assert erased["nets"] == added["nets"]
                assert not any(w["net"] == net_name for w in erased["layout"]["wires"])
                page.get_by_role("button", name="Checks", exact=False).click()
                result = page.locator(f'[data-check-id="undrawn-net:{net_name}"]')
                expect(result).to_be_visible(timeout=10000)
                result.click()
                expect(
                    page.get_by_role("button", name="Locate on sheet", exact=True)
                ).to_be_visible()
                page.get_by_role("button", name="View", exact=True).click()
                page.get_by_label("Show grid", exact=True).check()
                page.get_by_role("checkbox", name="Sheet navigator", exact=True).check()
                page.get_by_role("button", name="Close tools", exact=True).click()
                page.get_by_label("Search components", exact=True).fill("")
                page.get_by_role("button", name="Zoom to fit", exact=True).click()
                expect(
                    page.get_by_role("button", name="Auto layout", exact=True)
                ).to_be_enabled()
                page.wait_for_timeout(200)
                page.screenshot(
                    path=str(out / "authoring-checks-dark.png"), full_page=True
                )
                page.get_by_role("button", name="Switch to light theme").click()
                expect(
                    page.get_by_role("button", name="Auto layout", exact=True)
                ).to_be_enabled()
                page.wait_for_timeout(200)
                page.screenshot(
                    path=str(out / "authoring-checks-light.png"), full_page=True
                )
                page.set_viewport_size({"width": 1024, "height": 700})
                assert page.evaluate(
                    "document.documentElement.scrollWidth <= innerWidth"
                ), "Checks overflow the compact viewport"
                page.screenshot(path=str(out / "authoring-compact.png"), full_page=True)
                page.set_viewport_size({"width": 1600, "height": 1000})
                page.get_by_role("button", name="Undo", exact=True).click()
                page.wait_for_timeout(300)
                assert projects()[-1] == added
                # Organization is exercised against the real transaction server.
                page.get_by_role("tab", name="Objects", exact=True).click()
                objects = page.get_by_role("region", name="Schematic objects")
                objects.get_by_role("button", name="R1", exact=True).click()
                objects.get_by_role("button", name="R2", exact=True).click(
                    modifiers=["Shift"]
                )
                page.get_by_label("Group name", exact=True).fill("Input stage")
                page.get_by_role("button", name="Group selection", exact=True).click()
                expect(
                    page.get_by_role(
                        "button", name="Select group Input stage", exact=True
                    )
                ).to_have_count(1)
                objects.get_by_role("button", name="Lock group", exact=True).click()
                expect(
                    objects.get_by_role("button", name="Unlock R1", exact=True)
                ).to_be_visible()
                page.wait_for_timeout(250)
                organized = projects()[-1]
                assert organized["nets"] == added["nets"]
                assert organized["layout"]["groups"][0]["members"] == ["R1", "R2"]
                assert all(
                    c.get("layout", {}).get("locked")
                    for c in organized["components"]
                    if c["id"] in ["R1", "R2"]
                )
                objects.get_by_role("button", name="R1", exact=True).click()
                expect(page.get_by_label("Position X", exact=True)).to_be_disabled()
                hit = page.locator('[data-component="R1"] .es-component__hit')
                box = hit.bounding_box()
                assert box
                transactions = len(
                    [f for f in sent if f.get("method") == "project.transaction"]
                )
                page.mouse.move(
                    box["x"] + box["width"] / 2, box["y"] + box["height"] / 2
                )
                page.mouse.down()
                page.mouse.move(
                    box["x"] + box["width"] / 2 + 40,
                    box["y"] + box["height"] / 2 + 30,
                    steps=4,
                )
                page.mouse.up()
                page.wait_for_timeout(150)
                assert transactions == len(
                    [f for f in sent if f.get("method") == "project.transaction"]
                ), "Locked drag created a transaction"
                objects.get_by_role(
                    "button", name="Input stage · 2 parts", exact=True
                ).click()
                page.screenshot(
                    path=str(out / "organization-light.png"), full_page=True
                )
                page.get_by_role("button", name="Switch to dark theme").click()
                page.screenshot(path=str(out / "organization-dark.png"), full_page=True)
                page.get_by_role("button", name="Switch to light theme").click()
                page.set_viewport_size({"width": 1024, "height": 700})
                assert page.evaluate(
                    "document.documentElement.scrollWidth <= innerWidth"
                )
                page.screenshot(
                    path=str(out / "organization-compact.png"), full_page=True
                )
                page.set_viewport_size({"width": 1600, "height": 1000})
                page.get_by_role("button", name="Undo", exact=True).click()
                expect(
                    objects.get_by_role("button", name="Lock R1", exact=True)
                ).to_be_visible()
                page.get_by_role("button", name="Undo", exact=True).click()
                expect(page.locator("[data-group]")).to_have_count(0)
                page.wait_for_timeout(250)
                restored = projects()[-1]
                restored.get("layout", {}).pop("groups", None)
                for component in restored["components"]:
                    if component.get("layout", {}).get("locked") is False:
                        component["layout"].pop("locked")
                assert restored == added
                assert not errors, errors
                browser.close()
                print(
                    "PASS: real-engine preview, layout-only apply, exact undo/redo, finder, connect-then-arrange, manual routing, segment repair, batch properties, structural checks, persistent groups, native locks, view controls, themes and compact viewport"
                )
        finally:
            server.terminate()
            server.wait(timeout=10)


if __name__ == "__main__":
    main()
