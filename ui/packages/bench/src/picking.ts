// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { TerminalHit } from './Bench3D';

export interface ProjectedTerminal {
  terminal: TerminalHit;
  x: number;
  y: number;
  /** Normalized device depth: only the visible camera frustum is eligible. */
  depth: number;
}

export type TerminalPick = { kind: 'none' } | { kind: 'ambiguous' } | { kind: 'terminal'; terminal: TerminalHit };

/** CSS-pixel targeting keeps the hit area usable at every zoom, without
 * guessing between contacts which project almost on top of one another. */
export function pickTerminal(candidates: ProjectedTerminal[], x: number, y: number, radius = 11): TerminalPick {
  const near = candidates
    .filter((c) => Number.isFinite(c.x) && Number.isFinite(c.y) && c.depth >= -1 && c.depth <= 1)
    .map((c) => ({ ...c, distance: Math.hypot(c.x - x, c.y - y) }))
    .filter((c) => c.distance <= radius)
    .sort((a, b) => a.distance - b.distance || a.terminal.ref.localeCompare(b.terminal.ref));
  const first = near[0];
  if (!first) return { kind: 'none' };
  // A tiny difference is pointer noise, not a deliberate choice of pin.
  if (near[1] && near[1].distance - first.distance < 1.5) return { kind: 'ambiguous' };
  return { kind: 'terminal', terminal: first.terminal };
}
