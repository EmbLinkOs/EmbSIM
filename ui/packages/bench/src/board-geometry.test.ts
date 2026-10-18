// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Box3 } from 'three';
import { describe, expect, it } from 'vitest';
import { buildPart } from './parts3d';
import type { BenchShape } from './shape';

describe('board body and contact geometry', () => {
  it.each([
    ['none', 9.4, 9.45],
    ['male', 17.4, 17.4],
    ['female', 17.9, 17.945],
  ] as const)('renders %s headers at the declared mounting height and PCB thickness', (headers, contact, highest) => {
    const shape: BenchShape = { package: 'board', size: [30, 20, 2.4], pins: { D0: [5, 5] }, board: { standoff_mm: 7, thickness_mm: 2.4, headers } };
    const built = buildPart('B1', shape, { component: 'test/board' });
    expect(built.pinTop).toBeCloseTo(contact);
    expect(new Box3().setFromObject(built.group).max.y).toBeCloseTo(highest);
  });
});
