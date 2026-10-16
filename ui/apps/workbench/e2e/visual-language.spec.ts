// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test } from '@playwright/test';

test('colored selectable tracks, terminal sockets, semantic cursors and readable labels', async ({ page }, info) => {
  await page.goto('/?mock=1');
  const wire = page.locator('[data-wire="0"]');
  const line = wire.locator('.es-wire__line');
  const initial = await line.evaluate((el) => getComputedStyle(el).stroke);
  const symbol = await page.locator('[data-component="R1"] .es-symbol path, [data-component="R1"] .es-symbol line, [data-component="R1"] .es-symbol rect').first().evaluate((el) => getComputedStyle(el).stroke);
  expect(initial).not.toBe(symbol);
  await wire.press('Enter');
  await expect.poll(() => line.evaluate((el) => getComputedStyle(el).stroke)).not.toBe(initial);
  await expect(wire.locator('.es-wire__selection')).toHaveCount(1);
  const selectedStroke = await line.evaluate((el) => {
    const marker = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
    marker.style.stroke = 'var(--wire-selected)'; el.parentElement!.appendChild(marker);
    const color = getComputedStyle(marker).stroke; marker.remove(); return color;
  });
  await expect(line).toHaveCSS('stroke', selectedStroke);
  await page.getByRole('button', { name: 'Run', exact: true }).click();
  await expect(page.getByTestId('clock')).toContainText('run');
  await expect.poll(() => line.evaluate((el) => getComputedStyle(el).stroke)).toBe(selectedStroke);
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  const pin = page.locator('[data-pin="R1.1"]');
  await expect(pin.locator('.es-pin__dot')).toHaveAttribute('r', '3.2');
  expect(await pin.evaluate((el) => getComputedStyle(el).cursor)).toContain('data:image/svg+xml');
  const fill = await pin.locator('.es-pin__dot').evaluate((el) => getComputedStyle(el).fill);
  const ring = await pin.locator('.es-pin__dot').evaluate((el) => getComputedStyle(el).stroke);
  expect(fill).not.toBe(ring);
  await pin.click();
  await expect(page.locator('.es-scene__wire-preview')).toHaveCount(1);
  await page.keyboard.press('Escape');
  await expect(page.locator('.es-scene__wire-preview')).toHaveCount(0);
  // Navigation must not hide terminals or annotations, even before the viewport settles.
  await page.getByRole('application', { name: 'Schematic canvas' }).evaluate((el) => el.classList.add('es-scene__svg--moving'));
  await expect(pin).toBeVisible();
  await expect(page.locator('[data-component-label="R1"] .es-component__value')).toBeVisible();
  await page.getByRole('application', { name: 'Schematic canvas' }).evaluate((el) => el.classList.remove('es-scene__svg--moving'));
  expect(await page.locator('[data-component="R1"]').evaluate((el) => getComputedStyle(el).cursor)).toBe('grab');
  const label = page.locator('[data-component-label="R1"]');
  await expect(label.locator('.es-component__ref')).toHaveText('R1');
  await expect(label.locator('.es-component__name')).toHaveText('Resistor');
  await expect(label.locator('.es-component__value')).toContainText('Ω');
  await page.getByRole('button', { name: 'View', exact: true }).click();
  await page.getByLabel('Wire color', { exact: true }).selectOption('blue');
  await page.getByLabel('Component labels', { exact: true }).selectOption('compact');
  await page.getByRole('button', { name: 'Close tools', exact: true }).click();
  await wire.press('Escape');
  await expect(label.locator('.es-component__name')).toHaveCount(0);
  await expect.poll(() => line.evaluate((el) => getComputedStyle(el).stroke)).not.toBe(initial);
  await page.screenshot({ path: info.outputPath('schematic-visual-language.png') });
  await page.reload();
  await expect(page.locator('.es-scene--wire-blue')).toHaveCount(1);
  await expect(page.locator('[data-component-label="R1"] .es-component__name')).toHaveCount(0);
  await page.getByRole('button', { name: 'Place Resistor', exact: true }).click();
  await expect(page.locator('[data-component-label="R4"] .es-component__value')).toHaveText('1 kΩ');
});
