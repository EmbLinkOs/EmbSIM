// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Project } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { arrange, autoRoute, createLayout, layoutParts, freePlacement, routeConnection } from './autoLayout';
import { pinPosition, rectsIntersect, symbolBounds } from './geometry';
import type { SceneCatalog } from './SchematicCanvas';
const catalog: SceneCatalog = { part: { name: 'Test', svg: '', symbol: { svg: '', width: 60, height: 40, pins: { a: { x: 0, y: 20, side: 'left' }, b: { x: 60, y: 20, side: 'right' } } } } };
function project(): Project {
  return { schema_version: 1, kind: 'project', name: 'test', policy: 'electrical',
    components: ['A', 'B', 'C', 'D'].map((id, i) => ({ id, component: 'part', params: { resistance: 1000 }, layout: { x: i * 180 + 60, y: i % 2 ? 240 : 60 } })),
    boards: [], nets: { signal: ['A.b', 'B.a', 'C.a'], output: ['C.b', 'D.a'] }, layout: { wires: [], labels: [{ net: 'signal', x: 20, y: 20 }] } } as Project;
}
describe('schematic placement and routing', () => {
  it('is deterministic, preserves circuit identity and leaves input untouched', () => {
    const p = project(); const before = structuredClone(p);
    const options = { direction: 'horizontal', spacing: 'comfortable', route: true } as const;
    const first = createLayout(p, catalog, options);
    expect(first).toEqual(createLayout(p, catalog, options));
    expect(p).toEqual(before);
    expect(first.project.nets).toEqual(p.nets);
    expect(first.project.components.map(({ layout: _layout, ...rest }) => rest)).toEqual(p.components.map(({ layout: _layout, ...rest }) => rest));
    expect(first.project.layout?.labels).toEqual(p.layout?.labels);
    expect(first.routed).toBe(3); expect(first.unresolved).toBe(0);
    const parts = layoutParts(first.project, catalog);
    for (let i = 0; i < parts.length; i++) for (const b of parts.slice(i + 1)) {
      expect(rectsIntersect(symbolBounds(parts[i]!.layout, parts[i]!.symbol, 15), symbolBounds(b.layout, b.symbol, 15))).toBe(false);
    }
  });
  it('routes around an intervening symbol with exact unsnapped anchors', () => {
    const p = project();
    p.components = [
      { id: 'A', component: 'part', layout: { x: 20, y: 105 } },
      { id: 'B', component: 'part', layout: { x: 300, y: 105 } },
      { id: 'BLOCK', component: 'part', layout: { x: 160, y: 105 } },
    ];
    p.nets = { signal: ['A.b', 'B.a'] };
    const result = autoRoute(p, catalog);
    expect(result.unresolved).toBe(0);
    const wire = result.project.layout!.wires![0]!;
    expect(wire.points[0]).toEqual([80, 125]);
    expect(wire.points.at(-1)).toEqual([300, 125]);
    for (let i = 1; i < wire.points.length; i++) {
      const [x, y] = wire.points[i - 1]!; const [xx, yy] = wire.points[i]!;
      expect(x === xx || y === yy).toBe(true);
      // No segment may pass through the blocker (including its clearance).
      if (x === xx && x > 155 && x < 225) expect(Math.max(y, yy) <= 100 || Math.min(y, yy) >= 150).toBe(true);
      if (y === yy && y > 100 && y < 150) expect(Math.max(x, xx) <= 155 || Math.min(x, xx) >= 225).toBe(true);
    }
  });
  it('keeps unselected layouts and unrelated manual wiring', () => {
    const p = project(); p.nets.spare = ['D.b'];
    p.layout!.wires = [{ net: 'spare', points: [[660, 260], [700, 260]] }];
    const result = createLayout(p, catalog, { direction: 'vertical', spacing: 'compact', selected: ['A', 'B'], route: true });
    expect(result.project.components.slice(2)).toEqual(p.components.slice(2));
    expect(result.project.layout!.wires).toContainEqual(p.layout!.wires[0]);
    expect(result.project.nets).toEqual(p.nets);
  });
  it('reports missing anchors rather than fabricating a connection', () => {
    const p = project(); p.nets = { broken: ['A.no_such_pin', 'B.a'] };
    const result = autoRoute(p, catalog);
    expect(result.unresolved).toBe(1); expect(result.warnings[0]).toContain('unknown pin');
    expect(result.project.layout!.wires).toHaveLength(0);
  });
  it('preserves rotation/mirror and routes from their transformed pins', () => {
    const p = project(); p.components[0]!.layout = { x: 60, y: 60, rotation: 90, mirror: true };
    const result = createLayout(p, catalog, { direction: 'horizontal', spacing: 'compact', route: true });
    const layout = result.project.components[0]!.layout!;
    expect(layout.rotation).toBe(90); expect(layout.mirror).toBe(true);
    const point = pinPosition(layout, catalog.part!.symbol, 'b')!;
    const points = result.project.layout!.wires!.filter((w) => w.net === 'signal').flatMap((w) => [w.points[0], w.points.at(-1)]);
    expect(points).toContainEqual([point.x, point.y]);
  });
  it('aligns and distributes without modifying nets or parameters', () => {
    const p = project();
    const aligned = arrange(p, catalog, ['A', 'B', 'C'], 'top');
    expect(aligned.project.components.slice(0, 3).map((c) => c.layout!.y)).toEqual([60, 60, 60]);
    const distributed = arrange(p, catalog, ['A', 'B', 'D'], 'distribute-x');
    expect(distributed.project.components[1]!.layout!.x).toBe(330);
    expect(distributed.project.nets).toEqual(p.nets);
  });
});

