// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { expect, test, type Locator } from '@playwright/test';

// Island IDs and live voltages refresh asynchronously; these checks assert edits
// preserve the project net graph, regardless of simulation-report timing.
const connectivity = (table: Locator) => table.locator('tbody tr').evaluateAll((rows) => rows.map((row) => {
  const cells = row.querySelectorAll('td');
  return [cells[0]?.textContent, cells[2]?.textContent];
}));

test('preview every leg, reject occupied holes, move the assembly and undo', async ({ page }) => {
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  await page.getByRole('button', { name: 'Breadboard', exact: true }).click();
  const list = page.getByRole('complementary', { name: 'On the bench' });
  await expect(list.getByRole('button', { name: /^BB1/ })).toBeVisible();
  const nets = page.getByRole('table', { name: 'Nets' });
  await list.getByRole('button', { name: /^R2/ }).click();
  await list.getByLabel("Plug R2's first leg into").fill('a10');
  const preview = list.getByRole('status', { name: 'Insertion preview' });
  await expect(preview).toContainText('R2.1 → BB1.a10');
  await expect(preview).toContainText('R2.2 → BB1.a14');
  await list.getByRole('button', { name: 'Plug in', exact: true }).click();
  await expect(list.getByRole('button', { name: /^R2/ })).toContainText('in a10 a14');
  const rx = Number(await list.getByLabel('Bench X (mm)').inputValue());
  const ry = Number(await list.getByLabel('Bench Y (mm)').inputValue());
  const beforeConflict = await connectivity(nets);

  await list.getByRole('button', { name: /^R1/ }).click();
  await list.getByLabel("Plug R1's first leg into").fill('a10');
  await expect(preview).toContainText('occupied by R2.1');
  await expect(list.getByRole('button', { name: 'Plug in', exact: true })).toBeDisabled();
  await expect.poll(() => connectivity(nets)).toEqual(beforeConflict);
  await list.getByLabel("Plug R1's first leg into").fill('b10');
  await expect(list.getByRole('button', { name: 'Plug in', exact: true })).toBeEnabled();
  await list.getByRole('button', { name: 'Plug in', exact: true }).click();
  await expect(list.getByRole('button', { name: /^R1/ })).toContainText('in b10 b14');

  const beforeMove = await connectivity(nets);
  await list.getByRole('button', { name: /^BB1/ }).click();
  const bx = Number(await list.getByLabel('Bench X (mm)').inputValue());
  const by = Number(await list.getByLabel('Bench Y (mm)').inputValue());
  await list.getByLabel('Bench X (mm)').fill('80');
  await list.getByLabel('Bench Y (mm)').fill('90');
  await list.getByRole('combobox', { name: 'Bench rotation' }).click();
  await page.getByRole('option', { name: '90°', exact: true }).click();
  await list.getByRole('button', { name: 'Apply bench position', exact: true }).click();
  await expect(list.getByRole('button', { name: /^R2/ })).toContainText('in a10 a14');
  await expect(list.getByRole('button', { name: /^R1/ })).toContainText('in b10 b14');
  await expect.poll(() => connectivity(nets)).toEqual(beforeMove);
  await list.getByRole('button', { name: /^R2/ }).click();
  expect(Number(await list.getByLabel('Bench X (mm)').inputValue())).toBeCloseTo(80 - (ry - by), 2);
  expect(Number(await list.getByLabel('Bench Y (mm)').inputValue())).toBeCloseTo(90 + (rx - bx), 2);
  await expect(list.getByRole('combobox', { name: 'Bench rotation' })).toHaveText('90°');
  await page.keyboard.press('Control+z');
  await expect(list.getByLabel('Bench X (mm)')).toHaveValue(String(rx));
  await expect(list.getByLabel('Bench Y (mm)')).toHaveValue(String(ry));
  await expect(list.getByRole('combobox', { name: 'Bench rotation' })).toHaveText('0°');
  await expect.poll(() => connectivity(nets)).toEqual(beforeMove);
  await list.getByLabel('Bench X (mm)').fill('');
  await expect(list.getByRole('button', { name: 'Apply bench position', exact: true })).toBeDisabled();
});

test('a typed jumper cannot reuse a hole occupied by a leg', async ({ page }) => {
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  await page.getByRole('button', { name: 'Breadboard', exact: true }).click();
  const list = page.getByRole('complementary', { name: 'On the bench' });
  await expect(list.getByRole('button', { name: /^BB1/ })).toBeVisible();
  await list.getByRole('button', { name: /^R2/ }).click();
  await list.getByLabel("Plug R2's first leg into").fill('a10');
  await list.getByRole('button', { name: 'Plug in', exact: true }).click();
  await expect(list.getByRole('button', { name: /^R2/ })).toContainText('in a10 a14');
  const nets = page.getByRole('table', { name: 'Nets' });
  const before = await connectivity(nets);
  await list.getByLabel('Jumper from', { exact: true }).fill('BB1.a10');
  await list.getByLabel('to', { exact: true }).fill('Q1.B');
  await list.getByRole('button', { name: 'Add jumper', exact: true }).click();
  await expect(page.getByText('BB1.a10 is occupied by R2.1; use another hole on the strip', { exact: true })).toBeVisible();
  await expect.poll(() => connectivity(nets)).toEqual(before);
});


test('a pending breadboard placement preserves a later part selection', async ({ page }) => {
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  const list = page.getByRole('complementary', { name: 'On the bench' });
  await expect(list.getByRole('button', { name: /^R2/ })).toBeVisible();
  // Dispatch both intents in one task, before the placement promise can settle.
  await page.evaluate(() => {
    const add = [...document.querySelectorAll<HTMLButtonElement>('button')]
      .find((button) => button.textContent?.trim() === 'Breadboard');
    const select = [...document.querySelectorAll<HTMLButtonElement>('[aria-label="On the bench"] button')]
      .find((button) => button.querySelector('.wb-bench-list__id')?.textContent === 'R2');
    if (!add || !select) throw new Error('Bench controls missing');
    add.click();
    select.click();
  });
  await expect(list.getByRole('button', { name: /^BB1/ })).toBeVisible();
  await expect(list.getByRole('button', { name: /^R2/ })).toHaveAttribute('aria-pressed', 'true');
  await expect(list.getByLabel("Plug R2's first leg into")).toBeVisible();
});
