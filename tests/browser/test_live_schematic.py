# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native schematic light, control, display and differential-observation acceptance."""
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
    with tempfile.TemporaryDirectory(prefix='embsim-live-browser-') as tmp, sync_playwright() as pw:
        out = args.screenshots or Path(tmp)
        out.mkdir(parents=True, exist_ok=True)
        browser = pw.chromium.launch(executable_path=args.chromium, headless=True, args=['--no-sandbox'])
        page = browser.new_page(viewport={'width': 1800, 'height': 1200}, color_scheme='light')
        errors, sent, requests = [], [], []
        page.on('pageerror', lambda e: errors.append(str(e)))
        page.on('request', lambda r: requests.append(r.url))
        page.on('websocket', lambda ws: ws.on('framesent', lambda frame: sent.append(json.loads(frame)) if isinstance(frame,str) else None))

        def advance(amount):
            field=page.get_by_label('Advance amount',exact=True)
            field.fill(amount)
            field.press('Enter')
            page.wait_for_timeout(200)

        def emitter(id):
            return page.locator(f'[data-board-item="symbol-{id}"] #emitter')

        with serve(args.embsim.resolve(), 'live-schematic') as url:
            page.goto(url)
            page.get_by_role('radio', name='Run', exact=True).click()
            expect(page.get_by_role('application', name='Live schematic')).to_be_visible()
            page.get_by_label('Schematic display gain',exact=True).select_option('1')
            advance('1ms')
            expect(emitter('D3')).to_have_attribute('opacity',re.compile(r'0\.[2-5]\d+'))
            expect(emitter('D1')).to_have_attribute('opacity','0.000')
            expect(emitter('D3')).to_have_css('fill','rgb(10, 132, 255)')
            expect(page.locator('[data-board-item="symbol-MATRIX"] #D11')).to_have_attribute('opacity',re.compile(r'0\.[3-6]\d+'))
            expect(page.locator('[data-board-item="symbol-MATRIX"] #D12')).to_have_attribute('opacity','0.000')
            # Hold a real contact through an engine advance, then blur it. No editor transaction.
            button=page.get_by_role('button',name='HOLD pressed',exact=True)
            button.focus()
            page.keyboard.down('Space')
            page.wait_for_timeout(100)
            advance('1ms')  # moving focus releases the hold; verify both native input messages
            assert any(m.get('method')=='input.set' and m.get('params',{}).get('target')=='HOLD' and m['params'].get('value') is True for m in sent)
            assert any(m.get('method')=='input.set' and m.get('params',{}).get('target')=='HOLD' and m['params'].get('value') is False for m in sent)
            page.keyboard.up('Space')
            latch=page.get_by_role('button',name='LATCH closed',exact=True)
            latch.press('Space')
            advance('1ms')
            expect(emitter('D2')).to_have_attribute('opacity',re.compile(r'0\.[3-6]\d+'))
            latch.press('Space')
            advance('1ms')
            expect(emitter('D2')).to_have_attribute('opacity','0.000')
            pot=page.get_by_role('application',name='Live schematic').get_by_role('slider',name='POT position',exact=True)
            pot.press('Home')
            pot.press('ArrowRight')
            expect(pot).to_have_attribute('aria-valuenow','0.01')
            advance('1ms')
            # Measurements use two solved voltages; same-node and polarity are tested by the UI.
            page.get_by_role('button',name='Expand Voltage comparison panel',exact=True).click()
            page.get_by_label('Voltage probe A net',exact=True).select_option('SUPPLY')
            page.get_by_label('Voltage probe B net',exact=True).select_option('GND')
            expect(page.get_by_label('Differential voltage A minus B',exact=True)).to_have_text('5 V')
            expect(page.get_by_role('img',name='Voltage probe A on SUPPLY',exact=True)).to_be_visible()
            # Pointer drop changes the observation lead, preserving the electrical project.
            marker=page.get_by_role('img',name='Voltage probe A on SUPPLY',exact=True)
            source=marker.bounding_box()
            ground=page.locator('[data-pin="D3.K"] .es-pin__dot').bounding_box()
            assert source and ground
            page.mouse.move(source['x']+source['width']/2,source['y']+source['height']/2)
            page.mouse.down()
            page.mouse.move(ground['x']+ground['width']/2,ground['y']+ground['height']/2,steps=8)
            page.mouse.up()
            expect(page.get_by_label('Voltage probe A net',exact=True)).to_have_value('GND')
            expect(page.get_by_label('Differential voltage A minus B',exact=True)).to_have_text('0 V')
            assert not any(m.get('method')=='project.edit' for m in sent)
            # A 25% pulse still emits interval-average light at an off-time boundary.
            expect(emitter('DPWM')).to_have_attribute('opacity',re.compile(r'0\.[0-3]\d+'))
            page.get_by_label('Schematic light exposure',exact=True).select_option('instant')
            expect(emitter('DPWM')).to_have_attribute('opacity','0.000')
            page.get_by_label('Schematic light exposure',exact=True).select_option('average')
            page.get_by_label('Schematic display gain',exact=True).select_option('4')
            expect(emitter('D3')).to_have_attribute('opacity','1.000')
            latch.press('Space')
            advance('1ms')
            button.focus()
            page.keyboard.down('Space')
            page.wait_for_timeout(100)
            page.keyboard.press('F7')
            expect(page.get_by_test_id('clock')).to_contain_text('7.000',timeout=15000)
            expect(emitter('D1')).to_have_attribute('opacity','1.000')
            page.mouse.move(10,10)
            page.screenshot(path=str(out/'live-schematic.png'))
            page.keyboard.up('Space')
            for width,height in [(1280,800),(900,760)]:
                page.set_viewport_size({'width':width,'height':height})
                canvas=page.get_by_role('application',name='Live schematic')
                expect(canvas).to_be_visible()
                box=canvas.bounding_box()
                assert box and box['width']>=250 and box['height']>=180,box
                assert page.evaluate('document.documentElement.scrollWidth') <= width
            assert not any(re.search(r'/(?:Bench3D|BenchScene|three)[^/]*\.js',u,re.I) for u in requests), 'Run must not load the deferred 3D workbench'

        page.set_viewport_size({'width':1800,'height':1200})
        with serve(args.embsim.resolve(),'display-showcase') as url:
            page.goto(url)
            page.get_by_role('radio',name='Run',exact=True).click()
            advance('150ms')
            # These are native peripheral protocol outputs, not browser-injected display text.
            expect(page.locator('[data-board-item="symbol-LCD"] #line1')).to_contain_text('EmbSIM catalog')
            expect(page.locator('[data-board-item="symbol-OLED"] #pixels')).to_have_attribute('d',re.compile(r'M.+'))
            expect(page.locator('[data-board-item="symbol-DISP"] #digits')).to_have_text('20261016')
            page.screenshot(path=str(out/'live-displays.png'))
        with serve(args.embsim.resolve(),'esp32c3-qualified-rgb') as url:
            page.goto(url)
            page.get_by_role('radio',name='Run',exact=True).click()
            advance('250ms')
            expect(page.get_by_test_id('clock')).to_contain_text('250',timeout=45000)
            advance('10ms')
            expect(page.get_by_test_id('clock')).to_contain_text('260',timeout=30000)
            expect(emitter('D1')).to_have_attribute('opacity',re.compile(r'0\.[1-9]\d*'),timeout=15000)
            page.screenshot(path=str(out/'firmware-mixed-schematic.png'))
            # Keep the schematic contact held while F7 advances real firmware.
            amount=page.get_by_label('Advance amount',exact=True)
            amount.fill('5ms')
            mode=page.get_by_role('application',name='Live schematic').get_by_role('button',name='MODE pressed',exact=True)
            mode.focus()
            page.keyboard.down('Space')
            page.wait_for_timeout(100)
            page.keyboard.press('F7')
            expect(page.get_by_test_id('clock')).to_contain_text('265',timeout=30000)
            page.keyboard.up('Space')
            advance('5ms')
            expect(page.get_by_test_id('clock')).to_contain_text('270',timeout=30000)
            expect(emitter('D1')).to_have_attribute('opacity','0.000')
            expect(page.get_by_role('application',name='Live schematic').get_by_role('button',name='B1.SW1 pressed',exact=True)).to_be_attached()
            page.screenshot(path=str(out/'firmware-off-schematic.png'))
            mode.focus()
            page.keyboard.down('Space')
            page.wait_for_timeout(100)
            page.keyboard.press('F7')
            expect(page.get_by_test_id('clock')).to_contain_text('275',timeout=30000)
            page.keyboard.up('Space')
            advance('5ms')
            expect(page.get_by_test_id('clock')).to_contain_text('280',timeout=30000)
            expect(emitter('D1')).to_have_attribute('opacity','1.000')
            page.screenshot(path=str(out/'firmware-on-schematic.png'))
        assert not any(re.search(r'/assets/Bench3D-.*\.js',u) for u in requests)
        assert not errors,errors
        browser.close()
        print('Native live lights, input release, latch, pot, matrix, differential probes and display bindings pass; no 3D chunk or project edits.')


if __name__=='__main__':
    main()
