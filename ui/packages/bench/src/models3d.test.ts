// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import * as THREE from 'three';
import type { ComponentDescription } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { buildPart } from './parts3d';
import { benchShape } from './shape';
import { physical, type Model3d } from './models3d';

const dir = (name: string) => resolve(__dirname, `../../../../catalog/components/${name}`);
function part(name: string) {
  const description = JSON.parse(readFileSync(`${dir(name)}/component.json`, 'utf-8')) as ComponentDescription & { bench: { model3d: Omit<Model3d, 'text'> & { file: string } } };
  const m = description.bench.model3d;
  const model: Model3d = { text: readFileSync(`${dir(name)}/${m.file}`, 'utf-8'), scale: m.scale, offset_mm: m.offset_mm, rotation_deg: m.rotation_deg };
  const shape = benchShape(description);
  const built = buildPart('X1', shape, { component: `emblink/${name}`, model3d: model });
  return { shape, built };
}

/** the model's points below the seating plane: its legs, in the part's frame */
function legPoints(group: THREE.Object3D): THREE.Vector3[] {
  group.updateMatrixWorld(true);
  const out: THREE.Vector3[] = [];
  group.traverse((o) => {
    const mesh = o as THREE.Mesh;
    if (!mesh.isMesh) return;
    const pos = mesh.geometry.getAttribute('position');
    for (let i = 0; i < pos.count; i++) {
      const v = new THREE.Vector3().fromBufferAttribute(pos, i).applyMatrix4(mesh.matrixWorld);
      if (v.y < -0.5) out.push(v);
    }
  });
  return out;
}

describe('KiCad models on catalog parts', () => {
  for (const name of ['bourns_3296w_103', 'buzzer']) {
    it(`${name}: the model replaces the body and its legs stand in the pins' holes`, () => {
      const { shape, built } = part(name);
      expect(built.group.getObjectByName('model3d')).toBeDefined();
      const legs = legPoints(built.group);
      expect(legs.length).toBeGreaterThan(0);
      const [w, d] = shape.size;
      const pins = Object.values(shape.pins).map(([x, y]) => [x - w / 2, y - d / 2] as const);
      // every pin has a leg on it, and every leg is on a pin
      for (const [x, z] of pins) expect(Math.min(...legs.map((v) => Math.hypot(v.x - x, v.z - z)))).toBeLessThan(0.6);
      for (const v of legs) expect(Math.min(...pins.map(([x, z]) => Math.hypot(v.x - x, v.z - z)))).toBeLessThan(1);
    });
  }

  it('a file that does not parse leaves the built body', () => {
    const { shape } = part('buzzer');
    const built = buildPart('X1', shape, { component: 'emblink/buzzer', model3d: { text: 'not a model' } });
    expect(built.group.getObjectByName('model3d')).toBeUndefined();
    expect(built.group.children.length).toBeGreaterThan(0);
  });

  it('reads Phong as KiCad writes it: tin pins are metal, plastics are not, shininess 0..1 is smoothness', () => {
    const phong = (diffuse: [number, number, number], specular: [number, number, number], shininess: number) => {
      const m = new THREE.MeshPhongMaterial({ shininess });
      m.color.setRGB(...diffuse, THREE.SRGBColorSpace);
      m.specular.setRGB(...specular, THREE.SRGBColorSpace);
      return physical(m);
    };
    const tin = phong([0.824, 0.82, 0.781], [0.328, 0.258, 0.172], 0.7);
    expect(tin.metalness).toBeGreaterThan(0.5);
    expect(tin.roughness).toBeCloseTo(0.3);
    const orange = phong([0.809, 0.426, 0.148], [0.039, 0.102, 0.145], 0.25);
    expect(orange.metalness).toBe(0);
    expect(orange.roughness).toBeCloseTo(0.75);
  });
});
