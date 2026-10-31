// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { Box3 } from 'three';
import type { ComponentDescription, Project } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { addJumper, benchModel, moveParts, previewInsertion } from './layout';
import { buildPart } from './parts3d';
import { benchShape, isSurfaceMount } from './shape';
import { jumperViews, snapPart, terminalPoint } from './view';

const description = (name: string) => JSON.parse(readFileSync(resolve(__dirname, `../../../../catalog/components/${name}/component.json`), 'utf-8')) as ComponentDescription;
describe('named surface-mount packages', () => {
  it('maps BSS138 G1/S2/D3 to the SOT-23 top view with 1.90 mm outer-lead pitch', () => {
    const shape = benchShape(description('bss138'));
    expect(shape.package).toBe('sot23');
    expect(shape.pins.S![0] - shape.pins.G![0]).toBeCloseTo(1.9);
    expect(shape.pins.G![1]).toBe(shape.pins.S![1]);
    expect(shape.pins.D![1]).toBeLessThan(shape.pins.G![1]);
    const bounds = new Box3().setFromObject(buildPart('Q1', shape, { component: 'emblink/bss138' }).group);
    expect(bounds.max.x - bounds.min.x).toBeCloseTo(2.9);
    expect(bounds.max.z - bounds.min.z).toBeCloseTo(2.4);
    expect(bounds.max.y).toBeLessThanOrEqual(1.11); // CASE 318 maximum A
  });

  it('maps LM1117 GND1/OUT2/IN3 at 2.30 mm pitch and keeps the shared tab out of the electrical terminal list', () => {
    const shape = benchShape(description('lm1117_3v3'));
    expect(shape.package).toBe('sot223');
    expect(Object.keys(shape.pins).sort()).toEqual(['GND', 'IN', 'OUT']);
    expect(shape.pins.OUT![0] - shape.pins.GND![0]).toBeCloseTo(2.3);
    expect(shape.pins.IN![0] - shape.pins.OUT![0]).toBeCloseTo(2.3);
    const bounds = new Box3().setFromObject(buildPart('U1', shape, { component: 'emblink/lm1117_3v3' }).group);
    expect(bounds.max.x - bounds.min.x).toBeCloseTo(6.5);
    expect(bounds.max.z - bounds.min.z).toBeLessThanOrEqual(7.3);
    expect(bounds.max.y).toBeLessThanOrEqual(1.8);
  });

  it('keeps BAT54 A1/NC2/K3 while identifying the source as a representative family outline', () => {
    const desc = description('bat54');
    const shape = benchShape(desc);
    expect(shape.package).toBe('sot23');
    expect(shape.pins.NC![0] - shape.pins.A![0]).toBeCloseTo(1.9);
    expect(shape.pins.K![1]).toBeLessThan(shape.pins.A![1]);
    expect(JSON.stringify(desc.provenance)).toContain('still unverified');
  });

  it('leaves surface-mount leads uninserted even exactly over breadboard holes', () => {
    const p: Project = { schema_version: 1, kind: 'project', name: 'smd', components: [{ id: 'BB1', component: 'emblink/breadboard_830' }, { id: 'Q1', component: 'emblink/bss138' }], nets: {}, layout: { bench: { parts: { BB1: { x: 0, y: 0 }, Q1: { x: 150, y: 150 } } } } };
    const shapes = { BB1: benchShape(description('breadboard_830')), Q1: benchShape(description('bss138')) };
    const ctx = { shapes, ties: { BB1: description('breadboard_830').ties ?? [] } };
    const model = benchModel(p, shapes, ctx.ties);
    const hole = model.holes.get('BB1.a1')!;
    const q = model.byId.get('Q1')!;
    const at = { x: hole.x + q.place.x - q.legs.G![0], y: hole.y + q.place.y - q.legs.G![1], rotation: 0 as const };
    expect(isSurfaceMount(shapes.Q1)).toBe(true);
    expect(previewInsertion(model, 'Q1', at).valid).toBe(false);
    expect(snapPart(model, ctx, 'Q1', at.x, at.y, 0).holes).toEqual([]);
    moveParts(p, ctx, { Q1: at });
    const after = benchModel(p, shapes, ctx.ties);
    expect([...after.plugs.keys()].filter((pin) => pin.startsWith('Q1.'))).toEqual([]);
    expect(p.nets).toEqual({});
    expect(terminalPoint(after, 'Q1.G')?.[1]).toBeCloseTo(0.14);
    addJumper(p, ctx, { from: 'Q1.G', to: 'BB1.a1' });
    expect(p.layout?.bench?.wires).toEqual([expect.objectContaining({ from: 'Q1.G', to: 'BB1.a1' })]);
    expect(jumperViews(p, benchModel(p, shapes, ctx.ties))[0]?.housings).toEqual([false, false]);
  });
});

describe('small-outline parts on breakouts', () => {
  it('draws the VSSOP-8 LMV358 on its SO-to-DIP breakout, on the DIP-8 pins', () => {
    const shape = benchShape(description('lmv358'));
    expect(shape.package).toBe('dip');
    expect(shape.smallOutline).toMatchObject({ kind: 'VSSOP', pins: 8, pitch: 0.65 });
    const bounds = new Box3().setFromObject(buildPart('U1', shape, { component: 'emblink/lmv358' }).group);
    expect(bounds.max.y).toBeGreaterThan(4.1 + 1); // the board on its spacers, the package on it
    expect(bounds.min.y).toBeLessThan(-2);
  });
  it('a through-hole DIP stays a DIP', () => {
    expect(benchShape(description('lm393p')).smallOutline).toBeUndefined();
  });
});

describe('an SO-8 part with fewer pins than its package', () => {
  it('puts the STS7PF30L\'s S, G and D on SO-8 pins 1, 4 and 5 of its breakout', () => {
    const shape = benchShape(description('sts7pf30l'));
    expect(shape.package).toBe('dip');
    expect(shape.smallOutline).toMatchObject({ kind: 'SOIC', pins: 8 });
    const { S, G, D } = shape.pins as Record<string, [number, number]>;
    expect(G![0] - S![0]).toBeCloseTo(3 * 2.54);
    expect(G![1]).toBe(S![1]);
    expect(D![0]).toBeCloseTo(G![0]);
    expect(S![1] - D![1]).toBeCloseTo(7.62);
    expect(Object.keys(shape.pins)).toHaveLength(3);
  });
});
