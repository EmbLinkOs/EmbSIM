// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { create } from 'zustand';
export const useHierarchy = create<{
  open: boolean | null;
  path: string;
  target: { component: string } | null;
  show(path?: string, component?: string): void;
  close(): void;
}>((set) => ({
  open: null,
  path: '',
  target: null,
  show: (path = '', component) => set({ open: true, path, target: component ? { component } : null }),
  close: () => set({ open: false }),
}));
