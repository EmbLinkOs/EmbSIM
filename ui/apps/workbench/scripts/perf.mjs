// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The schematic's frame times on a large project (07 §2, M2: a
// 500-component schematic at 60 Hz). Starts `embsim serve` on the project,
// then measures requestAnimationFrame intervals in Chromium while panning,
// zooming and dragging a part.
//
//   python3 ../../../tools/perf/large_project.py 500 > /tmp/large/project.json
//   node scripts/perf.mjs /tmp/large/project.json [--chromium PATH]

/* global window, requestAnimationFrame */
import { spawn } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from '@playwright/test';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../../../..');
const project = resolve(process.argv[2] ?? '/tmp/large/project.json');
const ci = process.argv.indexOf('--chromium');
const chromiumPath = ci > 0 ? process.argv[ci + 1] : process.env.EMBSIM_CHROMIUM;
const port = 18990;
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

const srv = spawn(resolve(root, 'build/dev/bin/embsim'), ['serve', project, '--port', String(port), '--ui', resolve(here, '../dist')], { stdio: 'ignore' });
const browser = await chromium.launch(chromiumPath ? { executablePath: chromiumPath } : {});
try {
  await wait(800);
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const t0 = Date.now();
  await page.goto(`http://127.0.0.1:${port}/`);
  await page.getByRole('button', { name: /^U500, / }).waitFor({ state: 'attached', timeout: 60000 });
  console.log(`loaded and drawn: ${Date.now() - t0} ms, ${await page.locator('.es-component__hit').count()} parts mounted`);

  await page.evaluate(() => {
    const w = window;
    w.__frames = [];
    w.__last = 0;
    w.__on = false;
    const tick = (t) => {
      if (w.__on && w.__last) w.__frames.push(t - w.__last);
      w.__last = t;
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  });
  const measure = async (name, act) => {
    await page.evaluate(() => {
      window.__frames = [];
      window.__on = true;
    });
    await act();
    const f = await page.evaluate(() => {
      window.__on = false;
      return window.__frames;
    });
    f.sort((a, b) => a - b);
    const mean = f.reduce((a, b) => a + b, 0) / Math.max(1, f.length);
    const p95 = f[Math.floor(f.length * 0.95)] ?? 0;
    const long = f.filter((x) => x > 50).length;
    console.log(`${name.padEnd(12)} ${String(f.length).padStart(4)} frames  mean ${mean.toFixed(1)} ms (${(1000 / mean).toFixed(0)} Hz)  p95 ${p95.toFixed(1)} ms  max ${(f.at(-1) ?? 0).toFixed(1)} ms  over 50 ms: ${long}`);
  };

  const svg = page.locator('.es-scene__svg');
  const box = await svg.boundingBox();
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;
  await page.mouse.move(cx, cy);
  await measure('zoom', async () => {
    for (let i = 0; i < 30; i++) {
      await page.mouse.wheel(0, i < 15 ? -120 : 120);
      await wait(30);
    }
  });
  await measure('pan', async () => {
    // a drag on empty sheet with the middle button pans
    await page.mouse.move(box.x + 20, box.y + 20);
    await page.mouse.down({ button: 'middle' });
    for (let i = 0; i < 60; i++) {
      await page.mouse.move(box.x + 20 + i * 6, box.y + 20 + i * 3);
      await wait(16);
    }
    await page.mouse.up({ button: 'middle' });
  });
  await measure('drag part', async () => {
    const part = await page.getByRole('button', { name: /^U13, / }).boundingBox();
    await page.mouse.move(part.x + part.width / 2, part.y + part.height / 2);
    await page.mouse.down();
    for (let i = 0; i < 60; i++) {
      await page.mouse.move(part.x + part.width / 2 + i * 3, part.y + part.height / 2 + i * 2);
      await wait(16);
    }
    await page.keyboard.press('Escape');
    await page.mouse.up();
  });
  await measure('idle', () => wait(3000));
} finally {
  await browser.close();
  srv.kill();
}
