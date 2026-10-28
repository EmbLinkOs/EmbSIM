// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test, type Locator, type Page } from '@playwright/test';

async function insideWindow(page: Page, locator: Locator) {
  await expect(locator).toBeVisible();
  // A perspective's first open starts its 3D bench; on software GPUs that can
  // hold the page for several seconds, which is startup latency, not layout.
  await expect.poll(async () => {
    const box = await locator.boundingBox();
    return Boolean(box && box.x >= -1 && box.y >= -1 && box.x + box.width <= page.viewportSize()!.width + 1 && box.y + box.height <= page.viewportSize()!.height + 1);
  }, { timeout: 15_000 }).toBe(true);
}

test('global navigation and simulation controls stay reachable with a long project name', async ({ page }) => {
  await page.setViewportSize({ width: 900, height: 760 });
  await page.goto('/?mock=1');
  await page.getByRole('button', { name: /^Project menu/ }).click();
  await page.getByRole('menuitem', { name: /^New project/ }).click();
  const dialog = page.getByRole('dialog', { name: 'New project', exact: true });
  await dialog.getByLabel('Project name', { exact: true }).fill('industrial-controller-with-a-long-project-name-and-many-revisions');
  await dialog.getByRole('button', { name: 'Create', exact: true }).click();
  await expect(dialog).toHaveCount(0);
  for (const width of [900, 768, 1440]) {
    await page.setViewportSize({ width, height: 760 });
    for (const name of ['Design', 'Run', 'Debug', 'Verify']) {
      const target = page.getByRole('radio', { name, exact: true });
      await insideWindow(page, target);
      await target.click();
      await expect(page.getByRole('main', { name: `${name.toLowerCase()} perspective`, exact: true })).toBeVisible();
    }
    await insideWindow(page, page.getByRole('button', { name: 'Run', exact: true }));
    await insideWindow(page, page.getByRole('combobox', { name: 'Fidelity policy', exact: true }));
    await insideWindow(page, page.getByRole('button', { name: 'Open command palette', exact: true }));
    await insideWindow(page, page.getByRole('button', { name: /^Switch to .* theme$/ }));
    await expect.poll(() => page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(width);
  }
  const status = page.getByRole('contentinfo', { name: 'Status', exact: true });
  await page.setViewportSize({ width: 768, height: 760 });
  await status.focus();
  for (let step = 0; step < 8; step++) await page.keyboard.press('ArrowRight');
  await expect.poll(() => status.evaluate((node) => node.scrollLeft)).toBeGreaterThan(0);
  await page.getByRole('button', { name: 'Open command palette', exact: true }).click();
  await expect(page.getByRole('dialog', { name: /command/i })).toBeVisible();
});

test('open side panels fit a compact window, restore their proportions and resize from their visible edge', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('/?mock=1');
  await page.getByRole('button', { name: /^R1, / }).press('Enter');
  await page.getByRole('button', { name: 'Collapse Inspector panel', exact: true }).click();
  await page.getByRole('button', { name: 'Expand Inspector panel', exact: true }).click();
  const library = page.getByRole('region', { name: 'Library', exact: true });
  const inspector = page.getByRole('region', { name: 'Inspector', exact: true });
  const canvas = page.getByRole('region', { name: 'Schematic', exact: true });
  const key = 'embsim.layout.design.button-transistor-led.panels';
  const weights = () => page.evaluate((storageKey) => JSON.parse(localStorage.getItem(storageKey)!).weights, key);
  const original = await weights();
  const wide = (await library.boundingBox())!.width;
  for (const width of [900, 768]) {
    await page.setViewportSize({ width, height: 600 });
    for (const panel of [library, inspector, canvas]) await insideWindow(page, panel);
    await expect(inspector.getByLabel('Position X', { exact: true })).toBeVisible();
    await page.getByRole('radio', { name: 'Bench', exact: true }).click();
    await expect(page.getByRole('toolbar', { name: 'Bench tools', exact: true })).toBeVisible();
    await page.getByRole('radio', { name: 'Schematic', exact: true }).click();
    await page.getByRole('radio', { name: 'Wire', exact: true }).click();
    await page.getByRole('radio', { name: 'Select', exact: true }).click();
    expect(await weights()).toEqual(original);
  }
  await page.setViewportSize({ width: 1440, height: 900 });
  await expect.poll(async () => Math.abs((await library.boundingBox())!.width - wide)).toBeLessThan(2);
  await page.setViewportSize({ width: 1024, height: 760 });
  await insideWindow(page, inspector);
  const before = (await library.boundingBox())!.width;
  const inspectorWidth = (await inspector.boundingBox())!.width;
  await page.getByRole('separator', { name: 'Resize Library', exact: true }).press('ArrowRight');
  await expect.poll(async () => Math.abs((await library.boundingBox())!.width - before - 16)).toBeLessThan(2);
  expect(Math.abs((await inspector.boundingBox())!.width - inspectorWidth)).toBeLessThan(2);
  await page.reload();
  await expect(page.getByRole('button', { name: 'Collapse Inspector panel', exact: true })).toBeVisible();
  await insideWindow(page, inspector);
});
