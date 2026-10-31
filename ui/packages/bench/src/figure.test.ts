// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as THREE from 'three';
import type { WorldBody } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { gaitPhase, isPerson, personFigure, STRIDE } from './figure';

const mats = { clothes: new THREE.MeshStandardMaterial(), legs: new THREE.MeshStandardMaterial(), skin: new THREE.MeshStandardMaterial() };
const person: WorldBody = { id: 'p', at: [0, -1500, 100], size: [450, 300, 1700], warm: true };

describe('people in the world', () => {
  it('a warm body of a person\'s size is a person; a box, a ball or a cold body is not', () => {
    expect(isPerson(person)).toBe(true);
    expect(isPerson({ ...person, warm: undefined })).toBe(false);
    expect(isPerson({ ...person, size: [120, 120, 100] })).toBe(false);
    expect(isPerson({ ...person, shape: 'sphere' })).toBe(false);
  });

  it('the figure stands in its box: feet on its floor, head at its top, no wider than it', () => {
    const f = personFigure(person, mats);
    const b = new THREE.Box3().setFromObject(f);
    expect(b.min.y).toBeCloseTo(-850, -1);
    expect(b.max.y).toBeCloseTo(850, -1);
    expect(b.max.x - b.min.x).toBeLessThanOrEqual(450 + 1);
  });

  it('walks in step with the distance it has covered, and faces its walk', () => {
    const walking = { ...person, velocity_mm_s: [1000, 0, 0] as [number, number, number] };
    expect(gaitPhase({ ...walking, at: [0, -1500, 100] })).toBeCloseTo(0);
    expect(gaitPhase({ ...walking, at: [STRIDE / 4, -1500, 100] })).toBeCloseTo(0.25);
    expect(gaitPhase({ ...walking, at: [STRIDE * 3, -1500, 100] })).toBeCloseTo(0);
    const f = personFigure({ ...walking, at: [STRIDE / 4, -1500, 100] }, mats);
    expect((f.children[0] as THREE.Group).rotation.y).toBeCloseTo(Math.PI / 2); // walking along +x
    const legs = (f.children[0] as THREE.Group).children.slice(0, 2);
    expect(legs[0]!.rotation.x).toBeCloseTo(-legs[1]!.rotation.x); // one leg forward, one back
    expect(Math.abs(legs[0]!.rotation.x)).toBeGreaterThan(0.3);
  });
});
