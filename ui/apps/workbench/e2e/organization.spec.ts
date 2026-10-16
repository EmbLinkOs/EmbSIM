// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test } from '@playwright/test';

test('organize a circuit, lock placement, export inventory and undo', async ({ page }, info) => {
  const errors: string[] = []; page.on('pageerror', (e) => errors.push(e.message));
  await page.goto('/?mock=1');
  await page.getByRole('tab', { name: 'Objects', exact: true }).click();
  const objects = page.getByRole('region', { name: 'Schematic objects' });
  await objects.getByRole('button', { name: 'R1', exact: true }).click();
  await objects.getByRole('button', { name: 'R2', exact: true }).click({ modifiers: ['Shift'] });
  await page.getByLabel('Group name', { exact: true }).fill('Input filter');
  await page.getByRole('button', { name: 'Group selection', exact: true }).click();
  const header = page.getByRole('button', { name: 'Select group Input filter', exact: true });
  await expect(header).toHaveCount(1);
  await expect(objects.getByRole('button', { name: 'Input filter · 2 parts', exact: true })).toBeVisible();
  await objects.getByRole('button', { name: 'Input filter · 2 parts', exact: true }).click();
  const beforeMove = await page.locator('[data-component="R1"] .es-component__hit').getAttribute('x');
  // Wait for an unobscured, stable target before capturing raw pointer coordinates.
  await header.click({ trial: true });
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).not.toHaveClass(/es-scene__svg--moving/);
  const box = (await header.boundingBox())!;
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down(); await page.mouse.move(box.x + box.width / 2 + 40, box.y + box.height / 2 + 40, { steps: 4 }); await page.mouse.up();
  await expect(page.locator('[data-component="R1"] .es-component__hit')).not.toHaveAttribute('x', beforeMove!);
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  await expect(page.locator('[data-component="R1"] .es-component__hit')).toHaveAttribute('x', beforeMove!);
  await objects.getByRole('button', { name: 'Lock group', exact: true }).click();
  await expect(objects.getByRole('button', { name: 'Unlock R1', exact: true })).toBeVisible();
  await expect(objects.getByRole('button', { name: 'Unlock R2', exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  await expect(objects.getByRole('button', { name: 'Lock R1', exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Redo', exact: true }).click();
  await expect(objects.getByRole('button', { name: 'Unlock R1', exact: true })).toBeVisible();
  await page.getByLabel('Search objects', { exact: true }).fill('Input filter');
  await expect(objects.locator('tbody tr')).toHaveCount(2);
  await page.getByLabel('Search objects', { exact: true }).fill('no-such-part');
  await expect(objects.locator('tbody tr')).toHaveCount(0);
  await page.getByLabel('Search objects', { exact: true }).fill('');
  const download = page.waitForEvent('download');
  await objects.getByRole('button', { name: 'Export parts list', exact: true }).click();
  expect((await download).suggestedFilename()).toBe('parts-list.csv');
  await page.screenshot({ path: info.outputPath('circuit-organization.png') });
  await page.getByLabel('Group name', { exact: true }).fill('Analog input');
  await objects.getByRole('button', { name: 'Rename', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Select group Analog input', exact: true })).toHaveCount(1);
  await objects.getByRole('button', { name: 'Ungroup', exact: true }).click();
  await expect(page.locator('[data-group]')).toHaveCount(0);
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Select group Analog input', exact: true })).toHaveCount(1);
  expect(errors).toEqual([]);
});
