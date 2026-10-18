// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test, type Page } from '@playwright/test';

async function openBench(page: Page) {
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  await expect(page.locator('.eb-bench3d canvas')).toBeVisible();
}
async function viewMenu(page: Page) {
  await page.locator('.wb-bench-view-menu summary').click();
}

test('inspection preferences persist and the keyboard list remains usable on compact windows', async ({ page }) => {
  await openBench(page);
  const list = page.getByRole('complementary', { name: 'On the bench' });
  await list.getByRole('button', { name: /^R2/ }).click();
  await viewMenu(page);
  await page.getByRole('button', { name: 'Top', exact: true }).click();
  await page.getByRole('combobox', { name: 'Bench surface' }).click();
  await page.getByRole('option', { name: 'Graphite', exact: true }).click();
  await page.getByRole('combobox', { name: 'Bench labels' }).click();
  await page.getByRole('option', { name: 'Selected only', exact: true }).click();
  await page.getByRole('button', { name: 'Notes', exact: true }).click();
  await viewMenu(page);
  await page.getByRole('button', { name: 'Parts & wiring', exact: true }).click();
  await expect(list).toHaveCount(0);
  await page.reload();
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  await expect(list).toHaveCount(0);
  await viewMenu(page);
  await expect(page.getByRole('combobox', { name: 'Bench surface' })).toHaveText('Graphite');
  await expect(page.getByRole('combobox', { name: 'Bench labels' })).toHaveText('Selected only');
  await expect(page.getByRole('button', { name: 'Notes', exact: true })).toHaveAttribute('aria-pressed', 'false');
  await viewMenu(page);
  await page.setViewportSize({ width: 1000, height: 900 });
  await page.getByRole('button', { name: 'Parts & wiring', exact: true }).click();
  await expect(list).toBeVisible();
  await expect(list.getByLabel('Jumper from')).toBeVisible();
});

test('renderer context loss falls back to usable 2D and can be retried', async ({ page }) => {
  await openBench(page);
  await page.locator('.eb-bench3d canvas').dispatchEvent('webglcontextlost');
  await expect(page.getByText('3D rendering is unavailable.', { exact: false })).toBeVisible();
  await expect(page.locator('.eb-bench3d')).toHaveCount(0);
  await expect(page.getByRole('complementary', { name: 'On the bench' })).toBeVisible();
  await page.getByRole('radio', { name: '3D', exact: true }).click();
  await expect(page.locator('.eb-bench3d canvas')).toBeVisible();
});

// Locate through the same named hover feedback users see. No private renderer
// state or assumed world-to-screen transform is injected into the application.
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

test('named pointer wiring cancels without edits, commits once and undoes exactly', async ({ page }) => {
  await openBench(page);
  await page.getByRole('complementary', { name: 'On the bench' }).getByRole('button', { name: /^R2/ }).click();
  await viewMenu(page);
  await page.getByRole('button', { name: 'Top', exact: true }).click();
  await viewMenu(page);
  await page.getByRole('button', { name: 'Focus part', exact: true }).click();
  await page.getByRole('radio', { name: 'Jumper', exact: true }).click();
  const a = await contact(page, 'R2.1');
  const b = await contact(page, 'R2.2');
  const nets = page.getByRole('table').first();
  const before = (await nets.textContent())!;
  for (const cancel of ['escape', 'pointercancel', 'blur']) {
    await page.mouse.move(a.x, a.y);
    await page.mouse.down();
    await page.mouse.move(b.x, b.y);
    await expect(page.locator('.eb-connection-feedback')).toContainText('R2.1 → R2.2');
    if (cancel === 'escape') await page.keyboard.press('Escape');
    else if (cancel === 'blur') await page.evaluate(() => window.dispatchEvent(new Event('blur')));
    else await page.locator('.eb-bench3d canvas').dispatchEvent('pointercancel');
    await page.mouse.up();
    await expect(nets).toHaveText(before);
  }
  await page.mouse.move(a.x, a.y);
  await page.mouse.down();
  await page.mouse.move(b.x, b.y);
  await page.mouse.up();
  await expect(nets).not.toHaveText(before);
  await page.keyboard.press('Control+z');
  await expect(nets).toHaveText(before);
});

test('interrupted physical button holds release the engine input', async ({ page }) => {
  await openBench(page);
  await page.getByRole('complementary', { name: 'On the bench' }).getByRole('button', { name: /^SW1/ }).click();
  await viewMenu(page);
  await page.getByRole('button', { name: 'Top', exact: true }).click();
  await viewMenu(page);
  await page.getByRole('button', { name: 'Focus part', exact: true }).click();
  await page.getByTestId('run-toggle').click();
  const row = page.getByRole('table', { name: 'Nets' }).getByRole('row').filter({ has: page.getByRole('cell', { name: 'BTN', exact: true }) });
  await expect(row).toContainText('high (1)');
  const box = (await page.locator('.eb-bench3d canvas').boundingBox())!;
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  for (const cancel of ['pointercancel', 'escape', 'blur']) {
    await page.mouse.down();
    await expect(row).toContainText('low (0)');
    if (cancel === 'escape') await page.keyboard.press('Escape');
    else if (cancel === 'blur') await page.evaluate(() => window.dispatchEvent(new Event('blur')));
    else await page.locator('.eb-bench3d canvas').dispatchEvent('pointercancel');
    await expect(row).toContainText('high (1)');
    await page.mouse.up();
  }
});
