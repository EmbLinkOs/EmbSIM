// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { defineConfig } from '@playwright/test';
import { existsSync } from 'node:fs';

// The CI image has Chromium pre-installed under /opt/pw-browsers; a matching
// Playwright build is not required because we point at the executable.
const preinstalled = ['/opt/pw-browsers/chromium-1194/chrome-linux/chrome'].find((p) => existsSync(p));

export default defineConfig({
  testDir: './e2e',
  timeout: 60_000,
  retries: 0,
  reporter: [['list']],
  use: {
    baseURL: 'http://127.0.0.1:4173',
    viewport: { width: 1280, height: 800 },
    launchOptions: preinstalled ? { executablePath: preinstalled } : {},
  },
  webServer: {
    command: 'pnpm exec vite preview --port 4173 --strictPort',
    url: 'http://127.0.0.1:4173',
    reuseExistingServer: true,
    timeout: 30_000,
  },
});
