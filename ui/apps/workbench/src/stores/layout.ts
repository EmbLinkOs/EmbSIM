// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { CanvasMode } from '@embsim/scene';
import { create } from 'zustand';

export type Perspective = 'design' | 'run' | 'debug' | 'verify';
export const PERSPECTIVES: Perspective[] = ['design', 'run', 'debug', 'verify'];

export interface LayoutState {
  perspective: Perspective;
  canvasMode: CanvasMode;
  paletteOpen: boolean;
  diagnosticsOpen: boolean;
  setPerspective(p: Perspective): void;
  setCanvasMode(m: CanvasMode): void;
  setPaletteOpen(open: boolean): void;
  setDiagnosticsOpen(open: boolean): void;
}

function readPerspective(): Perspective {
  try {
    const v = localStorage.getItem('embsim.perspective');
    return PERSPECTIVES.includes(v as Perspective) ? (v as Perspective) : 'design';
  } catch {
    return 'design';
  }
}

export const useLayout = create<LayoutState>((set) => ({
  perspective: readPerspective(),
  canvasMode: 'select',
  paletteOpen: false,
  diagnosticsOpen: false,
  setPerspective: (perspective) => {
    try {
      localStorage.setItem('embsim.perspective', perspective);
    } catch {
      // ignore
    }
    set({ perspective, canvasMode: 'select' });
  },
  setCanvasMode: (canvasMode) => set({ canvasMode }),
  setPaletteOpen: (paletteOpen) => set({ paletteOpen }),
  setDiagnosticsOpen: (diagnosticsOpen) => set({ diagnosticsOpen }),
}));
