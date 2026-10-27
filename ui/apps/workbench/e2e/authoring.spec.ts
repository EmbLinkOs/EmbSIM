// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test } from '@playwright/test';

test('reshape an existing track with properties, drag preview, cancel and undo', async ({ page }, info) => {
  await page.goto('/?mock=1');
  const track = page.locator('[data-wire="0"]');
  await track.press('Enter');
  const line = track.locator('.es-wire__line');
  const original = await line.getAttribute('points');
  await page.getByLabel('Segment offset', { exact: true }).fill('30');
  await page.getByRole('button', { name: 'Move track segment', exact: true }).click();
  await expect(line).not.toHaveAttribute('points', original!);
  const oldVertices = original!.split(' ');
  const newVertices = (await line.getAttribute('points'))!.split(' ');
  expect(newVertices[0]).toBe(oldVertices[0]);
  expect(newVertices.at(-1)).toBe(oldVertices.at(-1));
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  await expect(line).toHaveAttribute('points', original!);
  const handle = page.locator('[data-wire-index="0"][data-wire-segment="0"]');
  const box = (await handle.boundingBox())!;
  const vertical = (await handle.getAttribute('aria-label'))!.includes('vertically');
  const end = { x: box.x + box.width / 2 + (vertical ? 0 : 30), y: box.y + box.height / 2 + (vertical ? 30 : 0) };
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(end.x, end.y, { steps: 6 });
  await expect(line).not.toHaveAttribute('points', original!);
  await page.keyboard.press('Escape');
  await page.mouse.up();
  await expect(line).toHaveAttribute('points', original!);
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeDisabled();
  await track.press('Enter');
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(end.x, end.y, { steps: 4 });
  await page.getByRole('application', { name: 'Schematic canvas' }).dispatchEvent('pointercancel', { pointerId: 1 });
  await page.mouse.up();
  await expect(line).toHaveAttribute('points', original!);
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeDisabled();
  const box2 = (await handle.boundingBox())!;
  await page.mouse.move(box2.x + box2.width / 2, box2.y + box2.height / 2);
  await page.mouse.down();
  await page.mouse.move(end.x, end.y, { steps: 6 });
  await page.mouse.up();
  await expect(line).not.toHaveAttribute('points', original!);
  await page.screenshot({ path: info.outputPath('track-editing.png') });
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  await expect(line).toHaveAttribute('points', original!);
});

test('context actions, exact placement, shared parameters and one-step undo', async ({ page }, info) => {
  await page.goto('/?mock=1');
  const first = page.getByRole('button', { name: /^R1, / });
  await first.press('Enter');
  const originalX = await page.getByLabel('Position X', { exact: true }).inputValue();
  await page.getByLabel('Position X', { exact: true }).fill(String(Number(originalX) + 100));
  await page.getByRole('button', { name: 'Apply placement', exact: true }).click();
  await expect(page.getByLabel('Position X', { exact: true })).toHaveValue(String(Number(originalX) + 100));
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  await expect(page.getByLabel('Position X', { exact: true })).toHaveValue(originalX);
  await first.press('Shift+F10');
  const menu = page.getByRole('menu', { name: 'Schematic actions', exact: true });
  await expect(menu).toBeVisible();
  await page.keyboard.press('End');
  await expect(page.getByRole('menuitem', { name: 'Delete selection' })).toBeFocused();
  await page.keyboard.press('Escape');
  await expect(menu).toHaveCount(0);
  await first.press('Shift+F10');
  await page.getByRole('menuitem', { name: 'Duplicate', exact: true }).click();
  await expect(page.getByRole('button', { name: /^R4, / })).toHaveAttribute('aria-pressed', 'true');
  // Select the source in addition to the duplicated part, retaining both.
  await first.press('Space');
  await page.getByRole('button', { name: /^R2, / }).press('Space');
  await expect(page.getByText('· mixed', { exact: true })).toBeVisible();
  await expect(page.getByRole('heading', { name: 'Shared parameters', exact: true })).toBeVisible();
  await page.getByRole('checkbox', { name: 'Set resistance', exact: true }).check();
  await page.getByLabel('Shared resistance', { exact: false }).fill('4.7k');
  await page.getByRole('button', { name: 'Apply shared properties', exact: true }).click();
  await expect(page.getByLabel('Shared resistance', { exact: false })).toHaveCount(0);
  await page.screenshot({ path: info.outputPath('shared-properties.png') });
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  await page.getByRole('checkbox', { name: 'Set resistance', exact: true }).check();
  await expect(page.getByLabel('Shared resistance', { exact: false })).not.toHaveValue('4.7k');
  await expect(page.getByText('3 items selected', { exact: true })).toBeVisible();
});

test('check report locates and repairs undrawn nets without changing connectivity', async ({ page }, info) => {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto('/?mock=1');
  await page.locator('[data-wire="0"]').press('Enter');
  const net = (await page.locator('[data-wire="0"]').getAttribute('aria-label'))!.match(/net (\S+)/)![1]!;
  await page.getByRole('button', { name: 'Clear drawing', exact: true }).click();
  await expect(page.locator('[data-wire]').filter({ has: page.locator('.es-wire__line') })).not.toHaveCount(0);
  await page.getByRole('button', { name: /^Checks/ }).click();
  await page.getByLabel('Filter schematic checks', { exact: true }).fill(`undrawn-net`);
  const result = page.locator(`[data-check-id="undrawn-net:${net}"]`);
  await expect(result).toBeVisible();
  await result.click();
  await expect(page.getByRole('img', { name: new RegExp(`${net}: .*undrawn`) })).toBeVisible();
  await page.screenshot({ path: info.outputPath('schematic-checks.png') });
  await page.getByRole('button', { name: 'Route this net', exact: true }).click();
  await expect(result).toHaveCount(0);
  await page.setViewportSize({ width: 1024, height: 700 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  expect(errors).toEqual([]);
});

test('disconnect a terminal deliberately and restore it with one undo', async ({ page }) => {
  await page.goto('/?mock=1');
  const track = page.locator('[data-wire="0"]');
  await track.press('Enter');
  const button = page.getByRole('button', { name: /^Disconnect / }).first();
  const pin = (await button.getAttribute('aria-label'))!.replace('Disconnect ', '');
  const terminal = page.locator(`[data-pin="${pin}"]`);
  const before = await terminal.getAttribute('aria-label');
  await button.click();
  await expect(terminal).toHaveAttribute('aria-label', new RegExp('unconnected$'));
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  // Connectivity is stable; asynchronously arriving live states may add a suffix.
  await expect.poll(async () => (await terminal.getAttribute('aria-label'))?.split(',').slice(0, 2).join(',')).toBe(before!.split(',').slice(0, 2).join(','));
});
