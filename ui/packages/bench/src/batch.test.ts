// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import * as THREE from 'three';
import { describe, expect, it } from 'vitest';
import { batchStatic } from './batch';
import { GOLD_PLATE, pinStrip, STATIC_MATERIALS } from './packages3d';
import { buildPart } from './parts3d';
import { benchShape } from './shape';

function meshes(o: THREE.Object3D): THREE.Mesh[] {
  const out: THREE.Mesh[] = [];
  o.traverse((x) => { if ((x as THREE.Mesh).isMesh) out.push(x as THREE.Mesh); });
  return out;
}
const triangles = (o: THREE.Object3D) => meshes(o).reduce((t, m) => t + (m.geometry.index ? m.geometry.index.count : m.geometry.attributes.position!.count) / 3, 0);

describe('static batching', () => {
  it('merges a header strip\'s posts into one mesh per material, keeping its shape exactly', () => {
    const strip = pinStrip(20);
    const before = new THREE.Box3().setFromObject(strip);
    const tris = triangles(strip);
    const n = meshes(strip).length;
    const removed = batchStatic(strip, STATIC_MATERIALS);
    expect(removed).toBeGreaterThan(50);
    expect(meshes(strip).length).toBe(n - removed);
    expect(meshes(strip).filter((m) => m.material === GOLD_PLATE)).toHaveLength(1);
    const after = new THREE.Box3().setFromObject(strip);
    expect(after.min.distanceTo(before.min)).toBeLessThan(1e-6);
    expect(after.max.distanceTo(before.max)).toBeLessThan(1e-6);
    expect(triangles(strip)).toBeCloseTo(tris);
  });

  it('a NUCLEO-64 is a few dozen draw calls, its buttons still pressable', () => {
    const base = resolve(__dirname, '../../../../catalog/boards/nucleo_f446re');
    const d = JSON.parse(readFileSync(`${base}/board.json`, 'utf-8'));
    const svg = readFileSync(`${base}/artwork.svg`, 'utf-8');
    const built = buildPart('B1', benchShape(d, undefined, svg), { component: d.id });
    const n = meshes(built.group).length;
    expect(n).toBeLessThan(150);
    const buttons = meshes(built.group).filter((m) => (m.userData as { gesture?: string }).gesture === 'hold');
    expect(buttons.length).toBeGreaterThanOrEqual(2); // B1 USER and B2 RESET
  });
});
