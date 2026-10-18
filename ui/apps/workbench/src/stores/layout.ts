// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { CanvasMode } from '@embsim/scene';
import { create } from 'zustand';

export type Perspective = 'design' | 'run' | 'debug' | 'verify';
export const PERSPECTIVES: Perspective[] = ['design', 'run', 'debug', 'verify'];

export type DesignView = 'schematic' | 'bench';

export interface LayoutState {
  perspective: Perspective;
  /** what the Design perspective's canvas shows: the schematic or the bench (ADR-027) */
  designView: DesignView;
  setDesignView(v: DesignView): void;
  canvasMode: CanvasMode;
  paletteOpen: boolean;
  diagnosticsOpen: boolean;
  /** the New project dialog */
  newProjectOpen: boolean;
  /** The board or composite part whose inside the schematic shows, or null. */
  inside: string | null;
  setInside(id: string | null): void;
  setPerspective(p: Perspective): void;
  setCanvasMode(m: CanvasMode): void;
  setPaletteOpen(open: boolean): void;
  setDiagnosticsOpen(open: boolean): void;
  setNewProjectOpen(open: boolean): void;
}

function readPerspective(): Perspective {
  try {
    const v = localStorage.getItem('embsim.perspective');
    return PERSPECTIVES.includes(v as Perspective) ? (v as Perspective) : 'design';
  } catch {
    return 'design';
  }
}

function readDesignView(): DesignView {
  try {
    return localStorage.getItem('embsim.designView') === 'bench' ? 'bench' : 'schematic';
  } catch {
    return 'schematic';
  }
}

export const useLayout = create<LayoutState>((set) => ({
  perspective: readPerspective(),
  designView: readDesignView(),
  setDesignView: (designView) => {
    try {
      localStorage.setItem('embsim.designView', designView);
    } catch {
      // ignore
    }
    set({ designView });
  },
  canvasMode: 'select',
  paletteOpen: false,
  diagnosticsOpen: false,
  newProjectOpen: false,
  inside: null,
  setInside: (inside) => set({ inside }),
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
  setNewProjectOpen: (newProjectOpen) => set({ newProjectOpen }),
}));
