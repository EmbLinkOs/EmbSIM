// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Compact density reaches what CSS cannot: the plots drawn on canvases (their
// rows, margins and labels) and the schematic canvas's screen-space chrome.
// The schematic's own drawing keeps its size. Screenshots of both densities
// go to docs/gallery/compact-density.
import { expect, test, type Page } from '@playwright/test';

const GALLERY = '../../../docs/gallery/compact-density';

async function density(page: Page, which: 'Comfortable' | 'Compact') {
  await page.getByRole('button', { name: 'Settings', exact: true }).click();
  await page.getByTestId('settings-page').getByRole('radio', { name: which, exact: true }).click();
  await page.getByRole('button', { name: 'Back to the workbench' }).click();
}

test('compact density tightens the plots and the canvas chrome', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('/?mock=1');

  // the schematic's navigator, comfortable then compact
  const navigator = page.locator('.es-scene__navigator').first();
  await expect(navigator).toBeVisible();
  const roomy = (await navigator.boundingBox())!;
  await page.screenshot({ path: `${GALLERY}/design-comfortable.png` });
  await density(page, 'Compact');
  await expect(page.locator('html')).toHaveAttribute('data-density', 'compact');
  const tight = (await navigator.boundingBox())!;
  expect(tight.width).toBeLessThan(roomy.width);
  expect(tight.height).toBeLessThan(roomy.height);
  await page.screenshot({ path: `${GALLERY}/design-compact.png` });

  // the Run perspective's plots
  await page.getByRole('radio', { name: 'Run', exact: true }).click();
  await expect(page.getByTestId('run-toggle')).toBeEnabled();
  await page.getByRole('textbox', { name: 'Advance amount', exact: true }).fill('25ms');
  await page.getByRole('textbox', { name: 'Advance amount', exact: true }).press('Enter');
  await expect(page.getByTestId('clock')).toContainText('25');
  await expect(page.getByRole('region', { name: 'Logic analyzer', exact: true })).toBeVisible();
  await page.screenshot({ path: `${GALLERY}/run-compact.png` });
  await density(page, 'Comfortable');
  await page.getByRole('radio', { name: 'Run', exact: true }).click();
  await expect(page.locator('html')).toHaveAttribute('data-density', 'comfortable');
  await page.screenshot({ path: `${GALLERY}/run-comfortable.png` });
});
