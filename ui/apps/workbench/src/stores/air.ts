// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The sniffers' captures (emblink/air_sniffer), gathered as the session
// reports them: each update carries a sniffer's last frames, and the frames
// not seen yet are appended, as Wireshark's packet list grows while it
// captures. Frames that left the part's window between two updates are
// counted as missed. A reset of the session clears them, as the parts do.

import { gatherCapture, parseCapture, type CapturedFrame } from '@embsim/instruments';
import { create } from 'zustand';
import { useSession } from './session';

export interface Capture {
  frames: CapturedFrame[];
  missed: number;
}

interface AirState {
  captures: Record<string, Capture>;
  clear(id: string): void;
}

export const useAir = create<AirState>((set) => ({
  captures: {},
  clear: (id) => set((s) => ({ captures: { ...s.captures, [id]: { frames: [], missed: 0 } } })),
}));

/** The sniffers' ids among the outputs: those with a `capture` text. */
function captures(outputs: Record<string, unknown>): [string, string][] {
  const out: [string, string][] = [];
  for (const [k, v] of Object.entries(outputs)) if (k.endsWith('.capture') && typeof v === 'string') out.push([k.slice(0, -8), v]);
  return out;
}

useSession.subscribe((s, prev) => {
  if (s.outputs === prev.outputs && s.now >= prev.now) return;
  const reset = s.now < prev.now;
  const cur = useAir.getState().captures;
  let next: Record<string, Capture> | null = reset ? {} : null;
  for (const [id, text] of captures(s.outputs)) {
    const was = (next ?? cur)[id] ?? { frames: [], missed: 0 };
    const latest = parseCapture(text);
    // the part started again (a reset, a reload): its numbering did too
    const base = latest.length && was.frames.length && latest[latest.length - 1]!.seq < was.frames[was.frames.length - 1]!.seq ? { frames: [], missed: 0 } : was;
    const g = gatherCapture(base.frames, latest);
    if (g.frames !== base.frames || base !== was) {
      next ??= { ...cur };
      next[id] = { frames: g.frames, missed: base.missed + g.missed };
    }
  }
  if (next) useAir.setState({ captures: next });
});
