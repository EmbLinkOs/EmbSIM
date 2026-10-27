# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native assembly counts, resolved configurations, exact identities and sheet cross-probing."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from playwright.sync_api import expect, sync_playwright
from test_instrument_loading import serve, ROOT


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim',required=True,type=Path);parser.add_argument('--chromium');parser.add_argument('--screenshots',type=Path)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='embsim-engineering-') as tmp,sync_playwright() as pw:
        out=args.screenshots or Path(tmp);out.mkdir(parents=True,exist_ok=True)
        browser=pw.chromium.launch(executable_path=args.chromium,headless=True,args=['--no-sandbox'])
        page=browser.new_page(viewport={'width':1800,'height':1200},color_scheme='light',accept_downloads=True)
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
        with serve(args.embsim.resolve(),'hierarchical-instruments') as url:
            page.goto(url)
            expect(page.get_by_label('Active sheet',exact=True)).to_be_visible(timeout=15000)
            amount=page.get_by_label('Advance amount',exact=True);amount.fill('1us');amount.press('Enter')
            expect(page.get_by_test_id('clock')).to_contain_text('1.000 µs')
            page.get_by_role('button',name='Engineering reports',exact=True).click()
            dialog=page.get_by_role('dialog',name='Engineering reports',exact=True)
            expect(dialog).to_contain_text('Native snapshot · SHA-256 verified')
            expect(page.get_by_test_id('clock')).to_contain_text('1.000 µs')
            table=dialog.get_by_role('table',name='Assembly inventory',exact=True)
            rows=table.locator('tbody tr');expect(rows).to_have_count(1)
            expect(rows.first.get_by_role('cell').first).to_have_text('2')
            expect(rows.first).to_contain_text('A.R, B.R')
            with page.expect_download() as pending:dialog.get_by_role('button',name='Export BOM CSV',exact=True).click()
            bom=list(csv.DictReader(io.StringIO(Path(pending.value.path()).read_text())))
            assert len(bom)==1 and bom[0]['Quantity']=='2' and bom[0]['Catalog reference']=='emblink/resistor',bom
            assert json.loads(bom[0]['Resolved model configuration'])['resistance']==1000000,bom
            with page.expect_download() as pending:dialog.get_by_role('button',name='Export model manifest',exact=True).click()
            manifest_path=Path(pending.value.path());capsule=json.loads(manifest_path.read_text())
            assert capsule['sha256']=='sha256:'+hashlib.sha256(capsule['json'].encode()).hexdigest()
            data=json.loads(capsule['json']);parts={r['path']:r for r in data['instances']}
            assert parts['A.METER']['params']['input_resistance']==1e6 and parts['B.METER']['params']['input_resistance']==1e7
            assert len(parts)==6 and all(not r['reference'].startswith('block/') for r in parts.values())
            subprocess.run([sys.executable,str(ROOT/'tools/validate.py'),'--strict','--quiet',str(manifest_path)],cwd=ROOT,check=True,capture_output=True,text=True)
            cli=subprocess.run([str(args.embsim.resolve()),'manifest',str(ROOT/'examples/hierarchical-instruments/project.json'),'--json'],cwd=ROOT,check=True,capture_output=True,text=True)
            cli_data=json.loads(json.loads(cli.stdout)['json'])
            assert cli_data['project']==data['project'] and cli_data['instances']==data['instances'],'GUI and CLI differ on native source facts'
            dialog.get_by_role('tab',name='Models and sources',exact=True).click()
            expect(dialog).to_contain_text(data['project']['sha256'])
            page.screenshot(path=str(out/'native-model-ledger.png'))
            dialog.get_by_role('tab',name='Assembly',exact=True).click()
            rows.first.locator('summary').first.click()
            rows.first.get_by_role('button',name='Locate B.R',exact=True).click()
            expect(page.get_by_label('Active sheet',exact=True)).to_have_value('B')
            expect(page.get_by_label('Parameter R.resistance',exact=True)).to_have_value('1meg')
        assert not errors,errors
        browser.close()
    print('Native engineering reports: correct hierarchy/package counts, resolved values, GUI/CLI identity parity, offline verification and exact sheet selection; 0 errors')


if __name__=='__main__':main()
