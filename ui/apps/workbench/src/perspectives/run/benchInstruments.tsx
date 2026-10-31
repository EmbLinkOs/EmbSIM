// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The bench instruments with a front panel of their own (bench-instruments.md
// §3): which catalog part each is, its panel, and how it sits in the Run
// perspective's dock. A panel opens from its part (the inspector, the command
// palette), docked in Run or floating over any perspective.

import { revealDockItem } from '@embsim/design';
import type { ReactNode } from 'react';
import type { Project } from '@embsim/protocol';
import { useFloating } from '../../stores/floating';
import { useLayout } from '../../stores/layout';
import { DS1054Z, Ds1054zPanel } from './Ds1054zPanel';
import { LOGIC8, Logic8Panel } from './Logic8Panel';
import { PPK2, Ppk2Panel } from './Ppk2Panel';

export interface BenchInstrument {
  /** the catalog part */
  component: string;
  /** what the dock item is called after the part's id */
  name: string;
  /** the dock item's id prefix */
  key: string;
  weight: number;
  minSize: number;
  /** the floating window's first size, px */
  size: { width: number; height: number };
  panel(part: string): ReactNode;
}

// in the order they stack in the dock
export const BENCH_INSTRUMENTS: BenchInstrument[] = [
  { component: DS1054Z, name: 'DS1054Z oscilloscope', key: 'ds1054z', weight: 1.6, minSize: 260, size: { width: 900, height: 560 }, panel: (part) => <Ds1054zPanel part={part} /> },
  { component: PPK2, name: 'Power Profiler Kit II', key: 'ppk2', weight: 1.3, minSize: 200, size: { width: 860, height: 480 }, panel: (part) => <Ppk2Panel part={part} /> },
  { component: LOGIC8, name: 'Logic 8', key: 'logic8', weight: 1.1, minSize: 160, size: { width: 860, height: 380 }, panel: (part) => <Logic8Panel part={part} /> },
];

export function benchInstrumentOf(component: string | undefined): BenchInstrument | undefined {
  return BENCH_INSTRUMENTS.find((i) => i.component === component);
}

/** the project's instruments with a front panel: each part and its kind */
export function benchInstrumentParts(project: Project | null | undefined): { part: string; instrument: BenchInstrument }[] {
  const out: { part: string; instrument: BenchInstrument }[] = [];
  for (const instrument of BENCH_INSTRUMENTS)
    for (const c of project?.components ?? []) if (c.component === instrument.component) out.push({ part: c.id, instrument });
  return out;
}

export const dockIdOf = (instrument: BenchInstrument, part: string) => `${instrument.key}-${part}`;
export const titleOf = (instrument: BenchInstrument, part: string) => `${part} · ${instrument.name}`;

/** Shows a part's front panel: brought forward where it floats, else unfolded in Run's dock. */
export function openFrontPanel(instrument: BenchInstrument, part: string): void {
  const floating = useFloating.getState();
  if (floating.floating.includes(part)) {
    floating.raise(part);
    return;
  }
  useLayout.getState().setPerspective('run');
  revealDockItem(dockIdOf(instrument, part));
}
