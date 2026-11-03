// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A workbench panel taken out of its perspective floats over every
// perspective, its home dock keeps a way back, and the palette and the
// window's own button dock it again. The window's place is kept over a reload.
import { expect, test } from '@playwright/test';

const GALLERY = '../../../docs/gallery/movable-panels';

test('a panel floats from Run over Design and docks back', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Run', exact: true }).click();
  await page.getByRole('tab', { name: 'Diagnostics', exact: true }).click();
  await page.getByRole('button', { name: 'Float Diagnostics over every perspective', exact: true }).click();

  const win = page.getByRole('dialog', { name: 'Diagnostics', exact: true });
  await expect(win).toBeVisible();
  // its home tab says where it went
  await expect(page.getByText('Diagnostics floats over every perspective.', { exact: true })).toBeVisible();

  // it stays over the Design perspective, and over a reload
  await page.getByRole('radio', { name: 'Design', exact: true }).click();
  await expect(win).toBeVisible();
  await page.screenshot({ path: `${GALLERY}/diagnostics-over-design.png` });
  await page.reload();
  await expect(page.getByRole('dialog', { name: 'Diagnostics', exact: true })).toBeVisible();

  // docked from its window, it is back in Run's tabs
  await page.getByRole('dialog', { name: 'Diagnostics', exact: true }).getByRole('button', { name: 'Dock Diagnostics in Run', exact: true }).click();
  await expect(page.getByRole('dialog', { name: 'Diagnostics', exact: true })).toHaveCount(0);
  await page.getByRole('radio', { name: 'Run', exact: true }).click();
  await page.getByRole('tab', { name: 'Diagnostics', exact: true }).click();
  await expect(page.getByText('Diagnostics floats over every perspective.', { exact: true })).toHaveCount(0);

  // the palette floats and docks the panels too
  await page.keyboard.press('Control+k');
  await page.getByRole('combobox').fill('Float Meters');
  await page.keyboard.press('Enter');
  await expect(page.getByRole('dialog', { name: 'Meters', exact: true })).toBeVisible();
  await page.keyboard.press('Control+k');
  await page.getByRole('combobox').fill('Dock Meters');
  await page.keyboard.press('Enter');
  await expect(page.getByRole('dialog', { name: 'Meters', exact: true })).toHaveCount(0);
});
