# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native graphical authoring, population/value changes, DNP pads, BOMs and base recovery."""
import argparse
import csv
import io
import json
import re
from pathlib import Path
import tempfile
from playwright.sync_api import expect, sync_playwright
from test_instrument_loading import serve


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True, type=Path); parser.add_argument('--chromium'); parser.add_argument('--screenshots', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-variants-ui-') as temp, sync_playwright() as pw:
        out = args.screenshots or Path(temp); out.mkdir(parents=True, exist_ok=True)
        browser = pw.chromium.launch(executable_path=args.chromium, headless=True, args=['--no-sandbox'])
        page = browser.new_page(viewport={'width': 1800, 'height': 1200}, color_scheme='light', accept_downloads=True)
        errors = []; page.on('pageerror', lambda e: errors.append(str(e)))
        with serve(args.embsim.resolve(), 'assembly-variants') as url:
            page.goto(url)
            sheet = page.get_by_label('Active sheet', exact=True); expect(sheet).to_be_visible(timeout=15000)
            amount = page.get_by_label('Advance amount', exact=True); amount.fill('1us'); amount.press('Enter')
            clock = page.get_by_test_id('clock'); expect(clock).to_contain_text('1.000 µs')
            page.get_by_role('button', name='Assembly variants', exact=True).click()
            dialog = page.get_by_role('dialog', name='Assembly variants', exact=True)
            expect(dialog.get_by_label('Selected assembly variant', exact=True)).to_have_value('production')
            dialog.get_by_label('New variant identifier', exact=True).fill('reviewed')
            dialog.get_by_role('button', name='Create variant', exact=True).click()
            expect(dialog.get_by_label('Selected assembly variant', exact=True)).to_have_value('reviewed')
            expect(clock).to_contain_text('1.000 µs')
            target = dialog.get_by_label('Variant component target', exact=True)
            expect(target).to_be_enabled(); target.select_option('A.R')
            form = dialog.get_by_role('form', name='Variant override for A.R', exact=True)
            form.get_by_label('Override population', exact=True).select_option('dnp')
            form.get_by_role('button', name='Save component alternative', exact=True).click()
            expect(dialog.get_by_role('table').first).to_contain_text('DNP')
            expect(target).to_be_enabled(); target.select_option('B.R')
            form = dialog.get_by_role('form', name='Variant override for B.R', exact=True)
            form.get_by_label('Variant parameter', exact=True).select_option('resistance')
            form.get_by_label('Variant parameter value', exact=True).fill('2meg')
            form.get_by_role('button', name='Set value override', exact=True).click()
            expect(form).to_contain_text('resistance = 2meg')
            form.get_by_role('button', name='Save component alternative', exact=True).click()
            expect(dialog.get_by_role('table').first).to_contain_text('2meg')
            expect(clock).to_contain_text('1.000 µs')
            expect(dialog.get_by_role('button', name='Preview selected variant', exact=True)).to_be_enabled()
            dialog.get_by_role('button', name='Preview selected variant', exact=True).click()
            preview = dialog.get_by_role('region', name='Native variant preview', exact=True)
            expect(preview).to_contain_text('Native validation passed', timeout=15000)
            table = preview.get_by_role('table', name='Variant assembly preview', exact=True)
            expect(table.get_by_role('row').filter(has_text='A.R')).to_contain_text('DNP')
            expect(table.get_by_role('row').filter(has_text='B.R')).to_contain_text('2000000')
            expect(clock).to_contain_text('1.000 µs')
            with page.expect_download() as pending: preview.get_by_role('button', name='Export derived project', exact=True).click()
            exported = json.loads(Path(pending.value.path()).read_text(encoding='utf-8'))
            assert exported['variant_origin']['name'] == 'reviewed'
            base = json.loads(exported['variant_origin']['json'])
            assert base['variants']['reviewed']['overrides']['A.R']['populated'] is False
            with page.expect_download() as pending: preview.get_by_role('button', name='Export variant BOM', exact=True).click()
            bom = list(csv.DictReader(io.StringIO(Path(pending.value.path()).read_text(encoding='utf-8'))))
            assert len(bom) == 2 and next(row for row in bom if row['References'] == 'A.R')['Quantity'] == '0', bom
            assert all(row['Variant'] == 'reviewed' for row in bom), bom
            page.screenshot(path=str(out / 'native-variant-preview.png'))
            preview.get_by_role('button', name='Activate reviewed', exact=True).click()
            expect(dialog).not_to_be_visible()
            expect(clock).to_contain_text('0 ps')
            expect(page.get_by_text('Derived circuit · return to base to edit', exact=True)).to_be_visible()
            amount.fill('1us'); amount.press('Enter'); expect(clock).to_contain_text('1.000 µs')
            sheet.select_option('A')
            symbol = page.locator('[data-component="R"]')
            expect(symbol).to_have_attribute('data-populated', 'false')
            expect(symbol.locator('[aria-label="R not populated"]')).to_be_visible()
            expect(page.locator('[data-pin="R.1"]')).to_be_visible()
            expect(page.locator('[data-pin="R.2"]')).to_be_visible()
            expect(page.locator('[data-pin="R.1"]')).to_have_attribute('aria-label', re.compile('10.000 V'))
            expect(page.locator('[data-pin="R.2"]')).to_have_attribute('aria-label', re.compile('0.000 V'))
            page.screenshot(path=str(out / 'native-dnp-footprint.png'))
            page.get_by_role('button', name='Engineering reports', exact=True).click()
            reports = page.get_by_role('dialog', name='Engineering reports', exact=True)
            inventory = reports.get_by_role('table', name='Assembly inventory', exact=True)
            expect(inventory).to_contain_text('B.R'); expect(inventory).not_to_contain_text('A.R')
            reports.get_by_label('Include DNP footprints', exact=True).check()
            expect(inventory.get_by_role('row').filter(has_text='A.R')).to_contain_text('DNP')
            reports.press('Escape')
            page.get_by_role('button', name='Assembly variants · reviewed', exact=True).click()
            expect(dialog).to_contain_text('This derived circuit is read-only')
            dialog.get_by_role('button', name='Preview base circuit', exact=True).click()
            preview = dialog.get_by_role('region', name='Native variant preview', exact=True)
            expect(preview).to_contain_text('Native validation passed')
            preview.get_by_role('button', name='Activate base circuit', exact=True).click()
            expect(dialog).not_to_be_visible()
            expect(page.get_by_role('button', name='Assembly variants', exact=True)).to_be_visible()
            expect(symbol).to_have_attribute('data-populated', 'true')
            assert not errors, errors
        browser.close()
    print('Native assembly variant UI: author per-instance DNP/value overrides, preserve 1us on review, export zero-quantity DNP BOMs, activate real open footprints, retain terminal sockets and recover the original base; 0 page errors')


if __name__ == '__main__': main()