describe('safe placement and interactive routing', () => {
  it('finds a clear position near an occupied drop location', () => {
    const p = project();
    const layout = freePlacement(p, catalog, catalog.part!.symbol, { x: 90, y: 80 });
    for (const c of layoutParts(p, catalog)) expect(rectsIntersect(symbolBounds(layout, catalog.part!.symbol), symbolBounds(c.layout, c.symbol, 20))).toBe(false);
    expect(Number.isInteger(layout.x / 10)).toBe(true); expect(Number.isInteger(layout.y / 10)).toBe(true);
  });
  it('reports an impossible endpoint instead of drawing through a covering symbol', () => {
    const p = project();
    p.components.push({ id: 'COVER', component: 'part', layout: { x: 100, y: 60 } });
    expect(routeConnection(p, catalog, { component: 'A', point: { x: 120, y: 80 } }, { component: 'B', point: { x: 240, y: 260 } })).toBeNull();
  });
  it('handles disconnected parts and empty sheets without inventing nets', () => {
    const p = project(); p.nets = {};
    const result = createLayout(p, catalog, { direction: 'vertical', spacing: 'compact', route: true });
    expect(result.project.nets).toEqual({}); expect(result.project.layout!.wires).toEqual([]);
    expect(createLayout({ ...p, components: [] }, catalog, { direction: 'horizontal', spacing: 'compact', route: true }).moved).toBe(0);
  });
});


describe('foreign-net track reservation', () => {
  it('keeps separate nets off the same collinear segment', () => {
    const p = project();
    p.components = [p.components[0]!, { ...p.components[1]!, layout: { x: 300, y: 60 } }];
    p.nets = { inner: ['A.b', 'B.a'], outer: ['A.a', 'B.b'] };
    const result = autoRoute(p, catalog);
    expect(result.unresolved).toBe(0);
    const [first, second] = result.project.layout!.wires!;
    for (let i = 1; i < first!.points.length; i++) for (let j = 1; j < second!.points.length; j++) {
      const a = first!.points[i - 1]!; const b = first!.points[i]!;
      const c = second!.points[j - 1]!; const d = second!.points[j]!;
      if (a[0] === b[0] && c[0] === d[0] && a[0] === c[0]) expect(Math.min(Math.max(a[1], b[1]), Math.max(c[1], d[1])) <= Math.max(Math.min(a[1], b[1]), Math.min(c[1], d[1]))).toBe(true);
      if (a[1] === b[1] && c[1] === d[1] && a[1] === c[1]) expect(Math.min(Math.max(a[0], b[0]), Math.max(c[0], d[0])) <= Math.max(Math.min(a[0], b[0]), Math.min(c[0], d[0]))).toBe(true);
    }
  });
});

describe('placement locks', () => {
  it('keeps locked parts fixed as routing obstacles during full and selected layout', () => {
    const p = project(); p.components[1]!.layout!.locked = true;
    for (const selected of [undefined, ['A', 'B', 'C']]) {
      const result = createLayout(p, catalog, { direction: 'horizontal', spacing: 'comfortable', route: true, selected });
      expect(result.project.components[1]!.layout).toEqual(p.components[1]!.layout);
      const fixed = layoutParts(result.project, catalog).find((part) => part.id === 'B')!;
      for (const part of layoutParts(result.project, catalog).filter((part) => part.id !== 'B')) expect(rectsIntersect(symbolBounds(fixed.layout, fixed.symbol), symbolBounds(part.layout, part.symbol))).toBe(false);
      expect(result.project.nets).toEqual(p.nets);
    }
  });
  it('refuses an entire arrangement containing a locked member', () => {
    const p = project(); p.components[0]!.layout!.locked = true;
    const result = arrange(p, catalog, ['A', 'B', 'C'], 'left');
    expect(result.project).toEqual(p); expect(result.moved).toBe(0); expect(result.warnings).toHaveLength(1);
  });
});
