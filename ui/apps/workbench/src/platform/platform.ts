// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The platform adapter (SPEC-011 §1, ADR-008): file dialogs, clipboard and
// the sidecar are the only places where the host differs. The UI never
// imports Tauri APIs directly; `tauri.ts` is loaded only inside the shell.

export interface OpenedFile {
  name: string;
  path?: string;
  text: string;
}

export interface Platform {
  readonly kind: 'browser' | 'tauri';
  /** Opens a project file through the host's dialog; null when cancelled. */
  openProjectFile(): Promise<OpenedFile | null>;
  /** Saves text through the host's dialog or a download; returns the path or name, null when cancelled. */
  saveTextFile(suggestedName: string, text: string, mime: string): Promise<string | null>;
  writeClipboard(text: string): Promise<void>;
  /** Starts (or finds) the `embsim serve` sidecar and returns its WebSocket URL; null when the host has none. */
  startSidecar(projectPath?: string): Promise<string | null>;
  /** Native menus: the desktop shell owns them; the browser has none. */
  setMenuState?(state: { running: boolean; canUndo: boolean; canRedo: boolean }): void;
}

declare global {
  interface Window {
    __TAURI_INTERNALS__?: unknown;
    __TAURI__?: unknown;
  }
}

export function isTauri(): boolean {
  return typeof window !== 'undefined' && (window.__TAURI_INTERNALS__ !== undefined || window.__TAURI__ !== undefined);
}

let instance: Promise<Platform> | null = null;

/** The platform for this host, loaded once. */
export function platform(): Promise<Platform> {
  if (!instance) {
    instance = isTauri()
      ? import('./tauri').then((m) => m.createTauriPlatform())
      : import('./browser').then((m) => m.createBrowserPlatform());
  }
  return instance;
}
