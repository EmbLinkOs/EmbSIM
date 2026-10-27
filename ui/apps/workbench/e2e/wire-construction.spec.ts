// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test } from '@playwright/test';

test('wire drafts preserve connectivity, trace across views and undo as one edit', async ({ page }) => {
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  const list = page.getByRole('complementary', { name: 'On the bench' });
  await list.getByLabel('Jumper from').fill('R2.1');
  await list.getByLabel('to', { exact: true }).fill('Q1.B');
  await list.getByRole('button', { name: 'Add jumper' }).click();
  const wire = list.getByRole('button', { name: 'R2.1 → Q1.B', exact: true });
  await wire.click();
  await expect(list.getByRole('region', { name: 'Traced net' })).toContainText('R2.1');
  const nets = page.getByRole('table', { name: 'Nets' });
  const before = (await nets.textContent())!;
  const editor = list.getByRole('region', { name: 'Selected jumper' });
  await editor.getByLabel('Wire colour').fill('#16a085');
  await editor.getByRole('button', { name: 'Add route point' }).click();
  await editor.getByLabel('Point 1 Height').fill('23');
  await editor.getByRole('button', { name: 'Apply appearance' }).click();
  await expect(editor.getByLabel('Point 1 Height')).toHaveValue('23');
  await expect(nets).toHaveText(before);
  await page.getByRole('radio', { name: 'Schematic', exact: true }).click();
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).toBeVisible();
  await expect(nets).toHaveText(before);
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  await wire.click();
  // A later undo removes the entire appearance change, never the connection.
  await page.locator('.eb-bench3d').focus();
  await page.keyboard.press('Control+z');
  await expect(editor.getByLabel('Point 1 Height')).toHaveCount(0);
  await expect(wire).toBeVisible();
  await expect(nets).toHaveText(before);
  await editor.getByLabel('Reconnect to').fill('missing.pin');
  await editor.getByRole('button', { name: 'Reconnect jumper' }).click();
  await expect(page.getByText('Unknown bench terminal missing.pin', { exact: true })).toBeVisible();
  await expect(wire).toBeVisible();
  await expect(nets).toHaveText(before);
  await editor.getByLabel('Reconnect to').fill('Q1.C');
  await editor.getByRole('button', { name: 'Reconnect jumper' }).click();
  await expect(list.getByRole('button', { name: 'R2.1 → Q1.C', exact: true })).toBeVisible();
  await expect(nets).not.toHaveText(before);
  await page.locator('.eb-bench3d').focus();
  await page.keyboard.press('Control+z');
  await expect(wire).toBeVisible();
  await expect(nets).toHaveText(before);
});

test('route handles cancel without edits and commit once with exact undo', async ({ page }) => {
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  await expect(page.locator('.eb-bench3d canvas')).toBeVisible({ timeout: 15_000 });
  const list = page.getByRole('complementary', { name: 'On the bench' });
  await list.getByRole('button', { name: /^R2 / }).click();
  const menu = page.locator('.wb-bench-view-menu summary');
  await menu.click();
  await page.getByRole('button', { name: 'Top', exact: true }).click();
  await menu.click();
  await page.getByRole('button', { name: 'Focus part', exact: true }).click();
  await list.getByLabel('Jumper from').fill('R2.1');
  await list.getByLabel('to', { exact: true }).fill('R2.2');
  await list.getByRole('button', { name: 'Add jumper' }).click();
  await list.getByRole('button', { name: 'R2.1 → R2.2', exact: true }).click();
  const editor = list.getByRole('region', { name: 'Selected jumper' });
  const handle = page.getByLabel('Jumper 1 route point 1', { exact: true });
  // Camera projection creates route handles asynchronously on slower GPUs.
  await expect(handle).toBeVisible();
  const original = (await handle.boundingBox())!;
  for (const cancellation of ['Escape', 'pointercancel']) {
    const box = (await handle.boundingBox())!;
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2 + 25, box.y + box.height / 2 + 15);
    await expect(page.locator('.eb-connection-feedback')).toContainText('Route point 1');
    if (cancellation === 'Escape') await page.keyboard.press('Escape');
    else await page.locator('.eb-bench3d canvas').dispatchEvent('pointercancel');
    await page.mouse.up();
    await expect(editor.getByLabel('Point 1 X', { exact: true })).toHaveCount(0);
    // Cancellation restores the visible target, not only the saved project.
    await expect.poll(async () => {
      const restored = await handle.boundingBox();
      return restored ? Math.hypot(restored.x - original.x, restored.y - original.y) : Infinity;
    }).toBeLessThan(1);
  }
  const box = (await handle.boundingBox())!;
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2 + 25, box.y + box.height / 2 + 15);
  await expect(page.locator('.eb-connection-feedback')).toContainText('Route point 1');
  await page.mouse.up();
  await expect(editor.getByLabel('Point 1 X', { exact: true })).toBeVisible();
  await page.locator('.eb-bench3d').focus();
  await page.keyboard.press('Control+z');
  await expect(editor.getByLabel('Point 1 X', { exact: true })).toHaveCount(0);
});
