#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Actual native waveform review, differential cursors, zoom, exports and unchanged bench."""
import argparse
import csv
import io
import json
from pathlib import Path
import tempfile
from playwright.sync_api import expect, sync_playwright
from test_instrument_loading import serve


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--embsim',required=True,type=Path);ap.add_argument('--chromium');ap.add_argument('--screenshots',type=Path)
    args=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-transient-ui-') as tmp, sync_playwright() as pw:
        out=args.screenshots or Path(tmp);out.mkdir(parents=True,exist_ok=True)
        browser=pw.chromium.launch(executable_path=args.chromium,headless=True,args=['--no-sandbox'])
        page=browser.new_page(viewport={'width':1800,'height':1500},color_scheme='light',accept_downloads=True)
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
        with serve(args.embsim.resolve(),'rc-filter') as url:
            page.goto(url);expect(page.get_by_role('button',name='Transient capture',exact=True)).to_be_enabled(timeout=15000)
            advance=page.get_by_label('Advance amount',exact=True);advance.fill('1us');advance.press('Enter')
            clock=page.get_by_test_id('clock');expect(clock).to_contain_text('1.000 µs')
            page.get_by_role('button',name='Transient capture',exact=True).click()
            dialog=page.get_by_role('dialog',name='Transient capture',exact=True)
            expect(dialog.get_by_label('Capture measurements',exact=True)).to_be_enabled(timeout=15000)
            dialog.get_by_label('Capture measurements',exact=True).fill('net:IN, net:OUT')
            dialog.get_by_role('button',name='Capture isolated response',exact=True).click()
            result=dialog.get_by_role('region',name='Transient results',exact=True)
            expect(result).to_contain_text('101 samples captured',timeout=15000);expect(result.get_by_role('img')).to_be_visible()
            expect(clock).to_contain_text('1.000 µs')
            dialog.get_by_label('Differential negative',exact=True).select_option('1')
            dialog.get_by_label('Cursor B sample',exact=True).fill('20')
            cursor=result.get_by_role('table',name='Transient cursor measurements',exact=True)
            expect(cursor.get_by_role('row').filter(has_text='net:OUT').first).to_contain_text('2.08595 V')
            expect(cursor.get_by_role('row').filter(has_text='net:IN − net:OUT')).to_contain_text('1.21405 V')
            dialog.get_by_label('View last sample',exact=True).fill('40')
            expect(result.get_by_role('img')).to_contain_text('200 µs')
            page.screenshot(path=str(out/'native-transient-review.png'))
            with page.expect_download() as pending:result.get_by_role('button',name='Export transient evidence',exact=True).click()
            data=json.loads(Path(pending.value.path()).read_text(encoding='utf-8'))
            assert data['ok'] and len(data['samples'])==101 and data['request']['step']=='5us'
            assert abs(data['samples'][20]['measurements'][1]['voltage']-2.085997844)<2e-4
            with page.expect_download() as pending:result.get_by_role('button',name='Export transient CSV',exact=True).click()
            rows=list(csv.DictReader(io.StringIO(Path(pending.value.path()).read_text(encoding='utf-8'))))
            assert len(rows)==101 and rows[20]['At ps']=='100000000' and rows[0]['Source SHA-256']==data['source']['sha256']
            dialog.get_by_text('Exact captured samples',exact=True).click()
            table=result.get_by_role('table',name='Native transient samples',exact=True)
            expect(table).to_contain_text('100000000 ps')
            result.get_by_role('button',name='Next samples',exact=True).click();expect(table).to_contain_text('500000000 ps')
            expect(clock).to_contain_text('1.000 µs')
            dialog.get_by_label('View first sample',exact=True).fill('20');dialog.get_by_label('View last sample',exact=True).fill('20')
            expect(result.get_by_role('img').locator('circle')).to_have_count(3)
            # Reject a nonuniform requested grid without changing the open source.
            dialog.get_by_label('Capture interval',exact=True).fill('3us')
            dialog.get_by_role('button',name='Capture isolated response',exact=True).click()
            expect(dialog.get_by_role('alert')).to_contain_text('dividing the interval exactly');expect(clock).to_contain_text('1.000 µs')
        assert not errors,errors
        browser.close()
    print('Native transient UI: 101 actual samples, overlaid differential review, exact cursors, zoom, source-bound exports and unchanged 1us bench; 0 page errors')


if __name__=='__main__':main()
