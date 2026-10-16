// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Project } from '@embsim/protocol';
import { describe, expect, it, vi } from 'vitest';
import { act, render, screen } from '@testing-library/react';
import { ToastProvider, TooltipProvider } from '@embsim/design';
import { useProject } from '../../stores/project';
import { applyLayoutFields, LayoutStudio } from './SchematicTools';
import { diffProject } from '../../stores/projectDiff';
const base = (): Project => ({ schema_version: 1, kind: 'project', name: 'safe',
  components: [{ id: 'R1', component: 'resistor', params: { resistance: 1000 }, layout: { x: 0, y: 0 } }],
  boards: [{ id: 'B1', board: 'board', firmware: { image: 'firmware.elf' }, layout: { x: 100, y: 0 } }],
  nets: { signal: ['R1.1', 'B1.PA0'] }, layout: { labels: [{ net: 'signal', x: 50, y: 0 }] } });
describe('layout transaction boundary', () => {
  it('accepts placement/wires but rejects accidental planner changes to the circuit', () => {
    const before = base(); const proposed = base();
    proposed.components[0]!.layout = { x: 80, y: 100, rotation: 90 };
    proposed.components[0]!.params = { resistance: 1 };
    proposed.boards![0]!.layout = { x: 300, y: 100 };
    proposed.boards![0]!.firmware = { image: 'wrong.elf' };
    proposed.nets = { wrong: ['R1.1'] };
    proposed.layout = { wires: [{ net: 'signal', points: [[80, 100], [300, 100]] }], labels: [] };
    const after = applyLayoutFields(structuredClone(before), proposed);
    expect(after.nets).toEqual(before.nets);
    expect(after.boards![0]!.firmware).toEqual(before.boards![0]!.firmware);
    expect(after.components[0]!.params).toEqual(before.components[0]!.params);
    expect(after.layout!.labels).toEqual(before.layout!.labels);
    const edits = diffProject(before, after);
    expect(edits.length).toBeGreaterThan(0);
    expect(edits.every((e) => 'set_layout' in e)).toBe(true);
    expect(after.components[0]!.layout).toEqual(proposed.components[0]!.layout);
  });
});


describe('layout preview revision guard', () => {
  it('disables Apply if the live project changes after planning', () => {
    const source = base();
    useProject.setState({ project: source, editable: true, busy: false });
    render(<TooltipProvider><ToastProvider><LayoutStudio preview={{ source,
      result: { project: base(), moved: 1, routed: 1, unresolved: 0, warnings: [] }, label: 'Auto layout' }}
      onPreview={vi.fn()} onApplied={vi.fn()} /></ToastProvider></TooltipProvider>);
    expect((screen.getByRole('button', { name: 'Apply layout' }) as HTMLButtonElement).disabled).toBe(false);
    act(() => useProject.setState({ project: structuredClone(source) }));
    expect((screen.getByRole('button', { name: 'Apply layout' }) as HTMLButtonElement).disabled).toBe(true);
    expect(screen.getByRole('alert').textContent).toContain('Generate a fresh preview');
  });
});
