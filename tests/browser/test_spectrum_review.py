#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native captured spectrum review: calibrated RC amplitude/phase, display modes and raw exports."""
import argparse
import csv
import io
import json
import math
from pathlib import Path
import tempfile
from playwright.sync_api import expect,sync_playwright
from test_instrument_loading import serve


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--embsim',required=True,type=Path);ap.add_argument('--chromium');ap.add_argument('--screenshots',type=Path)
    args=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-spectrum-ui-') as tmp,sync_playwright() as pw:
        out=args.screenshots or Path(tmp);out.mkdir(parents=True,exist_ok=True)
        browser=pw.chromium.launch(executable_path=args.chromium,headless=True,args=['--no-sandbox'])
        page=browser.new_page(viewport={'width':1800,'height':1500},color_scheme='light',accept_downloads=True)
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
        with serve(args.embsim.resolve(),'spectrum-rc') as url:
            page.goto(url);expect(page.get_by_role('button',name='Transient capture',exact=True)).to_be_enabled(timeout=15000)
            advance=page.get_by_label('Advance amount',exact=True);advance.fill('1us');advance.press('Enter')
            clock=page.get_by_test_id('clock');expect(clock).to_contain_text('1.000 µs')
            page.get_by_role('button',name='Transient capture',exact=True).click()
            d=page.get_by_role('dialog',name='Transient capture',exact=True)
            expect(d.get_by_label('Capture measurements',exact=True)).to_be_enabled(timeout=15000)
            d.get_by_label('Capture start',exact=True).fill('16ms');d.get_by_label('Capture stop',exact=True).fill('32ms')
            d.get_by_role('button',name='Use 256 FFT intervals',exact=True).click()
            expect(d.get_by_label('Capture interval',exact=True)).to_have_value('62500000ps')
            d.get_by_label('Capture measurements',exact=True).fill('net:IN, net:OUT')
            d.get_by_role('button',name='Capture isolated response',exact=True).click()
            expect(d).to_contain_text('257 samples captured',timeout=15000)
            d.get_by_text('Transient waveform and sample cursors',exact=True).click()
            d.get_by_text('Frequency spectrum',exact=True).click()
            d.get_by_label('Spectrum target',exact=True).select_option('net:OUT')
            d.get_by_role('button',name='Analyze captured spectrum',exact=True).click()
            region=d.get_by_role('region',name='Frequency spectrum results',exact=True)
            expect(region).to_contain_text('256 FFT samples',timeout=15000)
            expect(region).to_contain_text('62.5 Hz');expect(region).to_contain_text('8 kHz')
            table=region.get_by_role('table',name='Spectrum bin measurements',exact=True)
            expect(table).to_contain_text('1 kHz');expect(table).to_contain_text('846.73')
            d.get_by_label('Spectrum last bin',exact=True).fill('32')
            expect(region.get_by_role('img')).to_be_visible();expect(clock).to_contain_text('1.000 µs')
            region.scroll_into_view_if_needed();page.screenshot(path=str(out/'native-rc-spectrum.png'))
            with page.expect_download() as pending:region.get_by_role('button',name='Export spectrum evidence',exact=True).click()
            result=json.loads(Path(pending.value.path()).read_text(encoding='utf-8'));b=result['bins'][16]
            assert abs(b['peak_v']-1/math.sqrt(1+(2*math.pi*.1)**2))<2e-5
            assert abs(b['phase_deg']-(-90-math.degrees(math.atan(2*math.pi*.1))))<.01
            assert len(json.loads(result['capture']['json'])['samples'])==257 and len(result['bins'])==129
            with page.expect_download() as pending:region.get_by_role('button',name='Export spectrum CSV',exact=True).click()
            rows=list(csv.DictReader(io.StringIO(Path(pending.value.path()).read_text(encoding='utf-8'))))
            assert len(rows)==129 and rows[16]['Target']=='net:OUT' and rows[16]['Capture SHA-256']==result['capture']['sha256']
            d.get_by_label('Spectrum reference',exact=True).select_option('net:IN')
            d.get_by_role('button',name='Analyze captured spectrum',exact=True).click()
            ratios=region.get_by_role('table',name='Channel ratio measurements',exact=True)
            expect(ratios).to_contain_text('Valid',timeout=15000)
            expect(ratios).to_contain_text('-32.14')
            with page.expect_download() as pending:region.get_by_role('button',name='Export comparison evidence',exact=True).click()
            comparison=json.loads(Path(pending.value.path()).read_text(encoding='utf-8'))
            rb=comparison['bins'][16]
            assert abs(rb['gain']-1/math.sqrt(1+(2*math.pi*.1)**2))<2e-5
            assert abs(rb['phase_deg']+math.degrees(math.atan(2*math.pi*.1)))<.01
            assert comparison['output']['capture']==comparison['reference']['capture']
            assert comparison['bins'][0]['status']=='weak_reference'
            with page.expect_download() as pending:region.get_by_role('button',name='Export comparison CSV',exact=True).click()
            ratios_csv=list(csv.DictReader(io.StringIO(Path(pending.value.path()).read_text(encoding='utf-8'))))
            assert len(ratios_csv)==129 and ratios_csv[16]['Reference']=='net:IN'
            d.get_by_label('Inspect spectrum bin',exact=True).fill('0')
            expect(ratios).to_contain_text('Reference below threshold or zero')
            expect(ratios).to_contain_text('Undefined')
            d.get_by_label('Inspect spectrum bin',exact=True).fill('16')
            region.scroll_into_view_if_needed();page.screenshot(path=str(out/'native-rc-comparison.png'))
            d.get_by_label('Spectrum amplitude display',exact=True).select_option('dbv')
            expect(region.get_by_role('img')).to_have_attribute('aria-label','Native frequency spectrum in RMS-bin dBV')
            expect(table).to_contain_text('-4.455')
            d.get_by_label('Minimum spectrum reference peak voltage',exact=True).fill('2')
            expect(region).to_contain_text('Options changed.')
            d.get_by_role('button',name='Analyze captured spectrum',exact=True).click()
            expect(ratios).to_contain_text('Reference below threshold or zero',timeout=15000)
            d.get_by_label('Minimum spectrum reference peak voltage',exact=True).fill('0.000001')
            d.get_by_label('Spectrum window',exact=True).select_option('rectangular')
            expect(region).to_contain_text('Options changed.')
            d.get_by_role('button',name='Analyze captured spectrum',exact=True).click()
            expect(region.get_by_text('Options changed.',exact=False)).to_have_count(0)
            expect(clock).to_contain_text('1.000 µs')
        assert not errors,errors
        browser.close()
    print('Native spectrum UI: FFT-ready capture, calibrated 1kHz RC peak/RMS/dBV/phase, window and channel-ratio review, weak-reference exclusion, complete exports and unchanged 1us bench; 0 page errors')


if __name__=='__main__':main()
