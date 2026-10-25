// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The air panel against the demo session: with nothing on the bench that
// listens to the air it says so and offers the receivers; one added from it
// is a project edit like any other, listed at once and undone.

import { expect, test } from '@playwright/test';

test('the air: add a Wi-Fi analyzer from the panel, undo', async ({ page }) => {
  await page.goto('/?mock=1');
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).toBeVisible();
  await page.getByRole('radio', { name: 'Run', exact: true }).click();
  await page.getByRole('tab', { name: 'Air' }).click();
  await expect(page.getByText('Nothing on the bench listens to the air')).toBeVisible();
  await page.getByRole('button', { name: 'Wi-Fi analyzer' }).click();
  const air = page.getByLabel('What the receivers hear');
  await expect(air).toContainText('Wi-Fi, as PHONE1 sees it');
  await expect(air).toContainText('No network found yet');
  await expect(air.getByRole('button', { name: 'Wi-Fi analyzer' })).toHaveCount(0);
  await page.keyboard.press('Control+z');
  await expect(page.getByText('Nothing on the bench listens to the air')).toBeVisible();
});
