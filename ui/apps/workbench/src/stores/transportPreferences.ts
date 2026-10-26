// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { create } from 'zustand';

/** Shared by the toolbar, keyboard shortcuts and command palette. */
export const useTransportPreferences = create<{ advanceBy: string; setAdvanceBy(value: string): void }>((set) => ({
  advanceBy: '1ms',
  setAdvanceBy: (advanceBy) => set({ advanceBy }),
}));
