// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { componentValue, placeComponentLabels, type LabelPart } from './componentLabels';
import { rectsIntersect } from './geometry';
const part = (id: string, x = 0): LabelPart => ({ id, name: 'Resistor', value: '4.7 kΩ', bounds: { x, y: 0, width: 60, height: 20 }, pins: [{ x, y: 10 }, { x: x + 60, y: 10 }] });
describe('component annotations', () => {
  it('formats SI values with real units and preserves named parts', () => {
    expect(componentValue('4.7k', 'ohm')).toBe('4.7 kΩ');
    expect(componentValue('100n', 'F')).toBe('100 nF');
    expect(componentValue(0, 'V')).toBe('0 V');
    expect(componentValue('2N3904')).toBe('2N3904');
    expect(componentValue(undefined)).toBe('');
  });
  it('avoids a neighboring body, terminal and an already placed label', () => {
    const parts = [part('R1'), part('R2', 90)];
    const labels = placeComponentLabels(parts, [], true);
    for (const label of labels.values()) for (const body of parts) expect(rectsIntersect(label, body.bounds)).toBe(false);
    expect(rectsIntersect(labels.get('R1')!, labels.get('R2')!)).toBe(false);
    expect(placeComponentLabels([...parts].reverse(), [], true)).toEqual(labels);
  });
  it('prefers a candidate clear of a track and supports compact detail', () => {
    const p = part('R1');
    const clear = placeComponentLabels([p], [], true).get('R1')!;
    const routed = placeComponentLabels([p], [{ points: [[80, -50], [80, 80]] }], true).get('R1')!;
    expect(routed.x).not.toBe(clear.x);
    const compact = placeComponentLabels([p], [], false).get('R1')!;
    expect(compact.name).toBe(''); expect(compact.height).toBeLessThan(clear.height);
  });
});
