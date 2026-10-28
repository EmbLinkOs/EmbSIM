// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { create } from 'zustand';
export const useHierarchy = create<{ open: boolean | null; path: string; show(path?: string): void; close(): void }>(
  (set) => ({
    open: null,
    path: '',
    show: (path = '') => set({ open: true, path }),
    close: () => set({ open: false }),
  }),
);
