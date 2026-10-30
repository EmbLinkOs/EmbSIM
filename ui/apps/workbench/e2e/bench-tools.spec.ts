// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The 3D bench's inspection tools (docs/engineering/bench-3d.md, stage 1)
// against the demo session: a measurement between a resistor's legs, the
// standard views and the orthographic camera, the height plane's report,
// hidden layers, the section, and the exports. None of them edits the project.

import { expect, test, type Page } from '@playwright/test';

async function openBench(page: Page) {
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  await expect(page.locator('.eb-bench3d canvas')).toBeVisible({ timeout: 15_000 });
}
const menu = (page: Page, name: 'View' | 'Inspect') => page.locator('.wb-bench-view-menu summary', { hasText: name }).click();

/** a contact's screen position, found through the jumper tool's named hover feedback */
async function contact(page: Page, ref: string) {
  const bench = page.locator('.eb-bench3d');
  const box = (await bench.boundingBox())!;
  for (const dy of [0, -6, 6, -12, 12]) {
    for (let x = box.x + 12; x < box.x + box.width - 12; x += 7) {
      const y = box.y + box.height / 2 + dy;
      await page.mouse.move(x, y);
      if (await bench.getAttribute('data-terminal') === ref) return { x, y };
    }
  }
  throw new Error(`No visible contact ${ref}`);
}

test('measure between two pins: snapped to the contacts, on the 0.1 in grid', async ({ page }) => {
  await openBench(page);
  const list = page.getByRole('complementary', { name: 'On the bench' });
  await list.getByRole('button', { name: /^R2/ }).click();
  await menu(page, 'View');
  await page.getByRole('button', { name: 'Top', exact: true }).click();
  await menu(page, 'View');
  await page.getByRole('button', { name: 'Focus part', exact: true }).click();
  await page.getByRole('radio', { name: 'Jumper', exact: true }).click();
  const a = await contact(page, 'R2.1');
  const b = await contact(page, 'R2.2');
  const nets = page.getByRole('table').first();
  const before = (await nets.textContent())!;
  await page.getByRole('radio', { name: 'Measure', exact: true }).click();
  await page.mouse.click(a.x, a.y);
  await expect(page.locator('.eb-connection-feedback')).toContainText('click the second point');
  await page.mouse.click(b.x, b.y);
  // the resistor's legs are 0.4 in apart
  await expect(page.locator('.wb-bench-status')).toContainText('10.16 mm (4 × 2.54 mm)');
  await expect(page.locator('.eb-measure')).toContainText('10.16 mm');
  await page.locator('.eb-bench3d').press('Escape');
  await expect(page.locator('.eb-measure')).toHaveCount(0);
  await expect(nets).toHaveText(before);
});

test('views, projection, height plane, layers and section are presentation only', async ({ page }) => {
  await openBench(page);
  const nets = page.getByRole('table').first();
  const before = (await nets.textContent())!;
  await menu(page, 'View');
  for (const view of ['Top', 'Bottom', 'Front', 'Back', 'Left', 'Right', 'Isometric', 'Desk']) await page.getByRole('button', { name: view, exact: true }).click();
  await page.getByRole('radio', { name: 'Orthographic' }).click();
  await menu(page, 'View');
  // the view cube's visible faces frame the standard views too: from the desk, Top and Front show
  const face = (view: string) => page.locator(`.eb-viewcube__face[data-view="${view}"]`);
  await face('front').click();
  await expect(face('top')).toBeVisible();
  await face('top').click();
  const bench = page.locator('.eb-bench3d');
  await bench.focus();
  for (const key of ['z', 'Shift+Z', 'y', 'x', 'i']) await page.keyboard.press(key);

  await menu(page, 'Inspect');
  await page.getByRole('button', { name: 'Height plane' }).click();
  await page.getByLabel('Height above the desk').fill('0.5');
  await expect(page.locator('.wb-bench-status')).toContainText('Above 0.5 mm:');
  await page.getByLabel('Height above the desk').fill('500');
  await expect(page.locator('.wb-bench-status')).toContainText('Nothing rises above 500 mm');
  await page.getByRole('button', { name: 'Height plane' }).click();
  await page.getByRole('combobox', { name: 'Section' }).click();
  await page.getByRole('option', { name: 'Cut across X' }).click();
  await page.getByLabel('Section at').fill('5');
  await page.getByRole('button', { name: 'Keep the other side' }).click();
  await page.getByRole('button', { name: 'Labels', exact: true }).click();
  await expect(page.locator('.eb-labels')).toBeHidden();
  await page.getByRole('button', { name: 'Labels', exact: true }).click();
  await expect(page.locator('.eb-labels')).toBeVisible();

  // the exports: a PNG and a glTF binary
  const png = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Image 2×' }).click();
  expect((await png).suggestedFilename()).toMatch(/\.png$/);
  const glb = page.waitForEvent('download');
  await page.getByRole('button', { name: 'GLB model' }).click();
  const file = await (await glb).path();
  const { readFileSync } = await import('node:fs');
  expect(readFileSync(file).subarray(0, 4).toString('latin1')).toBe('glTF');

  await expect(nets).toHaveText(before);
  // the projection is kept as the other appearance preferences are
  await page.reload();
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  await menu(page, 'View');
  await expect(page.getByRole('radio', { name: 'Orthographic' })).toHaveAttribute('aria-checked', 'true');
});
