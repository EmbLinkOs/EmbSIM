// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Smoke test (SPEC-011 §6.7, against the demo session): open project, run,
// press the button, observe, export. Run with `pnpm e2e` after `pnpm build`.

import { expect, test } from '@playwright/test';

test('open, run, press, observe, export', async ({ page }) => {
  await page.goto('/?mock=1');
  await expect(page.getByText('Demo session').first()).toBeVisible();
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).toBeVisible();

  await page.getByRole('radio', { name: 'Run', exact: true }).click();
  await expect(page.getByRole('img', { name: /Oscilloscope/ })).toBeVisible();

  await page.getByRole('button', { name: 'Run', exact: true }).click();
  await expect(page.getByTestId('clock')).toContainText('run');

  const hold = page.getByRole('button', { name: /SW1 Push button Button cap/ });
  await hold.dispatchEvent('pointerdown', { button: 0 });
  await page.waitForTimeout(300);
  await hold.dispatchEvent('pointerup');
  await page.waitForTimeout(300);

  await page.getByRole('button', { name: 'Pause' }).click();
  await expect(page.getByTestId('clock')).toContainText('paused');

  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'CSV' }).first().click();
  const file = await download;
  expect(file.suggestedFilename()).toMatch(/\.csv$/);
});

test('keyboard: place and wire from the library, light theme contrast', async ({ page }) => {
  await page.goto('/?mock=1');
  await page.getByRole('button', { name: 'Place Capacitor' }).click();
  await expect(page.getByRole('button', { name: /^C1, Capacitor/ })).toBeVisible();
  await page.getByRole('button', { name: 'Pin C1.1, unconnected' }).focus();
  await page.keyboard.press('Enter');
  await page.getByRole('button', { name: /^Pin Q1.C/ }).focus();
  await page.keyboard.press('Enter');
  await expect(page.getByRole('button', { name: /Pin C1.1, net COLL/ })).toBeVisible();

  // the theme follows the system first; the toggle switches to the other
  const before = await page.locator('html').getAttribute('data-theme');
  const other = before === 'dark' ? 'light' : 'dark';
  await page.getByRole('button', { name: `Switch to ${other} theme` }).click();
  await expect(page.locator('html')).toHaveAttribute('data-theme', other);
});
