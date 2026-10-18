// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { pickTerminal, type ProjectedTerminal } from './picking';

const contact = (ref: string, x: number, y: number, depth = 0): ProjectedTerminal => ({ terminal: { ref, at: [0, 0, 0] }, x, y, depth });
describe('camera-space contacts', () => {
  it('uses CSS-pixel distance, not declaration order or world-plane distance', () => {
    const candidates = [contact('B1.D0', 28, 20), contact('B1.D1', 20, 20)];
    expect(pickTerminal(candidates, 21, 20)).toMatchObject({ kind: 'terminal', terminal: { ref: 'B1.D1' } });
    expect(pickTerminal(candidates.reverse(), 21, 20)).toMatchObject({ kind: 'terminal', terminal: { ref: 'B1.D1' } });
  });
  it('refuses ambiguous neighboring pins until the user zooms or points precisely', () => {
    expect(pickTerminal([contact('B1.D0', 20, 20), contact('B1.D1', 22, 20)], 21, 20)).toEqual({ kind: 'ambiguous' });
    expect(pickTerminal([contact('B1.D0', 20, 20), contact('B1.D1', 30, 20)], 20, 20)).toMatchObject({ kind: 'terminal', terminal: { ref: 'B1.D0' } });
  });
  it('excludes clipped, off-radius and invalid projections', () => {
    expect(pickTerminal([contact('behind', 20, 20, 2), contact('far', 50, 20), contact('bad', NaN, 20)], 20, 20)).toEqual({ kind: 'none' });
  });
});
