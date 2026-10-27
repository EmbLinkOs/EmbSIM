#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real native AC Bode review, exact cursors, complete evidence and unchanged bench."""
import argparse
import csv
import io
import json
import math
from pathlib import Path
import tempfile
from playwright.sync_api import expect, sync_playwright
from test_instrument_loading import serve


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--embsim',required=True,type=Path)
    ap.add_argument('--chromium')
    ap.add_argument('--screenshots',type=Path)
    args=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-ac-ui-') as tmp, sync_playwright() as pw:
        out=args.screenshots or Path(tmp);out.mkdir(parents=True,exist_ok=True)
        browser=pw.chromium.launch(executable_path=args.chromium,headless=True,args=['--no-sandbox'])
        page=browser.new_page(viewport={'width':1800,'height':1500},color_scheme='light',accept_downloads=True)
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
        with serve(args.embsim.resolve(),'spectrum-rc') as url:
            page.goto(url)
            expect(page.get_by_role('button',name='AC sweep',exact=True)).to_be_enabled(timeout=30000)
            advance=page.get_by_label('Advance amount',exact=True);advance.fill('1us');advance.press('Enter')
            clock=page.get_by_test_id('clock');expect(clock).to_contain_text('1.000 µs')
            page.get_by_role('button',name='AC sweep',exact=True).click()
            dialog=page.get_by_role('dialog',name='AC sweep',exact=True)
            expect(dialog.get_by_label('AC source',exact=True)).to_have_value('V1',timeout=30000)
            dialog.get_by_label('AC point count',exact=True).fill('41')
            dialog.get_by_label('AC voltage targets',exact=True).fill('net:IN, net:OUT, net:GND')
            dialog.get_by_role('button',name='Run AC sweep',exact=True).click()
            region=dialog.get_by_role('region',name='AC sweep results',exact=True)
            expect(region).to_contain_text('41 native AC points',timeout=30000)
            region.get_by_label('AC review output',exact=True).select_option('1')
            region.get_by_label('AC frequency cursor',exact=True).fill('20')
            table=region.get_by_role('table',name='AC cursor measurements',exact=True)
            expect(table).to_contain_text('1 kHz');expect(table).to_contain_text('0.846733016')
            expect(table).to_contain_text('-32.141908');expect(table).to_contain_text('-1.445070')
            expect(region.get_by_role('img',name='AC magnitude Bode plot',exact=True)).to_be_visible()
            expect(region.get_by_role('img',name='AC phase Bode plot',exact=True)).to_be_visible()
            expect(clock).to_contain_text('1.000 µs')
            region.scroll_into_view_if_needed();page.screenshot(path=str(out/'native-rc-ac-bode.png'))
            with page.expect_download() as pending:region.get_by_role('button',name='Export AC evidence',exact=True).click()
            artifact=json.loads(Path(pending.value.path()).read_text(encoding='utf-8'))
            assert len(artifact['points'])==41 and artifact['request']['source']=='V1'
            response=artifact['points'][20]['responses'][1]
            assert abs(response['magnitude']-1/math.sqrt(1+(2*math.pi*.1)**2))<1e-12
            assert abs(response['phase_deg']+math.degrees(math.atan(2*math.pi*.1)))<1e-10
            with page.expect_download() as pending:region.get_by_role('button',name='Export AC CSV',exact=True).click()
            rows=list(csv.DictReader(io.StringIO(Path(pending.value.path()).read_text(encoding='utf-8'))))
            assert len(rows)==123 and rows[61]['Target']=='net:OUT'
            assert rows[61]['Manifest SHA-256']==artifact['source']['sha256']
            region.get_by_label('AC review output',exact=True).select_option('2')
            expect(table).to_contain_text('Undefined (zero)');expect(table).to_contain_text('−∞ (zero)')
            dialog.get_by_label('AC spacing',exact=True).select_option('linear')
            expect(region).to_contain_text('Options changed.')
            dialog.get_by_label('AC point count',exact=True).fill('1')
            dialog.get_by_label('AC start frequency',exact=True).fill('1000')
            dialog.get_by_label('AC stop frequency',exact=True).fill('1000')
            dialog.get_by_role('button',name='Run AC sweep',exact=True).click()
            expect(region).to_contain_text('1 native AC points',timeout=30000)
            region.get_by_label('AC review output',exact=True).select_option('1')
            expect(table).to_contain_text('0.846733016')
            expect(region.get_by_text('Options changed.',exact=False)).to_have_count(0)
            expect(clock).to_contain_text('1.000 µs')
            dialog.get_by_label('AC start frequency',exact=True).fill('0')
            dialog.get_by_role('button',name='Run AC sweep',exact=True).click()
            expect(dialog.get_by_role('alert')).to_contain_text('ascending frequencies')
        assert not errors,errors
        browser.close()
    print('Native AC browser: analytic RC Bode/cursors, zero/single-point semantics, raw exports, revised settings, invalid-grid refusal and unchanged bench; 0 page errors')


if __name__=='__main__':main()
