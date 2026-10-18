// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Smoke test (SPEC-011 §6.7, against the demo session): open project, run,
// press the button, observe, export. Run with `pnpm e2e` after `pnpm build`.

import { expect, test } from '@playwright/test';

test('open, run, press, observe, export', async ({ page }) => {
  await page.goto('/?mock=1');
  await expect(page.getByText('Demo session').first()).toBeVisible();
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).toBeVisible();

  await page.getByRole('radio', { name: 'Run', exact: true }).click();
  await expect(page.getByRole('img', { name: /Oscilloscope/ })).toBeVisible();

  await page.getByRole('button', { name: 'Run', exact: true }).click();
  await expect(page.getByTestId('clock')).toContainText('run');

  // the bench's flat view: the parts' artwork with its hotspots
  await page.getByRole('radio', { name: '2D', exact: true }).click();
  const hold = page.getByRole('button', { name: /SW1 Push button Button cap/ });
  await hold.dispatchEvent('pointerdown', { button: 0 });
  await page.waitForTimeout(300);
  await hold.dispatchEvent('pointerup');
  await page.waitForTimeout(300);

  await page.getByRole('button', { name: 'Pause' }).click();
  await expect(page.getByTestId('clock')).toContainText('paused');

  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'CSV' }).first().click();
  const file = await download;
  expect(file.suggestedFilename()).toMatch(/\.csv$/);
});

test('keyboard: place and wire from the library, light theme contrast', async ({ page }) => {
  await page.goto('/?mock=1');
  await page.getByRole('button', { name: 'Place Capacitor' }).click();
  await expect(page.getByRole('button', { name: /^C1, Capacitor/ })).toBeVisible();
  await page.getByRole('button', { name: 'Pin C1.1, unconnected' }).focus();
  await page.keyboard.press('Enter');
  await page.getByRole('button', { name: /^Pin Q1.C/ }).focus();
  await page.keyboard.press('Enter');
  await expect(page.getByRole('button', { name: /Pin C1.1, net COLL/ })).toBeVisible();

  // the theme follows the system first; the toggle switches to the other
  const before = await page.locator('html').getAttribute('data-theme');
  const other = before === 'dark' ? 'light' : 'dark';
  await page.getByRole('button', { name: `Switch to ${other} theme` }).click();
  await expect(page.locator('html')).toHaveAttribute('data-theme', other);
});

