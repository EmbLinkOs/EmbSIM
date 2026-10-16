// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

// The workbench is served either by `vite dev`, by `embsim serve` (from dist/)
// or inside the Tauri webview. Assets use relative paths so any base works.
export default defineConfig({
  plugins: [react()],
  base: './',
  server: { port: 5173, strictPort: false },
  build: {
    target: 'es2022',
    sourcemap: true,
    chunkSizeWarningLimit: 900,
  },
  esbuild: { supported: { bigint: true } },
});
