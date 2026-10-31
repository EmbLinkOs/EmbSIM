// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The bench (ADR-027) against the demo session: a breadboard put on the
// bench, a resistor plugged into it and a jumper added from the keyboard
// list, each an edit of the same nets the schematic shows, undone as the
// schematic's edits are.

import { expect, test } from '@playwright/test';

test('build on the bench: plug a part in, add a jumper, undo', async ({ page }) => {
  await page.goto('/?mock=1');
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).toBeVisible();

  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  const list = page.getByRole('complementary', { name: 'On the bench' });
  await expect(list).toBeVisible();

  await page.getByRole('button', { name: 'Breadboard', exact: true }).click();
  await expect(list.getByRole('button', { name: /^BB1/ })).toBeVisible({ timeout: 15_000 });

  // R2 into a10: its legs, 0.4 in apart, land in a10 and a14
  await list.getByRole('button', { name: /^R2/ }).click();
  await list.getByLabel("Plug R2's first leg into").fill('a10');
  await list.getByRole('button', { name: 'Plug in' }).click();
  await expect(list.getByRole('button', { name: /^R2/ })).toContainText('in a10 a14');
  const nets = page.getByRole('table').first();
  await expect(nets).toContainText('BB1.a10');
  await expect(nets).toContainText('BB1.a14');

  // a jumper from the strip under R2's second leg to Q1's base joins their nets
  await list.getByLabel('Jumper from').fill('BB1.e14');
  await list.getByLabel('to', { exact: true }).fill('Q1.B');
  await list.getByRole('button', { name: 'Add jumper' }).click();
  await expect(nets).toContainText('BB1.e14');

  await page.keyboard.press('Control+z');
  await expect(nets).not.toContainText('BB1.e14');
  await expect(nets).toContainText('BB1.a10');

  // the schematic hides the breadboard and keeps the parts
  await page.getByRole('radio', { name: 'Schematic', exact: true }).click();
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).toBeVisible();
});

test('the design workspace: the inspector is a rail until something is selected', async ({ page }) => {
  await page.goto('/?mock=1');
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).toBeVisible();
  // nothing selected: the Inspector is a rail, the canvas has its width
  await expect(page.getByText('Nothing selected')).toHaveCount(0);

  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  const list = page.getByRole('complementary', { name: 'On the bench' });
  await expect(list).toBeVisible();
  const toggle = page.getByRole('button', { name: 'Parts & wiring' });
  await toggle.click();
  await expect(list).toHaveCount(0);
  await toggle.click();
  await expect(list).toBeVisible();

  // a part selected opens the Inspector on it
  await list.getByRole('button', { name: /^R2/ }).click();
  await expect(page.getByRole('region', { name: 'Inspector' })).toContainText('R2');
});
