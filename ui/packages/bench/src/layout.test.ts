// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Project } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { addJumper, benchModel, dressJumper, jumperNet, legsOf, moveParts, previewInsertion, reconnectJumper, removeJumper, traceBench, turn, unbuilt, type BenchContext } from './layout';
import { PITCH, type BenchShape } from './shape';

// a breadboard of six strips (a-e over f-j would be more; one half is enough):
// columns 1..6, rows a..e, holes 2.54 mm apart from (1.27, 1.27)
function breadboard(): BenchShape {
  const pins: Record<string, [number, number]> = {};
  for (let c = 1; c <= 6; c++) for (const [k, r] of [...'abcde'].entries()) pins[`${r}${c}`] = [1.27 + (c - 1) * PITCH, 1.27 + k * PITCH];
  return { package: 'breadboard', size: [6 * PITCH, 5 * PITCH, 8.5], pins, wiringAid: true };
}
const resistor: BenchShape = { package: 'axial', size: [10.16, 2.4, 2.4], pins: { '1': [0, 1.2], '2': [10.16, 1.2] } };
const header: BenchShape = { package: 'board', size: [10, 10, 1.6], pins: { D13: [5, 5] } };
const ties = { BB1: Array.from({ length: 6 }, (_, i) => [...'abcde'].map((r) => `${r}${i + 1}`)) };
const ctx: BenchContext = { shapes: { BB1: breadboard(), R1: resistor, R2: resistor, U1: header }, ties };

function project(): Project {
  return {
    schema_version: 1,
    kind: 'project',
    name: 't',
    boards: [{ id: 'U1', board: 'emblink/arduino_uno' }],
    components: [
      { id: 'BB1', component: 'emblink/breadboard_830' },
      { id: 'R1', component: 'emblink/resistor' },
      { id: 'R2', component: 'emblink/resistor' },
    ],
    nets: {},
    layout: { bench: { parts: { BB1: { x: 7.62, y: 6.35 }, U1: { x: 100, y: 100 }, R1: { x: 200, y: 0 }, R2: { x: 200, y: 20 } } } },
  };
}

describe('wire appearance, reconnection and tracing', () => {
  it('dresses a wire without changing its electrical graph and rejects invalid proposals atomically', () => {
    const p = addJumper(project(), ctx, { from: 'R1.1', to: 'U1.D13' });
    const nets = structuredClone(p.nets);
    dressJumper(p, 0, { color: '#16a085', points: [[20, 15, 40]], rise_mm: 10 });
    expect(p.nets).toEqual(nets);
    expect(p.layout?.bench?.wires?.[0]?.points).toEqual([[20, 15, 40]]);
    const before = structuredClone(p);
    expect(() => dressJumper(p, 0, { points: [[0, -1, 0]] })).toThrow();
    expect(() => dressJumper(p, 0, { rise_mm: Infinity })).toThrow();
    expect(() => reconnectJumper(p, ctx, 0, 'R1.1', 'missing.pin')).toThrow();
    expect(p).toEqual(before);
    const reconnected = reconnectJumper(p, ctx, 0, 'R1.2', 'U1.D13');
    expect(reconnected.layout?.bench?.wires?.[0]).toEqual({ ...before.layout?.bench?.wires?.[0], from: 'R1.2' });
    expect(jumperNet(reconnected, reconnected.layout!.bench!.wires![0]!)).toBeDefined();
    expect(Object.values(reconnected.nets).some((pins) => pins.includes('R1.1') && pins.includes('U1.D13'))).toBe(false);
    expect(p).toEqual(before);
  });
  it('traces all contacts of a connected strip without crossing to adjacent strips', () => {
    const p = addJumper(project(), ctx, { from: 'BB1.a1', to: 'U1.D13' });
    const wire = p.layout!.bench!.wires![0]!;
    const trace = traceBench(p, benchModel(p, ctx.shapes, ctx.ties), jumperNet(p, wire)!);
    expect(trace.has('BB1.e1')).toBe(true);
    expect(trace.has('U1.D13')).toBe(true);
    expect(trace.has('BB1.a2')).toBe(false);
  });
});

// hole a1 is at (1.27, 1.27) relative to BB1's corner (0, 0): the board's centre is (7.62, 6.35)
const hole = (col: number, row: number): [number, number] => [1.27 + (col - 1) * PITCH, 1.27 + row * PITCH];
// R1's centre so that its leg 1 is in the hole of (col, row) and it runs along the row
const along = (col: number, row: number) => ({ x: hole(col, row)[0] + 5.08, y: hole(col, row)[1] });

