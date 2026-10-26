// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, it, expect } from 'vitest';
import { blockDependencies, blockPortOrder, hierarchyOf, sheetAt, type Project } from '@embsim/protocol';
import { connectSheet, editSheet, exposePort, extractBlock, renamePort, routeSheet } from './hierarchyEditing';

const flat = (): Project => ({
  schema_version: 1,
  kind: 'project',
  name: 'Divider',
  components: [
    { id: 'V', component: 'emblink/vsource' },
    { id: 'RT', component: 'emblink/resistor', layout: { x: 100, y: 100 } },
    { id: 'RB', component: 'emblink/resistor', layout: { x: 240, y: 100 } },
    { id: 'G', component: 'emblink/gnd' },
  ],
  nets: { IN: ['V.+', 'RT.1'], OUT: ['RT.2', 'RB.1'], GND: ['RB.2', 'V.-', 'G.GND'] },
  probes: [
    { id: 'out', target: 'net:OUT', kind: 'analog' },
    { id: 'part', target: 'device:RT.i', kind: 'value' },
  ],
});

describe('editable hierarchy', () => {
  it('extracts a real internal net and retains boundary connections and observations', () => {
    const p = flat(),
      id = extractBlock(p, '', ['RT', 'RB'], 'divider');
    expect(id).toBe('X1');
    expect(p.components.map((c) => c.id)).toEqual(['V', 'G', 'X1']);
    expect(p.nets).toEqual({ IN: ['V.+', 'X1.IN'], GND: ['V.-', 'G.GND', 'X1.GND'] });
    expect(p.blocks?.divider?.nets.OUT).toEqual(['RT.2', 'RB.1']);
    expect(p.probes?.map((pr) => pr.target)).toEqual(['net:X1.OUT', 'device:X1.RT.i']);
    expect(hierarchyOf(p).find((n) => n.path === 'X1')?.nets).toEqual({ IN: 'IN', OUT: 'X1.OUT', GND: 'GND' });
  });
  it('isolates repeated local names unless ports connect them', () => {
    const p = flat();
    extractBlock(p, '', ['RT', 'RB'], 'divider');
    p.components.push({ id: 'X2', component: 'block/divider' });
    const tree = hierarchyOf(p);
    expect(tree.find((n) => n.path === 'X2')?.nets.OUT).toBe('X2.OUT');
    expect(tree.find((n) => n.path === 'X1')?.nets.OUT).toBe('X1.OUT');
    expect(hierarchyOf(p, 'X1.OUT').map((n) => n.path)).toEqual(['X1']);
  });
  it('renames ports through all parent net references and parent exported mappings', () => {
    const p = flat();
    extractBlock(p, '', ['RT', 'RB'], 'divider');
    p.blocks!.stage = {
      name: 'Stage',
      components: [{ id: 'D', component: 'block/divider' }],
      nets: { IN: ['D.IN'] },
      ports: { IN: 'D.IN' },
    };
    p.components.push({ id: 'A', component: 'block/stage' });
    renamePort(p, 'divider', 'IN', 'INPUT');
    expect(p.nets.IN).toContain('X1.INPUT');
    expect(p.blocks!.stage!.ports.IN).toBe('D.INPUT');
    expect(p.blocks!.stage!.nets.IN).toEqual(['D.INPUT']);
    expect(p.blocks!.divider!.ports.INPUT).toBe('RT.1');
  });
  it('edits shared definitions by instance path and exposes unconnected pins as local nets', () => {
    const p = flat();
    extractBlock(p, '', ['RT', 'RB'], 'divider');
    p.components.push({ id: 'X2', component: 'block/divider' });
    editSheet(p, 'X1', (s) => {
      s.components[0]!.params = { resistance: 2200 };
    });
    expect(sheetAt(p, 'X2').sheet.components[0]!.params?.resistance).toBe(2200);
    const b = p.blocks!.divider!;
    exposePort(b, 'SENSE', 'RT.2');
    expect(b.ports.SENSE).toBe('RT.2');
    expect(
      Object.values(b.nets)
        .flat()
        .filter((pin) => pin === 'RT.2'),
    ).toHaveLength(1);
  });
  it('rejects recursive placement including unused definitions', () => {
    const p = flat();
    extractBlock(p, '', ['RT', 'RB'], 'divider');
    expect(() =>
      editSheet(p, '@divider', (s) => s.components.push({ id: 'LOOP', component: 'block/divider' })),
    ).toThrow('recursive');
  });
  it('merges nets without duplicating pins and remaps drawings', () => {
    const p = flat();
    p.layout = {
      wires: [
        {
          net: 'IN',
          points: [
            [0, 0],
            [10, 0],
          ],
        },
        {
          net: 'OUT',
          points: [
            [10, 0],
            [20, 0],
          ],
        },
      ],
    };
    connectSheet(
      p,
      { component: 'RT', pin: '1', point: { x: 0, y: 0 } },
      { component: 'RT', pin: '2', point: { x: 10, y: 0 } },
      [
        [0, 0],
        [10, 0],
      ],
    );
    expect(p.nets.OUT).toBeUndefined();
    expect(p.nets.IN).toEqual(['V.+', 'RT.1', 'RT.2', 'RB.1']);
    expect(p.layout.wires?.every((w) => w.net === 'IN')).toBe(true);
  });
  it('preserves an exported parent connection while extracting its only terminal', () => {
    const p = flat();
    extractBlock(p, '', ['RT', 'RB'], 'divider');
    const id = extractBlock(p, '@divider', ['RT'], 'upper');
    expect(p.blocks!.divider!.ports.IN).toBe(`${id}.IN`);
    expect(p.blocks!.upper!.ports.IN).toBe('RT.1');
    expect(hierarchyOf(p).map((n) => n.path)).toContain('X1.X1');
  });
});

