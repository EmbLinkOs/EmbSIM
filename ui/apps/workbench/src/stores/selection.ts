// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { EMPTY_SELECTION, type Selection } from '@embsim/scene';
import { create } from 'zustand';

export interface SelectionState {
  selection: Selection;
  /** A net chosen in the nets list or by hovering; drives the inspector when nothing else is selected. */
  net: string | null;
  setSelection(selection: Selection): void;
  selectComponent(id: string): void;
  selectNet(net: string | null): void;
  clear(): void;
}

export const useSelection = create<SelectionState>((set) => ({
  selection: EMPTY_SELECTION,
  net: null,
  setSelection: (selection) => set({ selection, net: null }),
  selectComponent: (id) => set({ selection: { components: [id], wires: [] }, net: null }),
  selectNet: (net) => set({ net }),
  clear: () => set({ selection: EMPTY_SELECTION, net: null }),
}));
