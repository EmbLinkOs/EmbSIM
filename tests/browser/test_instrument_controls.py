# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native loaded instrument configuration, differential leads and frozen acquisition."""
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True, type=Path)
    parser.add_argument('--chromium')
    parser.add_argument('--screenshots', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-instrument-controls-') as tmp, sync_playwright() as pw:
        out = args.screenshots or Path(tmp); out.mkdir(parents=True, exist_ok=True)
        browser = pw.chromium.launch(executable_path=args.chromium, headless=True, args=['--no-sandbox'])
        page = browser.new_page(viewport={'width':1800,'height':1200}, color_scheme='light', accept_downloads=True)
        errors, sent, values = [], [], {}
        page.on('pageerror', lambda e: errors.append(str(e)))
        def received(frame):
            if not isinstance(frame, str): return
            msg = json.loads(frame)
            if msg.get('method') == 'observe.update':
                data=msg['params'];values.update(data.get('outputs', {}))
                for id, component in data.get('components', {}).items():
                    for name, value in component.get('outputs', {}).items(): values[f'{id}.{name}']=value
        def websocket(ws):
            ws.on('framereceived', received)
            ws.on('framesent', lambda f: sent.append(json.loads(f)) if isinstance(f, str) else None)
        page.on('websocket', websocket)
        def advance(text, clock):
            field=page.get_by_label('Advance amount',exact=True);field.fill(text);field.press('Enter')
            expect(page.get_by_test_id('clock')).to_contain_text(clock)
            page.wait_for_timeout(75)
        def card(id, kind): return page.get_by_role('region',name=f'{id} {kind}',exact=True)
        def configure(c): c.get_by_role('button',name='Configure instrument',exact=True).click()
        def apply(c):
            c.get_by_role('button',name='Apply instrument settings',exact=True).click()
            expect(c.get_by_role('button',name='Configure instrument',exact=True)).to_be_visible()
        def export():
            with page.expect_download() as pending: page.get_by_role('button',name='CSV',exact=True).first.click()
            return Path(pending.value.path()).read_text()
        with serve(args.embsim.resolve(),'instrument-loading') as url:
            page.goto(url);page.get_by_role('radio',name='Run',exact=True).click()
            vm=card('VM','Voltmeter');configure(vm)
            page.get_by_label('VM input_resistance',exact=True).fill('1meg');apply(vm)
            advance('21us','21.000 µs')
            assert abs(values['VM.voltage']-5)<1e-4,values
            # No-op configuration does not discard native time or capture state.
            configure(vm);apply(vm)
            expect(page.get_by_test_id('clock')).to_contain_text('21.000 µs')
            configure(vm);page.get_by_label('VM negative lead',exact=True).select_option('SUPPLY_5V');apply(vm)
            expect(page.get_by_test_id('clock')).to_contain_text('0.000 ps')
            advance('1us','1.000 µs')
            assert abs(values['VM.voltage']-2.5)<1e-4,values
            # The shunt and output resistances really enter their separate solves.
            am=card('AM','Ammeter');configure(am)
            page.get_by_label('AM shunt_resistance',exact=True).fill('100');apply(am)
            advance('1us','1.000 µs');assert abs(values['AM.current']-.025)<1e-7,values
            gen=card('GEN','Generator');configure(gen)
            page.get_by_label('GEN output_resistance',exact=True).fill('100');apply(gen)
            advance('1us','1.000 µs');assert abs(values['GEN.voltage']-2/3)<1e-5,values
            scope=card('SCOPE','Scope input');configure(scope)
            page.get_by_label('SCOPE input preset',exact=True).select_option('10x');apply(scope)
            expect(scope).to_contain_text('Input 10 MΩ ∥ 12 pF')
            page.get_by_label('SCOPE sample rate',exact=True).fill('1000000')
            scope.get_by_role('button',name='Add trace',exact=True).click()
            trigger=page.get_by_label('Trigger source',exact=True)
            option=trigger.locator('option').filter(has_text='device:SCOPE.voltage').last
            expect(option).to_be_attached();probe=option.get_attribute('value');assert probe
            trigger.select_option(probe)
            page.get_by_label('Trigger level',exact=True).fill('4')
            page.get_by_label('Hysteresis',exact=True).fill('0.05')
            page.get_by_label('Capture span (ms)',exact=True).fill('0.02')
            page.get_by_label('Pre-trigger (%)',exact=True).fill('25')
            page.get_by_role('button',name='Arm capture',exact=True).click()
            advance('30us','30.000 µs')
            expect(page.get_by_role('status',name='Acquisition status')).to_contain_text('captured:')
            before=export();rows=list(csv.DictReader(io.StringIO(before)))
            point=min(rows,key=lambda r:abs(int(r['time_ps'])-21000000))
            expected=10*(10/11)*(1-math.exp(-20/((10/11)*12)))
            assert abs(float(point[probe])-expected)<.025,(point,expected)
            assert any(m.get('method')=='probe.add' and m['params'].get('target')=='device:SCOPE.voltage' and m['params'].get('rate_hz')==1000000 for m in sent)
            advance('20us','50.000 µs');assert export()==before,'completed loaded capture changed'
            configure(scope)
            page.screenshot(path=str(out/'loaded-instrument-controls.png'))
            scope.get_by_role('button',name='Cancel instrument settings',exact=True).click()
            # Another physical input load changes the circuit and clears acquisition as stated.
            configure(scope);page.get_by_label('SCOPE input preset',exact=True).select_option('1x');apply(scope)
            expect(page.get_by_role('status',name='Acquisition status')).to_contain_text('Live')
            advance('21us','21.000 µs')
            assert abs(values['SCOPE.voltage']-5*(1-math.exp(-20/50)))<.025,values
            configure(scope);page.get_by_label('SCOPE input preset',exact=True).select_option('50');apply(scope)
            advance('21us','21.000 µs')
            assert abs(values['SCOPE.voltage']-10*50/1000050)<1e-6,values
        with serve(args.embsim.resolve(),'hierarchical-instruments') as url:
            page.goto(url);page.get_by_role('radio',name='Run',exact=True).click()
            advance('1us','1.000 µs')
            a=card('A.METER','Voltmeter');b=card('B.METER','Voltmeter')
            expect(a.get_by_role('group',name='Terminal voltage: 5 V',exact=True)).to_be_attached()
            expect(b.get_by_role('group',name='Terminal voltage: 9.091 V',exact=True)).to_be_attached()
            configure(a)
            expect(page.get_by_label('A.METER input_resistance',exact=True)).to_be_disabled()
            expect(a).to_contain_text('changes affect 2 instances')
            a.get_by_role('button',name='Cancel instrument settings',exact=True).click()
            a.get_by_role('button',name='Trace current',exact=True).click()
            expect(page.get_by_label('Trigger source',exact=True).locator('option').filter(has_text='device:A.METER.current')).to_be_attached()
            a.get_by_role('button',name='Open instrument sheet',exact=True).click()
            expect(page.get_by_label('Active sheet',exact=True)).to_have_value('A')
        assert not errors,errors
        browser.close()
    print('Native instrument controls: differential loading, shunt burden, source impedance, physical scope presets, loaded trigger/immutable CSV and explicit restart; 0 errors')


if __name__=='__main__':main()
