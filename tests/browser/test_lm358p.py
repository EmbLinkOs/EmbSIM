# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Named LM358P model metadata and actual native bandwidth/slew CSV evidence."""
import argparse
import csv
import io
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
    with tempfile.TemporaryDirectory(prefix='embsim-lm358p-') as tmp, sync_playwright() as pw:
        out = args.screenshots or Path(tmp)
        out.mkdir(parents=True, exist_ok=True)
        browser = pw.chromium.launch(executable_path=args.chromium, headless=True, args=['--no-sandbox'])
        page = browser.new_page(viewport={'width':1800, 'height':1200}, color_scheme='light', accept_downloads=True)
        errors = []
        page.on('pageerror', lambda e: errors.append(str(e)))
        with serve(args.embsim.resolve(), 'lm358p-dynamics') as url:
            page.goto(url)
            page.get_by_role('radio', name='Design', exact=True).click()
            page.get_by_role('button', name='Sheets and reusable blocks', exact=True).click()
            page.get_by_role('button', name='U1, LM358P', exact=True).press('Space')
            expect(page.get_by_label('Parameter U1.gbw', exact=True)).to_have_value('700k')
            expect(page.get_by_label('Parameter U1.slew', exact=True)).to_have_value('0.3')
            page.get_by_role('radio', name='Run', exact=True).click()
            expect(page.get_by_role('application', name='Live schematic', exact=True)).to_be_visible()
            amount = page.get_by_label('Advance amount', exact=True)
            amount.fill('55us'); amount.press('Enter')
            expect(page.get_by_test_id('clock')).to_contain_text('55.000 µs')
            scope = page.get_by_role('region', name='Oscilloscope', exact=True)
            expect(scope).to_contain_text('gain-output')
            expect(scope).to_contain_text('slew-output')
            page.get_by_role('button', name='Fit captured waveforms', exact=True).first.click()
            with page.expect_download() as pending:
                page.get_by_role('button', name='CSV', exact=True).first.click()
            rows = list(csv.DictReader(io.StringIO(Path(pending.value.path()).read_text())))
            assert len(rows) > 500, len(rows)
            for time, column, lo, hi in [(12500000,'gain-output',1.16,1.179), (11000000,'slew-output',1.255,1.345), (25000000,'slew-output',2.98,3.02), (31000000,'slew-output',2.65,2.755), (50000000,'slew-output',.98,1.02)]:
                row = min(rows, key=lambda r: abs(int(r['time_ps']) - time))
                assert abs(int(row['time_ps'])-time) <= 100000 and lo <= float(row[column]) <= hi, (column, time, row)
            page.screenshot(path=str(out/'lm358p-native-dynamics.png'))
        assert not errors, errors
        browser.close()
    print('Native LM358P browser: source defaults, loaded gain/pole and rising/falling slew exported from actual waveforms; 0 errors')


if __name__ == '__main__':
    main()
