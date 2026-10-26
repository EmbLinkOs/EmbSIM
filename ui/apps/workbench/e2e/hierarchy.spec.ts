// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test } from '@playwright/test';

test('extract, reuse, edit and trace a shared schematic block without 3D', async ({ page }) => {
  const errors: string[] = [];
  const renderers: string[] = [];
  page.on('request',(r)=>{if(r.url().includes('Bench3D-'))renderers.push(r.url());});
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto('/?mock=1');
  await page.getByRole('button', { name: 'Sheets and reusable blocks', exact: true }).click();
  const active = page.getByLabel('Active sheet', { exact: true });
  await page.getByRole('button', { name: /^R1, / }).press('Enter');
  await page.getByLabel('Block identifier', { exact: true }).fill('pullup');
  await page.getByRole('button', { name: 'Extract selection', exact: true }).click();
  await expect(active).toHaveValue('@pullup');
  await expect(page.getByRole('button', { name: 'Sheet port BTN, R1.2', exact: true })).toBeVisible();
  await active.selectOption('');
  await page.getByRole('button', { name: 'Place', exact: true }).click();
  await expect(active.locator('option[value="X2"]')).toHaveCount(1);
  await active.selectOption('X1');
  await page.getByRole('button', { name: /^R1, / }).press('Enter');
  const value = page.getByLabel('Parameter R1.resistance', { exact: true });
  await value.fill('2200');
  await value.press('Tab');
  await expect(value).toBeEnabled();
  await active.selectOption('X2');
  await page.getByRole('button', { name: /^R1, / }).press('Enter');
  await expect(page.getByLabel('Parameter R1.resistance', { exact: true })).toHaveValue('2200');
  await page.getByRole('button', { name: 'Undo sheet edit', exact: true }).click();
  await expect(page.getByLabel('Parameter R1.resistance', { exact: true })).toHaveValue('10k');
  await active.selectOption('X1');
  await page.getByLabel('Trace local net', { exact: true }).selectOption('BTN');
  const trace = page.getByRole('region', { name: 'Cross-sheet trace', exact: true });
  await expect(trace).toContainText('Main · BTN');
  await expect(trace).toContainText('X1 · BTN');
  await expect(trace).not.toContainText('X2 · BTN');
  expect(errors).toEqual([]);
  expect(renderers).toEqual([]);
});
