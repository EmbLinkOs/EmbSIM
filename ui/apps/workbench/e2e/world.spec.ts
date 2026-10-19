// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The world (ADR-026) against the demo session: the room's temperature
// declared and a box put on the bench from the World panel, each a project
// edit applied live and undone like any other.

import { expect, test } from '@playwright/test';

test('the world: declare the room, add a body, undo', async ({ page }) => {
  await page.goto('/?mock=1');
  await expect(page.getByRole('application', { name: 'Schematic canvas' })).toBeVisible();
  await page.getByRole('tab', { name: 'World' }).click();
  const world = page.getByLabel('The world');
  await expect(world).toBeVisible();

  const temperature = world.getByRole('checkbox', { name: 'Temperature' });
  await expect(temperature).not.toBeChecked();
  await expect(world.getByRole('slider', { name: 'Temperature, degC' })).toBeDisabled();
  await temperature.click(); // checked once the edit comes back from the session
  await expect(temperature).toBeChecked();
  await expect(world.getByRole('slider', { name: 'Temperature, degC' })).toBeEnabled();
  await expect(world).toContainText('22.0 °C');

  await world.getByRole('button', { name: 'Box' }).click();
  await expect(world).toContainText('Box 1');
  await expect(world.getByRole('button', { name: 'Remove Box 1' })).toBeVisible();

  // two undos pressed at once: the second waits for the first, not lost
  await page.keyboard.press('Control+z');
  await page.keyboard.press('Control+z');
  await expect(world).not.toContainText('Box 1');
  await expect(temperature).not.toBeChecked();
});
