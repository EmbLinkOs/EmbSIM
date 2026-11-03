// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Frame times of the schematic on a 500-component sheet against ADR-028: a
// frame's work within 8.3 ms for 120 Hz, the 16.7 ms floor at worst, while
// zooming, panning and dragging a part (the interactions ADR-028 names).
// The sheet has 500 parts and about 750 wires and 250 labels (the shape of
// tools/perf/large_project.py's), opened as a person opens a file.
//
// A frame's work is the main thread's: from the frame's start (the
// timestamp requestAnimationFrame gives) to a message posted from that
// callback, which runs once the frame's input, style, layout and paint are
// done. Rasterising and compositing happen off the main thread (on a GPU, or
// SwiftShader's threads in headless CI) and are not in it. Headless CI is a
// shared CPU, so there the numbers are reported, not judged;
// EMBSIM_FRAME_BUDGET=enforce judges them on a reference machine.

import { mkdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { expect, test, type Page } from '@playwright/test';
import { BUDGET_120_MS, BUDGET_60_MS, meets, summarize, type FrameSummary } from '../../../packages/bench/src/frames';

const enforce = process.env.EMBSIM_FRAME_BUDGET === 'enforce';

/** a 500-part sheet, as tools/perf/large_project.py's but of parts the demo
 * session's catalog has: 250 RC sections in a ladder, each resistor into the
 * next, each capacitor to a ground label, every connection a drawn wire */
function largeProject(n: number): object {
  const COLS = 25, DX = 100, DY = 80, cells = n / 2;
  const components: object[] = [];
  const nets: Record<string, string[]> = { GND: [] };
  const wires: { net: string; points: number[][] }[] = [];
  const labels: { net: string; x: number; y: number }[] = [];
  for (let i = 0; i < cells; i++) {
    const x = (i % COLS) * DX, y = 80 + Math.floor(i / COLS) * DY, r = `R${i + 1}`, c = `C${i + 1}`, net = `N${i + 1}`;
    components.push({ id: r, component: 'emblink/resistor', layout: { x, y } }, { id: c, component: 'emblink/capacitor', layout: { x: x + 20, y: y + 30 } });
    nets[net] = [`${r}.2`, `${c}.2`];
    nets['GND']!.push(`${c}.1`);
    wires.push({ net, points: [[x + 60, y + 40], [x + 60, y + 10]] }, { net: 'GND', points: [[x + 20, y + 40], [x + 10, y + 40]] });
    labels.push({ net: 'GND', x: x + 10, y: y + 40 });
    if (i) {
      const px = ((i - 1) % COLS) * DX, py = 80 + Math.floor((i - 1) / COLS) * DY, prev = `N${i}`;
      nets[prev]!.push(`${r}.1`);
      wires.push(py === y
        ? { net: prev, points: [[px + 60, py + 10], [x, y + 10]] }
        : { net: prev, points: [[px + 60, py + 10], [px + 80, py + 10], [px + 80, py + 65], [x - 20, py + 65], [x - 20, y + 10], [x, y + 10]] });
    }
  }
  return {
    schema_version: 1, kind: 'project', name: `large-${n}`, policy: 'functional', components, nets,
    layout: { wires, labels, sheet: { width: COLS * DX + 200, height: (Math.ceil(cells / COLS) + 2) * DY + 200 } },
  };
}

/** starts counting each frame's main-thread work */
async function startFrames(page: Page): Promise<void> {
  await page.evaluate(() => {
    const w = window as unknown as { __work: number[]; __on: boolean; __loop?: boolean };
    w.__work = [];
    w.__on = true;
    if (w.__loop) return;
    w.__loop = true;
    const channel = new MessageChannel();
    let start = 0;
    channel.port1.onmessage = () => {
      if (w.__on) w.__work.push(performance.now() - start);
    };
    const tick = (t: number) => {
      start = t;
      channel.port2.postMessage(0);
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  });
}

async function stopFrames(page: Page): Promise<number[]> {
  return page.evaluate(() => {
    const w = window as unknown as { __work: number[]; __on: boolean };
    w.__on = false;
    return w.__work;
  });
}

const row = (name: string, s: FrameSummary) =>
  `${name.padEnd(10)} ${String(s.frames).padStart(4)} frames  p50 ${s.p50.toFixed(2)}  p95 ${s.p95.toFixed(2)}  p99 ${s.p99.toFixed(2)}  max ${s.max.toFixed(2)} ms  over 8.3 ms ${(s.over120 * 100).toFixed(0)}%  over 16.7 ms ${(s.over60 * 100).toFixed(0)}%`;

test('frame times on a 500-component schematic, zoom, pan and drag, against ADR-028', async ({ page }, info) => {
  test.setTimeout(180_000);
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('/?mock=1');
  await expect(page.locator('.es-scene__svg')).toBeVisible({ timeout: 15_000 });
  const file = join(tmpdir(), `embsim-large-${process.pid}.json`);
  writeFileSync(file, JSON.stringify(largeProject(500)), 'utf-8');
  const chooser = page.waitForEvent('filechooser');
  await page.keyboard.press('Control+K');
  await page.keyboard.type('Open project');
  await page.keyboard.press('Enter');
  await (await chooser).setFiles(file);
  await expect(page.getByText('500 parts')).toBeVisible({ timeout: 60_000 });
  await page.waitForTimeout(1500);

  const svg = page.locator('.es-scene__svg');
  const box = (await svg.boundingBox())!;
  const cx = box.x + box.width / 2, cy = box.y + box.height / 2;
  const frame = () => page.evaluate(() => new Promise((r) => requestAnimationFrame(() => r(null))));
  const results: Record<string, FrameSummary> = {};

  // idle: the sheet drawn, nothing moving (what the probe itself reads)
  await startFrames(page);
  await page.waitForTimeout(1500);
  results['idle'] = summarize(await stopFrames(page));

  // zoom: the wheel in and out about the middle, a notch a frame
  await page.mouse.move(cx, cy);
  await startFrames(page);
  for (let i = 0; i < 40; i++) {
    await page.mouse.wheel(0, i < 20 ? -100 : 100);
    await frame();
  }
  await page.waitForTimeout(300); // the redraw at the new zoom counts too
  results['zoom'] = summarize(await stopFrames(page));

  // pan: a middle-button drag on the sheet
  await page.mouse.move(box.x + 40, box.y + 40);
  await startFrames(page);
  await page.mouse.down({ button: 'middle' });
  for (let i = 0; i < 60; i++) {
    await page.mouse.move(box.x + 40 + i * 6, box.y + 40 + i * 3);
    await frame();
  }
  await page.mouse.up({ button: 'middle' });
  await page.waitForTimeout(300);
  results['pan'] = summarize(await stopFrames(page));

  // drag a part across the sheet, then let it go back (Escape)
  const part = (await page.getByRole('button', { name: /^R3, / }).boundingBox())!;
  await page.mouse.move(part.x + part.width / 2, part.y + part.height / 2);
  await startFrames(page);
  await page.mouse.down();
  for (let i = 0; i < 60; i++) {
    await page.mouse.move(part.x + part.width / 2 + i * 3, part.y + part.height / 2 + i * 2);
    await frame();
  }
  await page.keyboard.press('Escape');
  await page.mouse.up();
  await page.waitForTimeout(300);
  results['drag part'] = summarize(await stopFrames(page));

  const report = Object.entries(results).map(([k, v]) => row(k, v)).join('\n');
  console.log(`\n${report}${enforce ? '' : '\n(reported, not judged: set EMBSIM_FRAME_BUDGET=enforce on a reference machine)'}`);
  await info.attach('schematic-frame-times', { body: JSON.stringify({ results, budgets: { interaction: BUDGET_120_MS, floor: BUDGET_60_MS } }, null, 2), contentType: 'application/json' });
  mkdirSync('test-results', { recursive: true });
  writeFileSync('test-results/schematic-frame-times.json', JSON.stringify(results, null, 2), 'utf-8');

  for (const [k, s] of Object.entries(results)) {
    expect(s.frames, `${k}: frames measured`).toBeGreaterThan(10);
    // ADR-028: interaction within the 60 Hz floor at least, 120 Hz where the display offers it
    if (enforce) expect(meets(s, BUDGET_60_MS), row(k, s)).toBe(true);
  }
});
