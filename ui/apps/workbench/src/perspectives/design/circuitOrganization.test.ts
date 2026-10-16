// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Project } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { assignGroup, partsListCsv } from './circuitOrganization';
import { diffProject } from '../../stores/projectDiff';
const base = (): Project => ({ schema_version: 1, kind: 'project', name: 'parts', components: ['R1', 'R2', 'R3'].map((id) => ({ id, component: 'test/resistor', params: { resistance: '1k' } })), boards: [{ id: 'B1', board: 'test/board', firmware: { image: 'secret.elf' } }], nets: { signal: ['R1.a', 'R2.a'] } });
describe('circuit organization', () => {
  it('reassigns unique membership and drops empty groups without touching the circuit', () => {
    const p = base(); const nets = structuredClone(p.nets);
    assignGroup(p, ['R1', 'R2', 'R2', 'missing'], ' Input ');
    assignGroup(p, ['R2', 'B1'], 'Controller');
    expect(p.layout?.groups).toEqual([{ id: 'group-1', name: 'Input', members: ['R1'] }, { id: 'group-2', name: 'Controller', members: ['B1', 'R2'] }]);
    assignGroup(p, ['R1', 'R3'], 'Output', 'group-2');
    expect(p.layout?.groups).toEqual([{ id: 'group-2', name: 'Output', members: ['R1', 'R3'] }]);
    expect(p.nets).toEqual(nets);
    expect(() => assignGroup(p, [], 'Empty')).toThrow();
    expect(() => assignGroup(p, ['R1'], '  ')).toThrow();
  });
  it('sends reversible, presentation-only group and lock transactions', () => {
    const before = base(), after = structuredClone(before);
    assignGroup(after, ['R1', 'B1'], 'Input'); after.components[0]!.layout = { x: 0, y: 0, locked: true };
    const forward = diffProject(before, after), backward = diffProject(after, before);
    expect(forward).toHaveLength(2); expect(backward).toHaveLength(2);
    expect([...forward, ...backward].every((e) => 'set_layout' in e)).toBe(true);
    expect(backward).toContainEqual({ set_layout: { groups: [] } });
    expect(backward).toContainEqual({ set_layout: { id: 'R1', layout: { x: 0, y: 0, locked: false } } });
  });
  it('exports deterministic aggregated top-level inventory without firmware and quotes spreadsheet formulas', () => {
    const p = base(); p.components[2]!.params = { resistance: '10k' };
    p.components[0]!.id = '=SUM(1,2)';
    const csv = partsListCsv(p);
    expect(csv).toContain('"2","\'=SUM(1,2), R2"');
    expect(csv).toContain('"Board","test/board"'); expect(csv).not.toContain('secret.elf');
    expect(csv).toContain('""resistance""');
    p.components.reverse(); expect(partsListCsv(p)).toBe(csv);
  });
});
