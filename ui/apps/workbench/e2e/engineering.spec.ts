// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test } from '@playwright/test';

test('export grouped hierarchy inventory, locate parts and reject stale snapshots', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto('/?mock=1');
  await page.getByRole('button', { name: 'Sheets and reusable blocks', exact: true }).click();
  await page.getByRole('button', { name: /^R1, / }).press('Enter');
  await page.getByLabel('Block identifier', { exact: true }).fill('resistor_stage');
  await page.getByRole('button', { name: 'Extract selection', exact: true }).click();
  const active = page.getByLabel('Active sheet', { exact: true });
  await expect(active).toHaveValue('@resistor_stage');
  await active.selectOption('');
  await page.getByRole('button', { name: 'Place', exact: true }).click();
  await expect(active.locator('option[value="X2"]')).toHaveCount(1);
  await page.getByRole('button', { name: 'Engineering reports', exact: true }).click();
  const dialog = page.getByRole('dialog', { name: 'Engineering reports', exact: true });
  await expect(dialog).toContainText('Demo · unverified parameters');
  const table = dialog.getByRole('table', { name: 'Assembly inventory', exact: true });
  const row = table.getByRole('row').filter({ hasText: 'X1.R1, X2.R1' });
  await expect(row.getByRole('cell').first()).toHaveText('2');
  const download = page.waitForEvent('download');
  await dialog.getByRole('button', { name: 'Export BOM CSV', exact: true }).click();
  expect((await download).suggestedFilename()).toContain('-bom.csv');
  await row.locator('summary').first().click();
  await row.getByRole('button', { name: 'Locate X2.R1', exact: true }).click();
  await expect(active).toHaveValue('X2');
  await expect(page.getByLabel('Parameter R1.resistance', { exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Engineering reports', exact: true }).click();
  await expect(dialog).toContainText('Demo · unverified parameters');
  await dialog.press('Control+z');
  await expect(dialog).toContainText('Project changed since this snapshot');
  await expect(dialog.getByRole('button', { name: 'Export BOM CSV', exact: true })).toBeDisabled();
  await dialog.getByRole('button', { name: 'Refresh report', exact: true }).click();
  await expect(dialog).not.toContainText('Project changed since this snapshot');
  await expect(dialog.getByRole('button', { name: 'Export BOM CSV', exact: true })).toBeEnabled();
  await expect(dialog.getByRole('table', { name: 'Assembly inventory' })).not.toContainText('X2.R1');
  await dialog.getByRole('tab', { name: 'Models and sources', exact: true }).click();
  await expect(dialog).toContainText('Loaded libraries');
  expect(errors).toEqual([]);
});

test('demo reports explain native-only library locking', async ({page}) => {
  await page.goto('/?mock=1');
  await page.getByRole('button', {name: 'Engineering reports', exact: true}).click();
  const dialog = page.getByRole('dialog', {name: 'Engineering reports', exact: true});
  await expect(dialog).toContainText('Library locking requires a native session with filesystem access.');
  await expect(dialog.getByRole('button', {name: 'Pin reviewed library inputs', exact: true})).toHaveCount(0);
});
