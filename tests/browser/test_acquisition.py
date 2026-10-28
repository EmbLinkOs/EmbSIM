# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native loaded RC capture, immutable exports and retargeted waveform probes."""
import argparse
import json
from pathlib import Path
import re
import tempfile
from playwright.sync_api import expect, sync_playwright
from test_instrument_loading import serve


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True, type=Path)
    parser.add_argument('--chromium')
    parser.add_argument('--screenshots', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-acquisition-browser-') as tmp, sync_playwright() as pw:
        out = args.screenshots or Path(tmp)
        out.mkdir(parents=True, exist_ok=True)
        browser = pw.chromium.launch(executable_path=args.chromium, headless=True, args=['--no-sandbox'])
        page = browser.new_page(viewport={'width': 1800, 'height': 1200}, color_scheme='light', accept_downloads=True)
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

        def export():
            with page.expect_download() as pending:
                page.get_by_role('button', name='CSV', exact=True).first.click()
            return Path(pending.value.path()).read_text()

        with serve(args.embsim.resolve(), 'acquisition-bench') as url:
            page.goto(url)
            page.get_by_role('radio', name='Run', exact=True).click()
            expect(page.get_by_role('application', name='Live schematic')).to_be_visible(timeout=15000)
            page.get_by_label('Capture span (ms)', exact=True).fill('2')
            page.get_by_label('Pre-trigger (%)', exact=True).fill('25')
            page.get_by_role('button', name='Arm capture', exact=True).click()
            advance('2ms')
            status = page.get_by_role('status', name='Acquisition status')
            expect(status).to_contain_text('post-trigger:')
            advance('1ms')
            expect(status).to_contain_text('captured:')
            expect(status).to_contain_text('#1')
            scope = page.get_by_role('region', name='Oscilloscope', exact=True)
            expect(scope.locator('.es-instrument__measurements')).to_contain_text('100.0 %')
            page.screenshot(path=str(out/'triggered-loaded-rc.png'))
            before = export()
            lines = before.splitlines()
            assert lines[0] == 'time_ps,drive,response', lines[0]
            first = int(lines[1].split(',')[0]); last = int(lines[-1].split(',')[0])
            assert 500000000 <= first <= 510000000 and last-first == 2000000000, 'sampled trigger pre/post window is wrong'
            # Independent loaded-RC response, not a UI-generated waveform.
            point = min((line.split(',') for line in lines[1:]), key=lambda row: abs(int(row[0])-1500000000))
            assert abs(float(point[2])-1.96687302) < .01, point
            advance('8ms')
            expect(page.get_by_test_id('clock')).to_contain_text('11')
            assert export() == before, 'frozen export changed while the native engine continued'
            # A source change preserves frozen evidence, but live starts a new epoch.
            page.get_by_label('Move probe drive', exact=True).select_option('GND')
            expect(page.get_by_role('img', name='Waveform probe drive on GND', exact=True)).to_be_visible()
            assert export() == before, 'probe move altered frozen evidence'
            page.get_by_role('button', name='Live view', exact=True).click()
            advance('1ms')
            expect(status).to_contain_text('Live')
            expect(page.get_by_test_id('clock')).to_contain_text('12')
            page.wait_for_timeout(100)
            frames = [m['params'] for m in received if m.get('method') == 'waveform.samples' and m['params'].get('probe') == 'drive']
            epoch = max(f.get('capture_epoch', 0) for f in frames)
            moved = [p for f in frames if f.get('capture_epoch') == epoch for p in f.get('samples', [])]
            assert moved and all(int(t) >= 11000000000 and abs(v) < 1e-9 for t,v in moved), f'retarget mixed old and new nets: epoch={epoch}, head={moved[:3]}, tail={moved[-3:]}'
            # Actual pointer move to the RC response wire; no project transaction.
            marker = page.get_by_role('img', name='Waveform probe drive on GND', exact=True)
            target = page.locator('[data-pin="R.2"] .es-pin__ring')
            if not target.count():
                target = page.locator('[data-pin="R.2"]')
            a, b = marker.bounding_box(), target.bounding_box()
            assert a and b
            page.mouse.move(a['x']+a['width']/2, a['y']+a['height']/2)
            page.mouse.down()
            page.mouse.move(b['x']+b['width']/2, b['y']+b['height']/2, steps=8)
            page.mouse.up()
            expect(page.get_by_label('Move probe drive', exact=True)).to_have_value('RESPONSE')
            assert any(m.get('method') == 'probe.move' for m in sent)
            assert not any(m.get('method') == 'project.begin' for m in sent), 'ideal probe changed the project'
            assert errors == [], errors
        browser.close()
    print('Native acquisition browser: loaded RC trigger, frozen export, epoch-safe keyboard/pointer probe moves; 0 errors')


if __name__ == '__main__':
    main()
