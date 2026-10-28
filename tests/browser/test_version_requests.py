# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native shared-sheet version requests, rollback, restart, evidence and undo."""
import argparse
import json
from pathlib import Path
import tempfile
from playwright.sync_api import expect, sync_playwright
from test_instrument_loading import serve


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True, type=Path); parser.add_argument('--chromium'); parser.add_argument('--screenshots', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-versions-ui-') as tmp, sync_playwright() as pw:
        out = args.screenshots or Path(tmp); out.mkdir(parents=True, exist_ok=True)
        browser = pw.chromium.launch(executable_path=args.chromium, headless=True, args=['--no-sandbox'])
        page = browser.new_page(viewport={'width': 1800, 'height': 1200}, color_scheme='light', accept_downloads=True)
        errors = []; page.on('pageerror', lambda e: errors.append(str(e)))
        with serve(args.embsim.resolve(), 'hierarchical-instruments') as url:
            page.goto(url)
            expect(page.get_by_label('Active sheet', exact=True)).to_be_visible(timeout=15000)
            advance = page.get_by_label('Advance amount', exact=True); advance.fill('1us'); advance.press('Enter')
            clock = page.get_by_test_id('clock'); expect(clock).to_contain_text('1.000 µs')
            page.get_by_role('button', name='Engineering reports', exact=True).click()
            dialog = page.get_by_role('dialog', name='Engineering reports', exact=True)
            table = dialog.get_by_role('table', name='Assembly inventory', exact=True)
            row = table.get_by_role('row').filter(has_text='A.R, B.R')
            row.locator('summary').first.click()
            row.get_by_label('Edit version request for A.R', exact=True).click()
            request = row.get_by_label('Requested version for A.R', exact=True)
            form = row.get_by_role('form', name='Version request for A.R', exact=True)
            expect(form).to_contain_text('Edits this shared sheet definition')
            request.fill('^2.0.0'); form.get_by_role('button', name='Apply version request', exact=True).click()
            expect(form.get_by_role('alert')).to_contain_text('requests version ^2.0.0')
            expect(clock).to_contain_text('1.000 µs')
            expect(dialog).not_to_contain_text('Project changed since this snapshot')
            request.fill('^1.0.0'); form.get_by_role('button', name='Apply version request', exact=True).click()
            expect(clock).to_contain_text('0 ps')
            expect(dialog).to_contain_text('Project changed since this snapshot')
            dialog.get_by_role('button', name='Refresh report', exact=True).click()
            expect(dialog).not_to_contain_text('Project changed since this snapshot')
            row = table.get_by_role('row').filter(has_text='A.R, B.R')
            row.locator('summary').first.click()
            row.get_by_label('Edit version request for A.R', exact=True).click()
            row.get_by_label('Edit version request for B.R', exact=True).click()
            expect(row.get_by_label('Requested version for A.R', exact=True)).to_have_value('^1.0.0')
            expect(row.get_by_label('Requested version for B.R', exact=True)).to_have_value('^1.0.0')
            expect(row).to_contain_text('satisfied')
            with page.expect_download() as pending:
                dialog.get_by_role('button', name='Export model manifest', exact=True).click()
            data = json.loads(json.loads(Path(pending.value.path()).read_text(encoding='utf-8'))['json'])
            for item in data['instances']:
                if item['path'] in ['A.R', 'B.R']:
                    assert item['version_satisfied'] and item['requested_version'] == '^1.0.0', item
            row.get_by_label('Edit version request for B.R', exact=True).click()
            page.screenshot(path=str(out / 'native-version-requests.png'))
            dialog.press('Control+z')
            expect(dialog).to_contain_text('Project changed since this snapshot')
            dialog.get_by_role('button', name='Refresh report', exact=True).click()
            row = table.get_by_role('row').filter(has_text='A.R, B.R'); row.locator('summary').first.click()
            expect(row.get_by_label('Requested version for A.R', exact=True)).to_have_value('')
            expect(row.get_by_label('Requested version for B.R', exact=True)).to_have_value('')
            assert not errors, errors
        browser.close()
    print('Native version UI: incompatible rollback retains 1us, compatible shared request restarts, native manifest verifies both instances and Undo removes the request; 0 page errors')


if __name__ == '__main__': main()
