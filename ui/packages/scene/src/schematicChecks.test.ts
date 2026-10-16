// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Project } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import type { SceneCatalog } from './SchematicCanvas';
import { checkSchematic } from './schematicChecks';
const catalog: SceneCatalog = { resistor: { name: 'Resistor', svg: '', symbol: { svg: '', width: 60, height: 20, pins: { '1': { x: 0, y: 10, side: 'left' }, '2': { x: 60, y: 10, side: 'right' } } } } };
const project = (): Project => ({ schema_version: 1, kind: 'project', name: 'checks', components: [{ id: 'R1', component: 'resistor', layout: { x: 0, y: 0 } }, { id: 'R2', component: 'resistor', layout: { x: 200, y: 0 } }], nets: { N: ['R1.2', 'R2.1'] }, layout: { wires: [{ net: 'N', points: [[60, 10], [200, 10]] }] } });

describe('schematic drawing and structure checks', () => {
  it('accepts a complete drawing, classifying unused pins as informational', () => {
    const p = project(), before = structuredClone(p);
    const report = checkSchematic(p, catalog);
    expect(report.map((i) => i.code)).toEqual(['unconnected-pins', 'unconnected-pins']);
    expect(report.every((i) => i.severity === 'info')).toBe(true);
    expect(p).toEqual(before);
    expect(checkSchematic(p, catalog)).toEqual(report);
  });
  it('detects invalid references, duplicate memberships and small nets', () => {
    const p = project(); p.nets.OTHER = ['R1.2', 'R3.1', 'R1.NOPE'];
    p.nets.SINGLE = ['R2.2'];
    const report = checkSchematic(p, catalog);
    expect(report.filter((i) => i.code === 'invalid-pin')).toHaveLength(2);
    expect(report.find((i) => i.code === 'duplicate-terminal')?.severity).toBe('error');
    expect(report.some((i) => i.code === 'small-net')).toBe(true);
    expect(report[0]!.severity).toBe('error');
  });
  it('finds disconnected clusters even with every pin touching a drawn wire', () => {
    const p = project(); p.layout!.wires = [{ net: 'N', points: [[60, 10], [100, 10]] }, { net: 'N', points: [[150, 10], [200, 10]] }];
    expect(checkSchematic(p, catalog).find((i) => i.code === 'undrawn-net')).toMatchObject({ net: 'N', severity: 'info', point: { x: 60, y: 10 } });
  });
  it('distinguishes foreign overlapping/end-touching tracks from ordinary crossings', () => {
    const p = project(); p.nets.A = []; p.nets.B = []; p.nets.C = [];
    p.layout!.wires = [{ net: 'A', points: [[0, 100], [100, 100]] }, { net: 'B', points: [[50, 50], [50, 150]] }];
    expect(checkSchematic(p, catalog).filter((i) => i.code === 'ambiguous-contact')).toHaveLength(0);
    p.layout!.wires.push({ net: 'C', points: [[20, 100], [80, 100]] });
    expect(checkSchematic(p, catalog).filter((i) => i.code === 'ambiguous-contact')).toHaveLength(1);
    p.layout!.wires[1]!.points = [[50, 100], [50, 150]];
    expect(checkSchematic(p, catalog).filter((i) => i.code === 'ambiguous-contact')).toHaveLength(3);
  });
  it('reports bounds overlaps, off-grid, orphan and diagonal drawing geometry', () => {
    const p = project(); p.components[1]!.layout = { x: 13, y: 0 };
    p.layout!.wires!.push({ net: 'ABSENT', points: [[10, 10], [50, 10], [100, 50]] });
    const codes = checkSchematic(p, catalog).map((i) => i.code);
    expect(codes).toEqual(expect.arrayContaining(['overlap', 'off-grid', 'orphan-wire', 'invalid-wire', 'wire-through-symbol']));
  });
  it('recognizes declared hidden terminals without inventing a symbol anchor', () => {
    const p = project(); p.nets.N!.push('R1.HIDDEN');
    const report = checkSchematic(p, { resistor: { ...catalog.resistor!, terminals: ['1', '2', 'HIDDEN'] } });
    expect(report.some((i) => i.code === 'invalid-pin')).toBe(false);
    expect(report.some((i) => i.code === 'undrawn-terminal')).toBe(true);
  });
  it('does not invent missing-terminal errors when descriptions are unavailable', () => {
    const p = project(); p.components[0]!.component = 'unknown';
    const report = checkSchematic(p, catalog);
    expect(report.some((i) => i.code === 'missing-symbol')).toBe(true);
    expect(report.some((i) => i.code === 'invalid-pin')).toBe(false);
  });
});