it('keeps template bindings when extracting from a parameterized definition', () => {
  const p = flat();
  extractBlock(p, '', ['RT', 'RB'], 'divider');
  p.blocks!.divider!.params = { r: { type: 'number', default: 1000 } };
  p.blocks!.divider!.components[0]!.params = { resistance: '$r' };
  const id = extractBlock(p, '@divider', ['RT'], 'upper');
  expect(p.blocks!.upper!.params?.r?.default).toBe(1000);
  expect(p.blocks!.divider!.components.find((c) => c.id === id)?.params?.r).toBe('$r');
});
it('repairs labels onto routed geometry without changing connectivity', () => {
  const p = flat();
  p.layout = { labels: [{ net: 'OUT', x: 0, y: 0 }] };
  const symbol = {
    svg: 'test.svg',
    width: 60,
    height: 20,
    pins: { '1': { x: 0, y: 10, side: 'left' as const }, '2': { x: 60, y: 10, side: 'right' as const } },
  };
  const next = routeSheet(p, { 'emblink/resistor': { name: 'Resistor', symbol, svg: '' } }, new Set(['OUT']));
  expect(next.nets).toEqual(p.nets);
  expect(next.layout?.labels?.[0]).toMatchObject({ x: 160, y: 110 });
});

it('preserves explicit anchors for numeric port names across rename and append', () => {
  const p = flat();
  extractBlock(p, '', ['RT', 'RB'], 'divider');
  const b = p.blocks!.divider!;
  b.ports = { '2': 'RT.2', '1': 'RT.1' };
  b.layout = { ...b.layout, port_order: ['2', '1'] };
  expect(blockPortOrder(b)).toEqual(['2', '1']);
  renamePort(p, 'divider', '2', 'OUT');
  expect(blockPortOrder(b)).toEqual(['OUT', '1']);
  exposePort(b, '3', 'RB.2');
  expect(blockPortOrder(b)).toEqual(['OUT', '1', '3']);
});

it('exports only the selected reusable dependency closure', () => {
  const p = flat();
  extractBlock(p, '', ['RT', 'RB'], 'divider');
  p.blocks!.unrelated = { name: 'Private unrelated design', components: [], nets: {}, ports: {} };
  extractBlock(p, '@divider', ['RT'], 'upper');
  expect(Object.keys(blockDependencies(p, 'divider'))).toEqual(['divider', 'upper']);
  expect(Object.keys(blockDependencies(p, 'upper'))).toEqual(['upper']);
});
