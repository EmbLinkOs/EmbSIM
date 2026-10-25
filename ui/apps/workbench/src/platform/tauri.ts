// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Tauri 2 implementation, loaded only when window.__TAURI_INTERNALS__ exists.
// The Rust shell (desktop/src-tauri) exposes `start_sidecar`, which spawns
// `embsim serve` on a free port and returns its WebSocket URL.

import { invoke } from '@tauri-apps/api/core';
import { open, save } from '@tauri-apps/plugin-dialog';
import { readTextFile, writeFile, writeTextFile } from '@tauri-apps/plugin-fs';
import type { OpenedFile, Platform } from './platform';

/** Sends the workbench's errors to the shell, which prints them: the webview's console is not visible. */
function forwardErrors(): void {
  const send = (message: string) => void invoke('log_message', { level: 'error', message }).catch(() => undefined);
  const text = (args: unknown[]) => args.map((a) => (a instanceof Error ? `${a.message}\n${a.stack ?? ''}` : String(a))).join(' ');
  const error = console.error.bind(console);
  const warn = console.warn.bind(console);
  console.error = (...args: unknown[]) => {
    error(...args);
    send(text(args));
  };
  console.warn = (...args: unknown[]) => {
    warn(...args);
    send(`(warning) ${text(args)}`);
  };
  window.addEventListener('error', (e) => send(`${e.message} at ${e.filename}:${e.lineno}:${e.colno}`));
  window.addEventListener('unhandledrejection', (e) => send(`unhandled rejection: ${e.reason instanceof Error ? `${e.reason.message}\n${e.reason.stack ?? ''}` : String(e.reason)}`));
}

export function createTauriPlatform(): Platform {
  forwardErrors();
  return {
    kind: 'tauri',
    async openProjectFile(): Promise<OpenedFile | null> {
      const path = await open({ multiple: false, filters: [{ name: 'EmbSIM project', extensions: ['json'] }] });
      if (!path || typeof path !== 'string') return null;
      const text = await readTextFile(path);
      return { name: path.split(/[\\/]/).pop() ?? path, path, text };
    },
    async saveTextFile(suggestedName, text) {
      const path = await save({ defaultPath: suggestedName });
      if (!path) return null;
      await writeTextFile(path, text);
      return path;
    },
    async saveBinaryFile(suggestedName, bytes) {
      const path = await save({ defaultPath: suggestedName });
      if (!path) return null;
      await writeFile(path, bytes);
      return path;
    },
    async writeClipboard(text) {
      await navigator.clipboard?.writeText(text);
    },
    async startSidecar(projectPath?: string) {
      const result = await invoke<{ ws_url: string }>('start_sidecar', { project: projectPath ?? null });
      return result.ws_url;
    },
    setMenuState(state) {
      void invoke('set_menu_state', { state }).catch(() => undefined);
    },
  };
}
