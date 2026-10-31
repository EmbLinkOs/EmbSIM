// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Frame times of the 3D bench (docs/engineering/bench-3d.md, stage 7): the
// demo bench orbited in draft and in quality, each frame's render read back
// from its User Timing measure, and the percentiles reported against ADR-028
// (draft within 8.3 ms for 120 Hz, quality within the 16.7 ms floor). Headless
// CI renders on the CPU (SwiftShader), so there the numbers are reported, not
// judged; EMBSIM_FRAME_BUDGET=enforce judges them on a machine with a GPU.

import { writeFileSync, mkdirSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { BUDGET_120_MS, BUDGET_60_MS, FRAME_MEASURE, GPU_MEASURE, meets, summarize, type FrameSummary } from '../../../packages/bench/src/frames';

const enforce = process.env.EMBSIM_FRAME_BUDGET === 'enforce';

/** orbits for at most `moves` frames or `seconds`, whichever ends first: a CPU renderer on a busy runner is slow */
async function orbit(page: Page, moves: number, mode: 'draft' | 'quality', seconds = 20): Promise<number[]> {
  const bench = page.locator('.eb-bench3d');
  const box = (await bench.boundingBox())!;
  await page.evaluate((name) => performance.clearMeasures(name), FRAME_MEASURE);
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;
  await page.mouse.move(cx, cy);
  await page.mouse.down({ button: 'right' });
  const deadline = Date.now() + seconds * 1000;
  for (let i = 0; i < moves && Date.now() < deadline; i++) {
    await page.mouse.move(cx + Math.sin(i / 8) * box.width * 0.3, cy + Math.cos(i / 11) * box.height * 0.15);
    // one frame per move: wait for the bench's frame before the next
    await page.evaluate(() => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))));
  }
  await page.mouse.up({ button: 'right' });
  const frames = await page.evaluate((name) => performance.getEntriesByName(name).map((e) => ({ ms: e.duration, mode: ((e as PerformanceMeasure).detail as { mode?: string } | null)?.mode })), FRAME_MEASURE);
  // the frames were drawn in the mode asked for (quality had not fallen back to draft); hover outlines count as draft
  const own = frames.filter((f) => f.mode === mode || (mode === 'draft' && f.mode === 'outline'));
  expect(own.length, `${mode} frames among ${frames.length}`).toBe(frames.length);
  return own.map((f) => f.ms);
}

const row = (mode: string, s: FrameSummary) =>
  `${mode.padEnd(8)} ${String(s.frames).padStart(4)} frames  p50 ${s.p50.toFixed(2)}  p95 ${s.p95.toFixed(2)}  p99 ${s.p99.toFixed(2)}  max ${s.max.toFixed(2)} ms  over 8.3 ms ${(s.over120 * 100).toFixed(0)}%  over 16.7 ms ${(s.over60 * 100).toFixed(0)}%`;

test('frame times while orbiting the bench, draft and quality, against ADR-028', async ({ page }, info) => {
  test.setTimeout(150_000);
  await page.goto('/?mock=1');
  await page.getByRole('radio', { name: 'Bench', exact: true }).click();
  await expect(page.locator('.eb-bench3d canvas')).toBeVisible({ timeout: 15_000 });
  await page.waitForTimeout(800);

  const draft = summarize(await orbit(page, 90, 'draft'));

  await page.locator('.wb-bench-view-menu summary', { hasText: 'View' }).click();
  await page.getByRole('combobox', { name: 'Render quality' }).click();
  await page.getByRole('option', { name: /Quality render/ }).click();
  await page.locator('.wb-bench-view-menu summary', { hasText: 'View' }).click();
  await page.waitForTimeout(500);
  const quality = summarize(await orbit(page, 60, 'quality'));

  // the GPU's own time, where the browser exposes timer queries (not SwiftShader)
  const gpu = await page.evaluate((name) => performance.getEntriesByName(name).map((e) => e.duration), GPU_MEASURE);
  const report = [row('draft', draft), row('quality', quality), gpu.length ? row('gpu', summarize(gpu)) : 'gpu      no timer queries in this browser (CPU time only)'].join('\n');
  console.log(`\n${report}${enforce ? '' : '\n(reported, not judged: set EMBSIM_FRAME_BUDGET=enforce on a GPU)'}`);
  await info.attach('frame-times', { body: JSON.stringify({ draft, quality, budgets: { draft: BUDGET_120_MS, quality: BUDGET_60_MS } }, null, 2), contentType: 'application/json' });
  mkdirSync('test-results', { recursive: true });
  writeFileSync('test-results/frame-times.json', JSON.stringify({ draft, quality }, null, 2), 'utf-8');

  // every frame drawn was measured
  expect(draft.frames).toBeGreaterThan(5);
  expect(quality.frames).toBeGreaterThan(2);
  if (enforce) {
    expect(meets(draft, BUDGET_120_MS), row('draft', draft)).toBe(true);
    expect(meets(quality, BUDGET_60_MS), row('quality', quality)).toBe(true);
  }
});