describe('bench geometry', () => {
  it('turns clockwise seen from above', () => {
    expect(turn(1, 0, 90)).toEqual([-0, 1]);
    expect(turn(1, 0, 180)).toEqual([-1, -0]);
    expect(turn(1, 0, 270)).toEqual([0, -1]);
  });

  it('places legs from the centre and the turn', () => {
    const legs = legsOf(resistor, { x: 10, y: 10, rotation: 90 });
    expect(legs['1']![0]).toBeCloseTo(10.0);
    expect(legs['1']![1]).toBeCloseTo(4.92);
    expect(legs['2']![1]).toBeCloseTo(15.08);
  });

  it('finds the holes the legs are in', () => {
    const p = project();
    p.layout!.bench!.parts!.R1 = along(1, 0);
    const m = benchModel(p, ctx.shapes, ctx.ties);
    expect(m.plugs.get('R1.1')).toBe('BB1.a1');
    // the other leg is 0.4 in along: four strips on, in a5
    expect(m.plugs.get('R1.2')).toBe('BB1.a5');
  });
});

describe('building on the bench', () => {
  it('previews each leg, distinguishes shared strips from occupied holes and rejects without mutation', () => {
    const p = moveParts(project(), ctx, { R1: along(1, 0) });
    const before = structuredClone(p);
    const model = benchModel(p, ctx.shapes, ctx.ties);
    const blocked = previewInsertion(model, 'R2', { ...along(1, 0), rotation: 0 });
    expect(blocked.valid).toBe(false);
    expect(blocked.contacts[0]).toMatchObject({ pin: 'R2.1', hole: 'BB1.a1', occupiedBy: ['R1.1'] });
    expect(() => moveParts(p, ctx, { R2: along(1, 0) })).toThrow('BB1.a1 is occupied');
    expect(p).toEqual(before);
    const sharedStrip = previewInsertion(model, 'R2', { ...along(1, 1), rotation: 0 });
    expect(sharedStrip.valid).toBe(true);
    expect(sharedStrip.contacts.map((contact) => contact.hole)).toEqual(['BB1.b1', 'BB1.b5']);
    expect(p).toEqual(before);
  });

  it('reserves jumper holes, permits another hole of the same strip and rejects occupied endpoints', () => {
    const p = addJumper(project(), ctx, { from: 'BB1.a1', to: 'U1.D13' });
    const before = structuredClone(p);
    expect(() => moveParts(p, ctx, { R1: along(1, 0) })).toThrow('jumper 1');
    expect(() => addJumper(p, ctx, { from: 'BB1.a1', to: 'R2.1' })).toThrow('occupied');
    expect(p).toEqual(before);
    moveParts(p, ctx, { R1: along(1, 1) });
    expect(Object.values(p.nets).find((pins) => pins.includes('U1.D13'))).toContain('R1.1');
  });

  it('allows an atomic swap of two inserted parts without treating their old positions as occupied', () => {
    const p = moveParts(project(), ctx, { R1: along(1, 0), R2: along(1, 2) });
    moveParts(p, ctx, { R1: along(1, 2), R2: along(1, 0) });
    const model = benchModel(p, ctx.shapes, ctx.ties);
    expect(model.plugs.get('R1.1')).toBe('BB1.c1');
    expect(model.plugs.get('R2.1')).toBe('BB1.a1');
  });

  it('carries inserted parts through all four breadboard rotations without changing contacts or nets', () => {
    const p = moveParts(project(), ctx, { R1: along(1, 0), R2: along(1, 2) });
    const nets = structuredClone(p.nets);
    const plugs = [...benchModel(p, ctx.shapes, ctx.ties).plugs];
    for (const rotation of [90, 180, 270, 0] as const) {
      moveParts(p, ctx, { BB1: { x: 50, y: 50, rotation } });
      expect([...benchModel(p, ctx.shapes, ctx.ties).plugs]).toEqual(plugs);
      expect(p.nets).toEqual(nets);
      expect(p.layout!.bench!.parts!.R1!.rotation).toBe(rotation);
    }
  });

  it('does not plug a board\'s raised header sockets into a breadboard below it', () => {
    const p = project();
    p.layout!.bench!.parts!.U1 = { x: hole(1, 0)[0], y: hole(1, 0)[1] };
    expect(benchModel(p, ctx.shapes, ctx.ties).plugs.has('U1.D13')).toBe(false);
  });

  it('plugs a part in: each leg joins its strip', () => {
    const p = moveParts(project(), ctx, { R1: along(1, 0) });
    expect(Object.values(p.nets).map((n) => [...n].sort())).toEqual([
      ['BB1.a1', 'R1.1'],
      ['BB1.a5', 'R1.2'],
    ]);
  });

  it('joins two parts through a strip into one net', () => {
    let p = moveParts(project(), ctx, { R1: along(1, 0) });
    // R2 from c5 (strip 5, as R1's leg 2) along to... it runs off the board: its leg 2 is in no hole
    p = moveParts(p, ctx, { R2: along(5, 2) });
    const net = Object.values(p.nets).find((n) => n.includes('R1.2'))!;
    expect([...net].sort()).toEqual(['BB1.a5', 'BB1.c5', 'R1.2', 'R2.1']);
    expect(Object.values(p.nets).some((n) => n.includes('R2.2'))).toBe(false);
  });

  it('unplugs a part lifted off: its pins leave, the strip keeps the rest', () => {
    let p = moveParts(project(), ctx, { R1: along(1, 0) });
    p = moveParts(p, ctx, { R2: along(5, 2) });
    p = moveParts(p, ctx, { R1: { x: 200, y: 0 } });
    expect(Object.values(p.nets).flat()).not.toContain('R1.1');
    expect(Object.values(p.nets).flat()).not.toContain('R1.2');
    // R2 alone on strip 5: its leg and its hole, nothing else
    expect(Object.values(p.nets).map((n) => [...n].sort())).toEqual([['BB1.c5', 'R2.1']]);
  });

  it('carries what is plugged into a breadboard that moves', () => {
    let p = moveParts(project(), ctx, { R1: along(1, 0) });
    const nets = structuredClone(p.nets);
    p = moveParts(p, ctx, { BB1: { x: 57.62, y: 56.35 } });
    expect(p.layout!.bench!.parts!.R1).toEqual({ x: along(1, 0).x + 50, y: along(1, 0).y + 50, rotation: 0 });
    expect(p.nets).toEqual(nets);
  });

  it('a jumper joins its ends; taking it out splits what it alone joined', () => {
    let p = moveParts(project(), ctx, { R1: along(1, 0) });
    p = addJumper(p, ctx, { from: 'BB1.e5', to: 'U1.D13', color: '#27ae60' });
    const net = Object.values(p.nets).find((n) => n.includes('U1.D13'))!;
    expect([...net].sort()).toEqual(['BB1.a5', 'BB1.e5', 'R1.2', 'U1.D13']);
    p = removeJumper(p, ctx, 0);
    expect(p.layout!.bench!.wires).toEqual([]);
    expect(Object.values(p.nets).flat()).not.toContain('U1.D13');
    expect(Object.values(p.nets).flat()).not.toContain('BB1.e5');
    expect(Object.values(p.nets).find((n) => n.includes('R1.2'))!.sort()).toEqual(['BB1.a5', 'R1.2']);
  });

  it('keeps a connection the schematic drew and shows it as not yet built', () => {
    const p = project();
    p.nets = { LED: ['R1.2', 'U1.D13'] };
    const m = benchModel(p, ctx.shapes, ctx.ties);
    const links = unbuilt(p, m);
    expect(links).toHaveLength(1);
    expect(new Set([links[0]!.from, links[0]!.to])).toEqual(new Set(['R1.2', 'U1.D13']));
    // a jumper from R1.2 to the header builds it
    const built = addJumper(structuredClone(p), ctx, { from: 'R1.2', to: 'U1.D13' });
    expect(unbuilt(built, benchModel(built, ctx.shapes, ctx.ties))).toEqual([]);
    expect(Object.keys(built.nets)).toEqual(['LED']);
  });

  it('a strip joins what is plugged into it without any wire', () => {
    let p = moveParts(project(), ctx, { R1: along(1, 0) });
    p = moveParts(p, ctx, { R2: along(5, 2) });
    expect(unbuilt(p, benchModel(p, ctx.shapes, ctx.ties))).toEqual([]);
  });
});
