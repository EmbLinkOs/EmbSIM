// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The Settings page: each setting acts at once and survives a reload, and the
// main workflows read the same in French as in English.
import { expect, test, type Page } from '@playwright/test';

async function pick(page: Page, combobox: string, option: string) {
  await page.getByRole('combobox', { name: combobox, exact: true }).click();
  await page.getByRole('option', { name: option, exact: true }).click();
}

test('settings apply at once and are kept between sessions', async ({ page }) => {
  await page.goto('/?mock=1');
  await page.getByRole('button', { name: 'Settings', exact: true }).click();
  const settings = page.getByTestId('settings-page');
  await expect(settings).toBeVisible();

  // appearance: text size and density act on the whole workbench
  await settings.getByRole('radio', { name: 'Large', exact: true }).click();
  await expect(page.locator('html')).toHaveAttribute('data-text-size', 'large');
  await settings.getByRole('radio', { name: 'Compact', exact: true }).click();
  await expect(page.locator('html')).toHaveAttribute('data-density', 'compact');

  // simulation: the time unit and decimals reach the preview, then the clock
  await pick(page, 'Time unit', 'ms');
  await pick(page, 'Time precision', '1');
  await expect(page.getByTestId('time-preview')).toHaveText('1547.3 ms'); // truncated: a clock never shows a time not yet reached

  // reset layout says so
  await page.getByTestId('reset-layout').click();
  await expect(page.getByText('Panel layout reset to the defaults', { exact: true })).toBeVisible();

  await page.reload();
  await expect(page.locator('html')).toHaveAttribute('data-text-size', 'large');
  await expect(page.locator('html')).toHaveAttribute('data-density', 'compact');
  await expect(page.getByTestId('clock')).toContainText('ms');

  // restore the defaults
  await page.getByRole('button', { name: 'Settings', exact: true }).click();
  await page.getByRole('button', { name: 'Restore defaults', exact: true }).click();
  await expect(page.locator('html')).toHaveAttribute('data-text-size', 'medium');
});

test('the workbench in French: navigation, run, design and settings, then back to English', async ({ page }) => {
  await page.goto('/?mock=1');
  await page.getByRole('button', { name: 'Settings', exact: true }).click();
  await pick(page, 'Interface language', 'Français');
  await expect(page.locator('html')).toHaveAttribute('lang', 'fr');
  await expect(page.getByRole('heading', { name: 'Paramètres', level: 1 })).toBeVisible();
  await page.getByRole('button', { name: 'Retour à l’atelier' }).click();

  // the perspectives, the transport and the status read in French
  await expect(page.getByRole('main', { name: 'Perspective Conception', exact: true })).toBeVisible();
  await expect(page.getByRole('region', { name: 'Bibliothèque', exact: true })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Placer Capacitor' })).toBeVisible();
  await page.getByRole('radio', { name: 'Exécution', exact: true }).click();
  await expect(page.getByRole('main', { name: 'Perspective Exécution', exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Exécuter', exact: true }).click();
  await expect(page.getByTestId('sim-state')).toHaveAttribute('data-state', 'running');
  await expect(page.getByTestId('sim-state')).toContainText('En cours');
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  await expect(page.getByTestId('sim-state')).toContainText('En pause');
  await expect(page.getByTestId('connection')).toContainText('Moteur connecté');

  // a reload keeps the language; nothing overflows the window with the longer words
  await page.reload();
  await expect(page.locator('html')).toHaveAttribute('lang', 'fr');
  for (const name of ['Conception', 'Exécution', 'Débogage', 'Vérification']) {
    await page.getByRole('radio', { name, exact: true }).click();
    await expect(page.getByRole('main', { name: `Perspective ${name}`, exact: true })).toBeVisible();
    await expect.poll(() => page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(page.viewportSize()!.width);
  }

  await page.getByRole('button', { name: 'Paramètres', exact: true }).click();
  await pick(page, 'Langue de l’interface', 'English');
  await expect(page.locator('html')).toHaveAttribute('lang', 'en');
  await expect(page.getByRole('heading', { name: 'Settings', level: 1 })).toBeVisible();
});
