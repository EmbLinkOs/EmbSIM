// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { hierarchyOf, type Project } from '@embsim/protocol';
import {
  copySheet,
  pasteSheet,
  portUses,
  removeParameter,
  removePort,
  saveParameter,
  setBus,
  validateParameter,
} from './hierarchyTools';
import { projectSearch } from './projectSearch';
const project = (): Project => ({
  schema_version: 1,
  kind: 'project',
  name: 'Tools',
  components: [
    { id: 'A', component: 'block/divider', params: { resistance: '2k' } },
    { id: 'B', component: 'block/divider' },
  ],
  nets: { OUT: ['A.OUT', 'B.IN'] },
  blocks: {
    divider: {
      name: 'Divider',
      params: { resistance: { type: 'number', default: '1k' } },
      components: [
        {
          id: 'R1',
          component: 'emblink/resistor',
          params: { resistance: '$resistance' },
          layout: { x: 100, y: 100 },
        },
        { id: 'R2', component: 'emblink/resistor' },
      ],
      nets: { IN: ['R1.1'], OUT: ['R1.2', 'R2.1'], GND: ['R2.2'] },
      ports: { IN: 'R1.1', OUT: 'R1.2', GND: 'R2.2' },
      layout: { port_order: ['IN', 'OUT', 'GND'] },
    },
  },
});

describe('project-wide schematic tools', () => {
  it('copies between sheets without joining boundary nets or aliasing the original document', () => {
    const p = project(),
      clip = copySheet(p, 'A', ['R1', 'R2']);
    const ids = pasteSheet(p, 'B', clip);
    expect(ids).toEqual(['R3', 'R4']);
    expect(p.blocks!.divider!.nets.N1).toEqual(['R3.2', 'R4.1']);
    expect(p.blocks!.divider!.nets.OUT).toEqual(['R1.2', 'R2.1']);
    expect(p.blocks!.divider!.components[2]!.params!.resistance).toBe('$resistance');
    p.blocks!.divider!.components[2]!.params!.resistance = '9k';
    expect(clip.clip.components[0]!.params!.resistance).toBe('$resistance');
  });
  it('carries nested dependencies, rejects recursion and conflicting parameters', () => {
    const p = project(),
      clip = copySheet(p, '', ['A']);
    expect(Object.keys(clip.blocks)).toEqual(['divider']);
    expect(() => pasteSheet(structuredClone(p), 'A', clip)).toThrow('recursive');
    const c = copySheet(p, 'A', ['R1']);
    p.blocks!.other = {
      name: 'Other',
      components: [],
      nets: {},
      ports: {},
      params: { resistance: { type: 'boolean' } },
    };
    expect(() => pasteSheet(p, '@other', c)).toThrow('different declaration');
    expect(() => pasteSheet(p, '', c)).toThrow('parameterized');
  });
  it('compares imported definitions independent of JSON object order', () => {
    const p = project(),
      c = copySheet(p, '', ['A']);
    c.blocks.divider = Object.fromEntries(Object.entries(c.blocks.divider!).reverse()) as NonNullable<
      typeof c.blocks.divider
    >;
    expect(pasteSheet(p, '', c)).toEqual(['X1']);
    c.blocks.divider!.name = 'Different';
    expect(() => pasteSheet(p, '', c)).toThrow('differs');
  });
  it('refuses removal of connected/probed ports, removes unused ports and keeps order stable', () => {
    const p = project();
    expect(portUses(p, 'divider', 'OUT')).toContain('Main: A.OUT on OUT');
    expect(() => removePort(p, 'divider', 'OUT')).toThrow('Reconnect');
    p.probes = [{ id: 'watch', kind: 'analog', target: 'pin:B.GND' }];
    expect(() => removePort(p, 'divider', 'GND')).toThrow('Probe watch');
    p.probes = [];
    removePort(p, 'divider', 'GND');
    expect(p.blocks!.divider!.layout!.port_order).toEqual(['IN', 'OUT']);
  });
  it('renames declaration, instance overrides and parameter forwarding together', () => {
    const p = project();
    saveParameter(p, 'divider', 'resistance', 'upper', {
      type: 'number',
      min: 10,
      max: '10k',
      default: '1k',
    });
    expect(p.components[0]!.params).toEqual({ upper: '2k' });
    expect(p.blocks!.divider!.components[0]!.params!.resistance).toBe('$upper');
    expect(p.blocks!.divider!.params!.resistance).toBeUndefined();
    expect(() => removeParameter(p, 'divider', 'upper')).toThrow('bindings');
    saveParameter(p, 'divider', null, 'unused', {
      type: 'boolean',
      default: false,
    });
    removeParameter(p, 'divider', 'unused');
    expect(p.blocks!.divider!.params!.unused).toBeUndefined();
  });
  it('validates scalar, enum and table declarations without coercing bad defaults', () => {
    expect(() =>
      validateParameter('r', {
        type: 'number',
        default: '1k',
        min: 0,
        max: '2k',
      }),
    ).not.toThrow();
    for (const spec of [
      { type: 'integer', default: '1.5' },
      { type: 'number', default: Infinity },
      { type: 'boolean', default: 'false' },
      { type: 'enum', enum: ['A', 'A'], default: 'A' },
      { type: 'table', default: [[0, NaN]] },
      { type: 'number', min: 10, max: 1 },
    ])
      expect(() => validateParameter('r', spec as Parameters<typeof validateParameter>[1])).toThrow();
  });
  it('preserves declared bus ordering and geometry while remaining presentation only', () => {
    const p = project(),
      s = p.blocks!.divider!,
      before = structuredClone(s.nets);
    setBus(s, null, 'DATA', ['OUT', 'IN']);
    const points = structuredClone(s.layout!.buses![0]!.points);
    setBus(s, 'DATA', 'CONTROL', ['IN', 'OUT']);
    expect(s.layout!.buses![0]).toEqual({
      name: 'CONTROL',
      nets: ['IN', 'OUT'],
      points,
    });
    expect(s.nets).toEqual(before);
    expect(() => setBus(s, null, 'CONTROL', ['IN', 'OUT'])).toThrow('already exists');
    expect(() => setBus(s, null, 'BAD', ['IN', 'IN'])).toThrow('distinct');
  });
  it('searches concrete references, ports, values and reusable definitions with separate locations', () => {
    const p = project();
    const hits = projectSearch(p, hierarchyOf(p), 'R1 resistor');
    expect(hits.filter((h) => h.kind === 'component').map((h) => h.path)).toEqual(['A', 'B', '@divider']);
    expect(projectSearch(p, hierarchyOf(p), 'port OUT').map((h) => h.path)).toEqual(['A', 'B', '@divider']);
    expect(projectSearch(p, hierarchyOf(p), '')).toEqual([]);
  });
});
