// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { NetState, Project } from '@embsim/protocol';
import type { SceneCatalog } from './SchematicCanvas';

export interface ElectricalIssue { net: string; code: 'driver-conflict' | 'floating-input' | 'unknown-input'; title: string; detail: string }

/** Report observed states, never infer a driver conflict from X alone. Analog
 * nodes without digital receivers are not classified as floating from voltage. */
export function liveElectricalIssues(project: Project, catalog: SceneCatalog, states: Record<string, NetState>): ElectricalIssue[] {
  const parts = new Map(project.components.map((c) => [c.id, catalog[c.component]]));
  return Object.entries(project.nets).flatMap(([net, pins]): ElectricalIssue[] => {
    const state = states[net];
    if (!state) return [];
    if (state.contention) return [{ net, code: 'driver-conflict', title: `${net}: conflicting drivers`, detail: 'The engine reports contention. Inspect connected output enables and driven levels before continuing.' }];
    const inputs = state.receivers ?? pins.filter((ref) => {
      const dot = ref.indexOf('.');
      const pin = parts.get(ref.slice(0, dot))?.pins?.find((p) => p.name === ref.slice(dot + 1) || p.aliases?.includes(ref.slice(dot + 1)));
      return pin?.model?.kind === 'digital_in' || pin?.model?.kind === 'digital_io';
    });
    if (!inputs.length) return [];
    if (state.logic === 'Z') return [{ net, code: 'floating-input', title: `${net}: floating input`, detail: `${inputs.join(', ')} receives Z (high impedance). Check the driver, pull resistor and input configuration.` }];
    if (state.logic === 'X') return [{ net, code: 'unknown-input', title: `${net}: unknown input level`, detail: `${inputs.join(', ')} receives X. This can mean an uninitialized signal or an indeterminate voltage; it does not by itself prove a driver conflict.` }];
    return [];
  }).sort((a, b) => Number(b.code === 'driver-conflict') - Number(a.code === 'driver-conflict') || a.net.localeCompare(b.net));
}
