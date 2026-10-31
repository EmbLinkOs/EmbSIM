// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// An instrument's front panel opened from its part (bench-instruments.md §3):
// the inspector's Open front panel takes Run's dock to it; Float puts it in a
// window of its own that stays over the other perspectives, moves from the
// keyboard, and docks back.

import { expect, test } from '@playwright/test';

test('a front panel opens from its part, floats over the perspectives and docks back', async ({ page }, info) => {
  await page.goto('/?mock=1');
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).toBeVisible();

  // place a DS1054Z from the palette; it comes in selected
  await page.keyboard.press('Control+k');
  await page.getByRole('combobox').fill('Place Oscilloscope, 4 channels');
  await page.keyboard.press('Enter');
  const open = page.getByRole('button', { name: 'Open front panel' });
  await expect(open).toBeVisible();

  await open.click();
  await expect(page.getByRole('main', { name: 'run perspective' })).toBeVisible();
  const docked = page.getByRole('region', { name: /^U\d+ oscilloscope$/ });
  await expect(docked).toBeVisible();
  await expect(docked.getByRole('group', { name: / front panel$/ })).toBeVisible();

  // float it: a window over the workbench, the dock without it
  await page.getByRole('button', { name: /^Float .* DS1054Z oscilloscope in its own window$/ }).click();
  const win = page.getByRole('dialog', { name: /DS1054Z oscilloscope$/ });
  await expect(win).toBeVisible();
  await expect(win.getByRole('region', { name: /^U\d+ oscilloscope$/ })).toBeVisible();
  await expect(page.getByRole('region', { name: /^U\d+ oscilloscope$/ })).toHaveCount(1);

  // it stays over the Design perspective, and its title bar moves it from the keyboard
  await page.getByRole('radio', { name: 'Design', exact: true }).click();
  await expect(page.getByRole('main', { name: 'design perspective' })).toBeVisible();
  await expect(win).toBeVisible();
  const bar = win.getByLabel(/arrow keys move the window/);
  const before = (await win.boundingBox())!;
  await bar.focus();
  await page.keyboard.press('ArrowRight');
  await page.keyboard.press('ArrowDown');
  const after = (await win.boundingBox())!;
  expect(after.x - before.x).toBeCloseTo(16, 0);
  expect(after.y - before.y).toBeCloseTo(16, 0);

  await page.screenshot({ path: info.outputPath('floating-front-panel.png') });

  // Open front panel on a floating part brings its window forward, without leaving Design
  await open.click();
  await expect(page.getByRole('main', { name: 'design perspective' })).toBeVisible();

  // dock it back into Run
  await win.getByRole('button', { name: /^Dock .* in Run$/ }).click();
  await expect(win).toHaveCount(0);
  await open.click();
  await expect(page.getByRole('region', { name: /^U\d+ oscilloscope$/ })).toBeVisible();
});

test('the schematic part\'s context menu opens its front panel', async ({ page }) => {
  await page.goto('/?mock=1');
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).toBeVisible();
  await page.keyboard.press('Control+k');
  await page.getByRole('combobox').fill('Place Oscilloscope, 4 channels');
  await page.keyboard.press('Enter');
  await expect(page.getByRole('button', { name: 'Open front panel' })).toBeVisible();

  await page.getByRole('button', { name: /^U\d+, / }).last().click({ button: 'right', force: true });
  const menu = page.getByRole('menu', { name: 'Schematic actions' });
  await expect(menu).toBeVisible();
  await menu.getByRole('menuitem', { name: 'Open front panel' }).click();
  await expect(page.getByRole('main', { name: 'run perspective' })).toBeVisible();
  await expect(page.getByRole('region', { name: /^U\d+ oscilloscope$/ })).toBeVisible();
});
