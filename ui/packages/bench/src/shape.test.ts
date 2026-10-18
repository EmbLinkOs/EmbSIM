// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { readdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import type { ComponentDescription } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { artworkAnchors, benchShape, dipPins, PITCH } from './shape';

const CATALOG = resolve(__dirname, '../../../../catalog');
const read = (kind: string, name: string, file: string) => readFileSync(resolve(CATALOG, kind, name, file), 'utf-8');
const component = (name: string) => JSON.parse(read('components', name, 'component.json')) as ComponentDescription;
const span = (s: ReturnType<typeof benchShape>, a: string, b: string) => Math.hypot(s.pins[a]![0] - s.pins[b]![0], s.pins[a]![1] - s.pins[b]![1]);

describe('bench shapes', () => {
  it('gives a resistor its 0.4 in lead span and an LED its 0.1 in legs', () => {
    expect(span(benchShape(component('resistor')), '1', '2')).toBeCloseTo(10.16);
    const led = benchShape(component('led'));
    expect(led.package).toBe('led');
    expect(span(led, 'A', 'K')).toBeCloseTo(PITCH);
  });

  it('puts a DIP-14 on 0.3 in rows, pin 1 at the bottom left, counter-clockwise', () => {
    const s = benchShape(component('ic_74hc00'));
    expect(s.package).toBe('dip');
    const names = component('ic_74hc00').pins.slice().sort((a, b) => (a.number ?? 0) - (b.number ?? 0)).map((p) => p.name);
    const p1 = s.pins[names[0]!]!;
    const p7 = s.pins[names[6]!]!;
    const p8 = s.pins[names[7]!]!;
    const p14 = s.pins[names[13]!]!;
    expect(p7[0] - p1[0]).toBeCloseTo(6 * PITCH);
    expect(p1[1] - p14[1]).toBeCloseTo(7.62);
    expect(p8[0]).toBeCloseTo(p7[0]);
    expect(p14[0]).toBeCloseTo(p1[0]);
  });

  it('numbers a DIP-8 as the data sheet does', () => {
    const { pins } = dipPins(['1', '2', '3', '4', '5', '6', '7', '8'], 7.62);
    expect(pins['4']![0]).toBeCloseTo(pins['5']![0]);
    expect(pins['1']![0]).toBeCloseTo(pins['8']![0]);
    expect(pins['1']![1]).toBeGreaterThan(pins['8']![1]);
  });

  it('gives a TO-92 three legs 0.1 in apart', () => {
    const s = benchShape(component('bjt_2n3904'));
    expect(s.package).toBe('to92');
    const legs = Object.values(s.pins).map(([x]) => x).sort((a, b) => a - b);
    expect(legs[1]! - legs[0]!).toBeCloseTo(PITCH);
    expect(legs[2]! - legs[1]!).toBeCloseTo(PITCH);
  });

  it('takes a breadboard from its own bench description', () => {
    const s = benchShape(component('breadboard_830'));
    expect(s.package).toBe('breadboard');
    expect(s.wiringAid).toBe(true);
    expect(s.size).toEqual([165.1, 54.6, 8.5]);
    expect(Object.keys(s.pins)).toHaveLength(830);
    expect(s.pins.a2![0] - s.pins.a1![0]).toBeCloseTo(PITCH);
    // the centre channel: e to f is 0.3 in
    expect(s.pins.f1![1] - s.pins.e1![1]).toBeCloseTo(7.62);
  });

  it('places a board\'s ports from its artwork\'s pin anchors, in mm', () => {
    const board = JSON.parse(read('boards', 'arduino_uno', 'board.json')) as ComponentDescription & { ports: Record<string, unknown> };
    const svg = read('boards', 'arduino_uno', 'artwork.svg');
    const s = benchShape({ ...board, kind: 'board', artwork: { svg: 'artwork.svg', width_mm: 68.6, height_mm: 53.3 } }, undefined, svg);
    expect(s.package).toBe('board');
    expect(s.size[0]).toBeCloseTo(68.6);
    // D13 and the GND beside it: one header pitch apart
    const d13 = s.pins.D13 ?? s.pins.d13;
    expect(d13).toBeDefined();
    const anchors = artworkAnchors(svg, 68.6, 53.3);
    expect(anchors.d13![0] - anchors.gnd_d![0]).toBeCloseTo(PITCH, 1);
  });

  it('keeps supply symbols off the bench', () => {
    expect(benchShape(component('vcc')).package).toBe('virtual');
    expect(benchShape(component('gnd')).package).toBe('virtual');
  });

  it('gives every catalog part a shape with a leg for each pin, inside its body', () => {
    for (const name of readdirSync(resolve(CATALOG, 'components'))) {
      const desc = component(name);
      const s = benchShape(desc);
      if (s.package === 'virtual') continue;
      const own = name === 'header' ? 1 : desc.pins.length;
      expect(Object.keys(s.pins).length, name).toBeGreaterThanOrEqual(own);
      for (const [pin, [x, y]] of Object.entries(s.pins)) {
        expect(x, `${name}.${pin}`).toBeGreaterThanOrEqual(-0.01);
        expect(y, `${name}.${pin}`).toBeGreaterThanOrEqual(-0.01);
        expect(x, `${name}.${pin}`).toBeLessThanOrEqual(s.size[0] + 0.01);
        expect(y, `${name}.${pin}`).toBeLessThanOrEqual(s.size[1] + 0.01);
      }
    }
  });
});
