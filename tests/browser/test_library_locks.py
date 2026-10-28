# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native graphical lock review, explicit pin/removal, undo and clock preservation."""
import argparse
import json
from pathlib import Path
import tempfile
from playwright.sync_api import expect, sync_playwright
from test_instrument_loading import serve


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True, type=Path)
    parser.add_argument('--chromium')
    parser.add_argument('--screenshots', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-lock-ui-') as tmp, sync_playwright() as pw:
        out = args.screenshots or Path(tmp); out.mkdir(parents=True, exist_ok=True)
        browser = pw.chromium.launch(executable_path=args.chromium, headless=True, args=['--no-sandbox'])
        page = browser.new_page(viewport={'width': 1800, 'height': 1200}, color_scheme='light')
        errors = []; page.on('pageerror', lambda e: errors.append(str(e)))
        sent, received = [], []
        def socket(ws):
            ws.on('framesent', lambda f: sent.append(json.loads(f)) if isinstance(f, str) else None)
            ws.on('framereceived', lambda f: received.append(json.loads(f)) if isinstance(f, str) else None)
        page.on('websocket', socket)
        with serve(args.embsim.resolve(), 'hierarchical-instruments') as url:
            page.goto(url)
            expect(page.get_by_label('Active sheet', exact=True)).to_be_visible(timeout=15000)
            advance = page.get_by_label('Advance amount', exact=True); advance.fill('1us'); advance.press('Enter')
            clock = page.get_by_test_id('clock'); expect(clock).to_contain_text('1.000 µs')
            page.get_by_role('button', name='Engineering reports', exact=True).click()
            dialog = page.get_by_role('dialog', name='Engineering reports', exact=True)
            panel = page.get_by_role('region', name='Library reproducibility', exact=True)
            expect(panel).to_contain_text('Unpinned libraries')
            panel.get_by_role('button', name='Review current libraries', exact=True).click()
            expect(panel.get_by_role('button', name='Pin reviewed library inputs', exact=True)).to_be_enabled(timeout=15000)
            expect(panel).to_contain_text('Current inputs are ready for review.')
            expect(clock).to_contain_text('1.000 µs')
            capture = next(x for x in sent if x.get('method') == 'project.library_lock' and x['params']['op'] == 'capture')
            captured = next(x['result'] for x in received if x.get('id') == capture['id'])
            assert captured['kind'] == 'library_lock' and 'component:emblink/resistor' in captured['catalog']
            panel.get_by_text('Review file identities', exact=True).click()
            panel.get_by_label('Find library file', exact=True).fill('components/resistor/component.json')
            expect(panel).to_contain_text(captured['libraries']['emblink']['files']['components/resistor/component.json'])
            panel.get_by_text('Review file identities', exact=True).click()
            with page.expect_download() as pending:
                panel.get_by_role('button', name='Export reviewed lock', exact=True).click()
            assert json.loads(Path(pending.value.path()).read_text(encoding='utf-8')) == captured
            panel.get_by_role('button', name='Pin reviewed library inputs', exact=True).click()
            expect(panel).to_contain_text('Pinned library inputs')
            expect(clock).to_contain_text('1.000 µs')
            expect(dialog).to_contain_text('Project changed since this snapshot')
            panel.get_by_role('button', name='Check saved lock', exact=True).click()
            expect(panel).to_contain_text('Saved library lock matches current inputs.')
            panel.get_by_role('button', name='Remove library lock', exact=True).click()
            expect(panel).to_contain_text('Unpinned libraries')
            dialog.press('Control+z')
            expect(panel).to_contain_text('Pinned library inputs')
            expect(clock).to_contain_text('1.000 µs')
            panel.get_by_role('button', name='Check saved lock', exact=True).click()
            expect(panel).to_contain_text('Saved library lock matches current inputs.')
            dialog.get_by_role('button', name='Refresh report', exact=True).click()
            expect(dialog).not_to_contain_text('Project changed since this snapshot')
            with page.expect_download() as pending:
                dialog.get_by_role('button', name='Export model manifest', exact=True).click()
            envelope = json.loads(Path(pending.value.path()).read_text(encoding="utf-8"))
            assert json.loads(json.loads(envelope['json'])['project']['json'])['library_lock'] == captured
            page.screenshot(path=str(out / 'native-library-lock.png'))
            assert not errors, errors
        browser.close()
    print('Native library lock UI: reviewed bytes, explicit pin/check/remove, undo, export and preserved 1us clock; 0 page errors')


if __name__ == '__main__': main()
