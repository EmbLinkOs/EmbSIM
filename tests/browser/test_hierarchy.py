# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real native shared-sheet editing, tracing, port rename and extraction."""
import argparse
import json
from pathlib import Path
import re
import tempfile
from playwright.sync_api import expect, sync_playwright
from test_instrument_loading import serve


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim',required=True,type=Path)
    parser.add_argument('--chromium')
    parser.add_argument('--screenshots',type=Path)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-hierarchy-browser-') as tmp,sync_playwright() as pw:
        out=args.screenshots or Path(tmp);out.mkdir(parents=True,exist_ok=True)
        browser=pw.chromium.launch(executable_path=args.chromium,headless=True,args=['--no-sandbox'])
        page=browser.new_page(viewport={'width':1800,'height':1200},color_scheme='light',accept_downloads=True)
        errors,sent,received=[],[],[]
        values={}
        page.on('pageerror',lambda e:errors.append(str(e)))
        def got(frame):
            if not isinstance(frame,str):return
            msg=json.loads(frame);received.append(msg)
            data=msg.get('params',{}) if msg.get('method')=='observe.update' else msg.get('result',{})
            if isinstance(data,dict) and isinstance(data.get('nets'),dict):values.update(data['nets'])
        def websocket(ws):
            ws.on('framesent',lambda frame:sent.append(json.loads(frame)) if isinstance(frame,str) else None)
            ws.on('framereceived',got)
        page.on('websocket',websocket)
        def advance(amount='1ms'):
            box=page.get_by_label('Advance amount',exact=True);box.fill(amount);box.press('Enter')
            expect(page.get_by_test_id('clock')).to_contain_text('1.000')
            page.wait_for_timeout(75)
        def check_voltages(a,b):
            assert abs(values['AOUT']['voltage']-a)<1e-5 and abs(values['BOUT']['voltage']-b)<1e-5,values
        with serve(args.embsim.resolve(),'hierarchical-divider') as url:
            page.goto(url)
            expect(page.get_by_role('application',name='Sheet Main',exact=True)).to_be_visible(timeout=15000)
            advance();check_voltages(2,1.65)
            active=page.get_by_label('Active sheet',exact=True);active.select_option('A.D')
            expect(page.get_by_role('application',name='Sheet A.D',exact=True)).to_be_visible()
            # Same shared contact definition, qualified to A.D rather than B.
            page.get_by_role('radio',name='Operate',exact=True).click()
            amount=page.get_by_label('Advance amount',exact=True);amount.fill('1us')
            hold=page.get_by_role('button',name='SW pressed',exact=True)
            hold.focus();page.keyboard.down('Space');expect(hold).to_have_attribute('aria-pressed','true')
            page.keyboard.press('F7');expect(page.get_by_test_id('clock')).to_contain_text('1.001')
            page.wait_for_timeout(75)
            assert values['AOUT']['voltage']<.001 and abs(values['BOUT']['voltage']-1.65)<1e-5, values
            page.keyboard.up('Space');page.keyboard.press('F7');expect(page.get_by_test_id('clock')).to_contain_text('1.002')
            page.wait_for_timeout(75);check_voltages(2,1.65)
            assert any(m.get('method')=='input.set' and m['params'].get('target')=='A.D.SW' for m in sent)
            page.get_by_role('radio',name='Edit',exact=True).click()
            page.get_by_label('Trace local net',exact=True).select_option('OUT')
            trace=page.get_by_role('region',name='Cross-sheet trace',exact=True)
            expect(trace).to_contain_text('Main · AOUT');expect(trace).to_contain_text('A · OUT');expect(trace).to_contain_text('A.D · OUT')
            expect(trace).not_to_contain_text('B ·')
            trace.get_by_role('button',name=re.compile(r'^Main · AOUT')).click()
            expect(active).to_have_value('')
            active.select_option('A.D')
            page.get_by_role('button',name=re.compile(r'^RT,')).press('Enter')
            field=page.get_by_label('Parameter RT.resistance',exact=True)
            field.fill('2k');field.press('Tab')
            expect(page.get_by_test_id('clock')).to_contain_text('0.000')
            advance();check_voltages(1.25,1.1)
            page.get_by_role('button',name='Undo sheet edit',exact=True).click()
            expect(page.get_by_test_id('clock')).to_contain_text('0.000')
            advance();check_voltages(2,1.65)
            # A drawing edit stays in the same native session and keeps its clock.
            hit=page.locator('[data-component="RT"] .es-component__hit')
            box=hit.bounding_box();assert box
            x,y=box['x']+box['width']/2,box['y']+box['height']/2
            page.mouse.move(x,y);page.mouse.down();page.mouse.move(x+45,y+35,steps=8);page.mouse.up()
            page.wait_for_timeout(150)
            expect(page.get_by_test_id('clock')).to_contain_text('1.000')
            commits=[m.get('result',{}) for m in received if isinstance(m.get('result'),dict) and 'restarted'in m['result']]
            assert commits[-1].get('restarted') is False,'child drawing edit restarted simulation'
            rename=page.get_by_label('Rename port IN',exact=True);rename.fill('VIN');rename.press('Tab')
            expect(page.get_by_label('Rename port VIN',exact=True)).to_be_visible()
            advance();check_voltages(2,1.65)
            page.screenshot(path=str(out/'nested-sheet-trace.png'))
            # Extracting one member of the shared definition changes both instances.
            page.get_by_role('button',name=re.compile(r'^RT,')).press('Enter')
            page.get_by_label('Block identifier',exact=True).fill('upper')
            page.get_by_role('button',name='Extract selection',exact=True).click()
            expect(active).to_have_value('@upper')
            expect(page.get_by_role('button',name='Sheet port IN, RT.1',exact=True)).to_be_visible()
            active.select_option('B.X1')
            expect(page.get_by_role('application',name='Sheet B.X1',exact=True)).to_be_visible()
            advance();check_voltages(2,1.65)
            with page.expect_download() as pending:
                page.get_by_role('button',name='Export block project',exact=True).click()
            exported=json.loads(Path(pending.value.path()).read_text())
            assert exported['blocks']['upper']['ports']['IN']=='RT.1' and set(exported['blocks'])=={'upper'},exported
            # Author native parameters through the GUI, bind a resistor and override one instance.
            active.select_option('@upper')
            page.get_by_role('button',name='New parameter',exact=True).click()
            page.get_by_label('Declaration name',exact=True).fill('resistance')
            page.get_by_label('Declaration default',exact=True).fill('1k')
            page.get_by_role('button',name='Add declaration',exact=True).click()
            expect(page.get_by_role('button',name='Edit resistance',exact=True)).to_be_visible()
            page.get_by_role('button',name=re.compile(r'^RT,')).press('Enter')
            value=page.get_by_label('Parameter RT.resistance',exact=True)
            value.fill('$resistance');value.press('Tab');expect(value).to_be_enabled()
            active.select_option('A.D')
            page.get_by_role('button',name=re.compile(r'^X1,')).press('Space')
            override=page.get_by_label('Parameter X1.resistance',exact=True)
            override.fill('2k');override.press('Tab');expect(override).to_be_enabled()
            advance();check_voltages(1.25,1.1)
            # A.D is a shared divider definition: its X1 override intentionally changes B too.
            page.get_by_role('button',name='Undo sheet edit',exact=True).click()
            advance();check_voltages(2,1.65)
            active.select_option('@upper')
            page.get_by_role('button',name='Edit resistance',exact=True).click()
            page.get_by_label('Declaration name',exact=True).fill('upper_r')
            page.get_by_role('button',name='Save declaration',exact=True).click()
            expect(page.get_by_role('button',name='Edit upper_r',exact=True)).to_be_visible()
            advance();check_voltages(2,1.65)
            # Bus edits are presentation-only and retain clock and actual voltages.
            active.select_option('A.D')
            page.get_by_label('Bus name',exact=True).fill('RAILS')
            page.get_by_label('Bus member nets',exact=True).fill('OUT, GND')
            page.get_by_role('button',name='Add bus',exact=True).click()
            expect(page.get_by_role('button',name='Trace bus RAILS bit 0, OUT',exact=True)).to_be_visible()
            expect(page.get_by_test_id('clock')).to_contain_text('1.000')
            page.get_by_label('Find in project',exact=True).fill('RT resistor')
            results=page.get_by_role('region',name='Project search results',exact=True)
            results.get_by_role('button').filter(has_text='B.X1 · RT').click()
            expect(active).to_have_value('B.X1')
            page.get_by_role('button',name=re.compile(r'^Project checks')).click()
            expect(page.get_by_role('region',name='Project electrical and drawing review',exact=True)).to_be_visible()
            page.screenshot(path=str(out/'project-review.png'))
            assert any(m.get('method')=='project.hierarchy' for m in sent)
            assert errors==[],errors
        browser.close()
    print('Native hierarchy browser: nested tracing, shared electrical edit/undo, retained-clock move, port rename, extraction/export, GUI parameter declaration/binding/rename/override/undo, buses and project review; 0 errors')


if __name__=='__main__':main()
