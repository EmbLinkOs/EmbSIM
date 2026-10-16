// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Screenshots of the workbench for docs/gallery: the same views at every
// milestone, so the gallery shows how the application changes.
//
//   node scripts/screenshots.mjs --out ../../../docs/gallery/2026-10-16-m1 \
//     [--embsim ../../../build/dev/bin/embsim] [--chromium PATH] [--only design,run]
//
// Each shot starts its own `embsim serve` on the built workbench (dist/),
// because the server takes one client at a time. Run `pnpm build` first.

import { spawn } from 'node:child_process';
import { mkdirSync, existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from '@playwright/test';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../../../..');
const args = Object.fromEntries(
  process.argv.slice(2).reduce((acc, a, i, all) => (a.startsWith('--') ? [...acc, [a.slice(2), all[i + 1]]] : acc), []),
);
const out = resolve(args.out ?? `${root}/docs/gallery/latest`);
const embsim = resolve(args.embsim ?? `${root}/build/dev/bin/embsim`);
const chromiumPath = args.chromium ?? process.env.EMBSIM_CHROMIUM ?? undefined;
const only = args.only ? new Set(args.only.split(',')) : null;
const dist = resolve(here, '../dist');
let port = Number(args.port ?? 18900);

const wait = (ms) => new Promise((r) => setTimeout(r, ms));

async function serve(project) {
  const p = port++;
  const child = spawn(embsim, ['serve', resolve(root, project), '--port', String(p), '--ui', dist], { stdio: 'ignore' });
  for (let i = 0; i < 50; i++) {
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

/** One shot: a fresh server and page, `act` drives it, then the screenshot. */
async function shot(browser, name, project, act, { dark = false, scale = 1, element = null } = {}) {
  if (only && !only.has(name)) return;
  const srv = await serve(project);
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, colorScheme: dark ? 'dark' : 'light', deviceScaleFactor: scale });
  page.setDefaultTimeout(10000);
  try {
    await page.goto(srv.url);
    await page.getByTestId('run-toggle').waitFor();
    await wait(1200);
    await act(page);
    if (element) await page.locator(element).first().screenshot({ path: `${out}/${name}.png` });
    else await page.screenshot({ path: `${out}/${name}.png` });
    console.log(`${name}.png`);
  } finally {
    await page.close();
    srv.stop();
  }
}

const perspective = (page, n) => page.keyboard.press(`Control+${n}`);
async function run(page, ms) {
  await page.getByTestId('run-toggle').click();
  await wait(ms);
}
async function holdButton(page, name, ms) {
  const hold = page.getByRole('button', { name: new RegExp(`Hold ${name}`) });
  await hold.hover();
  await page.mouse.down();
  await wait(ms);
  await page.mouse.up();
}

if (!existsSync(embsim)) throw new Error(`no engine at ${embsim}: build it first`);
if (!existsSync(dist)) throw new Error(`no workbench build at ${dist}: run pnpm build`);
mkdirSync(out, { recursive: true });
const browser = await chromium.launch(chromiumPath ? { executablePath: chromiumPath } : {});
const transistor = 'examples/button-transistor-led/project.json';
const bluepill = 'examples/bluepill-button-led/project.json';
const counter = 'examples/counter-bus/project.json';
const showcase = 'examples/display-showcase/project.json';
const flasher = 'examples/kicad-flasher/project.json';
const uno = 'examples/uno-blink-serial/project.json';
const hifive = 'examples/hifive1-blink/project.json';
const twoBoards = 'examples/two-boards-uart/project.json';
const pico = 'examples/pico-blink-serial/project.json';
const dk = 'examples/nrf52840dk-blink/project.json';

/** the Debug perspective stopped at a breakpoint on `symbol` */
async function debugAt(page, symbol, ms) {
  await perspective(page, 3);
  await wait(800);
  await page.getByLabel('Breakpoint: a function or an address').fill(symbol);
  await page.getByRole('button', { name: 'Add' }).click();
  await wait(500);
  await run(page, ms);
  await wait(1200);
}

try {
  await shot(browser, 'design-transistor', transistor, async () => {});
  await shot(browser, 'run-transistor', transistor, async (page) => {
    await perspective(page, 2);
    await run(page, 2500);
  });
  // the counter running: the bus Q shows the count
  await shot(browser, 'design-counter', counter, (page) => run(page, 1200));
  await shot(browser, 'run-counter', counter, async (page) => {
    await perspective(page, 2);
    await run(page, 1500);
  });
  // M3: the catalog's displays and actuators on the board view, and an imported KiCad netlist
  await shot(browser, 'run-showcase', showcase, async (page) => {
    await perspective(page, 2);
    await run(page, 3500);
  });
  // the same board view up close
  await shot(browser, 'board-showcase', showcase, async (page) => {
    await perspective(page, 2);
    await run(page, 3500);
  }, { scale: 3, element: '.es-board svg' });
  await shot(browser, 'design-showcase', showcase, async (page) => run(page, 1500));
  await shot(browser, 'design-flasher', flasher, async (page) => run(page, 1500));
  // M4: the Arduino Uno (AVR), the HiFive1 (RISC-V), and two boards on one line
  await shot(browser, 'design-uno', uno, async (page) => run(page, 1500));
  await shot(browser, 'run-uno', uno, async (page) => {
    await perspective(page, 2);
    await run(page, 2500);
  });
  await shot(browser, 'board-uno', uno, async (page) => {
    await perspective(page, 2);
    await run(page, 2600);
  }, { scale: 2, element: '.es-board svg' });
  await shot(browser, 'debug-uno', uno, (page) => debugAt(page, '__vector_14', 1500));
  await shot(browser, 'run-hifive1', hifive, async (page) => {
    await perspective(page, 2);
    await run(page, 2700);
  });
  await shot(browser, 'board-hifive1', hifive, async (page) => {
    await perspective(page, 2);
    await run(page, 2700);
  }, { scale: 2, element: '.es-board svg' });
  await shot(browser, 'debug-hifive1', hifive, (page) => debugAt(page, 'trap_entry', 1500));
  // M4 part 2: the Raspberry Pi Pico (RP2040) and the nRF52840 DK
  await shot(browser, 'design-pico', pico, async (page) => run(page, 1500));
  await shot(browser, 'run-pico', pico, async (page) => {
    await perspective(page, 2);
    await run(page, 2700);
  });
  await shot(browser, 'board-pico', pico, async (page) => {
    await perspective(page, 2);
    await run(page, 2700);
  }, { scale: 3, element: '.es-board svg' });
  await shot(browser, 'debug-pico', pico, (page) => debugAt(page, 'timer_irq_0', 1500));
  await shot(browser, 'design-nrf52840dk', dk, async (page) => run(page, 1500));
  await shot(browser, 'run-nrf52840dk', dk, async (page) => {
    await perspective(page, 2);
    await run(page, 2700);
  });
  await shot(browser, 'board-nrf52840dk', dk, async (page) => {
    await perspective(page, 2);
    await run(page, 2700);
  }, { scale: 2, element: '.es-board svg' });
  await shot(browser, 'debug-nrf52840dk', dk, (page) => debugAt(page, 'rtc1_irq', 1500));
  await shot(browser, 'design-two-boards', twoBoards, async (page) => run(page, 2000));
  await shot(browser, 'run-two-boards', twoBoards, async (page) => {
    await perspective(page, 2);
    await run(page, 3000);
  });
  await shot(browser, 'design-bluepill', bluepill, async () => {});
  await shot(browser, 'design-bluepill-dark', bluepill, async () => {}, { dark: true });
  await shot(browser, 'inside-bluepill', bluepill, async (page) => {
    // the board's inside, live: double-click it while the firmware runs
    await run(page, 800);
    const box = await page.getByRole('button', { name: /^B1, / }).boundingBox();
    await page.mouse.dblclick(box.x + box.width / 2, box.y + box.height / 2);
    await wait(1200);
  });
  await shot(browser, 'run-bluepill', bluepill, async (page) => {
    await perspective(page, 2);
    await run(page, 1500);
    await holdButton(page, 'SW1', 800);
    await wait(600);
  });
  await shot(browser, 'decode-bluepill', bluepill, async (page) => {
    // the boot banner on USART1, decoded: advance 3 ms, then zoom where it is
    await perspective(page, 2);
    await wait(500);
    for (let i = 0; i < 3; i++) {
      await page.getByRole('button', { name: 'Advance by 1ms' }).click();
      await wait(400);
    }
    const box = await page.getByRole('img', { name: /^Logic analyzer/ }).boundingBox();
    await page.mouse.move(box.x + 72 + (box.width - 80) * 0.055, box.y + 40);
    for (let i = 0; i < 3; i++) {
      await page.mouse.wheel(0, -400);
      await wait(150);
    }
    await page.mouse.move(box.x + box.width / 2, box.y - 60);
    await wait(500);
  });
  await shot(browser, 'debug-bluepill', bluepill, async (page) => {
    await perspective(page, 3);
    await wait(800);
    await page.getByLabel('Breakpoint: a function or an address').fill('EXTI0_IRQHandler');
    await page.getByRole('button', { name: 'Add' }).click();
    await wait(500);
    await run(page, 1000);
    await perspective(page, 2);
    await wait(500);
    await holdButton(page, 'SW1', 400);
    await perspective(page, 3);
    await wait(1500);
    await page.getByTitle('One instruction (F11)').click();
    await wait(1000);
  });
} finally {
  await browser.close();
}
