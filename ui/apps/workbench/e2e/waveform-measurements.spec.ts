// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test } from '@playwright/test';

test('inspect time-weighted measurements with current units and complete coverage', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Run', exact: true }).click();
  await page.getByRole('textbox', { name: 'Advance amount', exact: true }).fill('25ms');
  await page.getByRole('textbox', { name: 'Advance amount', exact: true }).press('Enter');
  const scope = page.getByRole('region', { name: 'Oscilloscope', exact: true });
  await scope.getByRole('radio', { name: 'ic', exact: true }).click();
  await expect(scope.getByRole('img')).toHaveAttribute('aria-label', /RMS [\d.]+ mA/);
  await expect(scope.locator('.es-instrument__measurements')).toContainText('100.0 %');
  await scope.getByRole('radio', { name: 'coll', exact: true }).click();
  await expect(scope.getByRole('img')).toHaveAttribute('aria-label', /RMS [\d.]+ mV/);
  const logic = page.getByRole('region', { name: 'Logic analyzer', exact: true });
  await expect(logic.locator('.es-instrument__measurements')).toContainText('duty');
  await expect(logic.locator('.es-instrument__measurements')).toContainText('100.0 %');
  await page.getByRole('textbox', { name: 'Advance amount', exact: true }).fill('10ms');
  await scope.getByRole('img').focus();
  await page.keyboard.press('F7');
  await expect(page.getByTestId('clock')).toContainText('35');
  expect(errors).toEqual([]);
});
