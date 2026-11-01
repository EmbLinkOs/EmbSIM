#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The Visual Benchmark's screenshots: the same workflows, on the same
// projects and at the same window sizes, each time, so a "before" set and an
// "after" set compare like with like.
//
//   pnpm build
//   node scripts/benchmark-shots.mjs --out ../../../docs/gallery/visual-benchmark-v0.1/after \
//     [--embsim ../../../build/dev/bin/embsim] [--chromium PATH] [--only design,run] [--lang fr]
//
// Each shot starts its own `embsim serve` on the built workbench (dist/).
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from '@playwright/test';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../../../..');
const args = Object.fromEntries(
  process.argv.slice(2).reduce((acc, a, i, all) => (a.startsWith('--') ? [...acc, [a.slice(2), all[i + 1]]] : acc), []),
);
const out = resolve(args.out ?? `${root}/docs/gallery/visual-benchmark-v0.1/latest`);
const embsim = resolve(args.embsim ?? `${root}/build/dev/bin/embsim`);
const chromiumPath = args.chromium;
const only = args.only ? new Set(args.only.split(',')) : null;
const lang = args.lang ?? null;
const dist = resolve(here, '../dist');
let port = Number(args.port ?? 19100);
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

async function serve(project) {
  const p = port++;
  const child = spawn(embsim, ['serve', resolve(root, project), '--port', String(p), '--ui', dist], { stdio: 'ignore' });
  for (let i = 0; i < 80; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${p}/`);
      if (r.ok) return { url: `http://127.0.0.1:${p}/`, stop: () => child.kill() };
    } catch {
      /* not up yet */
    }
    await wait(100);
  }
  child.kill();
  throw new Error(`embsim serve ${project} did not start`);
}

async function shot(browser, name, project, act, { dark = false, width = 1600, height = 1000 } = {}) {
  if (only && !only.has(name)) return;
  const srv = await serve(project);
  const ctx = await browser.newContext({ viewport: { width, height }, colorScheme: dark ? 'dark' : 'light' });
  if (lang) await ctx.addInitScript((l) => { try { localStorage.setItem('embsim.settings', JSON.stringify({ language: l })); } catch { /* none */ } }, lang);
  const page = await ctx.newPage();
  page.setDefaultTimeout(10000);
  try {
    await page.goto(srv.url);
    await page.getByTestId('run-toggle').waitFor();
    await wait(1500);
    await act(page, srv);
    await page.screenshot({ path: `${out}/${name}.png` });
    console.log(`${name}.png`);
  } catch (e) {
    console.error(`${name}: ${e.message.split('\n')[0]}`);
  } finally {
    await ctx.close();
    srv.stop();
  }
}

const perspective = (page, n) => page.keyboard.press(`Control+${n}`);
async function run(page, ms) {
  await page.getByTestId('run-toggle').click();
  await wait(ms);
}

if (!existsSync(embsim)) throw new Error(`no engine at ${embsim}: build it first`);
if (!existsSync(dist)) throw new Error(`no workbench build at ${dist}: run pnpm build`);
mkdirSync(out, { recursive: true });
const browser = await chromium.launch(chromiumPath ? { executablePath: chromiumPath } : {});
const transistor = 'examples/button-transistor-led/project.json';
const bluepill = 'examples/bluepill-button-led/project.json';
const uno = 'examples/uno-breadboard/project.json';
const showcase = 'examples/display-showcase/project.json';
const nucleo = 'examples/nucleo-f446re-blink-serial/project.json';

try {
  // 1. opening a project: the Design perspective as it first appears
  await shot(browser, '01-design', transistor, async () => {});
  // 2. a board project in Design (the Uno on its breadboard)
  await shot(browser, '02-design-board', uno, async () => {});
  // 3. running a circuit: the Run perspective
  await shot(browser, '03-run-circuit', transistor, async (page) => {
    await perspective(page, 2);
    await run(page, 2500);
  });
  // 4. running firmware: a board, its serial output and probes
  await shot(browser, '04-run-firmware', bluepill, async (page) => {
    await perspective(page, 2);
    await run(page, 2500);
  });
  // 5. paused: the same session stopped after running
  await shot(browser, '05-run-paused', nucleo, async (page) => {
    await perspective(page, 2);
    await run(page, 2000);
    await page.getByTestId('run-toggle').click();
    await wait(800);
  });
  // 6. debugging: the Debug perspective on a board
  await shot(browser, '06-debug', bluepill, async (page) => {
    await perspective(page, 3);
    await run(page, 1500);
    await page.getByTestId('run-toggle').click();
    await wait(1000);
  });
  // 7. verifying: the Verify perspective
  await shot(browser, '07-verify', bluepill, async (page) => {
    await perspective(page, 4);
    await wait(800);
  });
  // 8. a dense project
  await shot(browser, '08-dense-project', showcase, async (page) => {
    await perspective(page, 2);
    await run(page, 2500);
  });
  // 9. dark theme
  await shot(browser, '09-dark-run', bluepill, async (page) => {
    await perspective(page, 2);
    await run(page, 2000);
  }, { dark: true });
  // 10. a laptop screen
  await shot(browser, '10-laptop-design', uno, async () => {}, { width: 1280, height: 720 });
  // 11. the engine gone: what the workbench shows when its connection drops
  await shot(browser, '11-disconnected', transistor, async (page, srv) => {
    srv.stop();
    await wait(2500);
  });
  // 12. Settings (where the workbench has them)
  await shot(browser, '12-settings', transistor, async (page) => {
    const open = page.getByRole('button', { name: /^(Settings|Paramètres)$/ });
    if (await open.count()) {
      await open.first().click();
      await wait(600);
    }
  });
} finally {
  await browser.close();
}