test('schematic studio: worker preview, apply, undo and focus mode', async ({ page }) => {
  await page.goto('/?mock=1');
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).toBeVisible();
  await page.getByRole('button', { name: 'Auto layout', exact: true }).click();
  await page.getByRole('button', { name: 'Preview layout', exact: true }).click();
  await expect(page.getByText('Preview · not applied', { exact: true })).toBeVisible();
  await expect(page.getByText('Layout preview', { exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Apply layout', exact: true }).click();
  await expect(page.getByText('Layout applied', { exact: true })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeEnabled();
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Redo', exact: true })).toBeEnabled();
  await page.getByRole('button', { name: 'View', exact: true }).click();
  await page.getByRole('checkbox', { name: 'Focus canvas (hide side panels)', exact: true }).check();
  await expect(page.getByRole('region', { name: 'Library', exact: true })).toHaveCount(0);
  await page.getByRole('checkbox', { name: 'Focus canvas (hide side panels)', exact: true }).uncheck();
  await expect(page.getByRole('region', { name: 'Library', exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Close tools', exact: true }).click();
  await page.getByRole('button', { name: 'Find', exact: true }).click();
  await page.getByLabel('Component or net', { exact: true }).fill('R1');
  await page.getByLabel('Component or net', { exact: true }).press('Enter');
  await expect(page.getByRole('button', { name: 'Focus', exact: true })).toBeEnabled();
});

test('canvas appearance persists and covers the panned workspace', async ({ page }) => {
  await page.goto('/?mock=1');
  const canvas = page.getByRole('application', { name: 'Schematic canvas' });
  await expect(canvas).toBeVisible();
  await page.getByRole('button', { name: 'View', exact: true }).click();
  await page.getByRole('radio', { name: 'Dots', exact: true }).press('Space');
  await page.getByRole('radio', { name: 'Warm', exact: true }).press('Space');
  await page.getByLabel('Grid spacing', { exact: true }).selectOption('40');
  await page.reload();
  await expect(canvas).toBeVisible();
  await page.getByRole('button', { name: 'View', exact: true }).click();
  await expect(page.getByRole('radio', { name: 'Dots', exact: true })).toBeChecked();
  await expect(page.getByRole('radio', { name: 'Warm', exact: true })).toBeChecked();
  await expect(page.getByLabel('Grid spacing', { exact: true })).toHaveValue('40');
  await page.getByRole('button', { name: 'Close tools', exact: true }).click();
  const before = await page.locator('.es-scene__background').evaluate((el) => (el as HTMLElement).style.backgroundPosition);
  await canvas.focus();
  for (let i = 0; i < 30; i++) await canvas.press('ArrowLeft');
  const after = await page.locator('.es-scene__background').evaluate((el) => (el as HTMLElement).style.backgroundPosition);
  expect(after).not.toBe(before);
  expect(await page.locator('.es-scene__background').evaluate((el) => {
    const background = el.getBoundingClientRect();
    const viewport = el.parentElement!.getBoundingClientRect();
    return background.width === viewport.width && background.height === viewport.height && background.left === viewport.left && background.top === viewport.top;
  })).toBe(true);
});

for (const autoArrange of [false, true]) {
  test(`manual wire: bends, elbow flip, backtrack, commit and undo (auto-arrange ${autoArrange})`, async ({ page }) => {
    await page.goto('/?mock=1');
    await page.getByRole('button', { name: 'View', exact: true }).click();
    await page.getByRole('checkbox', { name: 'Auto arrange after connecting', exact: true }).setChecked(autoArrange);
    await page.getByRole('button', { name: 'View', exact: true }).click();
    await page.getByRole('button', { name: 'Place Capacitor' }).click();
    const canvas = page.getByRole('application', { name: 'Schematic canvas' });
    const pin = page.getByRole('button', { name: 'Pin C1.1, unconnected' });
    await pin.focus();
    await page.keyboard.press('Enter');
    const box = (await canvas.boundingBox())!;
    // Empty sheet below the demo circuit. Each click commits a leg.
    await page.mouse.click(box.x + 100, box.y + box.height - 70);
    const preview = page.locator('.es-scene__wire-preview');
    await expect(page.locator('.es-scene__wire-waypoint')).toHaveCount(1);
    const first = await preview.getAttribute('points');
    await page.mouse.move(box.x + 200, box.y + box.height - 130);
    const beforeFlip = await preview.getAttribute('points');
    await page.keyboard.press('Space');
    await expect(preview).not.toHaveAttribute('points', beforeFlip!);
    await page.mouse.click(box.x + 200, box.y + box.height - 130);
    await expect(page.locator('.es-scene__wire-waypoint')).toHaveCount(2);
    await page.keyboard.press('Backspace');
    await expect(page.locator('.es-scene__wire-waypoint')).toHaveCount(1);
    // The committed first leg is retained after removing the second.
    await expect(preview).toHaveAttribute('points', new RegExp(`^${first!.split(' ').slice(0, -1).join(' ')}`));
    const count = await page.locator('[data-wire]').count();
    await page.getByRole('button', { name: /^Pin Q1.C/ }).focus();
    await page.keyboard.press('Enter');
    await expect(preview).toHaveCount(0);
    await expect(page.locator('[data-wire]')).toHaveCount(count + 1);
    const authored = page.locator('[data-wire]').last().locator('.es-wire__line');
    expect((await authored.getAttribute('points'))!.split(' ').length).toBeGreaterThan(3);
    await page.getByRole('button', { name: 'Undo', exact: true }).click();
    await expect(page.getByRole('button', { name: 'Pin C1.1, unconnected' })).toBeVisible();
    await expect(page.locator('[data-wire]')).toHaveCount(count);
    await pin.focus();
    await page.keyboard.press('Enter');
    await page.keyboard.press('Shift+ArrowDown');
    await page.keyboard.press('Shift+ArrowLeft');
    await page.keyboard.press('Enter');
    await expect(page.locator('.es-scene__wire-waypoint')).toHaveCount(1);
    await page.keyboard.press('Escape');
    await expect(preview).toHaveCount(0);
    await expect(page.locator('[data-wire]')).toHaveCount(count);
  });

}
