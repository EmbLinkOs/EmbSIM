// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test } from '@playwright/test';

test('arm a sampled capture, operate the sheet, and retarget a probe by keyboard', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Run', exact: true }).click();
  await page.getByLabel('Trigger source', { exact: true }).selectOption('coll');
  await page.getByLabel('Capture span (ms)', { exact: true }).fill('2');
  await page.getByRole('button', { name: 'Arm capture', exact: true }).click();
  const clock = page.getByLabel('Advance amount', { exact: true });
  await clock.fill('1ms'); await clock.press('Enter');
  await expect(page.getByTestId('clock')).toContainText('1');
  const hold = page.getByRole('application', { name: 'Live schematic' }).getByRole('button', { name: 'SW1 pressed', exact: true });
  await hold.focus(); await page.keyboard.down('Space');
  await expect(hold).toHaveAttribute('aria-pressed', 'true');
  await page.keyboard.press('F7');
  const status = page.getByRole('status', { name: 'Acquisition status' });
  await expect(status).toContainText('post-trigger');
  await page.keyboard.press('F7');
  await expect(status).toContainText('captured');
  await page.keyboard.up('Space');
  const captured = await status.innerText();
  await clock.fill('5ms'); await clock.press('Enter');
  await expect(page.getByTestId('clock')).toContainText('8');
  await expect(status).toHaveText(captured);
  await page.getByLabel('Move probe coll', { exact: true }).selectOption('BTN');
  await expect(page.getByRole('img', { name: 'Waveform probe coll on BTN', exact: true })).toBeVisible();
  await expect(status).toHaveText(captured);
  await page.getByRole('button', { name: 'Live view', exact: true }).click();
  await expect(status).toContainText('Live');
  expect(errors).toEqual([]);
});
