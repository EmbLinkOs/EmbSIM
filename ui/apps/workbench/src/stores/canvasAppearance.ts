// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { DEFAULT_CANVAS_APPEARANCE, normalizeAppearance, type CanvasAppearance } from '@embsim/scene';
import { create } from 'zustand';
const KEY = 'embsim.canvas.appearance.v1';
function read(): CanvasAppearance {
  try { return normalizeAppearance(JSON.parse(localStorage.getItem(KEY) ?? 'null')); }
  catch { return { ...DEFAULT_CANVAS_APPEARANCE }; }
}
export const useCanvasAppearance = create<{
  appearance: CanvasAppearance;
  update(patch: Partial<CanvasAppearance>): void;
  reset(): void;
}>((set, get) => {
  const save = (appearance: CanvasAppearance) => {
    set({ appearance });
    try { localStorage.setItem(KEY, JSON.stringify(appearance)); } catch { /* Preferences remain usable when storage is unavailable. */ }
  };
  return { appearance: read(), update: (patch) => save(normalizeAppearance({ ...get().appearance, ...patch })), reset: () => save({ ...DEFAULT_CANVAS_APPEARANCE }) };
});
