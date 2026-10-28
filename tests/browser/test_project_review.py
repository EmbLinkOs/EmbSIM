# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native evidence classification and export in project-wide schematic review."""
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
    with tempfile.TemporaryDirectory(prefix='embsim-review-') as tmp, sync_playwright() as pw:
        out = args.screenshots or Path(tmp)
        out.mkdir(parents=True, exist_ok=True)
        browser = pw.chromium.launch(executable_path=args.chromium, headless=True, args=['--no-sandbox'])
        page = browser.new_page(viewport={'width':1800,'height':1200}, accept_downloads=True)
        errors = []
        page.on('pageerror', lambda e: errors.append(str(e)))
        with serve(args.embsim.resolve(), 'electrical-feedback') as url:
            page.goto(url)
            page.get_by_role('radio', name='Design', exact=True).click()
            page.get_by_role('button', name='Sheets and reusable blocks', exact=True).click()
            amount = page.get_by_label('Advance amount', exact=True)
            amount.fill('100us'); amount.press('Enter')
            expect(page.get_by_test_id('clock')).to_contain_text('100.000 µs')
            page.get_by_role('button', name=re.compile('^Project checks')).click()
            page.get_by_label('Check source', exact=True).select_option('live')
            results = page.get_by_role('list', name='Project check results', exact=True)
            expect(results).to_contain_text('BUS: engine reports contention')
            expect(results).to_contain_text('FLOAT: floating input')
            results.get_by_role('button', name=re.compile('BUS: engine reports contention')).click()
            expect(page.locator('.es-net-spotlight')).to_contain_text('1.650 V')
            with page.expect_download() as pending:
                page.get_by_role('button', name='Export check report', exact=True).click()
            report = json.loads(Path(pending.value.path()).read_text())
            assert report['time_ps'] == '100000000' and report['scope'] == 'advisory-project-erc-and-drawing', report
            conflicts = [i for i in report['issues'] if i['code'] == 'live-contention']
            assert len(conflicts) == 1 and conflicts[0]['net'] == 'BUS' and conflicts[0]['source'] == 'live'
            canvas = page.get_by_role('application', name='Sheet Main', exact=True).bounding_box()
            for part in ['HIGH', 'CLOCK', 'INPUT', 'RECEIVER']:
                label = page.locator(f'[data-component-label="{part}"]').bounding_box()
                assert canvas and label and label['x'] >= canvas['x'] and label['x'] + label['width'] <= canvas['x'] + canvas['width'], (part, canvas, label)
            page.screenshot(path=str(out/'native-project-review.png'))
            amount.fill('1ms'); amount.press('Enter')
            expect(page.get_by_test_id('clock')).to_contain_text('1.100')
            expect(results).not_to_contain_text('engine reports contention')
            expect(results).to_contain_text('FLOAT: floating input')
        assert not errors, errors
        browser.close()
    print('Native project review: confirmed contention, solved voltage, floating receiver, structured export and cleared conflict; 0 errors')


if __name__ == '__main__':
    main()
