#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real native sweep UI, plotted loading response and retained live session."""
import argparse
import csv
import io
import json
from pathlib import Path
import tempfile
from playwright.sync_api import expect, sync_playwright
from test_instrument_loading import serve


def main():
    ap = argparse.ArgumentParser(description=__doc__); ap.add_argument('--embsim', required=True, type=Path); ap.add_argument('--chromium'); ap.add_argument('--screenshots', type=Path)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-sweep-ui-') as tmp, sync_playwright() as pw:
        out = args.screenshots or Path(tmp); out.mkdir(parents=True, exist_ok=True)
        browser = pw.chromium.launch(executable_path=args.chromium, headless=True, args=['--no-sandbox'])
        page = browser.new_page(viewport={'width': 1800, 'height': 1500}, color_scheme='light', accept_downloads=True)
        errors = []; page.on('pageerror', lambda e: errors.append(str(e)))
        with serve(args.embsim.resolve(), 'assembly-variants') as url:
            page.goto(url); expect(page.get_by_label('Active sheet', exact=True)).to_be_visible(timeout=15000)
            amount = page.get_by_label('Advance amount', exact=True); amount.fill('1us'); amount.press('Enter')
            clock = page.get_by_test_id('clock'); expect(clock).to_contain_text('1.000 µs')
            page.get_by_role('button', name='Parameter sweep', exact=True).click()
            dialog = page.get_by_role('dialog', name='Parameter sweep', exact=True)
            expect(dialog.get_by_label('Sweep component', exact=True)).to_be_enabled(timeout=15000)
            dialog.get_by_label('Sweep component', exact=True).select_option('A.R')
            dialog.get_by_label('Sweep parameter', exact=True).select_option('resistance')
            dialog.get_by_label('Sweep points', exact=True).fill('3')
            dialog.get_by_label('Sweep measurements', exact=True).fill('pin:A.R.2, pin:B.R.2')
            dialog.get_by_role('button', name='Run isolated sweep', exact=True).click()
            region = dialog.get_by_role('region', name='Sweep results', exact=True)
            expect(region).to_contain_text('3 points sampled', timeout=15000)
            expect(region.get_by_role('img')).to_be_visible(); expect(clock).to_contain_text('1.000 µs')
            table = region.get_by_role('table', name='Native sweep samples', exact=True)
            expect(table).to_contain_text('9.09091 V'); expect(table).to_contain_text('5 V'); expect(table).to_contain_text('909.09 mV')
            with page.expect_download() as download: region.get_by_role('button', name='Export sweep evidence', exact=True).click()
            data = json.loads(Path(download.value.path()).read_text(encoding='utf-8'))
            assert data['method'] == 'independent_reset_sample' and data['source']['sha256'].startswith('sha256:')
            assert abs(data['points'][1]['measurements'][0]['voltage'] - 5) < 1e-5
            with page.expect_download() as download: region.get_by_role('button', name='Export sweep CSV', exact=True).click()
            rows = list(csv.DictReader(io.StringIO(Path(download.value.path()).read_text(encoding='utf-8'))))
            assert len(rows) == 3 and rows[0]['Result'] == 'sampled'
            assert abs(float(rows[-1]['pin:A.R.2 V']) - 10/11) < 1e-5
            page.screenshot(path=str(out/'native-loaded-parameter-sweep.png'))
            # A bad electrical point remains a failure, not a zero or interpolated dot.
            dialog.get_by_label('Sweep start', exact=True).fill('-1'); dialog.get_by_label('Sweep stop', exact=True).fill('1meg')
            dialog.get_by_label('Logarithmic parameter spacing', exact=True).uncheck()
            dialog.get_by_role('button', name='Run isolated sweep', exact=True).click()
            expect(region).to_contain_text('Some points failed'); expect(table).to_contain_text('Unavailable'); expect(clock).to_contain_text('1.000 µs')
            page.screenshot(path=str(out/'native-sweep-point-failure.png'))
        assert not errors, errors
        browser.close()
    print('Native sweep UI: real loaded voltages, independent sibling, source-bound JSON/CSV, explicit failed points and unchanged 1us clock; 0 page errors')


if __name__ == '__main__': main()
