# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real-engine wired loading, net tracing and current electrical warning workflow."""
import argparse
from contextlib import contextmanager
import json
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time
import urllib.request

from playwright.sync_api import expect, sync_playwright

ROOT = Path(__file__).resolve().parents[2]


@contextmanager
def serve(binary, example):
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    server = subprocess.Popen([str(binary), 'serve', str(ROOT / f'examples/{example}/project.json'), '--port', str(port), '--ui', str(ROOT / 'ui/apps/workbench/dist')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            try:
                urllib.request.urlopen(f'http://127.0.0.1:{port}/', timeout=1)
                break
            except OSError:
                time.sleep(.1)
        else:
            raise AssertionError('native HTTP server did not start')
        yield f'http://127.0.0.1:{port}/'
    finally:
        server.terminate()
        server.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True, type=Path)
    parser.add_argument('--chromium')
    parser.add_argument('--screenshots', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-instrument-browser-') as tmp, sync_playwright() as pw:
        out = args.screenshots or Path(tmp)
        out.mkdir(parents=True, exist_ok=True)
        browser = pw.chromium.launch(executable_path=args.chromium, headless=True, args=['--no-sandbox'])
        page = browser.new_page(viewport={'width': 1800, 'height': 1200}, color_scheme='light')
        errors, sent, received = [], [], []
        page.on('pageerror', lambda e: errors.append(str(e)))

        def websocket(ws):
            ws.on('framesent', lambda frame: sent.append(json.loads(frame)) if isinstance(frame, str) else None)
            ws.on('framereceived', lambda frame: received.append(json.loads(frame)) if isinstance(frame, str) else None)
        page.on('websocket', websocket)

        def advance(amount):
            clock = page.get_by_label('Advance amount', exact=True)
            clock.fill(amount)
            clock.press('Enter')

        with serve(args.embsim.resolve(), 'instrument-loading') as url:
            page.goto(url)
            canvas = page.get_by_role('application', name='Schematic canvas')
            expect(canvas).to_be_visible(timeout=15000)
            track = page.get_by_role('button', name=re.compile(r'Track \d+, net METER_LOADED')).first
            track.press('Enter')
            expect(page.locator('.es-net-spotlight')).to_contain_text('METER_LOADED')
            for ref in ['VM.+', 'R_VM.2']:
                expect(page.locator(f'[data-pin="{ref}"]')).to_have_class(re.compile('es-pin--spotlight'))
                expect(page.locator(f'[data-pin="{ref}"] .es-pin__label')).to_have_css('opacity', '1')
            page.screenshot(path=str(out / 'selected-meter-net.png'))
            page.get_by_role('radio', name='Run', exact=True).click()
            advance('11us')
            vm = page.get_by_role('region', name='VM Voltmeter', exact=True)
            expect(vm.get_by_role('group', name='Terminal voltage: 9.091 V')).to_be_visible()
            expect(vm).to_contain_text('Input 10 MΩ')
            am = page.get_by_role('region', name='AM Ammeter', exact=True)
            expect(am.get_by_role('group', name='Burden voltage: 454.5 mV')).to_be_attached()
            expect(am.get_by_role('group', name='Current into +: 45.45 mA')).to_be_attached()
            gen = page.get_by_role('region', name='GEN Generator', exact=True)
            expect(gen.get_by_role('group', name='Terminal voltage: 1 V')).to_be_attached()
            scope_input = page.get_by_role('region', name='SCOPE Scope input', exact=True)
            expect(scope_input.get_by_role('group', name=re.compile(r'Terminal voltage: 3\.1\d+ V'))).to_be_attached()
            page.get_by_role('button', name='Fit captured waveforms', exact=True).first.click()
            expect(page.get_by_role('region', name='Oscilloscope', exact=True).locator('.es-instrument__measurements')).to_contain_text('100.0 %')
            vm.get_by_role('button', name='Add trace').click()
            page.wait_for_timeout(150)
            assert any(m.get('method') == 'probe.add' and m['params'].get('target') == 'device:VM.voltage' for m in sent)
            am.get_by_role('button', name='View leads').click()
            expect(page.get_by_role('application', name='Live schematic')).to_be_visible()
            expect(page.locator('[data-pin="AM.+"]')).to_have_class(re.compile('es-pin--labelled'))
            expect(page.locator('[data-pin="AM.+"] .es-pin__label')).to_have_css('opacity', '1')
            page.wait_for_timeout(100)
            rect = page.locator('[data-component="AM"] .es-component__hit').bounding_box()
            bounds = page.get_by_role('application', name='Live schematic').bounding_box()
            label = page.locator('[data-component-label="AM"]').bounding_box()
            assert rect and bounds and label
            for item in [rect, label]:
                assert item['x'] >= bounds['x'] and item['y'] >= bounds['y'] and item['x'] + item['width'] <= bounds['x'] + bounds['width'] and item['y'] + item['height'] <= bounds['y'] + bounds['height'], 'View leads must include the selected symbol and its labels in the actual panel'
            page.screenshot(path=str(out / 'wired-instruments.png'))
            assert not any(m.get('method') == 'project.edit' for m in sent), 'inspection/trace actions must not rewire the project'
            # Remove one lead through the real editor, then undo. This used to
            # reject the project or manufacture an implicit ground return.
            page.get_by_role('radio', name='Design', exact=True).click()
            page.get_by_role('button', name=re.compile(r'Track \d+, net GND')).first.press('Enter')
            page.get_by_role('button', name='Disconnect VM.-', exact=True).click()
            page.get_by_role('radio', name='Run', exact=True).click()
            partial = page.get_by_role('region', name='VM Voltmeter', exact=True)
            expect(partial.get_by_role('button', name='Add trace')).to_be_disabled()
            expect(partial.get_by_role('status')).to_contain_text('Wire both leads')
            page.get_by_role('radio', name='Design', exact=True).click()
            page.get_by_role('button', name='Undo', exact=True).click()
            page.get_by_role('radio', name='Run', exact=True).click()
            restored = page.get_by_role('region', name='VM Voltmeter', exact=True)
            expect(restored.get_by_role('button', name='Add trace')).to_be_enabled()
            expect(restored.get_by_role('group', name='Terminal voltage: 9.091 V')).to_be_attached()


        with serve(args.embsim.resolve(), 'electrical-feedback') as url:
            page.goto(url)
            page.get_by_role('radio', name='Design', exact=True).click()
            expect(page.get_by_role('application', name='Schematic canvas')).to_be_visible(timeout=15000)
            advance('100us')
            review = page.get_by_role('button', name=re.compile('2 electrical warnings'))
            expect(review).to_be_visible()
            review.click()
            page.get_by_role('button', name=re.compile('BUS: conflicting drivers')).click()
            expect(page.locator('.es-net-spotlight')).to_contain_text('BUS')
            expect(page.locator('.es-net-spotlight')).to_contain_text('1.650 V')
            expect(page.locator('[data-pin="HIGH.OUT"]')).to_have_class(re.compile('es-pin--spotlight'))
            page.screenshot(path=str(out / 'conflicting-drivers.png'))
            page.get_by_role('button', name=re.compile('FLOAT: floating input')).click()
            expect(page.locator('.es-net-spotlight')).to_contain_text('FLOAT')
            expect(page.locator('[data-pin="FLOATING.IN"] .es-pin__label')).to_have_text('IN · Z')
            page.screenshot(path=str(out / 'floating-input.png'))
            advance('1ms')
            expect(page.get_by_role('button', name=re.compile('1 electrical warning'))).to_be_visible()
            expect(page.get_by_role('button', name=re.compile('BUS: conflicting drivers'))).to_have_count(0)
            expect(page.get_by_role('button', name=re.compile('FLOAT: floating input'))).to_be_visible()
        assert not errors, errors
        browser.close()
        print('Native loading/readouts/tracing and conflict/floating/clearing browser workflows pass; no page errors.')


if __name__ == '__main__':
    main()
