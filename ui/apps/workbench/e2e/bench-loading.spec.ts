// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test } from '@playwright/test';

const rendererAsset = '**/assets/Bench3D-*.js';

test('the schematic opens without 3D assets and the bench stays selectable while they load', async ({ page }) => {
  let requests = 0;
  let release!: () => void;
  const pending = new Promise<void>((resolve) => { release = resolve; });
  await page.route(rendererAsset, async (route) => {
    requests++;
    await pending;
    await route.continue();
  });
  await page.goto('/?mock=1');
  await expect(page.getByRole('button', { name: 'Auto layout', exact: true })).toBeVisible();
  expect(requests).toBe(0);
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  await expect(page.getByRole('status').filter({ hasText: 'Opening 3D bench…' })).toBeVisible();
  await expect(page.getByRole('toolbar', { name: 'Schematic tools', exact: true })).toHaveCount(0);
  await expect.poll(() => requests).toBe(1);
  const list = page.getByRole('complementary', { name: 'On the bench' });
  await list.getByRole('button', { name: /^R2/ }).click();
  const downloaded = page.waitForResponse((response) => /\/assets\/Bench3D-.*\.js$/.test(response.url()));
  await page.getByRole('radio', { name: '2D', exact: true }).click();
  release();
  await (await downloaded).finished();
  await expect(page.getByRole('radio', { name: '2D', exact: true })).toHaveAttribute('aria-checked', 'true');
  await expect(page.locator('.eb-bench3d')).toHaveCount(0);
  await page.getByRole('radio', { name: '3D', exact: true }).click();
  // This includes a cold software-WebGL initialization after the deferred fetch.
  await expect(page.locator('.eb-bench3d canvas')).toBeVisible({ timeout: 15_000 });
  await expect(list.getByRole('button', { name: /^R2/ })).toHaveAttribute('aria-pressed', 'true');
  await page.getByRole('radio', { name: 'Schematic', exact: true }).click();
  await expect(page.getByRole('toolbar', { name: 'Schematic tools', exact: true })).toBeVisible();
});

test('a failed 3D download preserves 2D and keyboard access and recovers after reload', async ({ page }) => {
  await page.route(rendererAsset, (route) => route.abort('failed'));
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  await expect(page.getByText('The 3D bench could not be loaded.', { exact: false })).toBeVisible();
  await expect(page.getByRole('radio', { name: '2D', exact: true })).toHaveAttribute('aria-checked', 'true');
  await expect(page.getByRole('radio', { name: '3D', exact: true })).toBeDisabled();
  const list = page.getByRole('complementary', { name: 'On the bench' });
  await list.getByRole('button', { name: /^R2/ }).click();
  await expect(list.getByLabel('Bench X (mm)')).toBeVisible();
  await page.unroute(rendererAsset);
  await page.getByRole('button', { name: 'Reload to retry 3D', exact: true }).click();
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  // This includes a cold software-WebGL initialization after the deferred fetch.
  await expect(page.locator('.eb-bench3d canvas')).toBeVisible({ timeout: 15_000 });
});
