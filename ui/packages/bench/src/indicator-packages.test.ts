// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Box3 } from 'three';
import { describe, expect, it } from 'vitest';
import red from '../../../../catalog/components/wp7113id/component.json';
import rgb from '../../../../catalog/components/wp154a4sureqbfzgc/component.json';
import type { ComponentDescription } from '@embsim/protocol';
import { buildPart } from './parts3d';
import { benchShape } from './shape';

describe('source-backed indicator packages', () => {
  it.each([red, rgb])('honors $name body diameter, flange, height and mounting clearance', (doc) => {
    const shape = benchShape(doc as unknown as ComponentDescription);
    const part = buildPart('D1', shape, { component: doc.id });
    const body = part.group.getObjectByName('led-body')!;
    const box = new Box3().setFromObject(body);
    expect(box.max.x - box.min.x).toBeCloseTo(5.9);
    expect(box.max.z - box.min.z).toBeCloseTo(5.9);
    expect(box.min.y).toBeCloseTo(3);
    expect(box.max.y).toBeCloseTo(11.6);
    expect(shape.geometrySource).toBe('catalog');
  });
  it('preserves the exact RGB physical order and half-breadboard pitch', () => {
    const shape = benchShape(rgb as unknown as ComponentDescription);
    const ordered = [...rgb.pins].sort((a, b) => a.number - b.number).map((p) => p.name);
    expect(ordered).toEqual(['R', 'COM', 'B', 'G']);
    const coordinates = ordered.map((pin) => shape.pins[pin]![0]);
    for (let i = 1; i < coordinates.length; i++) expect(coordinates[i]! - coordinates[i - 1]!).toBeCloseTo(1.27);
    expect(shape.size).toEqual([5.9, 5.9, 11.6]);
  });
});
