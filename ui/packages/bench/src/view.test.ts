// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import type { ComponentDescription, Project } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { addJumper, benchModel, moveParts } from './layout';
import { parseValue, resistorBands } from './parts3d';
import { benchContext, elevationOf, jumperViews, linkViews, snapPart, terminalNear, terminalPoint, terminalTargets, type BenchCatalogEntry } from './view';

const CATALOG = resolve(__dirname, '../../../../catalog');
const entry = (ref: string): BenchCatalogEntry | undefined => {
  const name = ref.split('/')[1]!;
  try {
    return { description: JSON.parse(readFileSync(resolve(CATALOG, 'components', name, 'component.json'), 'utf-8')) as ComponentDescription };
  } catch {
    return undefined;
  }
};

function project(): Project {
  return {
    schema_version: 1,
    kind: 'project',
    name: 'bench',
    components: [
      { id: 'BB1', component: 'emblink/breadboard_830' },
      { id: 'R1', component: 'emblink/resistor', params: { resistance: '220' } },
      { id: 'D1', component: 'emblink/led', params: { color: 'red' } },
    ],
    nets: {},
    layout: { bench: { parts: { BB1: { x: 0, y: 0 }, R1: { x: 120, y: 60 }, D1: { x: 140, y: 60 } } } },
  };
}

describe('resistor colour code', () => {
  it('reads the bands from the resistance', () => {
    const [red, violet, yellow, brown, black, orange, green] = ['#d63031', '#8e44ad', '#f1c40f', '#7b3f00', '#111111', '#f39c12', '#27ae60'];
    expect(resistorBands(220)).toEqual([red, red, brown, '#c9a227']);
    expect(resistorBands(parseValue('4.7k')!)).toEqual([yellow, violet, red, '#c9a227']);
    expect(resistorBands(parseValue('10k')!).slice(0, 3)).toEqual([brown, black, orange]);
    expect(resistorBands(parseValue('1M')!).slice(0, 3)).toEqual([brown, black, green]);
    expect(resistorBands(4.7).slice(0, 3)).toEqual([yellow, violet, '#c9a227']);
  });
});

describe('the bench view', () => {
  it('snaps a dragged resistor so its legs drop into holes 0.4 in apart', () => {
    const p = project();
    const ctx = benchContext(p, entry);
    const model = benchModel(p, ctx.shapes, ctx.ties);
    const a1 = model.holes.get('BB1.a1')!;
    // dropped 0.8 mm off: it shifts onto a1 and a5
    const s = snapPart(model, ctx, 'R1', a1.x + 5.08 + 0.8, a1.y - 0.5, 0);
    expect(s.holes).toHaveLength(2);
    expect(s.elevation).toBeCloseTo(8.5);
    expect(s.x).toBeCloseTo(a1.x + 5.08);
    expect(s.y).toBeCloseTo(a1.y);
  });

  it('stands a plugged part on the breadboard and wires to the hole under its leg', () => {
    let p = project();
    const ctx = benchContext(p, entry);
    const a1 = benchModel(p, ctx.shapes, ctx.ties).holes.get('BB1.a1')!;
    p = moveParts(p, ctx, { R1: { x: a1.x + 5.08, y: a1.y } });
    const model = benchModel(p, ctx.shapes, ctx.ties);
    expect(elevationOf(model, 'R1')).toBeCloseTo(8.5);
    expect(terminalPoint(model, 'R1.1')).toEqual([a1.x, 8.5, a1.y]);
    expect(terminalNear(model, a1.x + 0.6, a1.y + 0.4)?.ref).toBe('BB1.a1');
    const contacts = terminalTargets(model).map((t) => t.ref);
    expect(contacts).toContain('BB1.a1');
    expect(contacts).not.toContain('R1.1');
    expect(contacts).toContain('D1.A');
  });

  it('rejects nonexistent endpoints before mutation and does not duplicate reversed jumpers', () => {
    const p = project();
    const before = structuredClone(p);
    const ctx = benchContext(p, entry);
    expect(() => addJumper(p, ctx, { from: 'BB1.no-such-hole', to: 'R1.1' })).toThrow('Unknown bench terminal');
    expect(() => addJumper(p, ctx, { from: 'missing.pin', to: 'missing.pin' })).toThrow('Unknown bench terminal');
    expect(p).toEqual(before);
    addJumper(p, ctx, { from: 'R1.1', to: 'D1.A' });
    const wired = structuredClone(p);
    addJumper(p, ctx, { from: 'D1.A', to: 'R1.1' });
    expect(p).toEqual(wired);
  });

  it('places board contacts at the declared mounting height, PCB thickness and header style', () => {
    const p = project();
    const ctx = benchContext(p, entry);
    ctx.shapes.B1 = { package: 'board', size: [30, 20, 2.4], pins: { D0: [5, 5] }, board: { standoff_mm: 7, thickness_mm: 2.4, headers: 'none' } };
    p.boards = [{ id: 'B1', board: 'test/board' }];
    expect(terminalPoint(benchModel(p, ctx.shapes, ctx.ties), 'B1.D0')?.[1]).toBeCloseTo(9.4);
    ctx.shapes.B1.board!.headers = 'female';
    expect(terminalPoint(benchModel(p, ctx.shapes, ctx.ties), 'B1.D0')?.[1]).toBeCloseTo(17.9);
    ctx.shapes.B1.board!.headers = 'male';
    expect(terminalPoint(benchModel(p, ctx.shapes, ctx.ties), 'B1.D0')?.[1]).toBeCloseTo(17.4);
  });

  it('draws the jumpers and what the nets still ask for', () => {
    const p = project();
    const ctx = benchContext(p, entry);
    p.nets = { LED: ['R1.2', 'D1.A'] };
    let model = benchModel(p, ctx.shapes, ctx.ties);
    expect(linkViews(p, model)).toHaveLength(1);
    p.layout!.bench!.wires = [{ from: 'R1.2', to: 'D1.A', color: '#27ae60' }];
    model = benchModel(p, ctx.shapes, ctx.ties);
    expect(linkViews(p, model)).toHaveLength(0);
    const [j] = jumperViews(p, model);
    expect(j!.color).toBe('#27ae60');
  });
});
