// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Tauri 2 implementation, loaded only when window.__TAURI_INTERNALS__ exists.
// The Rust shell (desktop/src-tauri) exposes `start_sidecar`, which spawns
// `embsim serve` on a free port and returns its WebSocket URL.

import { invoke } from '@tauri-apps/api/core';
import { open, save } from '@tauri-apps/plugin-dialog';
import { readTextFile, writeTextFile } from '@tauri-apps/plugin-fs';
import type { OpenedFile, Platform } from './platform';

export function createTauriPlatform(): Platform {
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
